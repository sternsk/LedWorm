#pragma once

#include <stdint.h>

class LedWorm {
public:
    static constexpr uint8_t MAX_LENGTH = 255;
    float joystick_x = 0.0f;
    float joystick_y = 0.0f;
    float x_position = 0.0f;
    float y_position = 0.0f;
    uint8_t length = 8;
    uint8_t brightness = 63;
    uint8_t nominal_speed = 20;
    bool active = false;

    bool init(uint32_t playerId);
    bool move(float x, float y, uint64_t now);
    void setJoystick(float x, float y);
    void deactivate();
    uint8_t bodyCount() const { return path_count < length ? path_count : length; }
    uint8_t bodyCell(unsigned age) const {
        return path[(path_head + MAX_LENGTH - age) % MAX_LENGTH];
    }

private:
    uint64_t last_move_time = 0;
    bool clock_started = false;
    uint8_t path[MAX_LENGTH] = {};
    unsigned path_head = 0;
    uint8_t path_count = 0;
    void rememberPosition(float x, float y);
};
