// led_effects.h
#pragma once

#include "freertos/FreeRTOS.h"

void setupLEDs();
void led_render_task(void *pvParameters);
