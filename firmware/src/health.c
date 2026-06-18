#include "health.h"

#include "config.h"
#include "sensors.h"
#include "webserver.h"
#include "wifi.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "health";

#define HEALTH_MARK_VALID_TIMEOUT_MS 120000
#define HEALTH_POLL_INTERVAL_MS 1000

static const char *ota_state_name(esp_ota_img_states_t state)
{
    switch (state) {
        case ESP_OTA_IMG_NEW:            return "NEW";
        case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
        case ESP_OTA_IMG_VALID:          return "VALID";
        case ESP_OTA_IMG_INVALID:        return "INVALID";
        case ESP_OTA_IMG_ABORTED:        return "ABORTED";
        case ESP_OTA_IMG_UNDEFINED:      return "UNDEFINED";
        default:                         return "UNKNOWN";
    }
}

static bool wifi_health_ready(void)
{
    const growhub_config_t *cfg = config_get();
    return cfg->sta_ssid[0] == '\0' || wifi_is_connected();
}

static bool health_gates_passed(void)
{
    return webserver_is_running()
        && wifi_health_ready()
        && sensors_first_read_attempt_done();
}

static void health_task(void *arg)
{
    (void)arg;

#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    ESP_LOGW(TAG, "Bootloader app rollback disabled; mark-valid gate inactive");
    vTaskDelete(NULL);
    return;
#else
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "OTA image state unavailable (%s); rollback validation not required",
                 esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    if (ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "OTA image state %s; rollback validation not required",
                 ota_state_name(ota_state));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA image pending verification; waiting for health gates");

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(HEALTH_MARK_VALID_TIMEOUT_MS);
    while ((xTaskGetTickCount() - start) < timeout) {
        if (health_gates_passed()) {
            err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "OTA image marked valid after health checks");
            } else {
                ESP_LOGE(TAG, "Failed to mark OTA image valid: %s", esp_err_to_name(err));
            }
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(HEALTH_POLL_INTERVAL_MS));
    }

    ESP_LOGE(TAG,
             "Health checks timed out; web=%d wifi=%d sensor_attempt=%d. Rolling back.",
             webserver_is_running(), wifi_health_ready(), sensors_first_read_attempt_done());
    err = esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(TAG, "Rollback request failed: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
#endif
}

void health_init(void)
{
    xTaskCreate(health_task, "health", 4096, NULL, 3, NULL);
}
