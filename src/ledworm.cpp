#include "ledworm.h"
#include "globals.h"
#include <algorithm>
#include <cmath>

static float wrapPosition(float position, float extent) {
    float wrapped = std::fmod(position, extent);
    if (wrapped < 0) wrapped += extent;
    return wrapped >= extent ? 0.0f : wrapped;
}

bool LedWorm::init(uint32_t playerId) {
    joystick_x = 0.0f;
    joystick_y = 0.0f;
    x_position = 0.0f;
    y_position = 0.0f;
    length = 8;
    brightness = 63;
    last_move_time = 0;
    clock_started = false;
    active = playerId != 0;
    return active;
}

bool LedWorm::move(float x, float y, uint64_t now) {
    if (!active || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    if (!clock_started || now < last_move_time) {
        last_move_time = now;
        clock_started = true;
        return false;
    }
    // esp_timer_get_time() liefert Mikrosekunden. Keine Sprünge nach Pausen.
    const float dt = std::min((now - last_move_time) / 1000000.0f, 0.1f);
    last_move_time = now;
    x_position = wrapPosition(x_position + nominal_speed * x * dt, MATRIX_WIDTH);
    y_position = wrapPosition(y_position + nominal_speed * y * dt, MATRIX_HEIGHT);
    return true;
}

void LedWorm::setJoystick(float x, float y) {
    joystick_x = x;
    joystick_y = y;
}

void LedWorm::deactivate() {
    active = false;
    joystick_x = joystick_y = 0.0f;
    clock_started = false;
}
