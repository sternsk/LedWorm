#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ledworm.h"
#include "globals.h"

struct player_t {
    int socketFd = -1;
    uint32_t player_id = 0;
    char player_name[32] = {};
    bool active = false;
    LedWorm worm;
};

// Kopien statt Referenzen: Der Renderer hält während der LED-Ausgabe keinen Lock.
struct player_frame_t {
    uint32_t player_id;
    float x;
    float y;
    uint8_t hue;
};

bool player_manager_init();
bool addPlayer(int client_fd);
void removePlayer(int client_fd);
bool updateJoystick(int client_fd, float x, float y);
size_t updatePlayerFrames(uint64_t now, player_frame_t *frames, size_t capacity);
