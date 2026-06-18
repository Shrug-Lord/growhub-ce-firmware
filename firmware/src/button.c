#include "button.h"
#include "config.h"
#include "wifi.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button";
#define FACTORY_RESET_HOLD_MS  5000
#define WIFI_RECOVERY_HOLD_MS  3000
#define POLL_INTERVAL_MS       50

static void button_task(void *arg)
{
    const growhub_config_t *cfg = config_get();
    uint8_t pin = cfg->pin_button;

    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);

    int held_ms = 0;

    while (1) {
        // Button is active-low (pulled up, grounded when pressed)
        if (gpio_get_level(pin) == 0) {
            held_ms += POLL_INTERVAL_MS;
            if (held_ms >= FACTORY_RESET_HOLD_MS) {
                ESP_LOGW(TAG, "Button held %d ms — FACTORY RESET", held_ms);
                config_factory_reset(); // does not return
            }
        } else {
            if (held_ms >= WIFI_RECOVERY_HOLD_MS) {
                // Medium hold: WiFi Recovery Mode toggle
                ESP_LOGI(TAG, "Button medium hold (%d ms) — WiFi Recovery toggle", held_ms);
                if (wifi_is_in_recovery_mode()) {
                    wifi_exit_recovery_mode(false);  // disconnect, keep creds
                } else if (config_get()->sta_ssid[0] != '\0') {
                    wifi_enter_recovery_mode();  // re-engage
                } else {
                    ESP_LOGI(TAG, "No WiFi credentials — medium hold ignored");
                }
            } else if (held_ms > 0) {
                ESP_LOGI(TAG, "Button short press (%d ms)", held_ms);
                // Short press: currently unused
            }
            held_ms = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void button_init(void)
{
    xTaskCreate(button_task, "button", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "Button monitor started (GPIO %d, 3s=wifi-recovery, 5s=factory-reset)",
             config_get()->pin_button);
}
