#include <stdio.h>
#include "led_effects.h"
#include "esp_timer.h"
#include "esp_log.h"

#define FASTLED_ESP32_RAW_PIN_ORDER
#include <FastLED.h>

#define LED_PIN     7
#define PIXEL_COUNT 18
#define BRIGHTNESS  150

CRGB leds[PIXEL_COUNT];
static const char *LED_TAG = "LED_TASK";

// Globale, volatile Variablen zur Kommunikation zwischen Wi-Fi (Core 0) und LED (Core 1)
// Volatile verhindert, dass der Compiler die Variablen für die Schleife wegoptimiert.
volatile float joystick_x = 0.0f;
volatile int8_t input_direction_trigger = 0; // 1 = Richtung wechseln angefordert

// Diese Funktion entspricht deinem "setupLEDs()"
void setupLEDs() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, PIXEL_COUNT).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);
    ESP_LOGI(LED_TAG, "AlphaRail LED-Task initialisiert");
}

// Der eigentliche FreeRTOS Task (Ersetzt die Kombination aus setup und loop)
void led_comet_task(void *pvParameters) {
    // === SETUP BEREICH ===
    setupLEDs();

    // Lokale Statusvariablen (entspricht deinen "static" Variablen aus dem Arduino-Loop)
    uint8_t currentLED = 0;
    uint8_t hue = 0;
    uint8_t saturation = 0;
    int8_t direction = 1; // 1 = vorwärts, -1 = rückwärts

    uint64_t lastUpdate = esp_timer_get_time() / 1000; // Zeitstempel in ms (Ersatz für millis())
    const uint64_t interval = 100;                     // Animationsgeschwindigkeit in ms

    // === LOOP BEREICH (Endlosschleife) ===
    while (1) {
        
        // 1. INPUT-PRÜFUNG (Sofort und ohne Verzögerung in jedem Task-Durchlauf)
        // Hier prüfen wir, ob der WebSocket-Server ein Event geschickt hat
        if (input_direction_trigger != 0) {
            input_direction_trigger = 0; // Trigger zurücksetzen
            
            direction = -direction; // Richtung umkehren
            ESP_LOGI(LED_TAG, "Richtung gewechselt! Saturation: %u", saturation);
        }

        // Optionale Erweiterung: Echte Joystick X-Achse auswerten
        // Wenn der Joystick stark nach links gedrückt wird (x < -0.5), zwinge Richtung nach links
        if (joystick_x < -0.5f && direction == 1) {
            direction = -1;
        } else if (joystick_x > 0.5f && direction == -1) {
            direction = 1;
        }

        // 2. LED-ANIMATION (Wird nur alle 100ms ausgeführt)
        uint64_t currentMillis = esp_timer_get_time() / 1000;
        if (currentMillis - lastUpdate >= interval) {
            lastUpdate = currentMillis; // Zeitstempel aktualisieren
            
            // Schweif-Effekt erzeugen
            fadeToBlackBy(leds, PIXEL_COUNT, 30);
            
            // Neuen Kopf zeichnen (CHSV statt CRGB für Regenbogeneffekt)
            leds[currentLED] = CHSV(hue, saturation, 63);
            FastLED.show();

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

        // 3. ENTLASYTUNG FÜR DAS SYSTEM (Zwingend erforderlich in FreeRTOS)
        // Ein kurzes Delay von 5-10ms sorgt dafür, dass der Watchdog nicht anspringt
        // und andere Hintergrundprozesse auf Core 1 (falls vorhanden) Atempause haben.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
