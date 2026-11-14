#pragma once

#include <stdint.h>
#include <atomic>

#define LED_PIN     7
constexpr uint8_t MATRIX_WIDTH = 20;
constexpr uint8_t MATRIX_HEIGHT = 16;
constexpr unsigned PIXEL_COUNT = MATRIX_WIDTH * MATRIX_HEIGHT;
constexpr uint8_t MATRIX_SKIPPED_COLUMN = 16;
// Benachbarte physische Spalten laufen entgegengesetzt. Ueber der Luecke an
// Position 16 findet deshalb kein Richtungswechsel statt.
constexpr bool MATRIX_SERPENTINE = true;

constexpr uint8_t MAX_PLAYERS = 8;

extern std::atomic<uint8_t> ambient_r;
extern std::atomic<uint8_t> ambient_g;
extern std::atomic<uint8_t> ambient_b;
