// led_effects.cpp
#include <stdio.h>
#include "led_effects.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "led_strip.h" 
#define LED_PIN     7
#define PIXEL_COUNT 18

static const char *LED_TAG = "LED_TASK";
static led_strip_handle_t led_strip;

// Structs für unsere eigene Farblogik
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_pixel_t;

// Lokaler Framebuffer, um Pixelwerte für den Schweif-Effekt (Fade) auszulesen
static rgb_pixel_t frame_buffer[PIXEL_COUNT];

// Globale, volatile Variablen zur Kommunikation zwischen Wi-Fi (Core 0) und LED (Core 1)
volatile float joystick_x = 0.0f;
volatile int8_t input_direction_trigger = 0; 

/**
 * Hilfsfunktion: Konvertiert HSV-Farbraum in RGB-Farbraum ohne Fließkommazahlen.
 */
static rgb_pixel_t hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v) {
    rgb_pixel_t rgb;
    if (s == 0) {
        rgb.r = v; rgb.g = v; rgb.b = v;
        return rgb;
    }
    uint8_t region = h / 43;
    uint8_t remainder = (h % 43) * 6; 

    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  rgb.r = v; rgb.g = t; rgb.b = p; break;
        case 1:  rgb.r = q; rgb.g = v; rgb.b = p; break;
        case 2:  rgb.r = p; rgb.g = v; rgb.b = t; break;
        case 3:  rgb.r = p; rgb.g = q; rgb.b = v; break;
        case 4:  rgb.r = t; rgb.g = p; rgb.b = v; break;
        default: rgb.r = v; rgb.g = p; rgb.b = q; break;
    }
    return rgb;
}

/**
 * Hilfsfunktion: Simuliert FastLEDs fadeToBlackBy für den led_strip
 */
static void fade_all_to_black(led_strip_handle_t strip, uint8_t fade_factor) {
    uint16_t keep_amount = 255 - fade_factor;

    for (int i = 0; i < PIXEL_COUNT; i++) {
        // Werte im Framebuffer abdunkeln
        frame_buffer[i].r = (frame_buffer[i].r * keep_amount) >> 8;
        frame_buffer[i].g = (frame_buffer[i].g * keep_amount) >> 8;
        frame_buffer[i].b = (frame_buffer[i].b * keep_amount) >> 8;

        // An den Hardware-Treiber übergeben
        led_strip_set_pixel(strip, i, frame_buffer[i].r, frame_buffer[i].g, frame_buffer[i].b);
    }
}

// Initialisierung des nativen RMT-Treibers von Espressif
void setupLEDs() {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_PIN,
        .max_leds = PIXEL_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false
        }
    };

    /* RMT-Hardware-Peripherie Konfiguration */
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz Präzision
        .flags = { .with_dma = false }
    };

    // Treiber registrieren und starten
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    // Framebuffer beim Start einmal komplett nullen
    for(int i = 0; i < PIXEL_COUNT; i++) {
        frame_buffer[i] = (rgb_pixel_t){0, 0, 0};
    }
    led_strip_clear(led_strip);

    ESP_LOGI(LED_TAG, "AlphaRail Native LED-strip Task initialisiert");
}

// Der FreeRTOS Task
void led_comet_task(void *pvParameters) {
    // === SETUP BEREICH ===
    setupLEDs();

    // Lokale Statusvariablen
    uint8_t currentLED = 0;
    uint8_t hue = 0;
    uint8_t saturation = 0;
    int8_t direction = 1; 

    uint64_t lastUpdate = esp_timer_get_time() / 1000; 
    const uint64_t interval = 100;                     

    // === LOOP BEREICH (Endlosschleife) ===
    while (1) {
        
        // 1. INPUT-PRÜFUNG
        if (input_direction_trigger != 0) {
            input_direction_trigger = 0; 
            direction = -direction; 
            ESP_LOGI(LED_TAG, "Richtung gewechselt! Saturation: %u", saturation);
        }

        if (joystick_x < -0.5f && direction == 1) {
            direction = -1;
        } else if (joystick_x > 0.5f && direction == -1) {
            direction = 1;
        }

        // 2. LED-ANIMATION (Alle 100ms)
        uint64_t currentMillis = esp_timer_get_time() / 1000;
        if (currentMillis - lastUpdate >= interval) {
            lastUpdate = currentMillis; 
            
            // 2a. Schweif-Effekt anwenden (Dimmt den Framebuffer und pusht an led_strip)
            fade_all_to_black(led_strip, 30);
            
            // 2b. Neuen Kopf berechnen (Wert "63" entspricht deiner gedrosselten FastLED-Helligkeit)
            rgb_pixel_t head_color = hsv_to_rgb(hue, saturation, 63);
            
            // 2c. Kopf in den Framebuffer und in den Treiber schreiben
            frame_buffer[currentLED] = head_color;
            led_strip_set_pixel(led_strip, currentLED, head_color.r, head_color.g, head_color.b);
            
            // 2d. Daten physisch an die LEDs senden (Entspricht FastLED.show())
            led_strip_refresh(led_strip);

            // Logik für automatischen Richtungswechsel an den Rändern
            if (currentLED == 0 && direction == -1) {
                direction = 1;
            } else if (currentLED == PIXEL_COUNT - 1 && direction == 1) {
                direction = -1;
            }

            // Werte inkrementieren
            currentLED += direction;
            hue += 8;
            saturation += 1;
        }

        // 3. ENTLASTUNG FÜR DAS SYSTEM
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
