/*
 * GrowHub Community Edition Firmware
 * -----------------------------------
 * Replacement firmware for Niwa Growhub ESP32 grow controllers.
 * Connects to a local Mosquitto MQTT broker instead of Niwa's AWS IoT cloud.
 *
 * Target: ESP32-WROOM-32 (ESP32-D0WDQ6 rev 1.1)
 * Original: Niwa firmware 3.9.0V
 *
 * License: MIT
 */

#include "config.h"
#include "wifi.h"
#include "mqtt.h"
#include "sensors.h"
#include "relays.h"
#include "webserver.h"
#include "ota.h"
#include "button.h"
#include "schedule.h"
#include "health.h"
#include "time_sync.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "main";

// LED status blink task
static void led_task(void *arg)
{
    const growhub_config_t *cfg = config_get();
    gpio_reset_pin(cfg->pin_led);
    gpio_set_direction(cfg->pin_led, GPIO_MODE_OUTPUT);

    while (1) {
        if (wifi_is_in_recovery_mode()) {
            // 3 fast pulses + pause — scanning for configured WiFi
            for (int i = 0; i < 3; i++) {
                gpio_set_level(cfg->pin_led, 1);
                vTaskDelay(pdMS_TO_TICKS(200));
                gpio_set_level(cfg->pin_led, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            vTaskDelay(pdMS_TO_TICKS(1800));  // pause to complete ~3s cycle
        } else if (schedule_time_sync_required()) {
            // 2 fast pulses + pause — active AUTO wall-clock schedule needs time
            for (int i = 0; i < 2; i++) {
                gpio_set_level(cfg->pin_led, 1);
                vTaskDelay(pdMS_TO_TICKS(200));
                gpio_set_level(cfg->pin_led, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            vTaskDelay(pdMS_TO_TICKS(1800));
        } else if (!wifi_is_connected()) {
            // Fast blink — AP mode, no WiFi credentials or manually disconnected
            gpio_set_level(cfg->pin_led, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(cfg->pin_led, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        } else if (!mqtt_is_connected()) {
            // Slow blink — WiFi OK, MQTT down
            gpio_set_level(cfg->pin_led, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            gpio_set_level(cfg->pin_led, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            // Solid on — everything good
            gpio_set_level(cfg->pin_led, 1);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

// Build and publish sensor JSON in Niwa-compatible format
static void publish_sensor_data(const sensor_reading_t *s)
{
    const char *mac = config_get_mac_str();

    // Timestamp in Niwa format: "YYYY-MM-DD HH:MM:SS:000Z"
    time_t now;
    time(&now);
    struct tm t;
    gmtime_r(&now, &t);
    char ts[48];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d:000Z",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);

    // Actuator state string
    char actuator[9];
    relays_get_actuator_str(actuator, sizeof(actuator));

    // Build JSON payload (Niwa-compatible format)
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "nId", mac);
    cJSON_AddStringToObject(root, "name", config_get()->device_name);
    cJSON_AddStringToObject(root, "fw", GROWHUB_VERSION);

    cJSON *data_arr = cJSON_AddArrayToObject(root, "data");
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "l", s->light);
    cJSON_AddNumberToObject(entry, "h", s->temp_valid ? s->humidity : 0);
    cJSON_AddNumberToObject(entry, "t", s->temp_valid ? s->temperature : 0);
    cJSON_AddStringToObject(entry, "a", actuator);
    cJSON_AddStringToObject(entry, "ts", ts);
    if (s->co2_valid) {
        cJSON_AddNumberToObject(entry, "c2", s->co2);
    }
    cJSON_AddItemToArray(data_arr, entry);

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        mqtt_publish_sensor(json);
        ESP_LOGD(TAG, "Sensor: %s", json);
        free(json);
    }
    cJSON_Delete(root);
}

// Main sensor loop task
static void sensor_loop_task(void *arg)
{
    const growhub_config_t *cfg = config_get();

    // Wait for WiFi before starting sensor loop
    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Sensor loop started (interval: %ds)", cfg->report_interval_s);

    while (1) {
        sensor_reading_t reading = sensors_read();

        if (reading.temp_valid) {
            ESP_LOGI(TAG, "T=%.1fC RH=%.1f%% L=%d%% CO2=%s%d",
                     reading.temperature, reading.humidity, reading.light,
                     reading.co2_valid ? "" : "N/A ", reading.co2);
        }

        if (mqtt_is_connected()) {
            publish_sensor_data(&reading);
        }

        vTaskDelay(pdMS_TO_TICKS(cfg->report_interval_s * 1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== GrowHub Community Edition " GROWHUB_VERSION " ===");

    // 0. Drive sensor power-enable GPIOs ASAP (must happen before sensor module's MCU boots)
    sensors_early_gpio_init();

    // 1. Load config from NVS
    config_init();

    // 2. Initialize hardware
    relays_init();
    sensors_init();
    button_init();

    // 3. Start WiFi (AP + optional STA)
    wifi_init();

    // 4. Web config server (always available, even in AP-only mode)
    webserver_init();

    // 5. Time sync
    time_sync_init();

    // 6. Schedule engine — init first, then restore persisted schedule
    schedule_init();
    {
        char *sched_buf = NULL;
        size_t sched_len = 0;
        if (config_load_schedule(&sched_buf, &sched_len)) {
            ESP_LOGI(TAG, "Restoring schedule from NVS");
            if (!schedule_load(sched_buf, (int)sched_len)) {
                ESP_LOGW(TAG, "Stored schedule invalid (%s, outlet %d); clearing NVS schedule",
                         schedule_last_error_reason(), schedule_last_error_outlet());
                config_clear_schedule();
            } else if (relays_get_mode() == RELAY_MODE_AUTO) {
                schedule_evaluate_now();
            }
            free(sched_buf);
        }
    }

    // 7. MQTT client (connects when WiFi station is up)
    mqtt_init();

    // 8. LED status indicator
    xTaskCreate(led_task, "led", 2048, NULL, 1, NULL);

    // 9. Sensor read + publish loop
    xTaskCreate(sensor_loop_task, "sensor_loop", 4096, NULL, 4, NULL);

    // 10. OTA rollback health gate
    health_init();

    ESP_LOGI(TAG, "All systems initialized. Web UI at http://%s.local/ or AP: %s",
             config_get()->device_name, config_get()->ap_ssid);
}
