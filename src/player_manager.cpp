
#include "player_manager.h"

#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Feste Anzahl von Spielerinstanzen
static player_t players[MAX_PLAYERS] = {};

// Synchronisierung zwischen Webserver und Render-Task
static SemaphoreHandle_t playerMutex = xSemaphoreCreateMutex();

// Nächste zu vergebende Spieler-ID
static uint8_t nextPlayerId = 1;

static QueueHandle_t joystickQueue = nullptr;

bool player_manager_init()
{
    joystickQueue = xQueueCreate(
        32,
        sizeof(joystick_event_t)
    );

    return joystickQueue != nullptr;
}

static bool findPlayerId(int client_fd, uint32_t& player_id)
{
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (players[i].active &&
            players[i].socketFd == client_fd) {

            player_id = players[i].player_id;
            return true;
        }
    }

    return false;
}


// Spieler hinzufügen
bool addPlayer(int socketFd)
{
    if (socketFd < 0) {
        return false;
    }

    if (playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    // Verbindung darf nicht bereits registriert sein
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (players[i].socketFd == socketFd) {
            xSemaphoreGive(playerMutex);
            return false;
        }
    }

    // Freien Platz suchen
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (!players[i].active) {
            const uint8_t playerId = nextPlayerId++;

            // ID 0 ist reserviert
            if (nextPlayerId == 0) {
                nextPlayerId = 1;
            }

            if (!players[i].worm.init(playerId)) {
                xSemaphoreGive(playerMutex);
                return false;
            }

            players[i].socketFd = socketFd;

            xSemaphoreGive(playerMutex);
            return true;
        }
    }

    // Keine freie Instanz
    xSemaphoreGive(playerMutex);
    return false;
}


// Spieler entfernen
void removePlayer(int socketFd)
{
    if (playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (players[i].socketFd == socketFd) {
            players[i].worm.deactivate();
            players[i].active = false;
            players[i].socketFd = -1;
            break;
        }
    }

    xSemaphoreGive(playerMutex);
}


// Joystickdaten aktualisieren
bool updateJoystick(int socketFd, float x, float y)
{
    if (socketFd < 0) {
        return false;
    }

    uint32_t player_id;

    if (!findPlayerId(socketFd, player_id)) {
        return false;
    }

    // Ungültige oder außerhalb des erwarteten Bereichs liegende Werte ablehnen
    if (x < -1.0f || x > 1.0f ||
        y < -1.0f || y > 1.0f) {
        return false;
    }

    joystick_event_t event = {
        .player_id = player_id,
        .x = x,
        .y = y
    };

    if (playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (players[i].socketFd == socketFd &&
            players[i].active) {
            players[i].worm.setJoystick(x, y);

            xSemaphoreGive(playerMutex);
            return true;
        }
    }

    xSemaphoreGive(playerMutex);
    return false;
}
