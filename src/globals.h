#pragma once

#include <atomic>

/*
extern std::atomic<float> joystick_x;
extern std::atomic<float> joystick_y;
*/

constexpr uint8_t MAX_PLAYERS = 8;

extern volatile uint8_t ambient_r;
extern volatile uint8_t ambient_g;
extern volatile uint8_t ambient_b;