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
    uint32_t score = 0;
    uint8_t hue = 0;
    uint8_t saturation = 255;
    uint64_t last_input = 0;
    uint64_t last_frame = 0;
    bool input_pending = false;
    bool input_clock_started = false;
    bool force_automatic = false;
    float velocity_x = 0;
    float velocity_y = 0;
};

// Kopien statt Referenzen: Der Renderer hält während der LED-Ausgabe keinen Lock.
struct player_frame_t {
    uint32_t player_id;
    float x;
    float y;
    uint8_t hue;
    uint8_t brightness = 63;
    uint8_t saturation = 255;
    uint8_t body_count = 0;
    // Logical LED cells (y * MATRIX_WIDTH + x), newest first, including head.
    uint8_t body[LedWorm::MAX_LENGTH] = {};
    uint8_t area[LedWorm::AREA_BYTES] = {};
    uint8_t cut[LedWorm::AREA_BYTES] = {};
};

bool player_manager_init();
bool addPlayer(int client_fd);
void removePlayer(int client_fd);
bool updateJoystick(int client_fd, float x, float y);
constexpr uint64_t VITAMIN_LIFETIME_US = 8000000;
struct vitamin_frame_t {
    bool active = false;
    uint8_t x = 0;
    uint8_t y = 0;
    uint64_t age_us = 0;
};

constexpr size_t MARQUEE_TEXT_MAX = 32;
struct marquee_frame_t {
    bool active = false;
    int16_t x = 0;
    char text[MARQUEE_TEXT_MAX + 1] = {};
};

struct clock_frame_t {
    bool active = false;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
};

// Vitamin and player snapshots are captured under the same lock.
size_t updatePlayerFrames(uint64_t now, player_frame_t *frames, size_t capacity,
                          vitamin_frame_t *vitamin = nullptr);
bool setMarqueeText(const char *text);
void updateMarqueeFrame(uint64_t now, marquee_frame_t &frame);
bool setClockEnabled(bool enabled);
void updateClockFrame(clock_frame_t &frame);

struct player_state_t {
    uint32_t player_id;
    float x;
    float y;
    uint8_t length;
    uint8_t brightness;
    size_t active_players;
    uint32_t score;
    bool clock_enabled;
};

bool updateBrightness(int client_fd, double value);
// Read-only snapshot: telemetry must not advance movement or its clock.
bool getPlayerState(int client_fd, player_state_t &state);
#ifdef LEDWORM_HOST_TEST
void resetPlayersForTest();
#endif
