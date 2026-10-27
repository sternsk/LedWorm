//webserver.cpp
#include "webserver.h"
#include "player_manager.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h" // JSON Parsing

static const char *TAG = "WEB_SERVER";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// Maximale Anzahl der HTTP-Sessions merken
static size_t max_open_sockets = 0;

// Beispielhafte Mock-Verwaltung von Spielern im Spiel
void send_player_list_to_all(httpd_handle_t hd) {
    // Hier würde im echten Spiel deine globale Spielerliste ausgelesen werden
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "players");
    cJSON_AddStringToObject(root, "myId", "player_1"); // Dem Client sagen, wer er ist

    cJSON *list = cJSON_CreateArray();
    
    // Spieler 1 (Der eigene)
    cJSON *p1 = cJSON_CreateObject();
    cJSON_AddStringToObject(p1, "id", "player_1");
    cJSON_AddStringToObject(p1, "name", "LedWorm_Alpha");
    cJSON_AddItemToArray(list, p1);

    // Spieler 2 (Bot / Anderer Mitspieler)
    cJSON *p2 = cJSON_CreateObject();
    cJSON_AddStringToObject(p2, "id", "player_2");
    cJSON_AddStringToObject(p2, "name", "ShadowWorm");
    cJSON_AddItemToArray(list, p2);

    cJSON_AddItemToObject(root, "list", list);
    char *json_str = cJSON_PrintUnformatted(root);

    // An alle aktiven WebSocket-Clients senden (vereinfachte Darstellung für ein Device)
    // In der Praxis führt man hier eine Liste von fds (File Descriptors)
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)json_str;
    ws_pkt.len = strlen(json_str);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Sende an Client (Beispielhaft an Filedescriptor 4, besser dynamisch verwalten)
    // httpd_ws_send_frame_to_fd(fd, &ws_pkt); 

    ESP_LOGI(TAG, "Spielerliste gesendet: %s", json_str);
    cJSON_Delete(root);
    free(json_str);
}

// URI Handler für die Website (GET /)
static esp_err_t get_index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
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
        // Hier sollte der ws-handler im player-manager schauen, welcher slot frei ist und diesem dann dem client zuweisen. 
        addPlayer(client_fd);
        return ESP_OK;
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

    if (ws_pkt.len > 0) {
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
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
            if (type && strcmp(type->valuestring, "joystick") == 0) {
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
                            "Kein aktiver Spieler für fd=%d",
                            client_fd);
                }
                
                // Hier Steuerungsdaten direkt an den Ledworm-Spieletask übergeben!
                ESP_LOGD(TAG, "Joystick fd=%d: %.2f, Y: %.2f", client_fd, x, y);
            }
                
            } else if (type && strcmp(type->valuestring, "ambient_color") == 0) {
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
                            
                            ESP_LOGD("SERVER", "Hintergrundbeleuchtung geändert auf R:%d G:%d B:%d", ambient_r, ambient_g, ambient_b);
                        }
                    }
                }
            }
            cJSON_Delete(root);
        }
        free(buf);
    }
    return ESP_OK;
}


// Konfiguration des Servers
httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

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

        // Route für WebSocket
        httpd_uri_t ws_uri = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = ws_handler,
            .user_ctx   = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws_uri);

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
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