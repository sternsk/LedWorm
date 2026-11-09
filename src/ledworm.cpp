#include "ledworm.h"
#include "globals.h"
#include <algorithm>
#include <cmath>

static float wrapPosition(float position, float extent) {
    float wrapped = std::fmod(position, extent);
    if (wrapped < 0) wrapped += extent;
    return wrapped >= extent ? 0.0f : wrapped;
}

void LedWorm::rememberPosition(float x, float y) {
    const unsigned column = int(wrapPosition(x, MATRIX_WIDTH) + 0.5f) % MATRIX_WIDTH;
    const unsigned row = int(wrapPosition(y, MATRIX_HEIGHT) + 0.5f) % MATRIX_HEIGHT;
    const uint8_t cell = row * MATRIX_WIDTH + column;
    if (path_count && path[path_head] == cell) return;
    if (path_count) path_head = (path_head + 1) % MAX_LENGTH;
    path[path_head] = cell;
    if (path_count < length) ++path_count;
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
    path_count = 0;
    path_head = 0;
    active = playerId != 0;
    return active;
}

bool LedWorm::move(float x, float y, uint64_t now) {
    if (!active || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    rememberPosition(x_position, y_position);
    if (!clock_started || now < last_move_time) {
        last_move_time = now;
        clock_started = true;
        return false;
    }
    // esp_timer_get_time() liefert Mikrosekunden. Keine Sprünge nach Pausen.
    const float dt = std::min((now - last_move_time) / 1000000.0f, 0.1f);
    last_move_time = now;
    const float dx = nominal_speed * x * dt, dy = nominal_speed * y * dt;
    // Sample at most half a cell apart so longer frames retain intermediate cells.
    const unsigned steps = unsigned(std::ceil(std::max(std::abs(dx), std::abs(dy)) * 2));
    for (unsigned i = 1; i <= steps; ++i) {
        rememberPosition(x_position + dx * i / steps, y_position + dy * i / steps);
    }
    x_position = wrapPosition(x_position + dx, MATRIX_WIDTH);
    y_position = wrapPosition(y_position + dy, MATRIX_HEIGHT);
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
