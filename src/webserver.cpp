//webserver.cpp
#include "webserver.h"
#include "player_manager.h"
#include "globals.h"
#include "wifi_manager.h"
#include "esp_wifi.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cstdlib>
#include "esp_log.h"
#include "cJSON.h" // JSON Parsing

static const char *TAG = "WEB_SERVER";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// Maximale Anzahl der HTTP-Sessions merken
static size_t max_open_sockets = 0;

static esp_err_t reset_wifi_handler(httpd_req_t *req) {
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode != WIFI_MODE_STA) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "WLAN-Reset ist im Offline-Spiel nicht verfuegbar");
    }
    // Require an explicit same-origin GUI request, not a cross-site form POST.
    char confirmation[8] = {};
    if (httpd_req_get_hdr_value_str(req, "X-LedWorm-Reset", confirmation,
                                   sizeof(confirmation)) != ESP_OK ||
        strcmp(confirmation, "confirm") != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bestaetigung fehlt");
    }
    if (wifi_manager_reset() != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "WLAN konnte nicht zurueckgesetzt werden");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"reset\":true}");
}

static esp_err_t select_wifi_handler(httpd_req_t *req) {
    if (wifi_manager_select_wifi() != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                  "WLAN-Auswahl nur im Offline-Spiel verfuegbar");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"setup\":true}");
}

// Only the requesting socket receives its own authoritative player snapshot.
static esp_err_t send_player_state(httpd_req_t *req) {
    player_state_t state;
    if (!getPlayerState(httpd_req_to_sockfd(req), state)) return ESP_FAIL;
    wifi_mode_t mode;
    const bool can_reset_wifi = esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_STA;
    char json[320];
    int length = snprintf(json, sizeof(json),
        "{\"type\":\"player_state\",\"id\":%lu,\"x\":%.3f,\"y\":%.3f,"
        "\"length\":%u,\"brightness\":%u,\"count\":%u,\"score\":%lu,"
        "\"can_reset_wifi\":%s,\"clock_enabled\":%s}",
        static_cast<unsigned long>(state.player_id), state.x, state.y,
        unsigned(state.length), unsigned(state.brightness), unsigned(state.active_players),
        static_cast<unsigned long>(state.score),
        can_reset_wifi ? "true" : "false", state.clock_enabled ? "true" : "false");
    if (length < 0 || size_t(length) >= sizeof(json)) return ESP_FAIL;
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t *>(json);
    frame.len = length;
    return httpd_ws_send_frame(req, &frame);
}

// URI Handler für die Website (GET /)
static esp_err_t get_index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    // Berechne die Dateigröße anhand der Start- und End-Zeiger
    size_t index_html_len = index_html_end - index_html_start;

    // FALSCH war: httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    // RICHTIG: Sende die im Flash eingebettete HTML-Datei über die Zeiger
    httpd_resp_send(req, (const char *)index_html_start, index_html_len);
    
    return ESP_OK;
}

// WebSocket Handler 
static esp_err_t ws_handler(httpd_req_t *req) {
    int client_fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG,
                 "WebSocket-Handshake erfolgreich, fd=%d",
                 client_fd);
        return addPlayer(client_fd) ? ESP_OK : ESP_FAIL;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Ersten Aufruf machen, um die Länge des Buffers zu erfahren
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fehler beim Abrufen der Frame-Länge");
        return ret;
    }

    if (ws_pkt.len > 512) return ESP_ERR_INVALID_SIZE;
    if (ws_pkt.type != HTTPD_WS_TYPE_TEXT) return ESP_FAIL;

    if (ws_pkt.len > 0) {
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Fehler beim Empfangen des Frames");
            free(buf);
            return ret;
        }

        // JSON parsen
        cJSON *root = cJSON_Parse((const char*)buf);
        if (root) {
            cJSON *type = cJSON_GetObjectItem(root, "type");
            if (cJSON_IsString(type) && strcmp(type->valuestring, "joystick") == 0) {
                // Übergabe der Joystick-Werte an die identifizierte LedWorm-Instanz
                cJSON *x_item = cJSON_GetObjectItem(root, "x");
                cJSON *y_item = cJSON_GetObjectItem(root, "y");

                if (cJSON_IsNumber(x_item) &&
                    cJSON_IsNumber(y_item)) {
                        float x = static_cast<float>(x_item->valuedouble);
                        float y = static_cast<float>(y_item->valuedouble);

                // Joystickdaten dem richtigen Spieler zuordnen
                if (!updateJoystick(client_fd, x, y)) {
                    ESP_LOGW(TAG,
                            "Kein Joystick-update für fd=%d, x: %.2f, y: %.2f",
                            client_fd, x, y);
                }
            }
                
            } else if (cJSON_IsString(type) && strcmp(type->valuestring, "value") == 0) {
                const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
                if (cJSON_IsNumber(value)) updateBrightness(client_fd, value->valuedouble);
                ret = send_player_state(req);
            } else if (cJSON_IsString(type) && strcmp(type->valuestring, "state") == 0) {
                ret = send_player_state(req);
            } else if (cJSON_IsString(type) && strcmp(type->valuestring, "ambient_color") == 0) {
                cJSON *hex_item = cJSON_GetObjectItemCaseSensitive(root, "hex");
                
                if (cJSON_IsString(hex_item) && (hex_item->valuestring != NULL)) {
                    const char *hex = hex_item->valuestring;
                    
                    // Prüfen, ob der String das korrekte Format hat (z.B. "#2ba16a" = 7 Zeichen)
                    if (hex[0] == '#' && strlen(hex) == 7) {
                        unsigned int r, g, b;
                        // sscanf liest die Hexadezimalwerte ab Zeichenposition 1 aus
                        if (sscanf(&hex[1], "%02x%02x%02x", &r, &g, &b) == 3) {
                            ambient_r = static_cast<uint8_t>(r);
                            ambient_g = static_cast<uint8_t>(g);
                            ambient_b = static_cast<uint8_t>(b);
                            
                            ESP_LOGD("SERVER", "Hintergrundbeleuchtung geändert auf R:%u G:%u B:%u", r, g, b);
                        }
                    }
                }
            } else if (cJSON_IsString(type) && strcmp(type->valuestring, "marquee_text") == 0) {
                const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
                if (!cJSON_IsString(text) || !setMarqueeText(text->valuestring)) {
                    ESP_LOGW(TAG, "Ungültiger Lauftext von fd=%d", client_fd);
                }
            } else if (cJSON_IsString(type) && strcmp(type->valuestring, "clock") == 0) {
                const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
                if (!cJSON_IsBool(enabled) || !setClockEnabled(cJSON_IsTrue(enabled))) {
                    ESP_LOGW(TAG, "Ungueltiger Uhrmodus von fd=%d", client_fd);
                }
            }
            cJSON_Delete(root);
        }
        free(buf);
    }
    return ret;
}


// Konfiguration des Servers
httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Acht WebSockets plus Platz für den Abruf der Controller-Seite.
    config.max_open_sockets = MAX_PLAYERS + 2;
    config.lru_purge_enable = true;
    config.close_fn = [](httpd_handle_t, int fd) {
        removePlayer(fd);
        close(fd);
    };

    ESP_LOGI(TAG, "Starte Server auf Port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        
        // Route für Website
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = get_index_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        httpd_uri_t play_uri = index_uri;
        play_uri.uri = "/play";
        httpd_register_uri_handler(server, &play_uri);

        httpd_uri_t reset_uri = {};
        reset_uri.uri = "/api/wifi/reset";
        reset_uri.method = HTTP_POST;
        reset_uri.handler = reset_wifi_handler;
        httpd_register_uri_handler(server, &reset_uri);
        httpd_uri_t selection_uri = {};
        selection_uri.uri = "/api/wifi/select";
        selection_uri.method = HTTP_POST;
        selection_uri.handler = select_wifi_handler;
        httpd_register_uri_handler(server, &selection_uri);

        // Route für WebSocket
        httpd_uri_t ws_uri = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = ws_handler,
            .user_ctx   = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws_uri);

        max_open_sockets = config.max_open_sockets;
        
        return server;
    }

    ESP_LOGI(TAG, "Fehler beim Starten des Servers.");
    return NULL;
}


// Anzahl aktuell verbundener WebSocket-Clients ermitteln
size_t get_websocket_client_count(httpd_handle_t server)
{
    if (server == NULL || max_open_sockets == 0) {
        return 0;
    }

    int *client_fds = static_cast<int *>(
        calloc(max_open_sockets, sizeof(int))
    );

    if (client_fds == NULL) {
        return 0;
    }

    size_t count = max_open_sockets;

    esp_err_t ret = httpd_get_client_list(
        server, &count, client_fds
    );

    size_t ws_count = 0;

    if (ret == ESP_OK) {
        for (size_t i = 0; i < count; i++) {
            if (httpd_ws_get_fd_info(server, client_fds[i])
                    == HTTPD_WS_CLIENT_WEBSOCKET) {
                ws_count++;
            }
        }
    } else {
        ESP_LOGE(TAG, "Client-Liste konnte nicht gelesen werden");
    }

    free(client_fds);
    return ws_count;
}
