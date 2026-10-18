//webserver.cpp
#include "webserver.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h" // JSON Parsing

static const char *TAG = "WEB_SERVER";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

std::atomic<float> joystick_x{0.0f};
std::atomic<float> joystick_y{0.0f};


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

// WebSocket Handler (WS /ws)
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake erfolgreich abgeschlossen.");
        // Hier könnte man kurz verzögern und die initiale Spielerliste schicken
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
                // Joystick X-Wert an den LED-Task übergeben
                double x = cJSON_GetObjectItem(root, "x")->valuedouble;
                double y = cJSON_GetObjectItem(root, "y")->valuedouble;
                
                // Hier Steuerungsdaten direkt an den Ledworm-Spieletask übergeben!
                ESP_LOGD("DEBUG", "Joystick x: %.2f, Y: %.2f", x, y);

                joystick_x.store(static_cast<float>(x),
                std::memory_order_relaxed);
                
                joystick_y.store(static_cast<float>(y),
                    std::memory_order_relaxed);    

            } else if (type && strcmp(type->valuestring, "add_player") == 0) {
                ESP_LOGI(TAG, "Event: Neuer Spieler angefordert!");
                // Spiellogik triggern: z.B. einen neuen Wurm auf dem LED-Streifen spawnen
                send_player_list_to_all(req->handle);
                
            } else if (type && strcmp(type->valuestring, "ambient_color") == 0) {
                cJSON *hex_item = cJSON_GetObjectItemCaseSensitive(root, "hex");
                
                if (cJSON_IsString(hex_item) && (hex_item->valuestring != NULL)) {
                    const char *hex = hex_item->valuestring;
                    
                    // Prüfen, ob der String das korrekte Format hat (z.B. "#ff00a0" = 7 Zeichen)
                    if (hex[0] == '#' && strlen(hex) == 7) {
                        unsigned int r, g, b;
                        // sscanf liest die Hexadezimalwerte ab Zeichenposition 1 aus
                        if (sscanf(&hex[1], "%02x%02x%02x", &r, &g, &b) == 3) {
                            ambient_r = static_cast<uint8_t>(r);
                            ambient_g = static_cast<uint8_t>(g);
                            ambient_b = static_cast<uint8_t>(b);
                            
                            ESP_LOGI("SERVER", "Hintergrundbeleuchtung geändert auf R:%d G:%d B:%d", ambient_r, ambient_g, ambient_b);
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
        
        return server;
    }

    ESP_LOGI(TAG, "Fehler beim Starten des Servers.");
    return NULL;
}
