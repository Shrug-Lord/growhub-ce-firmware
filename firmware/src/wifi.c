#include "wifi.h"
#include "config.h"
#include "time_sync.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "wifi";

#define MAX_QUICK_RETRIES          10
#define AP_FALLBACK_TIMEOUT_MS     (5UL * 60UL * 1000UL)
#define RECOVERY_RETRY_INTERVAL_MS (60UL * 1000UL)
#define AP_PREFERENCE_DELAY_MS     2000UL

static bool s_connected;
static bool s_ap_active;
static bool s_manual_ap_override;
static int s_retry_count;
static TickType_t s_fallback_deadline_tick;
static char s_sta_ip[16];

static TimerHandle_t s_fallback_timer;
static TimerHandle_t s_recovery_timer;
static TimerHandle_t s_preference_timer;

static void set_ap_active(bool active, const char *reason)
{
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return;

    wifi_mode_t wanted = mode;
    if (active) {
        wanted = (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
            ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    } else if (mode == WIFI_MODE_APSTA) {
        wanted = WIFI_MODE_STA;
    } else if (mode == WIFI_MODE_AP) {
        // AP-only is required when the device has no station credentials.
        return;
    }

    if (wanted != mode) {
        esp_err_t err = esp_wifi_set_mode(wanted);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to %s setup AP: %s",
                     active ? "enable" : "disable", esp_err_to_name(err));
            return;
        }
    }
    s_ap_active = active;
    ESP_LOGI(TAG, "Setup AP %s (%s)", active ? "enabled" : "disabled", reason);
}

static void stop_fallback_timer(void)
{
    s_fallback_deadline_tick = 0;
    if (s_fallback_timer) xTimerStop(s_fallback_timer, 0);
}

static void start_fallback_timer(void)
{
    if (config_get()->keep_ap_active || s_manual_ap_override ||
        !config_get()->sta_ssid[0] || s_ap_active) return;

    if (!s_fallback_timer) return;
    s_fallback_deadline_tick = xTaskGetTickCount() +
        pdMS_TO_TICKS(AP_FALLBACK_TIMEOUT_MS);
    xTimerStop(s_fallback_timer, 0);
    xTimerChangePeriod(s_fallback_timer,
                       pdMS_TO_TICKS(AP_FALLBACK_TIMEOUT_MS), 0);
    ESP_LOGI(TAG, "Setup AP fallback scheduled in 5 minutes");
}

static void fallback_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    s_fallback_deadline_tick = 0;
    if (!s_connected && !config_get()->keep_ap_active &&
        !s_manual_ap_override && config_get()->sta_ssid[0]) {
        set_ap_active(true, "station offline for 5 minutes");
    }
}

static void recovery_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_connected && config_get()->sta_ssid[0]) {
        ESP_LOGI(TAG, "Periodic station recovery attempt");
        esp_wifi_connect();
    }
}

static void preference_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (config_get()->keep_ap_active) {
        set_ap_active(true, "user preference");
    } else if (s_connected && !s_manual_ap_override) {
        set_ap_active(false, "user preference");
    } else if (!s_connected && !s_ap_active) {
        start_fallback_timer();
    }
}

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t event_id, void *event_data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            if (config_get()->sta_ssid[0]) {
                ESP_LOGI(TAG, "Station started — connecting...");
                start_fallback_timer();
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_connected || s_fallback_deadline_tick == 0) start_fallback_timer();
            s_connected = false;
            s_sta_ip[0] = '\0';
            if (!config_get()->sta_ssid[0]) {
                ESP_LOGI(TAG, "Station stopped — no saved credentials");
                break;
            }
            if (s_retry_count < MAX_QUICK_RETRIES) {
                s_retry_count++;
                ESP_LOGI(TAG, "Disconnected — quick retry %d/%d",
                         s_retry_count, MAX_QUICK_RETRIES);
                esp_wifi_connect();
            } else if (s_recovery_timer) {
                ESP_LOGW(TAG, "Quick retries exhausted — retrying once per minute");
                xTimerStart(s_recovery_timer, 0);
            }
            break;
        case WIFI_EVENT_AP_START:
            s_ap_active = true;
            break;
        case WIFI_EVENT_AP_STOP:
            s_ap_active = false;
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "Setup AP client connected");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Setup AP client disconnected");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = event_data;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "Connected — IP: %s", s_sta_ip);
        s_connected = true;
        s_retry_count = 0;
        stop_fallback_timer();
        if (s_recovery_timer) xTimerStop(s_recovery_timer, 0);

        if (!config_get()->keep_ap_active && !s_manual_ap_override) {
            set_ap_active(false, "station connected");
        }
        time_sync_on_wifi_connected();
    }
}

void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    s_fallback_timer = xTimerCreate("ap_fallback",
        pdMS_TO_TICKS(AP_FALLBACK_TIMEOUT_MS), pdFALSE, NULL, fallback_timer_cb);
    s_recovery_timer = xTimerCreate("wifi_retry",
        pdMS_TO_TICKS(RECOVERY_RETRY_INTERVAL_MS), pdTRUE, NULL, recovery_timer_cb);
    s_preference_timer = xTimerCreate("ap_pref",
        pdMS_TO_TICKS(AP_PREFERENCE_DELAY_MS), pdFALSE, NULL, preference_timer_cb);
    if (!s_fallback_timer || !s_recovery_timer || !s_preference_timer) {
        ESP_LOGE(TAG, "Failed to allocate WiFi recovery timers");
    }

    const growhub_config_t *cfg = config_get();
    bool has_sta_creds = cfg->sta_ssid[0] != '\0';

    wifi_config_t ap_cfg = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .channel = 1,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, cfg->ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(cfg->ap_ssid);
    if (cfg->ap_password[0]) {
        strncpy((char *)ap_cfg.ap.password, cfg->ap_password,
                sizeof(ap_cfg.ap.password));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    // Configure both interfaces before selecting the startup mode.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    if (has_sta_creds) {
        wifi_config_t sta_cfg = {0};
        strncpy((char *)sta_cfg.sta.ssid, cfg->sta_ssid, sizeof(sta_cfg.sta.ssid));
        strncpy((char *)sta_cfg.sta.password, cfg->sta_password,
                sizeof(sta_cfg.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        if (!cfg->keep_ap_active) ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        s_ap_active = cfg->keep_ap_active;
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        s_ap_active = true;
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Mode: %s — setup AP %s",
             has_sta_creds ? "station" : "provisioning",
             s_ap_active ? "active" : "disabled");
}

bool wifi_is_connected(void) { return s_connected; }
bool wifi_is_ap_active(void) { return s_ap_active; }

const char *wifi_ap_reason(void)
{
    if (!s_ap_active) return "off";
    if (!config_get()->sta_ssid[0]) return "provisioning";
    if (s_manual_ap_override) return "manual_override";
    if (config_get()->keep_ap_active) return "preference";
    return "fallback";
}

uint32_t wifi_ap_fallback_seconds(void)
{
    if (s_fallback_deadline_tick == 0 || s_ap_active || s_connected) return 0;
    TickType_t remaining = s_fallback_deadline_tick - xTaskGetTickCount();
    if ((int32_t)remaining <= 0) return 0;
    return (uint32_t)((remaining + pdMS_TO_TICKS(1000) - 1) /
                      pdMS_TO_TICKS(1000));
}

const char *wifi_get_sta_ip(void) { return s_sta_ip; }

void wifi_set_keep_ap_active(bool keep_active)
{
    config_save_keep_ap_active(keep_active);
    if (s_preference_timer) {
        xTimerStop(s_preference_timer, 0);
        xTimerStart(s_preference_timer, 0);
    }
}

void wifi_reconnect(void)
{
    const growhub_config_t *cfg = config_get();
    if (!cfg->sta_ssid[0]) return;

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP) esp_wifi_set_mode(WIFI_MODE_APSTA);

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, cfg->sta_ssid, sizeof(sta_cfg.sta.ssid));
    strncpy((char *)sta_cfg.sta.password, cfg->sta_password,
            sizeof(sta_cfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);

    bool was_connected = s_connected;
    s_retry_count = 0;
    s_connected = false;
    start_fallback_timer();
    if (was_connected) {
        // The disconnect event owns the first reconnect attempt.
        esp_wifi_disconnect();
    } else {
        esp_wifi_connect();
    }
}

bool wifi_is_in_recovery_mode(void)
{
    return s_manual_ap_override ||
        (s_ap_active && config_get()->sta_ssid[0] && !config_get()->keep_ap_active);
}

bool wifi_is_manual_ap_override(void) { return s_manual_ap_override; }

void wifi_enter_recovery_mode(void)
{
    s_manual_ap_override = true;
    stop_fallback_timer();
    set_ap_active(true, "physical button override");
}

void wifi_exit_recovery_mode(bool clear_creds)
{
    s_manual_ap_override = false;
    if (clear_creds) {
        stop_fallback_timer();
        if (s_recovery_timer) xTimerStop(s_recovery_timer, 0);
        config_save_wifi("", "");
        s_connected = false;
        s_sta_ip[0] = '\0';
        esp_wifi_disconnect();
        esp_wifi_set_mode(WIFI_MODE_AP);
        s_ap_active = true;
        ESP_LOGI(TAG, "WiFi credentials cleared — provisioning AP active");
    } else if (s_connected && !config_get()->keep_ap_active) {
        set_ap_active(false, "physical override cancelled");
    } else if (!s_connected && !config_get()->keep_ap_active) {
        set_ap_active(false, "physical override cancelled");
        start_fallback_timer();
    }
}
