#include "player_manager.h"
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static player_t players[MAX_PLAYERS];
static SemaphoreHandle_t playerMutex = nullptr;
static uint32_t nextPlayerId = 1;

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

size_t updatePlayerFrames(uint64_t now, player_frame_t *frames, size_t capacity) {
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
        }
    }
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
                     own->worm.length, own->worm.brightness, count};
    xSemaphoreGive(playerMutex);
    return own != nullptr;
}
