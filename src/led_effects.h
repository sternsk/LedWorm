// led_effects.h
#pragma once

#include "freertos/FreeRTOS.h"

void setupLEDs();
void led_comet_task(void *pvParameters);
