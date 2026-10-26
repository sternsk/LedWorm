
#include "player_manager.h"

#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Feste Anzahl von Spielerinstanzen
static LedWorm worms[MAX_PLAYERS];

// Zuordnung zwischen WebSocket-Verbindung und LedWorm
struct PlayerConnection {
    int socketFd = -1;
};

static PlayerConnection connections[MAX_PLAYERS];

// Synchronisierung zwischen Webserver und Render-Task
static SemaphoreHandle_t playerMutex = xSemaphoreCreateMutex();

// Nächste zu vergebende Spieler-ID
static uint8_t nextPlayerId = 1;


// Spieler hinzufügen
bool addPlayer(int socketFd, const char* name)
{
    if (socketFd < 0 || name == nullptr || name[0] == '\0') {
        return false;
    }

    if (playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    // Verbindung darf nicht bereits registriert sein
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (connections[i].socketFd == socketFd) {
            xSemaphoreGive(playerMutex);
            return false;
        }
    }

    // Freien Platz suchen
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (!worms[i].active) {
            const uint8_t playerId = nextPlayerId++;

            // ID 0 ist reserviert
            if (nextPlayerId == 0) {
                nextPlayerId = 1;
            }

            if (!worms[i].init(playerId, name)) {
                xSemaphoreGive(playerMutex);
                return false;
            }

            connections[i].socketFd = socketFd;

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
        if (connections[i].socketFd == socketFd) {
            worms[i].deactivate();
            connections[i].socketFd = -1;
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

    // Ungültige oder außerhalb des erwarteten Bereichs liegende Werte ablehnen
    if (x < -1.0f || x > 1.0f ||
        y < -1.0f || y > 1.0f) {
        return false;
    }

    if (playerMutex == nullptr ||
        xSemaphoreTake(playerMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (connections[i].socketFd == socketFd &&
            worms[i].active) {
            worms[i].setJoystick(x, y);

            xSemaphoreGive(playerMutex);
            return true;
        }
    }

    xSemaphoreGive(playerMutex);
    return false;
}
