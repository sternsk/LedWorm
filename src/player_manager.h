#pragma once

#include <stdint.h>
#include "ledworm.h"
#include "globals.h"

struct player_t {
    int socketFd;          // WebSocket-Verbindung
    uint32_t player_id;     // Interne, eindeutige Spieler-ID
    char player_name[32];   // Vom Spieler gewählter Name

    bool active;            // Spieler ist aktiv
    LedWorm worm;           // Spielinstanz dieses Spielers
};

struct joystick_event_t {
    uint32_t player_id;
    float x;
    float y;
};

bool player_manager_init();
static bool findPlayerId(int client_fd, uint32_t& player_id);

bool addPlayer(int client_fd);
void removePlayer(int client_fd);
bool updateJoystick(int client_fd, float x, float y);