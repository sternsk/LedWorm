// led_effects.h
#pragma once

#include <stddef.h>
#include <stdint.h>

void setupLEDs();
void led_render_task(void *pvParameters);

// One row-major RGB888 frame, starting with the top-left visible pixel.
bool set_uploaded_image(const uint8_t *rgb, size_t length);
void clear_uploaded_image();
