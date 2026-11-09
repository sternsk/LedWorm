#pragma once

#include "player_manager.h"

struct rgb_pixel_t {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

size_t matrixPixelIndex(uint8_t x, uint8_t y, bool serpentine = MATRIX_SERPENTINE);

// Hardwareunabhängiger Framebuffer: ein Fade und alle Spieler pro Frame.
class MatrixRenderer {
public:
    void render(const player_frame_t *players, size_t count, rgb_pixel_t ambient,
                const vitamin_frame_t *vitamin = nullptr);
    const rgb_pixel_t *pixels() const { return output_buffer; }
private:
    rgb_pixel_t frame_buffer[PIXEL_COUNT] = {};
    rgb_pixel_t output_buffer[PIXEL_COUNT] = {};
};
