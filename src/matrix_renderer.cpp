#include "matrix_renderer.h"
#include <algorithm>
#include <cmath>

size_t matrixPixelIndex(uint8_t x, uint8_t y, bool serpentine) {
    if (serpentine && (y % 2) != 0) x = MATRIX_WIDTH - 1 - x;
    return size_t(y) * MATRIX_WIDTH + x;
}

static rgb_pixel_t playerColor(uint8_t hue, uint8_t saturation, uint8_t value) {
    const uint8_t region = hue / 43;
    const uint8_t remainder = (hue % 43) * 6;
    const uint8_t rising = (value * remainder) / 255;
    const uint8_t falling = value - rising;
    rgb_pixel_t full;
    switch (region) {
        case 0: full = {value, rising, 0}; break;
        case 1: full = {falling, value, 0}; break;
        case 2: full = {0, value, rising}; break;
        case 3: full = {0, falling, value}; break;
        case 4: full = {rising, 0, value}; break;
        default: full = {value, 0, falling}; break;
    }
    const auto apply = [value, saturation](uint8_t channel) {
        return uint8_t(value - (unsigned(value - channel) * saturation) / 255);
    };
    return {apply(full.r), apply(full.g), apply(full.b)};
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
    rgb_pixel_t background[PIXEL_COUNT];
    bool clear[PIXEL_COUNT] = {};
    std::fill_n(background, PIXEL_COUNT, ambient);
    for (size_t i = 0; players != nullptr && i < count; ++i) {
        const auto &player = players[i];
        const auto tint = playerColor(player.hue, player.saturation, 5);
        for (unsigned cell = 0; cell < PIXEL_COUNT; ++cell) {
            const auto physical = matrixPixelIndex(cell % MATRIX_WIDTH, cell / MATRIX_WIDTH);
            const unsigned bit = 1u << (cell % 8);
            if (player.area[cell / 8] & bit) background[physical] = tint;
            if (player.cut[cell / 8] & bit) clear[physical] = true;
        }
        if (!std::isfinite(player.x) || !std::isfinite(player.y) ||
            player.x < 0 || player.x >= MATRIX_WIDTH ||
            player.y < 0 || player.y >= MATRIX_HEIGHT) continue;
        const auto index = matrixPixelIndex(uint8_t(int(player.x + 0.5f) % MATRIX_WIDTH),
                                            uint8_t(int(player.y + 0.5f) % MATRIX_HEIGHT));
        occupied[index] = true;
        const auto color = playerColor(player.hue, player.saturation, player.brightness);
        heads[index].r = add(heads[index].r, color.r);
        heads[index].g = add(heads[index].g, color.g);
        heads[index].b = add(heads[index].b, color.b);
        bool drawn[PIXEL_COUNT] = {};
        drawn[index] = true;
        for (unsigned segment = 0; segment < player.body_count; ++segment) {
            const unsigned cell = player.body[segment];
            const auto body_index = matrixPixelIndex(cell % MATRIX_WIDTH, cell / MATRIX_WIDTH);
            if (drawn[body_index]) continue;
            drawn[body_index] = occupied[body_index] = true;
            heads[body_index].r = add(heads[body_index].r, color.r);
            heads[body_index].g = add(heads[body_index].g, color.g);
            heads[body_index].b = add(heads[body_index].b, color.b);
        }
    }
    for (size_t i = 0; i < PIXEL_COUNT; ++i) {
        if (occupied[i]) {
            frame_buffer[i] = heads[i];
        } else if (clear[i]) {
            frame_buffer[i] = background[i];
        } else {
            auto &pixel = frame_buffer[i];
            pixel = {fade(pixel.r, background[i].r), fade(pixel.g, background[i].g),
                     fade(pixel.b, background[i].b)};
        }
        output_buffer[i] = frame_buffer[i];
    }
    // Overlay only: vitamins disappear without leaving a worm-like trail.
    if (vitamin && vitamin->active && vitamin->x < MATRIX_WIDTH && vitamin->y < MATRIX_HEIGHT) {
        const auto index = matrixPixelIndex(vitamin->x, vitamin->y);
        if (!occupied[index]) {
            const uint64_t age = std::min(vitamin->age_us, VITAMIN_LIFETIME_US);
            const float phase = float(std::min(age, VITAMIN_LIFETIME_US - age)) /
                                float(VITAMIN_LIFETIME_US / 2);
            const float amount = phase * phase * (3.0f - 2.0f * phase);
            // Blend with the actual pixel, including claimed areas and fading trails.
            const auto blend = [amount](uint8_t channel) {
                return uint8_t(std::lround(channel + (48.0f - channel) * amount));
            };
            auto &pixel = output_buffer[index];
            pixel = {blend(pixel.r), blend(pixel.g), blend(pixel.b)};
        }
    }
}
