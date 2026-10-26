#pragma once

#include <stdint.h>
#include "ledworm.h"

constexpr uint8_t MAX_PLAYERS = 8;

struct player_t {
    int client_fd;          // WebSocket-Verbindung
    uint32_t player_id;     // Interne, eindeutige Spieler-ID
    char player_name[32];   // Vom Spieler gewählter Name

    bool active;            // Spieler ist aktiv
    LedWorm worm;           // Spielinstanz dieses Spielers
};

bool addPlayer(int client_fd);
void removePlayer(int client_fd);
bool updateJoystick(int client_fd, float x, float y);