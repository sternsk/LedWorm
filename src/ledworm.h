#pragma once

#include <stdint.h>

class LedWorm {
public:
    float joystick_x = 0.0f;
    float joystick_y = 0.0f;
    float x_position = 0.0f;
    float y_position = 0.0f;
    uint8_t length = 8;
    uint8_t brightness = 63;
    uint8_t nominal_speed = 30;
    bool active = false;

    bool init(uint32_t playerId);
    bool move(float x, float y, uint64_t now);
    void setJoystick(float x, float y);
    void deactivate();

private:
    uint64_t last_move_time = 0;
    bool clock_started = false;
};
