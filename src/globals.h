#pragma once

#include <stdint.h>
#include <atomic>

#define LED_PIN     27
// Logical landscape canvas. The physical panel is an 8x32 matrix rotated 90 degrees.
constexpr uint8_t MATRIX_WIDTH = 32;
constexpr uint8_t MATRIX_HEIGHT = 8;
constexpr unsigned PIXEL_COUNT = MATRIX_WIDTH * MATRIX_HEIGHT;
constexpr uint8_t MATRIX_PHYSICAL_WIDTH = 8;
constexpr uint8_t MATRIX_PHYSICAL_HEIGHT = 32;
// true: every second physical 8-pixel row runs in the opposite direction.
constexpr bool MATRIX_SERPENTINE = true;

constexpr uint8_t MAX_PLAYERS = 8;

extern std::atomic<uint8_t> ambient_r;
extern std::atomic<uint8_t> ambient_g;
extern std::atomic<uint8_t> ambient_b;
