#pragma once
#include <string>  
#include "esp_http_server.h"
#include <stddef.h>


// Struktur für WebSocket-Verbindungsdaten
struct WebClientData {
    int fd;
    std::string player_id;
};

size_t get_websocket_client_count(httpd_handle_t server);

void send_player_list_to_all(httpd_handle_t hd);


httpd_handle_t start_webserver(void);