#include "ledworm.h"
#include "globals.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static float wrapPosition(float position, float extent) {
    float wrapped = std::fmod(position, extent);
    if (wrapped < 0) wrapped += extent;
    return wrapped >= extent ? 0.0f : wrapped;
}

void LedWorm::encloseArea(uint8_t cell, unsigned intersection) {
    // Unwrap the loop locally so a small loop across an edge fills the small area.
    int16_t px[MAX_LENGTH + 1], py[MAX_LENGTH + 1];
    px[0] = cell % MATRIX_WIDTH;
    py[0] = cell / MATRIX_WIDTH;
    int min_x = px[0], max_x = px[0], min_y = py[0], max_y = py[0];
    uint8_t previous = cell;
    const unsigned vertices = intersection + 2;
    for (unsigned i = 1; i < vertices; ++i) {
        const uint8_t next = bodyCell(i - 1);
        int dx = int(next % MATRIX_WIDTH) - int(previous % MATRIX_WIDTH);
        int dy = int(next / MATRIX_WIDTH) - int(previous / MATRIX_WIDTH);
        if (dx > MATRIX_WIDTH / 2) dx -= MATRIX_WIDTH;
        if (dx < -MATRIX_WIDTH / 2) dx += MATRIX_WIDTH;
        if (dy > MATRIX_HEIGHT / 2) dy -= MATRIX_HEIGHT;
        if (dy < -MATRIX_HEIGHT / 2) dy += MATRIX_HEIGHT;
        px[i] = px[i - 1] + dx; py[i] = py[i - 1] + dy;
        min_x = std::min(min_x, int(px[i])); max_x = std::max(max_x, int(px[i]));
        min_y = std::min(min_y, int(py[i])); max_y = std::max(max_y, int(py[i]));
        previous = next;
    }
    // A loop winding around the torus has no uniquely enclosed interior.
    if (px[vertices - 1] != px[0] || py[vertices - 1] != py[0]) return;
    for (int y = min_y + 1; y < max_y; ++y) {
        for (int x = min_x + 1; x < max_x; ++x) {
            bool inside = false, boundary = false;
            for (unsigned i = 1; i < vertices; ++i) {
                const int ax = px[i - 1], ay = py[i - 1], bx = px[i], by = py[i];
                if ((x - ax) * (by - ay) == (y - ay) * (bx - ax) &&
                    x >= std::min(ax, bx) && x <= std::max(ax, bx) &&
                    y >= std::min(ay, by) && y <= std::max(ay, by)) {
                    boundary = true; break;
                }
                if ((ay > y) != (by > y) &&
                    x < ax + float(bx - ax) * (y - ay) / (by - ay)) inside = !inside;
            }
            if (inside && !boundary) {
                const unsigned column = (x % MATRIX_WIDTH + MATRIX_WIDTH) % MATRIX_WIDTH;
                const unsigned row = (y % MATRIX_HEIGHT + MATRIX_HEIGHT) % MATRIX_HEIGHT;
                const unsigned index = row * MATRIX_WIDTH + column;
                area[index / 8] |= uint8_t(1u << (index % 8));
            }
        }
    }
}

void LedWorm::rememberPosition(float x, float y) {
    const unsigned column = int(wrapPosition(x, MATRIX_WIDTH) + 0.5f) % MATRIX_WIDTH;
    const unsigned row = int(wrapPosition(y, MATRIX_HEIGHT) + 0.5f) % MATRIX_HEIGHT;
    const uint8_t cell = row * MATRIX_WIDTH + column;
    if (path_count && path[path_head] == cell) return;
    ++movement_steps;
    for (unsigned age = 1; age < bodyCount(); ++age) {
        if (bodyCell(age) != cell) continue;
        encloseArea(cell, age);
        for (unsigned removed = 0; removed < age; ++removed) {
            const unsigned old = bodyCell(removed);
            cut[old / 8] |= uint8_t(1u << (old % 8));
        }
        path_head = (path_head + MAX_LENGTH - age) % MAX_LENGTH;
        path_count -= age;
        length = uint8_t(std::max(1, int(length) - int(age)));
        return; // The intersection is the new head; the older tail remains.
    }
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
    movement_steps = 0;
    memset(area, 0, sizeof(area));
    memset(cut, 0, sizeof(cut));
    active = playerId != 0;
    return active;
}

bool LedWorm::move(float x, float y, uint64_t now, bool wrap) {
    if (!active || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    memset(cut, 0, sizeof(cut));
    if (!wrap) {
        x_position = std::clamp(x_position, 0.0f, float(MATRIX_WIDTH - 1));
        y_position = std::clamp(y_position, 0.0f, float(MATRIX_HEIGHT - 1));
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
    const float next_x = x_position + nominal_speed * x * dt;
    const float next_y = y_position + nominal_speed * y * dt;
    const float dx = (wrap ? next_x : std::clamp(next_x, 0.0f, float(MATRIX_WIDTH - 1))) - x_position;
    const float dy = (wrap ? next_y : std::clamp(next_y, 0.0f, float(MATRIX_HEIGHT - 1))) - y_position;
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
