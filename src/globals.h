#pragma once

#include <stdint.h>
#include <atomic>

#define LED_PIN     7
constexpr uint8_t MATRIX_WIDTH = 16;
constexpr uint8_t MATRIX_HEIGHT = 16;
constexpr unsigned MATRIX_CELL_COUNT = MATRIX_WIDTH * MATRIX_HEIGHT;
constexpr uint8_t MATRIX_PHYSICAL_COLUMNS = MATRIX_WIDTH;
constexpr unsigned PIXEL_COUNT = MATRIX_PHYSICAL_COLUMNS * MATRIX_HEIGHT;
// Nullbasierter Index der verdeckten physischen 16. Spalte.
constexpr uint8_t MATRIX_SKIPPED_COLUMN = 15;
// Die verdeckte Spalte bleibt elektrisch Teil der LED-Kette.
constexpr bool MATRIX_SERPENTINE = true;

constexpr uint8_t MAX_PLAYERS = 8;

extern std::atomic<uint8_t> ambient_r;
extern std::atomic<uint8_t> ambient_g;
extern std::atomic<uint8_t> ambient_b;
