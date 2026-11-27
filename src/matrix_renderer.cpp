#include "matrix_renderer.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <utility>

size_t matrixPixelIndex(uint8_t x, uint8_t y, bool serpentine) {
    // application for 16x16-matrix
    if constexpr (MATRIX_ROTATION_DEGREES == 90) {
        const uint8_t original_x = x;
        x = y;
        y = MATRIX_HEIGHT - 1 - original_x;
    } else if constexpr (MATRIX_ROTATION_DEGREES == 180) {
        x = MATRIX_WIDTH - 1 - x;
        y = MATRIX_HEIGHT - 1 - y;
    } else if constexpr (MATRIX_ROTATION_DEGREES == 270) {
        const uint8_t original_x = x;
        x = MATRIX_WIDTH - 1 - y;
        y = original_x;
    }
    const uint8_t row_from_top = MATRIX_HEIGHT - 1 - y;
    const bool right_to_left = !serpentine || (row_from_top % 2) == 0;
    const uint8_t offset = right_to_left
        ? MATRIX_WIDTH - 1 - x
        : x;

    return size_t(row_from_top) * MATRIX_WIDTH + offset;
    /*
    // application for 20x16-matrix with hidden column

    const uint8_t physical_column = x + (x >= MATRIX_SKIPPED_COLUMN ? 1 : 0);
    // LED 0 sitzt oben links. Logisches Y waechst weiterhin von unten nach oben.
    const bool top_to_bottom = !serpentine || (physical_column % 2) == 0;
    const uint8_t offset = top_to_bottom ? MATRIX_HEIGHT - 1 - y : y;
    return size_t(physical_column) * MATRIX_HEIGHT + offset;
    */
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

static uint8_t glyphColumn(char character, unsigned column) {
    static constexpr uint8_t letters[][5] = {
        {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
    };
    static constexpr uint8_t digits[][5] = {
        {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e}
    };
    if (column >= 5 || character == ' ') return 0;
    const unsigned char upper = static_cast<unsigned char>(
        std::toupper(static_cast<unsigned char>(character)));
    if (upper >= 'A' && upper <= 'Z') return letters[upper - 'A'][column];
    if (upper >= '0' && upper <= '9') return digits[upper - '0'][column];
    static constexpr uint8_t question[] = {0x02,0x01,0x51,0x09,0x06};
    static constexpr uint8_t exclamation[] = {0x00,0x00,0x5f,0x00,0x00};
    static constexpr uint8_t period[] = {0x00,0x60,0x60,0x00,0x00};
    static constexpr uint8_t comma[] = {0x00,0x80,0x60,0x00,0x00};
    static constexpr uint8_t dash[] = {0x08,0x08,0x08,0x08,0x08};
    if (character == '!') return exclamation[column];
    if (character == '.') return period[column];
    if (character == ',') return comma[column] & 0x7f;
    if (character == '-') return dash[column];
    return question[column];
}

static void drawClockPixel(rgb_pixel_t *buffer, int x, int y, rgb_pixel_t color) {
    if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) return;
    buffer[matrixPixelIndex(uint8_t(x), uint8_t(y))] = color;
}

static void drawClockHand(rgb_pixel_t *buffer, int x1, int y1, rgb_pixel_t color) {
    int x0 = 8, y0 = 8;
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        drawClockPixel(buffer, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static uint8_t scaled(uint8_t value, float amount) {
    return uint8_t(std::lround(value * std::max(0.0f, std::min(1.0f, amount))));
}

static void renderAlarm(rgb_pixel_t *buffer, int32_t elapsed_seconds) {
    std::fill_n(buffer, PIXEL_COUNT, rgb_pixel_t{0, 0, 0});
    if (elapsed_seconds < 0) return;
    const float elapsed = std::min(float(elapsed_seconds), 600.0f);
    const float sky = std::max(0.0f, (elapsed - 300.0f) / 300.0f);
    const float ground = std::max(0.0f, (elapsed - 360.0f) / 240.0f);
    for (int y = 0; y < MATRIX_HEIGHT; ++y) {
        const float height = float(y) / float(MATRIX_HEIGHT - 1);
        rgb_pixel_t target;
        if (height >= 0.5f) {
            const float upper = (height - 0.5f) * 2.0f;
            target = {uint8_t(std::lround(54.0f * (1.0f - upper))),
                      uint8_t(std::lround(42.0f - 18.0f * upper)),
                      uint8_t(std::lround(6.0f + 62.0f * upper))};
        } else {
            const float lower = height * 2.0f;
            target = {uint8_t(std::lround(38.0f + 42.0f * lower)),
                      uint8_t(std::lround(12.0f + 38.0f * lower)),
                      uint8_t(std::lround(2.0f + 3.0f * lower))};
        }
        // Blue descends from the top; the warm horizon follows from the bottom.
        const float sky_amount = std::max(0.0f, std::min(1.0f,
            sky * MATRIX_HEIGHT - float(MATRIX_HEIGHT - 1 - y)));
        const float ground_amount = std::max(0.0f, std::min(1.0f,
            ground * MATRIX_HEIGHT - float(y)));
        const float amount = std::max(sky_amount, ground_amount);
        for (int x = 0; x < MATRIX_WIDTH; ++x) {
            buffer[matrixPixelIndex(uint8_t(x), uint8_t(y))] =
                {scaled(target.r, amount), scaled(target.g, amount), scaled(target.b, amount)};
        }
    }
    const float progress = elapsed / 600.0f;
    const float center_x = 3.0f + progress;
    const float center_y = 1.0f + progress * 6.0f;
    const float radius = 0.5f + progress * 3.0f;
    const float sun_amount = progress;
    for (int y = 0; y < MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < MATRIX_WIDTH; ++x) {
            const float distance = std::hypot(float(x) - center_x, float(y) - center_y);
            const float edge = std::max(0.0f, std::min(1.0f, radius + 0.75f - distance));
            if (edge <= 0) continue;
            const float amount = sun_amount * edge;
            auto &pixel = buffer[matrixPixelIndex(uint8_t(x), uint8_t(y))];
            pixel = {uint8_t(std::lround(pixel.r + (112.0f - pixel.r) * amount)),
                     uint8_t(std::lround(pixel.g * (1.0f - amount))),
                     uint8_t(std::lround(pixel.b * (1.0f - amount)))};
        }
    }
}

struct koch_point_t { float x, y; };

static void drawKochLine(rgb_pixel_t *buffer, koch_point_t from, koch_point_t to,
                         rgb_pixel_t color) {
    int x0 = int(std::lround(from.x)), y0 = int(std::lround(from.y));
    const int x1 = int(std::lround(to.x)), y1 = int(std::lround(to.y));
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < MATRIX_WIDTH && y0 >= 0 && y0 < MATRIX_HEIGHT)
            buffer[matrixPixelIndex(uint8_t(x0), uint8_t(y0))] = color;
        if (x0 == x1 && y0 == y1) break;
        const int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static void renderKochSnowflake(rgb_pixel_t *buffer, uint32_t elapsed_ms) {
    std::fill_n(buffer, PIXEL_COUNT, rgb_pixel_t{0, 0, 0});
    // Two subdivisions produce 3 * 4^2 segments and therefore 49 points.
    // Keep these arrays small: this renderer runs on a 4096-byte FreeRTOS stack.
    constexpr unsigned max_points = 49;
    koch_point_t points[max_points] = {{7.5f, 15.0f}, {0.5f, 2.0f},
                                       {14.5f, 2.0f}, {7.5f, 15.0f}};
    unsigned point_count = 4;
    const unsigned iteration = (elapsed_ms / 2400) % 3;
    for (unsigned level = 0; level < iteration; ++level) {
        koch_point_t expanded[max_points];
        unsigned next = 0;
        constexpr float root3_over_6 = 0.2886751346f;
        for (unsigned segment = 0; segment + 1 < point_count; ++segment) {
            const auto a = points[segment], b = points[segment + 1];
            const float dx = b.x - a.x, dy = b.y - a.y;
            const koch_point_t one = {a.x + dx / 3.0f, a.y + dy / 3.0f};
            const koch_point_t two = {a.x + dx * 2.0f / 3.0f, a.y + dy * 2.0f / 3.0f};
            const koch_point_t peak = {(a.x + b.x) * 0.5f + dy * root3_over_6,
                                       (a.y + b.y) * 0.5f - dx * root3_over_6};
            expanded[next++] = a;
            expanded[next++] = one;
            expanded[next++] = peak;
            expanded[next++] = two;
        }
        expanded[next++] = points[point_count - 1];
        std::copy_n(expanded, next, points);
        point_count = next;
    }
    const unsigned segment_count = point_count - 1;
    const uint32_t within_stage = elapsed_ms % 2400;
    const unsigned visible = std::min(segment_count,
        1u + unsigned(uint64_t(within_stage) * segment_count / 1900));
    const float pulse = 0.5f + 0.5f * std::sin(float(elapsed_ms) * 0.0062831853f);
    const rgb_pixel_t color = {uint8_t(10 + 8 * pulse), uint8_t(28 + 20 * pulse),
                               uint8_t(42 + 22 * pulse)};
    for (unsigned segment = 0; segment < visible; ++segment)
        drawKochLine(buffer, points[segment], points[segment + 1], color);
    const auto tip = points[visible];
    const int tx = int(std::lround(tip.x)), ty = int(std::lround(tip.y));
    if (tx >= 0 && tx < MATRIX_WIDTH && ty >= 0 && ty < MATRIX_HEIGHT)
        buffer[matrixPixelIndex(uint8_t(tx), uint8_t(ty))] = {48, 63, 63};
}

void MatrixRenderer::render(const player_frame_t *players, size_t count,
                            rgb_pixel_t ambient, const vitamin_frame_t *vitamin,
                            const marquee_frame_t *marquee,
                            const clock_frame_t *clock,
                            const alarm_frame_t *alarm,
                            const snowflake_frame_t *snowflake) {
    rgb_pixel_t heads[PIXEL_COUNT] = {};
    bool occupied[PIXEL_COUNT] = {};
    rgb_pixel_t background[PIXEL_COUNT];
    bool clear[PIXEL_COUNT] = {};
    std::fill_n(background, PIXEL_COUNT, rgb_pixel_t{0, 0, 0});
    // Nur logische Zellen erhalten den Hintergrund. Die verdeckte physische
    // Spalte bleibt dadurch auch bei einer Ambient-Farbe dauerhaft schwarz.
    for (unsigned y = 0; y < MATRIX_HEIGHT; ++y) {
        for (unsigned x = 0; x < MATRIX_WIDTH; ++x) {
            background[matrixPixelIndex(uint8_t(x), uint8_t(y))] = ambient;
        }
    }
    for (size_t i = 0; players != nullptr && i < count; ++i) {
        const auto &player = players[i];
        const bool display_overlay = (clock && clock->active) ||
            (snowflake && snowflake->active) ||
            (marquee && marquee->active && !marquee->initial_animation);
        if (player.automatic && display_overlay) continue;
        const auto tint = playerColor(player.hue, player.saturation, 5);
        for (unsigned cell = 0; cell < MATRIX_CELL_COUNT; ++cell) {
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
    if (marquee && marquee->active) {
        const size_t length = strnlen(marquee->text, MARQUEE_TEXT_MAX);
        size_t first = 0;
        if (marquee->x + 4 < 0)
            first = size_t((-int(marquee->x) - 4 + 5) / 6);
        size_t last = length;
        if (marquee->x <= MATRIX_WIDTH - 1)
            last = std::min(length, size_t((MATRIX_WIDTH - 1 - marquee->x) / 6 + 1));
        else
            last = 0;
        for (size_t character = first; character < last; ++character) {
            for (unsigned column = 0; column < 5; ++column) {
                const int x = marquee->x + int(character * 6 + column);
                if (x < 0 || x >= MATRIX_WIDTH) continue;
                const uint8_t bits = glyphColumn(marquee->text[character], column);
                for (unsigned row = 0; row < 7; ++row) {
                    if (!(bits & (1u << row))) continue;
                    const uint8_t y = uint8_t((MATRIX_HEIGHT - 7) / 2 + 6 - row);
                    output_buffer[matrixPixelIndex(uint8_t(x), y)] = {12, 12, 12};
                }
            }
        }
    }
    if (clock && clock->active) {
        constexpr float pi = 3.14159265358979323846f;
        constexpr float center = 7.5f;
        for (unsigned marker = 0; marker < 12; ++marker) {
            const float angle = float(marker) * 2.0f * pi / 12.0f;
            drawClockPixel(output_buffer,
                int(std::lround(center + std::sin(angle) * 7.0f)),
                int(std::lround(center + std::cos(angle) * 7.0f)), {16, 16, 16});
        }
        const auto endpoint = [center, pi](float units, float length) {
            const float angle = units * 2.0f * pi;
            return std::pair<int, int>{
                int(std::lround(center + std::sin(angle) * length)),
                int(std::lround(center + std::cos(angle) * length))};
        };
        const auto hour = endpoint((clock->hour % 12 + clock->minute / 60.0f) / 12.0f, 4.0f);
        const auto minute = endpoint(clock->minute / 60.0f, 6.0f);
        const auto second = endpoint(clock->second / 60.0f, 6.0f);
        drawClockHand(output_buffer, hour.first, hour.second, {12, 12, 12});
        drawClockHand(output_buffer, minute.first, minute.second, {12, 12, 12});
        drawClockHand(output_buffer, second.first, second.second, {12, 12, 12});
        drawClockPixel(output_buffer, 8, 8, {12, 12, 12});
    }
    if (snowflake && snowflake->active) {
        renderKochSnowflake(frame_buffer, snowflake->elapsed_ms);
        std::copy_n(frame_buffer, PIXEL_COUNT, output_buffer);
    }
    if (alarm && alarm->active) {
        renderAlarm(frame_buffer, alarm->elapsed_seconds);
        std::copy_n(frame_buffer, PIXEL_COUNT, output_buffer);
    }
}
