#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "config";
static const char *NVS_NAMESPACE = "growhub";

static growhub_config_t s_config;
static char s_mac_str[13]; // "AABBCCDDEEFF\0"

static void load_defaults(void)
{
    memset(&s_config, 0, sizeof(s_config));

    // WiFi — empty means AP mode on first boot
    s_config.sta_ssid[0] = '\0';
    s_config.sta_password[0] = '\0';
    s_config.ap_password[0] = '\0'; // open AP by default

    // MQTT — optional, user configures via web UI; empty default = disabled
    s_config.mqtt_host[0] = '\0';
    s_config.mqtt_port = DEFAULT_MQTT_PORT;

    // Temperature unit — Celsius default
    s_config.temp_unit = 0;

    // Time source — SNTP default
    s_config.time_src = 0;
    strncpy(s_config.sntp_primary, DEFAULT_SNTP_PRIMARY, MAX_SNTP_HOST_LEN);
    strncpy(s_config.sntp_secondary, DEFAULT_SNTP_SECONDARY, MAX_SNTP_HOST_LEN);

    // Timezone — US Eastern
    strncpy(s_config.timezone, "EST5EDT,M3.2.0,M11.1.0", MAX_TIMEZONE_LEN);

    // Relay names default to unassigned. Labels are shown only after the user assigns them.
    for (int i = 0; i < NUM_RELAY_SLOTS; i++) {
        s_config.relay_names[i][0] = '\0';
    }

    // GPIO defaults
    s_config.pin_relay_outlet1 = DEFAULT_RELAY_OUTLET1_PIN;
    s_config.pin_relay_outlet2 = DEFAULT_RELAY_OUTLET2_PIN;
    s_config.pin_relay_outlet3 = DEFAULT_RELAY_OUTLET3_PIN;
    s_config.pin_relay_outlet4 = DEFAULT_RELAY_OUTLET4_PIN;
    s_config.pin_sensor_uart_tx = DEFAULT_SENSOR_UART_TX_PIN;
    s_config.pin_sensor_uart_rx = DEFAULT_SENSOR_UART_RX_PIN;
    s_config.pin_button      = DEFAULT_BUTTON_PIN;
    s_config.pin_led         = DEFAULT_LED_PIN;

    // Calibration
    s_config.temp_offset = 0.0f;
    s_config.rh_offset   = 0.0f;

    // Reporting
    s_config.report_interval_s = DEFAULT_REPORT_INTERVAL;

    // MQTT soft-disable — enabled by default
    s_config.mqtt_disabled = 0;

    // Relay mode — AUTO on first boot
    s_config.relay_mode = 0;

    // Local schedule pause mask. Saved outlet rules are enabled by default.
    s_config.schedule_disabled_mask = 0;
}

static void nvs_read_str(nvs_handle_t h, const char *key, char *buf, size_t max_len)
{
    size_t len = max_len;
    if (nvs_get_str(h, key, buf, &len) != ESP_OK) {
        // keep existing default
    }
}

static void nvs_read_u8(nvs_handle_t h, const char *key, uint8_t *val)
{
    nvs_get_u8(h, key, val);
}

static void nvs_read_u16(nvs_handle_t h, const char *key, uint16_t *val)
{
    nvs_get_u16(h, key, val);
}

static void load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No saved config — using defaults");
        return;
    }

    nvs_read_str(h, "sta_ssid",     s_config.sta_ssid,     sizeof(s_config.sta_ssid));
    nvs_read_str(h, "sta_pass",     s_config.sta_password,  sizeof(s_config.sta_password));
    nvs_read_str(h, "ap_ssid",      s_config.ap_ssid,       sizeof(s_config.ap_ssid));
    nvs_read_str(h, "ap_pass",      s_config.ap_password,   sizeof(s_config.ap_password));
    nvs_read_str(h, "mqtt_host",    s_config.mqtt_host,     sizeof(s_config.mqtt_host));
    nvs_read_u16(h, "mqtt_port",    &s_config.mqtt_port);
    nvs_read_str(h, "dev_name",     s_config.device_name,   sizeof(s_config.device_name));
    nvs_read_str(h, "timezone",     s_config.timezone,      sizeof(s_config.timezone));
    nvs_read_u8(h,  "temp_unit",    &s_config.temp_unit);
    nvs_read_u8(h,  "time_src",     &s_config.time_src);
    nvs_read_str(h, "sntp_primary", s_config.sntp_primary,  sizeof(s_config.sntp_primary));
    nvs_read_str(h, "sntp_secondary", s_config.sntp_secondary, sizeof(s_config.sntp_secondary));

    nvs_read_str(h, "relay_0", s_config.relay_names[0], MAX_RELAY_NAME_LEN + 1);
    nvs_read_str(h, "relay_1", s_config.relay_names[1], MAX_RELAY_NAME_LEN + 1);
    nvs_read_str(h, "relay_2", s_config.relay_names[2], MAX_RELAY_NAME_LEN + 1);
    nvs_read_str(h, "relay_3", s_config.relay_names[3], MAX_RELAY_NAME_LEN + 1);

    nvs_read_u8(h, "pin_outlet1", &s_config.pin_relay_outlet1);
    nvs_read_u8(h, "pin_outlet2", &s_config.pin_relay_outlet2);
    nvs_read_u8(h, "pin_outlet3", &s_config.pin_relay_outlet3);
    nvs_read_u8(h, "pin_outlet4", &s_config.pin_relay_outlet4);
    nvs_read_u8(h, "pin_sensor_tx", &s_config.pin_sensor_uart_tx);
    nvs_read_u8(h, "pin_sensor_rx", &s_config.pin_sensor_uart_rx);
    nvs_read_u8(h, "pin_btn",     &s_config.pin_button);
    nvs_read_u8(h, "pin_led",     &s_config.pin_led);
    nvs_read_u8(h, "report_s",    &s_config.report_interval_s);
    nvs_read_u8(h, "mqtt_dis",    &s_config.mqtt_disabled);
    nvs_read_u8(h, "relay_mode",  &s_config.relay_mode);
    nvs_read_u8(h, "sched_dis",   &s_config.schedule_disabled_mask);

    // Float calibration offsets stored as int16 * 100
    int16_t tmp;
    if (nvs_get_i16(h, "temp_off", &tmp) == ESP_OK) s_config.temp_offset = tmp / 100.0f;
    if (nvs_get_i16(h, "rh_off",   &tmp) == ESP_OK) s_config.rh_offset   = tmp / 100.0f;

    nvs_close(h);
    ESP_LOGI(TAG, "Config loaded from NVS");
}

static void init_mac_string(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_mac_str, sizeof(s_mac_str),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Auto-generate AP SSID from MAC if not set
    if (s_config.ap_ssid[0] == '\0') {
        snprintf(s_config.ap_ssid, sizeof(s_config.ap_ssid),
                 "growhub_%02X%02X", mac[4], mac[5]);
    }

    // Auto-generate device name from MAC if not set
    if (s_config.device_name[0] == '\0') {
        snprintf(s_config.device_name, sizeof(s_config.device_name),
                 "GrowHub_%02X%02X", mac[4], mac[5]);
    }
}

void config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated — erasing");
        nvs_flash_erase();
        nvs_flash_init();
    }

    load_defaults();
    load_from_nvs();
    init_mac_string();

    ESP_LOGI(TAG, "Device MAC: %s", s_mac_str);
    ESP_LOGI(TAG, "WiFi SSID: %s", s_config.sta_ssid[0] ? s_config.sta_ssid : "(not set — AP mode)");
    ESP_LOGI(TAG, "MQTT host: %s:%d", s_config.mqtt_host, s_config.mqtt_port);
}

const growhub_config_t *config_get(void)
{
    return &s_config;
}

const char *config_get_mac_str(void)
{
    return s_mac_str;
}

// --- Save helpers ---

static void nvs_write_str(nvs_handle_t h, const char *key, const char *val)
{
    nvs_set_str(h, key, val);
}

void config_save_wifi(const char *ssid, const char *password)
{
    strncpy(s_config.sta_ssid, ssid, MAX_SSID_LEN);
    s_config.sta_ssid[MAX_SSID_LEN] = '\0';
    strncpy(s_config.sta_password, password, MAX_PASSWORD_LEN);
    s_config.sta_password[MAX_PASSWORD_LEN] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_write_str(h, "sta_ssid", s_config.sta_ssid);
        nvs_write_str(h, "sta_pass", s_config.sta_password);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "WiFi credentials saved");
    }
}

void config_save_mqtt(const char *host, uint16_t port)
{
    strncpy(s_config.mqtt_host, host, MAX_HOSTNAME_LEN);
    s_config.mqtt_host[MAX_HOSTNAME_LEN] = '\0';
    s_config.mqtt_port = port;
    s_config.mqtt_disabled = 0;  // saving a host implicitly re-enables MQTT

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_write_str(h, "mqtt_host", s_config.mqtt_host);
        nvs_set_u16(h, "mqtt_port", s_config.mqtt_port);
        nvs_set_u8(h, "mqtt_dis", 0);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "MQTT config saved");
    }
}

void config_save_mqtt_disabled(bool disabled)
{
    s_config.mqtt_disabled = disabled ? 1 : 0;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "mqtt_dis", s_config.mqtt_disabled);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "MQTT %s", disabled ? "disabled" : "enabled");
    }
}

void config_save_device(const char *name, const char *timezone, int temp_unit, int time_src)
{
    if (name) {
        strncpy(s_config.device_name, name, MAX_DEVICE_NAME_LEN);
        s_config.device_name[MAX_DEVICE_NAME_LEN] = '\0';
    }
    if (timezone) {
        strncpy(s_config.timezone, timezone, MAX_TIMEZONE_LEN);
        s_config.timezone[MAX_TIMEZONE_LEN] = '\0';
    }
    if (temp_unit >= 0) {
        s_config.temp_unit = (uint8_t)temp_unit;
    }
    if (time_src >= 0) {
        s_config.time_src = (uint8_t)time_src;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        if (name)         nvs_write_str(h, "dev_name", s_config.device_name);
        if (timezone)     nvs_write_str(h, "timezone", s_config.timezone);
        if (temp_unit >= 0) nvs_set_u8(h, "temp_unit", s_config.temp_unit);
        if (time_src >= 0)  nvs_set_u8(h, "time_src",  s_config.time_src);
        nvs_commit(h);
        nvs_close(h);
    }
}

void config_save_sntp_servers(const char *primary, const char *secondary)
{
    if (primary) {
        strncpy(s_config.sntp_primary, primary, MAX_SNTP_HOST_LEN);
        s_config.sntp_primary[MAX_SNTP_HOST_LEN] = '\0';
    }
    if (secondary) {
        strncpy(s_config.sntp_secondary, secondary, MAX_SNTP_HOST_LEN);
        s_config.sntp_secondary[MAX_SNTP_HOST_LEN] = '\0';
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        if (primary) nvs_write_str(h, "sntp_primary", s_config.sntp_primary);
        if (secondary) nvs_write_str(h, "sntp_secondary", s_config.sntp_secondary);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "SNTP servers saved: %s, %s",
                 s_config.sntp_primary, s_config.sntp_secondary);
    }
}

void config_save_pins(const growhub_config_t *cfg)
{
    s_config.pin_relay_outlet1 = cfg->pin_relay_outlet1;
    s_config.pin_relay_outlet2 = cfg->pin_relay_outlet2;
    s_config.pin_relay_outlet3 = cfg->pin_relay_outlet3;
    s_config.pin_relay_outlet4 = cfg->pin_relay_outlet4;
    s_config.pin_sensor_uart_tx = cfg->pin_sensor_uart_tx;
    s_config.pin_sensor_uart_rx = cfg->pin_sensor_uart_rx;
    for (int i = 0; i < NUM_RELAY_SLOTS; i++) {
        strncpy(s_config.relay_names[i], cfg->relay_names[i], MAX_RELAY_NAME_LEN);
        s_config.relay_names[i][MAX_RELAY_NAME_LEN] = '\0';
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "pin_outlet1", s_config.pin_relay_outlet1);
        nvs_set_u8(h, "pin_outlet2", s_config.pin_relay_outlet2);
        nvs_set_u8(h, "pin_outlet3", s_config.pin_relay_outlet3);
        nvs_set_u8(h, "pin_outlet4", s_config.pin_relay_outlet4);
        nvs_set_u8(h, "pin_sensor_tx", s_config.pin_sensor_uart_tx);
        nvs_set_u8(h, "pin_sensor_rx", s_config.pin_sensor_uart_rx);
        nvs_write_str(h, "relay_0", s_config.relay_names[0]);
        nvs_write_str(h, "relay_1", s_config.relay_names[1]);
        nvs_write_str(h, "relay_2", s_config.relay_names[2]);
        nvs_write_str(h, "relay_3", s_config.relay_names[3]);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Outlet config saved");
    }
}

void config_save_schedule(const char *json)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "sched_json", json, strlen(json) + 1);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Schedule saved to NVS (%d bytes)", (int)strlen(json));
    }
}

bool config_load_schedule(char **json_out, size_t *len_out)
{
    if (!json_out) return false;
    *json_out = NULL;
    if (len_out) *len_out = 0;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, "sched_json", NULL, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(h);
        return false;
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        nvs_close(h);
        return false;
    }

    err = nvs_get_blob(h, "sched_json", buf, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        free(buf);
        return false;
    }

    buf[len] = '\0';
    size_t content_len = 0;
    while (content_len < len && buf[content_len] != '\0') {
        content_len++;
    }

    *json_out = buf;
    if (len_out) *len_out = content_len;
    return true;
}

void config_clear_schedule(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "sched_json");
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Schedule cleared from NVS");
    }
}

void config_save_schedule_disabled_mask(uint8_t mask)
{
    s_config.schedule_disabled_mask = mask & 0x0F;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "sched_dis", s_config.schedule_disabled_mask);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Schedule disabled mask saved: 0x%02X", s_config.schedule_disabled_mask);
    }
}

void config_save_calibration(float temp_off, float rh_off)
{
    s_config.temp_offset = temp_off;
    s_config.rh_offset   = rh_off;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i16(h, "temp_off", (int16_t)(temp_off * 100));
        nvs_set_i16(h, "rh_off",   (int16_t)(rh_off * 100));
        nvs_commit(h);
        nvs_close(h);
    }
}

void config_save_relay_mode(uint8_t mode)
{
    s_config.relay_mode = mode;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "relay_mode", s_config.relay_mode);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Relay mode saved: %s", mode ? "MANUAL" : "AUTO");
    }
}

void config_factory_reset(void)
{
    ESP_LOGW(TAG, "FACTORY RESET — erasing NVS and rebooting");
    nvs_flash_erase();
    esp_restart();
}
