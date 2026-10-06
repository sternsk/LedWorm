#pragma once
#include <string>  
#include "esp_http_server.h"


// Struktur für WebSocket-Verbindungsdaten
struct WebClientData {
    int fd;
    std::string player_id;
};

void send_player_list_to_all(httpd_handle_t hd);

static esp_err_t get_index_handler(httpd_req_t *req);

static esp_err_t ws_handler(httpd_req_t *req);

httpd_handle_t start_webserver(void);