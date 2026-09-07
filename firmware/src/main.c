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

#define LED_ACTIVE_LEVEL   0
#define LED_INACTIVE_LEVEL 1

typedef enum {
    OP_LED_RECOVERY,
    OP_LED_WIFI_OFFLINE,
    OP_LED_MQTT_OFFLINE,
    OP_LED_HEALTHY,
} operation_led_state_t;

static esp_err_t led_pin_init(uint8_t pin, const char *name)
{
    esp_err_t err = gpio_reset_pin(pin);
    if (err == ESP_OK) err = gpio_set_level(pin, LED_INACTIVE_LEVEL);
    if (err == ESP_OK) err = gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s LED setup failed on GPIO %d: %s",
                 name, pin, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t led_pin_disable(uint8_t pin, const char *name)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s LED disable failed on GPIO %d: %s",
                 name, pin, esp_err_to_name(err));
    }
    return err;
}

static void led_write(uint8_t pin, bool on)
{
    esp_err_t err = gpio_set_level(pin, on ? LED_ACTIVE_LEVEL : LED_INACTIVE_LEVEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED write failed on GPIO %d: %s", pin, esp_err_to_name(err));
    }
}

static operation_led_state_t operation_led_state(void)
{
    if (wifi_is_in_recovery_mode()) return OP_LED_RECOVERY;
    if (!wifi_is_connected()) return OP_LED_WIFI_OFFLINE;
    if (mqtt_is_enabled() && !mqtt_is_connected()) return OP_LED_MQTT_OFFLINE;
    return OP_LED_HEALTHY;
}

// Drives the blue/green operation LED and red malfunction LED. Both LEDs are
// active-low on the verified Growhub and Growhub+ board design.
static void led_task(void *arg)
{
    (void)arg;
    const growhub_config_t *cfg = config_get();
    bool operation_ready = false;
    if (cfg->operation_led_enabled) {
        operation_ready = led_pin_init(cfg->pin_led, "Operation") == ESP_OK;
    } else {
        led_pin_disable(cfg->pin_led, "Operation");
        ESP_LOGW(TAG, "Operation LED disabled; GPIO %d is high-impedance",
                 cfg->pin_led);
    }
    bool malfunction_ready =
        led_pin_init(cfg->pin_error_led, "Malfunction") == ESP_OK;
    if (!operation_ready && !malfunction_ready) {
        vTaskDelete(NULL);
        return;
    }

    operation_led_state_t op_state = operation_led_state();
    bool time_warning = schedule_time_sync_required();
    TickType_t op_since = xTaskGetTickCount();
    TickType_t warning_since = op_since;

    while (1) {
        TickType_t now = xTaskGetTickCount();
        operation_led_state_t next_op_state = operation_led_state();
        bool next_time_warning = schedule_time_sync_required();
        if (next_op_state != op_state) {
            op_state = next_op_state;
            op_since = now;
        }
        if (next_time_warning != time_warning) {
            time_warning = next_time_warning;
            warning_since = now;
        }

        uint32_t op_ms = (uint32_t)((now - op_since) * portTICK_PERIOD_MS);
        uint32_t warning_ms = (uint32_t)((now - warning_since) * portTICK_PERIOD_MS);
        bool operation_on = false;
        switch (op_state) {
            case OP_LED_RECOVERY: {
                uint32_t phase = op_ms % 3000;
                operation_on = phase < 1200 && (phase % 400) < 200;
                break;
            }
            case OP_LED_WIFI_OFFLINE:
                operation_on = (op_ms % 400) < 200;
                break;
            case OP_LED_MQTT_OFFLINE:
                operation_on = (op_ms % 2000) < 1000;
                break;
            case OP_LED_HEALTHY:
                operation_on = true;
                break;
        }

        bool malfunction_on = false;
        if (time_warning) {
            uint32_t phase = warning_ms % 2600;
            malfunction_on = phase < 800 && (phase % 400) < 200;
        }

        if (operation_ready) {
            led_write(cfg->pin_led, operation_on);
        }
        if (malfunction_ready) {
            led_write(cfg->pin_error_led, malfunction_on);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
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

        // Address discovery must not wait for a long sensor reporting interval.
        for (int second = 0; second < cfg->report_interval_s; second++) {
            mqtt_poll_network_state();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
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

    // 3. Start WiFi (provisioning/preference AP + optional STA)
    wifi_init();

    // 4. Web config server (served on every active WiFi interface)
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
    if (xTaskCreate(led_task, "led", 2048, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start status LED task");
    }

    // 9. Sensor read + publish loop
    xTaskCreate(sensor_loop_task, "sensor_loop", 4096, NULL, 4, NULL);

    // 10. OTA rollback health gate
    health_init();

    ESP_LOGI(TAG, "All systems initialized. Web UI at http://%s.local/ or AP: %s",
             config_get()->device_name, config_get()->ap_ssid);
}
