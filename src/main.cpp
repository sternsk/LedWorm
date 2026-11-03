#include "wifi_manager.h"
#include "webserver.h"
#include "led_effects.h"
#include "player_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Only the Wi-Fi manager task owns this handle. Stop releases WebSocket players
// through the existing HTTP close callback before the setup server takes port 80.
static void network_changed(bool connected) {
    static httpd_handle_t server = nullptr;
    if (connected && !server) {
        server = start_webserver();
        ESP_ERROR_CHECK(server ? ESP_OK : ESP_FAIL);
    } else if (!connected && server) {
        ESP_ERROR_CHECK(httpd_stop(server));
        server = nullptr;
    }
}

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(player_manager_init() ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(led_render_task, "led_render_task",
                                          4096, nullptr, 5, nullptr, 1) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(wifi_manager_start(network_changed));
}
