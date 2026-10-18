// led_effects.cpp
#include <stdio.h>
#include "led_effects.h"
#include "globals.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "led_strip.h" 
#define LED_PIN     256
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
static void fade_to_ambient(led_strip_handle_t strip, uint8_t fade_factor) {
    uint16_t keep_amount = 255 - fade_factor;

    for (int i = 0; i < PIXEL_COUNT; i++) {
        // Werte im Framebuffer abdunkeln
        uint8_t faded_r = frame_buffer[i].r = (frame_buffer[i].r * keep_amount) >> 8;
        uint8_t faded_g = frame_buffer[i].g = (frame_buffer[i].g * keep_amount) >> 8;
        uint8_t faded_b = frame_buffer[i].b = (frame_buffer[i].b * keep_amount) >> 8;

        // 2. Berechne den Anteil der Hintergrundbeleuchtung, der dazukommen soll
        // (Damit die Hintergrundfarbe leuchtet, wenn kein Wurm da ist)
        uint8_t bg_r = (ambient_r * fade_factor) >> 8;
        uint8_t bg_g = (ambient_g * fade_factor) >> 8;
        uint8_t bg_b = (ambient_b * fade_factor) >> 8;

        // 3. Zusammenführen und im Puffer sichern
        frame_buffer[i].r = faded_r + bg_r;
        frame_buffer[i].g = faded_g + bg_g;
        frame_buffer[i].b = faded_b + bg_b;

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
void led_render_task(void *pvParameters) {
    // === SETUP BEREICH ===
    setupLEDs();

    // Lokale Statusvariablen
    bool automotion = true;
    uint8_t currentLED = 0;
    static float exact_position = 0.0f;
    uint8_t hue = 0;
    uint8_t saturation = 0;
    uint8_t value = 7;
    static float brightness_value = 63.0f; //brightness startwert
    int8_t direction = 1; 
    uint64_t lastUpdate = esp_timer_get_time();
    const int64_t intervalUs = 100 * 1000; // 100ms //16667 für 60Hz;    
    static uint64_t last_time = 0;
    static uint64_t last_motion = 0;
    
    // === LOOP BEREICH ===
    while (1) {
        static uint64_t last_brightness_update = 0;
        
        // Joystickwerte lokal einlesen
        const float x = joystick_x.load(std::memory_order_relaxed);
        const float y = joystick_y.load(std::memory_order_relaxed);
        
        uint64_t now = esp_timer_get_time(); // Aktuelle Zeit in us
        if (last_time == 0) last_time = now;
        float dt = (now - last_time) / (1000 * 1000.0f); // Delta-Zeit in Sekunden (z. B. 0.005s bei 5ms Delay)
        // Maximale Änderungsgeschwindigkeit: stufen pro Sekunde bei vollem Ausschlag
        const float MAX_SPEED_PER_SEC = 50.0f; 
        last_time = now;
        
        // Deadzone prüfen: Reagiere erst, wenn der Joystick spürbar bewegt wird (Rauschen eliminieren)
        if (y > .8f || y < -.8f) {
            
            // Änderung = Geschwindigkeit * Joystick-Ausschlag * Delta-Zeit
            brightness_value += MAX_SPEED_PER_SEC * y * dt;
            
            // Grenzen hart limitieren (0 bis 255), damit der Wert nicht wegläuft
            if (brightness_value > 255.0f) brightness_value = 255.0f;
            if (brightness_value < 0.0f)   brightness_value = 0.0f;
            
            uint8_t new_value = static_cast<uint8_t>(brightness_value);
            
            // Nur loggen, wenn sich die Ganzzahl tatsächlich geändert hat
            if (new_value != value) {
                value = new_value;
                ESP_LOGD("render_task", "value: %u", value);
            }
        }
        
        if (x < -0.2f || x > .2f) {
            automotion = false;
            last_motion = now;
            
            exact_position += 10 * x * dt;
            
            if (exact_position < 0.0f) exact_position = 0.0f;
            if (exact_position > PIXEL_COUNT-1) exact_position = PIXEL_COUNT - 1;
            
            ESP_LOGD("render_task", "exact_position: %.2f", exact_position);
            
            // 3. In eine Ganzzahl für die LEDs umwandeln (Kaufmännisches Runden mit +0.5f für weichere Übergänge)
            uint8_t newCurrentLED = currentLED;
            newCurrentLED = static_cast<uint8_t>(exact_position + .5f);
            
            if(newCurrentLED != currentLED){
                currentLED = newCurrentLED;
            }
        }
        
        // 2. LED-ANIMATION (Ziel: ca. 60 FPS)
        if (now - lastUpdate >= intervalUs) {
            lastUpdate = now;
            
            // 2a. Schweif-Effekt anwenden (Dimmt den Framebuffer und pusht an led_strip)
            fade_to_ambient(led_strip, 30);
            
            // 2b. Neuen Kopf berechnen (Wert "63" entspricht deiner gedrosselten FastLED-Helligkeit)
            rgb_pixel_t head_color = hsv_to_rgb(hue, saturation, value);
            
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
            if(now - last_motion > 3 * 1000 * 1000) automotion = true; // keine Bewegung - animation startet nach einer sekunde
            if(automotion){
                currentLED += direction;
                exact_position = currentLED;
            } 
            
            hue += 8;
            saturation += 1;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
