#pragma once

#include <stdint.h>
#include <atomic>

#define LED_PIN     7
constexpr uint8_t MATRIX_WIDTH = 16;
constexpr uint8_t MATRIX_HEIGHT = 16;
constexpr unsigned MATRIX_CELL_COUNT = MATRIX_WIDTH * MATRIX_HEIGHT;
constexpr uint8_t MATRIX_PHYSICAL_COLUMNS = 21;
constexpr unsigned PIXEL_COUNT = MATRIX_PHYSICAL_COLUMNS * MATRIX_HEIGHT;
// Nullbasierter Index der verdeckten physischen 16. Spalte.
constexpr uint8_t MATRIX_SKIPPED_COLUMN = 15;
// Die verdeckte Spalte bleibt elektrisch Teil der LED-Kette.
constexpr bool MATRIX_SERPENTINE = true;

// Logical display rotation for the physical mounting orientation.
#ifndef MATRIX_ROTATION
#define MATRIX_ROTATION 180
#endif
constexpr unsigned MATRIX_ROTATION_DEGREES = MATRIX_ROTATION;
static_assert(MATRIX_ROTATION_DEGREES == 0 || MATRIX_ROTATION_DEGREES == 90 ||
              MATRIX_ROTATION_DEGREES == 180 || MATRIX_ROTATION_DEGREES == 270,
              "MATRIX_ROTATION must be 0, 90, 180, or 270");
static_assert((MATRIX_ROTATION_DEGREES % 180) == 0 || MATRIX_WIDTH == MATRIX_HEIGHT,
              "90-degree matrix rotation requires a square logical matrix");

constexpr uint8_t MAX_PLAYERS = 8;

extern std::atomic<uint8_t> ambient_r;
extern std::atomic<uint8_t> ambient_g;
extern std::atomic<uint8_t> ambient_b;
