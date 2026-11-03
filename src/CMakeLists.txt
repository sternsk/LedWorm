idf_component_register(
    SRCS "main.cpp" "wifi_manager.cpp" "webserver.cpp" "globals.cpp"
         "led_effects.cpp" "ledworm.cpp" "matrix_renderer.cpp" "player_manager.cpp"
    EMBED_FILES "index.html" "wifi_setup.html"
    REQUIRES esp_wifi esp_event esp_netif esp_http_server nvs_flash json
             esp_timer esp_driver_rmt led_strip mdns
)
