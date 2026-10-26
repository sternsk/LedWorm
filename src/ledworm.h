#pragma once

#include <stdint.h>
#include <stdbool.h>

class LedWorm {
public:

    // Joystickzustand
    float joystick_x;
    float joystick_y;

    // Eigenschaften des LedWorms
    float x_position;
    float y_position;

    uint8_t length;
    bool active;

    // Konstruktor: sicherer Grundzustand
    LedWorm()
        : joystick_x(0.0f),
          joystick_y(0.0f),
          length(8),
          active(false)
    {
    }

    bool init(uint8_t playerId);
    void setJoystick(float x, float y);
    void deactivate();
};