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
    uint8_t nominal_speed;
    bool active;

    // Konstruktor: sicherer Grundzustand
    LedWorm()
        : joystick_x(0.0f),
          joystick_y(0.0f),
          length(8),
          nominal_speed(50),
          active(false)
    {
    }

    bool init(uint8_t playerId);
    bool move(float joystick_x, float joystick_y, uint64_t now);    
    void setJoystick(float x, float y);
    void deactivate();
};