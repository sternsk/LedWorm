#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "webserver.h"
#include "led_effects.h"
#include "esp_log.h"

#define WIFI_SSID      "LedWorm-Netz"
#define WIFI_PASS      "ledworm123"
#define MAX_STA_CONN   4

static const char *WIFI_TAG = "WIFI_INIT";

// Event-Handler für den Wi-Fi-Status
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(WIFI_TAG, "Spieler verbunden - MAC: " MACSTR ", AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(WIFI_TAG, "Spieler getrennt - MAC: " MACSTR ", AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// Option A: ESP32 als eigenen Access Point (AP) starten
void init_wifi_softap(void) {
    // 1. Netzwerk-Interface initialisieren
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 2. Standard-Event-Loop erstellen
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 3. Wi-Fi Netzwerkschnittstelle für AP-Modus erstellen
    esp_netif_create_default_wifi_ap();

    // 4. Wi-Fi Treiber mit Standardkonfiguration initialisieren
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 5. Event-Handler registrieren, um Verbindungen zu überwachen
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // 6. AP-Spezifische Einstellungen festlegen
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.ap.password, WIFI_PASS);
    wifi_config.ap.ssid_len = strlen(WIFI_SSID);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = MAX_STA_CONN;
    // Wenn kein Passwort gewünscht ist, zu WIFI_AUTH_OPEN wechseln
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK; 

    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
    // 7. Wi-Fi Hardware starten
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_TAG, "Wi-Fi Access Point gestartet. SSID: %s", WIFI_SSID);
    ESP_LOGI(WIFI_TAG, "Öffne im Browser: http://192.168.4.1");
}
extern "C" void app_main(void) {
    // 1. NVS Flash initialisieren (Zwingend erforderlich für Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI("MAIN", "Initialisiere Wi-Fi Subsystem...");
    // 2. Wi-Fi im Access Point Modus starten
    init_wifi_softap();

    ESP_LOGI("MAIN", "Starte HTTP und WebSocket Server...");
    // 3. Webserver aus dem letzten Schritt starten
    httpd_handle_t server = start_webserver();

    // 4. Deinen FastLED Kometen/Wurm-Animationstask auf Core 1 starten
    xTaskCreatePinnedToCore(
        led_comet_task,   // Kommt aus der vorherigen Konversation
        "led_worm_task", 
        4096,             
        NULL,             
        5,                
        NULL,             
        1                 // Festgepinnt auf Core 1 (WLAN läuft primär auf Core 0)
    );
}
