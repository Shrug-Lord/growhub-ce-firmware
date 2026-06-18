#include "time_sync.h"
#include "config.h"
#include "mqtt.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

static const char *TAG = "time_sync";
static const int64_t SNTP_UNHEALTHY_GRACE_US = 3600LL * 1000000LL;
static const int64_t SNTP_STALE_POLLS = 3;
static bool s_sntp_synced_this_boot = false;
static int64_t s_sntp_started_us = 0;
static int64_t s_sntp_last_sync_us = 0;
static bool s_config_applied = false;

static int64_t sntp_stale_after_us(void)
{
    int64_t sync_interval_us = (int64_t)esp_sntp_get_sync_interval() * 1000LL;
    int64_t stale_after_us = sync_interval_us * SNTP_STALE_POLLS;
    return stale_after_us < SNTP_UNHEALTHY_GRACE_US
        ? SNTP_UNHEALTHY_GRACE_US
        : stale_after_us;
}

static void note_sntp_sync(void)
{
    s_sntp_synced_this_boot = true;
    s_sntp_last_sync_us = esp_timer_get_time();
}

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    note_sntp_sync();
    ESP_LOGI(TAG, "SNTP time synchronized");
    mqtt_publish_schedule_state("time");
}

void time_sync_apply_config(void)
{
    const growhub_config_t *cfg = config_get();
    s_config_applied = true;
    setenv("TZ", cfg->timezone, 1);
    tzset();

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    s_sntp_synced_this_boot = false;
    s_sntp_last_sync_us = 0;

    if (cfg->time_src == 1) {
        s_sntp_started_us = 0;
        ESP_LOGI(TAG, "Time source: Manual (SNTP disabled, TZ=%s)", cfg->timezone);
        mqtt_publish_schedule_state("time");
        return;
    }

    s_sntp_started_us = esp_timer_get_time();
    esp_sntp_set_time_sync_notification_cb(on_sntp_sync);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, cfg->sntp_primary);
    esp_sntp_setservername(1, cfg->sntp_secondary);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP initialized (TZ=%s, primary=%s, secondary=%s)",
             cfg->timezone, cfg->sntp_primary, cfg->sntp_secondary);
    mqtt_publish_schedule_state("time");
}

void time_sync_init(void)
{
    time_sync_apply_config();
}

void time_sync_on_wifi_connected(void)
{
    if (config_get()->time_src == 1) return;

    if (!s_config_applied) {
        return;
    }

    s_sntp_started_us = esp_timer_get_time();
    if (esp_sntp_enabled()) {
        if (esp_sntp_restart()) {
            ESP_LOGI(TAG, "SNTP restarted after WiFi connection");
        }
    } else {
        time_sync_apply_config();
        ESP_LOGI(TAG, "SNTP started after WiFi connection");
    }
    mqtt_publish_schedule_state("time");
}

bool time_sync_wall_time_valid(void)
{
    time_t now;
    time(&now);
    return now >= 86400;
}

const char *time_sync_source_str(void)
{
    return config_get()->time_src == 1 ? "manual" : "sntp";
}

const char *time_sync_sntp_status_str(void)
{
    if (config_get()->time_src == 1) {
        return "disabled";
    }

    sntp_sync_status_t status = esp_sntp_get_sync_status();
    if (status == SNTP_SYNC_STATUS_COMPLETED) {
        note_sntp_sync();
    }

    return s_sntp_synced_this_boot ? "synced" : "pending";
}

bool time_sync_sntp_unhealthy(void)
{
    if (config_get()->time_src == 1 || !time_sync_wall_time_valid()) {
        return false;
    }

    int64_t now_us = esp_timer_get_time();
    bool in_start_grace = s_sntp_started_us > 0 &&
        now_us - s_sntp_started_us < SNTP_UNHEALTHY_GRACE_US;

    if (strcmp(time_sync_sntp_status_str(), "synced") != 0) {
        return !in_start_grace;
    }

    if (s_sntp_last_sync_us <= 0) {
        return !in_start_grace;
    }

    if (now_us - s_sntp_last_sync_us >= sntp_stale_after_us()) {
        return !in_start_grace;
    }

    return false;
}
