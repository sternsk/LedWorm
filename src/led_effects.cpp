#include "led_effects.h"
#include "globals.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "led_strip.h"
#include "player_manager.h"
#include "matrix_renderer.h"
#include "freertos/task.h"

static const char *LED_TAG = "LED_TASK";
static led_strip_handle_t led_strip;
static MatrixRenderer renderer;

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
    
    ESP_ERROR_CHECK(led_strip_clear(led_strip));

    ESP_LOGI(LED_TAG, "LedWorm 16x16-Matrix initialisiert");
}

void led_render_task(void *) {
    setupLEDs();
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t frameTicks = pdMS_TO_TICKS(20); // 50 Frames pro Sekunde
    while (true) {
        static player_frame_t players[MAX_PLAYERS];
        vitamin_frame_t vitamin;
        marquee_frame_t marquee;
        clock_frame_t clock;
        alarm_frame_t alarm;
        const uint64_t now = esp_timer_get_time();
        const size_t count = updatePlayerFrames(now, players, MAX_PLAYERS, &vitamin);
        updateMarqueeFrame(now, marquee);
        updateClockFrame(clock);
        updateAlarmFrame(alarm);
        renderer.render(players, count,
            {ambient_r.load(), ambient_g.load(), ambient_b.load()}, &vitamin, &marquee, &clock,
            &alarm);
        const auto *pixels = renderer.pixels();
        for (size_t i = 0; i < PIXEL_COUNT; ++i) {
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i,
                pixels[i].r, pixels[i].g, pixels[i].b));
        }
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        vTaskDelayUntil(&lastWake, frameTicks);
    }
}
