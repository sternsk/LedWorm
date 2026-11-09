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
constexpr uint64_t VITAMIN_LIFETIME = 8000000;

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
        if (elapsed >= VITAMIN_LIFETIME) {
            vitamin.active = false;
            vitamin_since = now;
            return;
        }
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
    for (auto &player : players) {
        if (player.active && player.socketFd == socketFd) {
            player.worm.deactivate();
            player.active = false;
            player.socketFd = -1;
            player.player_id = 0;
            break;
        }
    }
    xSemaphoreGive(playerMutex);
}

bool updateJoystick(int socketFd, float x, float y) {
    if (socketFd < 0 || !std::isfinite(x) || !std::isfinite(y) ||
        x < -1.0f || x > 1.0f || y < -1.0f || y > 1.0f ||
        playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) return false;
    for (auto &player : players) {
        if (player.active && player.socketFd == socketFd) {
            player.worm.setJoystick(x, y);
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
        player.worm.move(player.worm.joystick_x, player.worm.joystick_y, now);
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
