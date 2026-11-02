#pragma once

#include <stdint.h>
#include <atomic>

#define LED_PIN     7
constexpr uint8_t MATRIX_WIDTH = 16;
constexpr uint8_t MATRIX_HEIGHT = 16;
constexpr unsigned PIXEL_COUNT = MATRIX_WIDTH * MATRIX_HEIGHT;
// true: jede zweite Zeile ist in entgegengesetzter Richtung verdrahtet.
constexpr bool MATRIX_SERPENTINE = false;

constexpr uint8_t MAX_PLAYERS = 8;

extern std::atomic<uint8_t> ambient_r;
extern std::atomic<uint8_t> ambient_g;
extern std::atomic<uint8_t> ambient_b;
