#include "matrix_renderer.h"
#include <algorithm>
#include <cmath>

size_t matrixPixelIndex(uint8_t x, uint8_t y, bool serpentine) {
    if (serpentine && (y % 2) != 0) x = MATRIX_WIDTH - 1 - x;
    return size_t(y) * MATRIX_WIDTH + x;
}

static rgb_pixel_t playerColor(uint8_t hue, uint8_t value) {
    const uint8_t region = hue / 43;
    const uint8_t remainder = (hue % 43) * 6;
    const uint8_t rising = (value * remainder) / 255;
    const uint8_t falling = value - rising;
    switch (region) {
        case 0: return {value, rising, 0};
        case 1: return {falling, value, 0};
        case 2: return {0, value, rising};
        case 3: return {0, falling, value};
        case 4: return {rising, 0, value};
        default: return {value, 0, falling};
    }
}

static uint8_t fade(uint8_t current, uint8_t ambient) {
    const int difference = int(ambient) - current;
    // Aufrunden verhindert, dass der Fade kurz vor der Hintergrundfarbe stoppt.
    const int step = (std::abs(difference) * 30 + 254) / 255;
    return uint8_t(int(current) + (difference < 0 ? -step : step));
}

static uint8_t add(uint8_t a, uint8_t b) {
    return uint8_t(std::min(unsigned(a) + b, 255u));
}

void MatrixRenderer::render(const player_frame_t *players, size_t count,
                            rgb_pixel_t ambient, const vitamin_frame_t *vitamin) {
    rgb_pixel_t heads[PIXEL_COUNT] = {};
    bool occupied[PIXEL_COUNT] = {};
    for (size_t i = 0; players != nullptr && i < count; ++i) {
        const auto &player = players[i];
        if (!std::isfinite(player.x) || !std::isfinite(player.y) ||
            player.x < 0 || player.x >= MATRIX_WIDTH ||
            player.y < 0 || player.y >= MATRIX_HEIGHT) continue;
        const auto index = matrixPixelIndex(uint8_t(int(player.x + 0.5f) % MATRIX_WIDTH),
                                            uint8_t(int(player.y + 0.5f) % MATRIX_HEIGHT));
        occupied[index] = true;
        const auto color = playerColor(player.hue, player.brightness);
        heads[index].r = add(heads[index].r, color.r);
        heads[index].g = add(heads[index].g, color.g);
        heads[index].b = add(heads[index].b, color.b);
    }
    for (size_t i = 0; i < PIXEL_COUNT; ++i) {
        if (occupied[i]) {
            frame_buffer[i] = heads[i];
        } else {
            auto &pixel = frame_buffer[i];
            pixel = {fade(pixel.r, ambient.r), fade(pixel.g, ambient.g),
                     fade(pixel.b, ambient.b)};
        }
        output_buffer[i] = frame_buffer[i];
    }
    // Overlay only: vitamins disappear without leaving a worm-like trail.
    if (vitamin && vitamin->active && vitamin->x < MATRIX_WIDTH && vitamin->y < MATRIX_HEIGHT) {
        const auto index = matrixPixelIndex(vitamin->x, vitamin->y);
        if (!occupied[index]) output_buffer[index] = {180, 180, 180};
    }
}
