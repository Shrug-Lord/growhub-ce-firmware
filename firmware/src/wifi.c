#include "wifi.h"
#include "config.h"
#include "time_sync.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "wifi";
static bool s_connected = false;
static int s_retry_count = 0;
static bool s_ignore_next_sta_disconnect = false;
#define MAX_RETRIES 10

// WiFi Recovery Mode — entered after retry exhaustion when SSID not visible
static bool s_recovery_mode = false;
static TimerHandle_t s_recovery_timer = NULL;
#define RECOVERY_SCAN_INTERVAL_MS (3600UL * 1000UL)  // 1 hour

// Forward declaration
static void recovery_scan_task(void *arg);

static void recovery_timer_cb(TimerHandle_t timer)
{
    // Can't do blocking WiFi scan in timer context — spawn a task
    xTaskCreate(recovery_scan_task, "wifi_scan", 4096, NULL, 2, NULL);
}

static void recovery_scan_task(void *arg)
{
    const growhub_config_t *cfg = config_get();
    if (!cfg->sta_ssid[0]) {
        vTaskDelete(NULL);
        return;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 100, .max = 300},
    };
    esp_wifi_scan_start(&scan_cfg, true);

    uint16_t count = 20;
    wifi_ap_record_t *aps = malloc(count * sizeof(wifi_ap_record_t));
    if (!aps) {
        if (s_recovery_mode && s_recovery_timer) xTimerStart(s_recovery_timer, 0);
        vTaskDelete(NULL);
        return;
    }

    esp_wifi_scan_get_ap_records(&count, aps);

    bool found = false;
    for (int i = 0; i < count; i++) {
        if (strcmp((char *)aps[i].ssid, cfg->sta_ssid) == 0) {
            found = true;
            break;
        }
    }
    free(aps);

    if (found) {
        ESP_LOGI(TAG, "Recovery scan: '%s' found — attempting reconnect", cfg->sta_ssid);
        s_retry_count = 0;
        esp_wifi_connect();
        // Timer restarts only if this reconnect also fails (handled in DISCONNECTED handler)
    } else {
        ESP_LOGI(TAG, "Recovery scan: '%s' not visible — will try again in 1h", cfg->sta_ssid);
        if (!s_recovery_mode) {
            wifi_enter_recovery_mode();
        } else if (s_recovery_timer) {
            xTimerStart(s_recovery_timer, 0);
        }
    }

    vTaskDelete(NULL);
}

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            if (config_get()->sta_ssid[0] != '\0') {
                ESP_LOGI(TAG, "Station started — connecting...");
                esp_wifi_connect();
            } else {
                ESP_LOGI(TAG, "Station started for setup scans");
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_connected = false;
            if (s_ignore_next_sta_disconnect) {
                s_ignore_next_sta_disconnect = false;
                ESP_LOGI(TAG, "Station disconnected intentionally — staying AP-only");
                break;
            }
            if (s_retry_count < MAX_RETRIES) {
                s_retry_count++;
                ESP_LOGI(TAG, "Disconnected — retry %d/%d", s_retry_count, MAX_RETRIES);
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "Max retries reached — scanning for '%s'",
                         config_get()->sta_ssid);
                // Check if SSID is visible before entering recovery mode
                xTaskCreate(recovery_scan_task, "wifi_scan", 4096, NULL, 2, NULL);
                // recovery_scan_task will enter recovery mode if SSID not found,
                // or retry immediately if found
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "AP client connected");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "AP client disconnected");
            break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = event_data;
        ESP_LOGI(TAG, "Connected — IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        s_retry_count = 0;
        // Clear recovery mode on successful connection
        if (s_recovery_mode) {
            s_recovery_mode = false;
            if (s_recovery_timer) xTimerStop(s_recovery_timer, 0);
            ESP_LOGI(TAG, "Recovery Mode cleared — WiFi restored");
        }
        time_sync_on_wifi_connected();
    }
}

void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();

    // Create default netifs for both modes
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &event_handler, NULL, NULL);

    const growhub_config_t *cfg = config_get();
    bool has_sta_creds = cfg->sta_ssid[0] != '\0';

    // AP config — always active for configuration access
    wifi_config_t ap_cfg = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .channel = 1,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, cfg->ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(cfg->ap_ssid);

    if (cfg->ap_password[0] != '\0') {
        strncpy((char *)ap_cfg.ap.password, cfg->ap_password, sizeof(ap_cfg.ap.password));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    if (has_sta_creds) {
        // Both AP + Station
        esp_wifi_set_mode(WIFI_MODE_APSTA);

        wifi_config_t sta_cfg = {0};
        strncpy((char *)sta_cfg.sta.ssid, cfg->sta_ssid, sizeof(sta_cfg.sta.ssid));
        strncpy((char *)sta_cfg.sta.password, cfg->sta_password, sizeof(sta_cfg.sta.password));

        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        esp_wifi_start();

        ESP_LOGI(TAG, "Mode: AP+STA — AP SSID: %s, connecting to: %s",
                 cfg->ap_ssid, cfg->sta_ssid);
    } else {
        // AP only — waiting for user to configure WiFi
        esp_wifi_set_mode(WIFI_MODE_AP);
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        esp_wifi_start();

        ESP_LOGI(TAG, "Mode: AP only — SSID: %s (connect to configure WiFi)", cfg->ap_ssid);
    }
}

bool wifi_is_connected(void)
{
    return s_connected;
}

void wifi_reconnect(void)
{
    const growhub_config_t *cfg = config_get();
    if (cfg->sta_ssid[0] == '\0') return;

    ESP_LOGI(TAG, "Reconnecting to: %s", cfg->sta_ssid);
    s_retry_count = 0;
    s_ignore_next_sta_disconnect = false;

    esp_wifi_disconnect();

    // Switch to APSTA if currently AP-only
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, cfg->sta_ssid, sizeof(sta_cfg.sta.ssid));
    strncpy((char *)sta_cfg.sta.password, cfg->sta_password, sizeof(sta_cfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);

    esp_wifi_connect();
}

bool wifi_is_in_recovery_mode(void)
{
    return s_recovery_mode;
}

void wifi_enter_recovery_mode(void)
{
    if (s_recovery_mode) return;  // already in recovery
    s_recovery_mode = true;

    ESP_LOGI(TAG, "WiFi Recovery Mode: will scan for '%s' every hour",
             config_get()->sta_ssid);

    if (!s_recovery_timer) {
        s_recovery_timer = xTimerCreate("wifi_rec",
                                         pdMS_TO_TICKS(RECOVERY_SCAN_INTERVAL_MS),
                                         pdFALSE,  // one-shot; restarted manually after each scan
                                         NULL,
                                         recovery_timer_cb);
    }
    if (s_recovery_timer) {
        xTimerStart(s_recovery_timer, 0);
    }
}

void wifi_exit_recovery_mode(bool clear_creds)
{
    s_recovery_mode = false;
    s_retry_count = 0;
    s_connected = false;

    if (s_recovery_timer) xTimerStop(s_recovery_timer, 0);

    s_ignore_next_sta_disconnect = true;
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_AP);

    if (clear_creds) {
        config_save_wifi("", "");
        ESP_LOGI(TAG, "Recovery Mode exited — WiFi credentials cleared");
    } else {
        ESP_LOGI(TAG, "Recovery Mode exited — staying AP-only (creds kept)");
    }
}
