#include "ledworm.h"
#include "esp_timer.h"

// Joystickzustand
float worm_joystick_x;
float worm_joystick_y;

uint8_t length;
const char* playerName;

float x_position = .0f;
float y_position = .0f;
uint64_t last_move_time = 0;

bool active = false;

bool init(uint8_t playerId) {
    worm_joystick_x = 0.0f;
    worm_joystick_y = 0.0f;
    x_position = 12;
    y_position = 8;
    active = true;
    return true;
}

bool LedWorm::move(float joystick_x, float joystick_y, uint64_t now)
{
    if (last_move_time == 0) {
        last_move_time = now;
        return false;
    }

    float dt = (now - last_move_time) / 1000*1000.0f;
    last_move_time = now;

    // Horizontale Joystick-Achse steuert die Bewegung.
    float x_movement = nominal_speed * joystick_x * dt;
    float y_movement = nominal_speed * joystick_y * dt;

    x_position += x_movement;
    y_position += y_movement;

    return true;
}

void setJoystick(float x, float y) {
    worm_joystick_x = x;
    worm_joystick_y = y;
}

void deactivate(){
    active = false;
}