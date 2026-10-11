#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

constexpr size_t LEDWORM_NAME_LENGTH = 24;

class LedWorm {
public:
    uint8_t id;  // Interne Spieler-ID
    char name[LEDWORM_NAME_LENGTH];

    // Joystickzustand
    float joystick_x;
    float joystick_y;

    // Eigenschaften des LedWorms
    uint16_t speed;
    uint16_t length;
    int8_t orientation;

    bool active;

    void init(uint8_t playerId, const char* playerName);
    void setJoystick(float x, float y);
};