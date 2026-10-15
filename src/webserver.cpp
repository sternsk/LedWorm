//webserver.cpp
#include "webserver.h"
#include "joystick.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h" // JSON Parsing

static const char *TAG = "WEB_SERVER";

// Verwende ein Raw-String-Literal R"raw(...)raw"
// Alles innerhalb der Klammern wird exakt als HTML interpretiert.
const char index_html_content[] = R"raw(
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ledworm Controller</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; background: #222; color: #fff; margin: 0; padding: 20px; user-select: none; padding-bottom: 240px; }
        h1 { color: #00ffff; }
        .container { max-width: 400px; margin: 0 auto; }
        
        .status-box { background: #333; padding: 15px; border-radius: 8px; margin: 20px 0; text-align: left; }
        .status-row { display: flex; justify-content: space-between; margin-bottom: 10px; font-size: 16px; }
        .status-val { color: #00ffff; font-weight: bold; }
        
        .join-box { background: #333; padding: 15px; border-radius: 8px; margin: 20px 0; }
        .input-group { display: flex; gap: 10px; margin-bottom: 10px; }
        input[type="text"] { flex: 1; padding: 10px; border-radius: 4px; border: 1px solid #555; background: #444; color: #fff; font-size: 16px; }
        input[type="text"]:focus { border-color: #00ffff; outline: none; }
        
        button { background: #00ffff; color: #222; border: none; padding: 12px 24px; font-size: 16px; font-weight: bold; border-radius: 4px; cursor: pointer; transition: background 0.2s; }
        button:disabled { background: #555; color: #888; cursor: not-allowed; }
        #btn-join { width: 100%; border-radius: 50px; }
        
        /* Joystick am unteren Bildschirmrand fixiert */
        .bottom-controls { position: fixed; bottom: 0; left: 0; right: 0; background: linear-gradient(transparent, #1a1a1a 20%); padding-bottom: 30px; display: flex; justify-content: center; z-index: 100; }
        #joystick-zone { width: 180px; height: 180px; background: rgba(51, 51, 51, 0.9); border-radius: 50%; position: relative; border: 3px solid #555; touch-action: none; box-shadow: 0 0 20px rgba(0,0,0,0.5); }
        #joystick-stick { width: 54px; height: 54px; background: #00ffff; border-radius: 50%; position: absolute; top: 60px; left: 60px; box-shadow: 0 0 15px #00ffff; cursor: pointer; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Ledworm Steuerung</h1>
        
        <div class="status-box">
            <div class="status-row">
                <span>Aktive Spieler auf dem Server:</span>
                <span id="player-count" class="status-val">0</span>
            </div>
            <div class="status-row" id="my-id-row" style="display:none;">
                <span>Deine Spieler-ID:</span>
                <span id="my-id-val" class="status-val">-</span>
            </div>
        </div>

        <div class="join-box" id="join-section">
            <div class="input-group">
                <input type="text" id="player-id-input" placeholder="Wunsch-ID eingeben...">
            </div>
            <button id="btn-join">Join</button>
        </div>
    </div>

    <div class="bottom-controls">
        <div id="joystick-zone">
            <div id="joystick-stick"></div>
        </div>
    </div>

    <script>
        let ws;
        const stick = document.getElementById('joystick-stick');
        const zone = document.getElementById('joystick-zone');
        const playerCount = document.getElementById('player-count');
        const btnJoin = document.getElementById('btn-join');
        const idInput = document.getElementById('player-id-input');
        const myIdRow = document.getElementById('my-id-row');
        const myIdVal = document.getElementById('my-id-val');
        const joinSection = document.getElementById('join-section');
        
        let amIJoined = false;
        let active = false;
        let startX, startY;

        function initWebSocket() {
            ws = new WebSocket(`ws://${window.location.hostname}/ws`);
            ws.onmessage = (event) => {
                const data = JSON.parse(event.data);
                
                // 1. Server meldet allgemeinen Status & ID-Vorschlag
                if (data.type === "status") {
                    playerCount.innerText = data.count;
                    if (!amIJoined && data.suggestedId) {
                        idInput.value = data.suggestedId;
                    }
                }
                // 2. Server antwortet auf Join-Anfrage
                else if (data.type === "join_response") {
                    if (data.success) {
                        amIJoined = true;
                        joinSection.style.display = "none";
                        myIdRow.style.display = "flex";
                        myIdVal.innerText = data.id;
                    } else {
                        alert("ID bereits vergeben oder Server voll! Bitte andere ID wählen.");
                    }
                }
            };
            ws.onclose = () => {
                amIJoined = false;
                joinSection.style.display = "block";
                myIdRow.style.display = "none";
                setTimeout(initWebSocket, 2000);
            };
        }

        // Join Button Event
        btnJoin.addEventListener('click', () => {
            const desiredId = idInput.value.trim();
            if (desiredId && ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({ type: "join_request", id: desiredId }));
            }
        });

        // Joystick Logik (unverändert funktional, nur visuell verschoben)
        zone.addEventListener('touchstart', startMove, {passive: false});
        window.addEventListener('touchend', endMove);
        window.addEventListener('touchmove', move, {passive: false});
        zone.addEventListener('mousedown', startMove);
        window.addEventListener('mouseup', endMove);
        window.addEventListener('mousemove', move);

        function startMove(e) {
            active = true;
            const clientX = e.touches ? e.touches[0].clientX : e.clientX;
            const clientY = e.touches ? e.touches[0].clientY : e.clientY;
            
            // Berechnet den Startpunkt relativ zum Mittelpunkt der Joystick-Zone
            const rect = zone.getBoundingClientRect();
            startX = rect.left + 60; // 60px ist der voreingestellte Offset (Radius + halber Stick)
            startY = rect.top + 60;
            
            if(e.cancelable) e.preventDefault();
        }

        function endMove() {
            if (!active) return;
            active = false;
            stick.style.left = '60px';
            stick.style.top = '60px';
            sendJoystickData(0, 0);
        }

        function move(e) {
            if (!active) return;
            const clientX = e.touches ? e.touches[0].clientX : e.clientX;
            const clientY = e.touches ? e.touches[0].clientY : e.clientY;
            
            // Berechne den Abstand vom fixierten Mittelpunkt der Zone
            const rect = zone.getBoundingClientRect();
            const centerX = rect.left + rect.width / 2;
            const centerY = rect.top + rect.height / 2;
            
            let dx = clientX - centerX;
            let dy = clientY - centerY;
            
            const dist = Math.sqrt(dx * dx + dy * dy);
            const maxLen = 60; // Maximale Auslenkung in Pixeln

            // Begrenzung des Sticks auf den Kreisradius
            if (dist > maxLen) {
                dx = (dx / dist) * maxLen;
                dy = (dy / dist) * maxLen;
            }
               // Positioniere den Stick visuell (relativ zur oberen linken Ecke der Zone)
            stick.style.left = (60 + dx) + 'px';
            stick.style.top = (60 + dy) + 'px';

            // Normalisiere die Werte für den ESP32 (-1.0 bis 1.0)
            const normX = (dx / maxLen).toFixed(2);
            const normY = ((dy / maxLen) * -1).toFixed(2); // Y-Achse invertieren für klassische Koordinaten
            
            sendJoystickData(normX, normY);
            if(e.cancelable) e.preventDefault();
        }

        function sendJoystickData(x, y) {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({ type: "joystick", x: parseFloat(x), y: parseFloat(y) }));
            }
        }

        window.onload = initWebSocket;
    </script>
</body>
</html>

)raw";

// Die Zeiger-Variablen simulieren exakt das alte Verhalten,
// damit Sie im restlichen Code nichts umschreiben müssen:
const uint8_t* index_html_start = (const uint8_t*)index_html_content;
const uint8_t* index_html_end   = (const uint8_t*)(index_html_content + sizeof(index_html_content) - 1);


std::atomic<float> joystick_x{0.0f};
std::atomic<float> joystick_y{0.0f};

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
            .uri = "/",
            .method = HTTP_GET,
            .handler = get_index_handler,
            .user_ctx = NULL,
            .is_websocket = false,
            .handle_ws_control_frames = false,
            .supported_subprotocol = NULL,
            .ws_post_handshake_cb = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        // Route für WebSocket
        httpd_uri_t ws_uri = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .user_ctx = NULL,
            .is_websocket = true,
            .handle_ws_control_frames = false,
            .supported_subprotocol = NULL,
            .ws_post_handshake_cb = NULL
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