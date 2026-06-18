#include "ota.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include <string.h>

static const char *TAG = "ota";

// URL stored here so the task can access it after creation
static char s_ota_url[256];

static volatile ota_progress_t s_ota_progress = {OTA_IDLE, 0};
static portMUX_TYPE s_ota_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_ota_active = false;

ota_progress_t ota_get_progress(void)
{
    ota_progress_t snap = {s_ota_progress.stage, s_ota_progress.bytes_written};
    return snap;
}

void ota_set_progress(ota_stage_t stage, int bytes)
{
    s_ota_progress.stage = stage;
    s_ota_progress.bytes_written = bytes;
}

bool ota_begin_operation(void)
{
    bool started = false;
    portENTER_CRITICAL(&s_ota_lock);
    if (!s_ota_active) {
        s_ota_active = true;
        started = true;
    }
    portEXIT_CRITICAL(&s_ota_lock);
    return started;
}

void ota_end_operation(void)
{
    portENTER_CRITICAL(&s_ota_lock);
    s_ota_active = false;
    portEXIT_CRITICAL(&s_ota_lock);
}

static void ota_task(void *arg)
{
    ESP_LOGI(TAG, "Starting OTA from: %s", s_ota_url);
    s_ota_progress.stage = OTA_CONNECTING;
    s_ota_progress.bytes_written = 0;

    esp_http_client_config_t http_cfg = {
        .url = s_ota_url,
        .timeout_ms = 30000,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        s_ota_progress.stage = OTA_FAILED;
        ota_end_operation();
        vTaskDelete(NULL);
        return;
    }

    s_ota_progress.stage = OTA_FLASHING;
    while (true) {
        err = esp_https_ota_perform(ota_handle);
        s_ota_progress.bytes_written = esp_https_ota_get_image_len_read(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
    }

    if (err == ESP_OK && esp_https_ota_is_complete_data_received(ota_handle)) {
        esp_err_t finish_err = esp_https_ota_finish(ota_handle);
        if (finish_err == ESP_OK) {
            ESP_LOGI(TAG, "OTA succeeded — rebooting in 2 seconds");
            s_ota_progress.stage = OTA_DONE;
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(finish_err));
            s_ota_progress.stage = OTA_FAILED;
            ota_end_operation();
        }
    } else {
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        s_ota_progress.stage = OTA_FAILED;
        ota_end_operation();
    }

    vTaskDelete(NULL);
}

bool ota_start_from_url(const char *url)
{
    if (!url || strlen(url) == 0) {
        ESP_LOGW(TAG, "OTA: empty URL");
        return false;
    }

    if (!ota_begin_operation()) {
        ESP_LOGW(TAG, "OTA: update already in progress");
        return false;
    }

    strncpy(s_ota_url, url, sizeof(s_ota_url) - 1);
    s_ota_url[sizeof(s_ota_url) - 1] = '\0';

    // Run OTA in a separate task with enough stack for HTTP + flash ops
    if (xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "OTA: failed to start task");
        s_ota_progress.stage = OTA_FAILED;
        ota_end_operation();
        return false;
    }
    return true;
}
