#pragma once

#include <atomic>

#define LED_PIN     7
#define PIXEL_COUNT 256

constexpr uint8_t MAX_PLAYERS = 8;

extern volatile uint8_t ambient_r = 12;
extern volatile uint8_t ambient_g = 8;
extern volatile uint8_t ambient_b = 9;