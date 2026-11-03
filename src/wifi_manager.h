#pragma once
#include "esp_err.h"

// Called on the manager task, never on the Wi-Fi event loop.
// true: station has an IP; false: stop the game server before entering setup.
using wifi_manager_callback_t = void (*)(bool connected);
esp_err_t wifi_manager_start(wifi_manager_callback_t callback);
// For a future long-press handler. Task context only; clears only Wi-Fi credentials
// and schedules a reboot. Returns an error if storage could not be updated.
esp_err_t wifi_manager_reset();
