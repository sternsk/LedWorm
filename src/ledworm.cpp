#include "ledworm.h"

// Joystickzustand
float worm_joystick_x;
float worm_joystick_y;

uint8_t length;
const char* playerName;

float x_position = .0f;
float y_position = .0f;

bool active = false;

bool init(uint8_t playerId) {
    
    worm_joystick_x = 0.0f;
    worm_joystick_y = 0.0f;
    active = true;
    return true;
}

void setJoystick(float x, float y) {
    worm_joystick_x = x;
    worm_joystick_y = y;
}

void deactivate(){
    active = false;
}