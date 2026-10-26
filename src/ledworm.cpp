#include "ledworm.h"

// Joystickzustand
float worm_joystick_x;
float worm_joystick_y;

uint8_t length;

float x_position = .0f;
float y_position = .0f;

bool active = false;

void init(uint8_t playerId, const char* playerName) {
    
    worm_joystick_x = 0.0f;
    worm_joystick_y = 0.0f;
    active = true;
}

void setJoystick(float x, float y) {
    worm_joystick_x = x;
    worm_joystick_y = y;
}