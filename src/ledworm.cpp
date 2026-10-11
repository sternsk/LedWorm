#include "ledworm.h"

    uint8_t id = 0;  // Interne Spieler-ID
    char name[LEDWORM_NAME_LENGTH] = {};

    // Joystickzustand
    float worm_joystick_x = 0.0f;
    float worm_joystick_y = 0.0f;

    // Eigenschaften des LedWorms
    uint16_t speed = 10;
    uint16_t length = 6;
    int8_t orientation = 1;

    bool active = false;

void init(uint8_t playerId, const char* playerName) {
        id = playerId;

        if (playerName != nullptr) {
            strncpy(name, playerName, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        }

        worm_joystick_x = 0.0f;
        worm_joystick_y = 0.0f;
        active = true;
    }

    void setJoystick(float x, float y) {
        worm_joystick_x = x;
        worm_joystick_y = y;
    }