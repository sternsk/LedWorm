#include "wifi_manager.h"
#include "globals.h"
#include <cstring>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr EventBits_t CONNECTED = BIT0, REBOOT = BIT1, OFFLINE = BIT2, SELECT_WIFI = BIT3;
constexpr TickType_t CONNECTION_TIMEOUT = pdMS_TO_TICKS(30000);
const char *TAG = "wifi_manager";
EventGroupHandle_t events;
wifi_manager_callback_t notify;
httpd_handle_t setup_server;
bool mdns_running = false;
extern const uint8_t html_start[] asm("_binary_wifi_setup_html_start");
extern const uint8_t html_end[] asm("_binary_wifi_setup_html_end");

void start_mdns() {
    if (mdns_running) return;
    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        err = mdns_hostname_set("ledworm");
        if (err == ESP_OK) err = mdns_instance_name_set("LedWorm");
        if (err == ESP_OK) err = mdns_service_add("LedWorm", "_http", "_tcp", 80, nullptr, 0);
        if (err != ESP_OK) mdns_free();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS nicht verfuegbar (%s); Zugriff ueber IP bleibt moeglich",
                 esp_err_to_name(err));
        return;
    }
    mdns_running = true;
    ESP_LOGI(TAG, "Spiel erreichbar unter http://ledworm.local/");
}

esp_err_t store(const wifi_config_t *config) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ledworm_wifi", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    if (config) err = nvs_set_blob(handle, "station", &config->sta, sizeof(config->sta));
    else {
        err = nvs_erase_key(handle, "station");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

bool load(wifi_config_t &config) {
    nvs_handle_t handle;
    if (nvs_open("ledworm_wifi", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = sizeof(config.sta);
    esp_err_t err = nvs_get_blob(handle, "station", &config.sta, &size);
    nvs_close(handle);
    return err == ESP_OK && size == sizeof(config.sta) && config.sta.ssid[0];
}

void on_event(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(data);
        ESP_LOGI(TAG, "Spiel erreichbar unter http://" IPSTR "/", IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(events, CONNECTED);
    } else if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ||
               (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        xEventGroupClearBits(events, CONNECTED);
    }
}

esp_err_t page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, reinterpret_cast<const char *>(html_start), html_end - html_start);
}

esp_err_t scan(httpd_req_t *req) {
    // This is the dedicated setup HTTP task; no game server runs in setup mode.
    wifi_scan_config_t config = {};
    config.show_hidden = true;
    if (esp_wifi_scan_start(&config, true) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WLAN-Suche fehlgeschlagen");
    wifi_ap_record_t records[20] = {};
    uint16_t count = 20;
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WLAN-Liste nicht verfuegbar");
    cJSON *list = cJSON_CreateArray();
    if (!list) return ESP_ERR_NO_MEM;
    for (uint16_t i = 0; i < count; ++i) {
        // SSIDs can occupy all 32 bytes. Ensure a terminator before JSON encoding.
        char ssid[33] = {};
        memcpy(ssid, records[i].ssid, 32);
        cJSON_AddItemToArray(list, cJSON_CreateString(ssid));
    }
    char *json = cJSON_PrintUnformatted(list);
    cJSON_Delete(list);
    if (!json) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return result;
}

esp_err_t save(httpd_req_t *req) {
    if (req->content_len == 0 || req->content_len > 512)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltige Datenlaenge");
    char body[513] = {};
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            if (n == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
            return ESP_FAIL;
        }
        received += n;
    }
    cJSON *json = cJSON_Parse(body);
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    bool valid = cJSON_IsString(ssid) && cJSON_IsString(password);
    if (valid) {
        size_t s = strlen(ssid->valuestring), p = strlen(password->valuestring);
        valid = s >= 1 && s <= 32 && (p == 0 || (p >= 8 && p <= 63));
    }
    if (!valid) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                  "SSID: 1-32 Bytes; Passwort: leer oder 8-63 Bytes");
    }
    wifi_config_t config = {};
    memcpy(config.sta.ssid, ssid->valuestring, strlen(ssid->valuestring));
    memcpy(config.sta.password, password->valuestring, strlen(password->valuestring));
    cJSON_Delete(json);
    esp_err_t err = store(&config);
    memset(&config, 0, sizeof(config));
    memset(body, 0, sizeof(body));
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speichern fehlgeschlagen");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, "{\"saved\":true}");
    xEventGroupSetBits(events, REBOOT);
    return result;
}

esp_err_t offline(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, "{\"offline\":true}");
    if (result == ESP_OK) xEventGroupSetBits(events, OFFLINE);
    return result;
}

void setup(bool keep_ap = false) {
    if (mdns_running) {
        mdns_free();
        mdns_running = false;
    }
    if (!keep_ap) ESP_ERROR_CHECK(esp_wifi_stop());
    // STA interface enables scanning; no automatic connection attempts in setup.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    if (!keep_ap) {
        wifi_config_t config = {};
        const char *name = "LedWorm-Setup";
        memcpy(config.ap.ssid, name, strlen(name));
        config.ap.ssid_len = strlen(name);
        config.ap.channel = 1;
        config.ap.max_connection = MAX_PLAYERS;
        config.ap.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.max_open_sockets = 4;
    http.stack_size = 8192;
    http.lru_purge_enable = true;
    http.recv_wait_timeout = 5;
    ESP_ERROR_CHECK(httpd_start(&setup_server, &http));
    httpd_uri_t root = {}; root.uri = "/"; root.method = HTTP_GET; root.handler = page;
    httpd_uri_t networks = {}; networks.uri = "/api/scan"; networks.method = HTTP_GET; networks.handler = scan;
    httpd_uri_t credentials = {}; credentials.uri = "/api/wifi"; credentials.method = HTTP_POST; credentials.handler = save;
    httpd_uri_t play = {}; play.uri = "/api/offline"; play.method = HTTP_POST; play.handler = offline;
    ESP_ERROR_CHECK(httpd_register_uri_handler(setup_server, &root));
    httpd_uri_t selection = root; selection.uri = "/setup";
    ESP_ERROR_CHECK(httpd_register_uri_handler(setup_server, &selection));
    ESP_ERROR_CHECK(httpd_register_uri_handler(setup_server, &networks));
    ESP_ERROR_CHECK(httpd_register_uri_handler(setup_server, &credentials));
    ESP_ERROR_CHECK(httpd_register_uri_handler(setup_server, &play));
    ESP_LOGI(TAG, "Mit LedWorm-Setup verbinden und http://192.168.4.1/ oeffnen");
}

void manager(void *) {
    wifi_config_t config = {};
    bool have_credentials = load(config);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (have_credentials) ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    memset(&config, 0, sizeof(config));
    ESP_ERROR_CHECK(esp_wifi_start());
    bool provisioning = !have_credentials;
    if (provisioning) setup();
    TickType_t since = xTaskGetTickCount();
    TickType_t last_attempt = since - pdMS_TO_TICKS(3000);
    bool serving = false;
    bool playing_offline = false;
    for (;;) {
        EventBits_t bits = xEventGroupGetBits(events);
        if (bits & REBOOT) {
            vTaskDelay(pdMS_TO_TICKS(1000)); // allow HTTP response to leave the socket
            esp_restart();
        }
        if (provisioning && !playing_offline && (bits & OFFLINE)) {
            // Stop from the manager task: httpd_stop waits for the POST handler.
            ESP_ERROR_CHECK(httpd_stop(setup_server));
            xEventGroupClearBits(events, OFFLINE);
            setup_server = nullptr;
            // Keep the AP running and its clients associated; disable station mode.
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
            notify(true);
            serving = true;
            playing_offline = true;
            ESP_LOGI(TAG, "Offline-Spiel unter http://192.168.4.1/");
        }
        if (playing_offline && (bits & SELECT_WIFI)) {
            notify(false);
            xEventGroupClearBits(events, SELECT_WIFI);
            serving = false;
            setup(true);
            playing_offline = false;
        }
        TickType_t now = xTaskGetTickCount();
        if (!provisioning) {
            if (bits & CONNECTED) {
                since = now;
                if (!serving) {
                    notify(true);
                    start_mdns();
                    serving = true;
                }
            } else if (now - since >= CONNECTION_TIMEOUT) {
                if (serving) { notify(false); serving = false; }
                ESP_LOGW(TAG, "Keine IP nach 30 Sekunden; starte Setup");
                setup();
                provisioning = true;
            } else if (now - last_attempt >= pdMS_TO_TICKS(3000)) {
                // Connect is asynchronous. A busy driver simply retries on the next interval.
                esp_wifi_connect();
                last_attempt = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
} // namespace

esp_err_t wifi_manager_start(wifi_manager_callback_t callback) {
    if (events) return ESP_ERR_INVALID_STATE;
    if (!callback) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_flash_init();
    // Do not erase unrelated application data automatically on NVS errors.
    if (err != ESP_OK) return err;
    events = xEventGroupCreate();
    if (!events) return ESP_ERR_NO_MEM;
    notify = callback;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    if (!station || !esp_netif_create_default_wifi_ap()) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_set_hostname(station, "ledworm"));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_event, nullptr));
    return xTaskCreate(manager, "wifi_manager", 6144, nullptr, 4, nullptr) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t wifi_manager_reset() {
    if (!events) return ESP_ERR_INVALID_STATE;
    esp_err_t err = store(nullptr);
    if (err == ESP_OK) xEventGroupSetBits(events, REBOOT);
    return err;
}

esp_err_t wifi_manager_select_wifi() {
    if (!events) return ESP_ERR_INVALID_STATE;
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) return err;
    if (mode != WIFI_MODE_AP) return ESP_ERR_INVALID_STATE;
    xEventGroupSetBits(events, SELECT_WIFI);
    return ESP_OK;
}
