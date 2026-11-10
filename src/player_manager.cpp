#include "player_manager.h"
#include <cmath>
#include <limits>
#include <random>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static player_t players[MAX_PLAYERS];
static SemaphoreHandle_t playerMutex = nullptr;
static uint32_t nextPlayerId = 1;
static vitamin_frame_t vitamin;
static uint64_t vitamin_since = 0;
static bool vitamin_clock_started = false;
static bool waiting_for_first_vitamin = true;
static std::minstd_rand vitamin_random{1};
constexpr uint64_t FIRST_VITAMIN_DELAY = 5000000;
constexpr uint64_t VITAMIN_DELAY = 3000000;
constexpr uint64_t AUTOPILOT_DELAY = 3000000;
constexpr float AUTO_ACCELERATION = 8.0f; // LED cells / second squared.
constexpr float AUTO_DECELERATION = 6.0f;
constexpr float AUTO_CRUISE_SPEED = 2.0f;
constexpr float AUTO_TURN_RATE = 3.0f; // radians / second.

static void movePlayer(player_t &player, uint64_t now) {
    const bool reset_clock = !player.input_clock_started || now < player.last_frame;
    const float dt = reset_clock ? 0.0f :
        std::fmin(float(now - player.last_frame) / 1000000.0f, 0.1f);
    if (reset_clock) player.velocity_x = player.velocity_y = 0;
    if (reset_clock || player.input_pending) {
        player.last_input = now;
        player.input_clock_started = true;
        player.input_pending = false;
        player.force_automatic = false;
    }
    float x = player.worm.joystick_x, y = player.worm.joystick_y;
    const bool automatic = player.force_automatic ||
                           now - player.last_input >= AUTOPILOT_DELAY;
    if (automatic) {
        const float current_speed = std::hypot(player.velocity_x, player.velocity_y);
        float wanted_x = current_speed > 0 ? player.velocity_x : 1.0f;
        float wanted_y = current_speed > 0 ? player.velocity_y : 0.6f;
        float wanted_speed = std::fmin(AUTO_CRUISE_SPEED, float(player.worm.nominal_speed));
        if (vitamin.active && now >= vitamin_since && now - vitamin_since < VITAMIN_LIFETIME_US) {
            const float dx = float(vitamin.x) - player.worm.x_position;
            const float dy = float(vitamin.y) - player.worm.y_position;
            const float distance = std::hypot(dx, dy);
            // Slow near the vitamin, but continue cruising after collecting it.
            wanted_speed = std::fmin(float(player.worm.nominal_speed),
                std::fmax(wanted_speed, std::sqrt(2 * AUTO_DECELERATION * std::fmax(0.0f, distance - 0.25f))));
            if (distance > 0) {
                wanted_x = dx / distance * wanted_speed;
                wanted_y = dy / distance * wanted_speed;
            }
        } else {
            const float margin = std::fmax(1.5f, current_speed / AUTO_TURN_RATE + 0.5f);
            if (player.worm.x_position < margin) wanted_x = std::fabs(wanted_x) + 1;
            if (player.worm.x_position > MATRIX_WIDTH - 1 - margin) wanted_x = -std::fabs(wanted_x) - 1;
            if (player.worm.y_position < margin) wanted_y = std::fabs(wanted_y) + 1;
            if (player.worm.y_position > MATRIX_HEIGHT - 1 - margin) wanted_y = -std::fabs(wanted_y) - 1;
        }
        const float rate = wanted_speed < current_speed ? AUTO_DECELERATION : AUTO_ACCELERATION;
        const float next_speed = current_speed + std::fmax(-rate * dt, std::fmin(rate * dt, wanted_speed - current_speed));
        const float target_heading = std::atan2(wanted_y, wanted_x);
        float heading = current_speed > 0 ? std::atan2(player.velocity_y, player.velocity_x) : target_heading;
        const float turn = std::remainder(target_heading - heading, 2.0f * 3.14159265f);
        heading += std::fmax(-AUTO_TURN_RATE * dt, std::fmin(AUTO_TURN_RATE * dt, turn));
        player.velocity_x = std::cos(heading) * next_speed;
        player.velocity_y = std::sin(heading) * next_speed;
        // At an edge, slide along it instead of stopping or wrapping.
        if (dt > 0) {
            const float px = std::fmax(0.0f, std::fmin(float(MATRIX_WIDTH - 1), player.worm.x_position));
            const float py = std::fmax(0.0f, std::fmin(float(MATRIX_HEIGHT - 1), player.worm.y_position));
            const float bounded_x = std::fmax(-px / dt,
                std::fmin((MATRIX_WIDTH - 1 - px) / dt, player.velocity_x));
            const float bounded_y = std::fmax(-py / dt,
                std::fmin((MATRIX_HEIGHT - 1 - py) / dt, player.velocity_y));
            player.velocity_x = bounded_x; player.velocity_y = bounded_y;
            if (next_speed > 0 && std::hypot(bounded_x, bounded_y) < 0.001f) {
                const float inward_x = (MATRIX_WIDTH - 1) / 2.0f - player.worm.x_position;
                const float inward_y = (MATRIX_HEIGHT - 1) / 2.0f - player.worm.y_position;
                const float inward_length = std::hypot(inward_x, inward_y);
                if (inward_length > 0) {
                    player.velocity_x = inward_x / inward_length * next_speed;
                    player.velocity_y = inward_y / inward_length * next_speed;
                }
            }
        }
        const float speed = player.worm.nominal_speed;
        x = speed > 0 ? player.velocity_x / speed : 0;
        y = speed > 0 ? player.velocity_y / speed : 0;
    } else {
        // Manual input retains immediate control; remember its velocity for takeover.
        player.velocity_x = x * player.worm.nominal_speed;
        player.velocity_y = y * player.worm.nominal_speed;
    }
    player.worm.move(x, y, now, !automatic);
    if (automatic) {
        // Discard outward momentum at the edge, including while coasting.
        if ((player.worm.x_position <= 0 && player.velocity_x < 0) ||
            (player.worm.x_position >= MATRIX_WIDTH - 1 && player.velocity_x > 0)) player.velocity_x = 0;
        if ((player.worm.y_position <= 0 && player.velocity_y < 0) ||
            (player.worm.y_position >= MATRIX_HEIGHT - 1 && player.velocity_y > 0)) player.velocity_y = 0;
    }
    player.last_frame = now;
}

static bool headAt(const player_t &player, uint8_t x, uint8_t y) {
    return int(player.worm.x_position + 0.5f) % MATRIX_WIDTH == x &&
           int(player.worm.y_position + 0.5f) % MATRIX_HEIGHT == y;
}

// Called only under playerMutex, once all players have moved.
static void updateVitamin(uint64_t now) {
    bool any = false;
    for (const auto &player : players) any |= player.active;
    if (!any) {
        vitamin.active = false;
        vitamin_clock_started = false;
        return;
    }
    if (!vitamin_clock_started || now < vitamin_since) {
        vitamin.active = false;
        vitamin_since = now;
        vitamin_clock_started = true;
        waiting_for_first_vitamin = true;
        vitamin_random.seed(uint32_t(now) ^ nextPlayerId);
    }
    const uint64_t elapsed = now - vitamin_since;
    if (vitamin.active) {
        if (elapsed >= VITAMIN_LIFETIME_US) {
            vitamin.active = false;
            vitamin_since = now;
            return;
        }
        vitamin.age_us = elapsed;
        for (auto &player : players) {
            if (player.active && headAt(player, vitamin.x, vitamin.y)) {
                if (player.score < std::numeric_limits<uint32_t>::max()) ++player.score;
                if (player.worm.length < LedWorm::MAX_LENGTH) ++player.worm.length;
                vitamin.active = false;
                vitamin_since = now;
                break; // Exactly one player receives this vitamin.
            }
        }
        return;
    }
    if (elapsed < (waiting_for_first_vitamin ? FIRST_VITAMIN_DELAY : VITAMIN_DELAY)) return;
    const unsigned start = vitamin_random() % PIXEL_COUNT;
    for (unsigned offset = 0; offset < PIXEL_COUNT; ++offset) {
        const unsigned cell = (start + offset) % PIXEL_COUNT;
        const uint8_t x = cell % MATRIX_WIDTH, y = cell / MATRIX_WIDTH;
        bool occupied = false;
        for (const auto &player : players) {
            if (!player.active) continue;
            if (headAt(player, x, y)) { occupied = true; break; }
            for (unsigned segment = 0; segment < player.worm.bodyCount(); ++segment) {
                if (player.worm.bodyCell(segment) == cell) { occupied = true; break; }
            }
            if (occupied) break;
        }
        if (occupied) continue;
        vitamin = {true, x, y};
        vitamin_since = now;
        waiting_for_first_vitamin = false;
        break;
    }
}

// Vor dem Start von Webserver und Render-Task aufrufen.
bool player_manager_init() {
    if (playerMutex == nullptr) {
        playerMutex = xSemaphoreCreateMutex();
    }
    return playerMutex != nullptr;
}

bool addPlayer(int socketFd) {
    if (socketFd < 0 || playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    for (const auto &player : players) {
        if (player.active && player.socketFd == socketFd) {
            xSemaphoreGive(playerMutex);
            return false;
        }
    }
    // A lone player survives without a socket and is reclaimed on reconnect.
    for (auto &player : players) {
        if (player.active && player.socketFd < 0) {
            player.socketFd = socketFd;
            player.worm.setJoystick(0, 0);
            player.input_pending = true;
            xSemaphoreGive(playerMutex);
            return true;
        }
    }
    for (size_t i = 0; i < MAX_PLAYERS; ++i) {
        auto &player = players[i];
        if (!player.active) {
            const uint32_t id = nextPlayerId++;
            if (nextPlayerId == 0) nextPlayerId = 1;
            if (!player.worm.init(id)) {
                xSemaphoreGive(playerMutex);
                return false;
            }
            // Acht gut verteilte Startplätze auf der 16x16-Matrix.
            player.worm.x_position = float(2 + (i % 4) * 4);
            player.worm.y_position = float(4 + (i / 4) * 8);
            player.socketFd = socketFd;
            player.player_id = id;
            player.score = 0;
            player.last_input = player.last_frame = 0;
            player.input_pending = player.input_clock_started = false;
            player.force_automatic = false;
            player.velocity_x = player.velocity_y = 0;
            player.active = true;
            xSemaphoreGive(playerMutex);
            return true;
        }
    }
    xSemaphoreGive(playerMutex);
    return false;
}

void removePlayer(int socketFd) {
    if (playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) return;
    size_t active_count = 0;
    for (const auto &player : players) active_count += player.active;
    for (auto &player : players) {
        if (player.active && player.socketFd == socketFd) {
            if (active_count == 1) {
                // Keep the final worm alive under automatic control.
                player.socketFd = -1;
                player.worm.setJoystick(0, 0);
                player.input_pending = false;
                player.input_clock_started = true;
                player.force_automatic = true;
                break;
            }
            player.worm.deactivate();
            player.active = false;
            player.socketFd = -1;
            player.player_id = 0;
            break;
        }
    }
    xSemaphoreGive(playerMutex);
}

#ifdef LEDWORM_HOST_TEST
void resetPlayersForTest() {
    if (playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) return;
    for (auto &player : players) player = {};
    vitamin = {};
    vitamin_since = 0;
    vitamin_clock_started = false;
    waiting_for_first_vitamin = true;
    nextPlayerId = 1;
    xSemaphoreGive(playerMutex);
}
#endif

bool updateJoystick(int socketFd, float x, float y) {
    if (socketFd < 0 || !std::isfinite(x) || !std::isfinite(y) ||
        x < -1.0f || x > 1.0f || y < -1.0f || y > 1.0f ||
        playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) return false;
    for (auto &player : players) {
        if (player.active && player.socketFd == socketFd) {
            player.worm.setJoystick(x, y);
            player.input_pending = true;
            xSemaphoreGive(playerMutex);
            return true;
        }
    }
    xSemaphoreGive(playerMutex);
    return false;
}

size_t updatePlayerFrames(uint64_t now, player_frame_t *frames, size_t capacity,
                          vitamin_frame_t *vitamin_snapshot) {
    if (frames == nullptr || playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) return 0;
    size_t count = 0;
    for (size_t i = 0; i < MAX_PLAYERS; ++i) {
        auto &player = players[i];
        if (!player.active) continue;
        movePlayer(player, now);
        if (count < capacity) {
            frames[count++] = {player.player_id, player.worm.x_position,
                player.worm.y_position, uint8_t(i * 256 / MAX_PLAYERS),
                player.worm.brightness};
            auto &frame = frames[count - 1];
            frame.body_count = player.worm.bodyCount();
            memcpy(frame.area, player.worm.claimedArea(), sizeof(frame.area));
            memcpy(frame.cut, player.worm.cutCells(), sizeof(frame.cut));
            for (unsigned segment = 0; segment < frame.body_count; ++segment) {
                frame.body[segment] = player.worm.bodyCell(segment);
            }
        }
    }
    updateVitamin(now);
    if (vitamin_snapshot) *vitamin_snapshot = vitamin;
    xSemaphoreGive(playerMutex);
    return count;
}

bool updateBrightness(int socketFd, double value) {
    if (socketFd < 0 || !std::isfinite(value) || value < 0 || value > 255 ||
        std::floor(value) != value || playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) return false;
    bool updated = false;
    for (auto &player : players) {
        if (player.active && player.socketFd == socketFd) {
            player.worm.brightness = static_cast<uint8_t>(value);
            updated = true;
            break;
        }
    }
    xSemaphoreGive(playerMutex);
    return updated;
}

bool getPlayerState(int socketFd, player_state_t &state) {
    if (socketFd < 0 || playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) return false;
    size_t count = 0;
    const player_t *own = nullptr;
    for (const auto &player : players) {
        if (!player.active) continue;
        ++count;
        if (player.socketFd == socketFd) own = &player;
    }
    if (own) state = {own->player_id, own->worm.x_position, own->worm.y_position,
                     own->worm.length, own->worm.brightness, count, own->score};
    xSemaphoreGive(playerMutex);
    return own != nullptr;
}
