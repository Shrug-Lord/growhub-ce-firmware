#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// --- GPIO defaults (configurable via NVS) ---
// These must be confirmed on actual hardware — set via web UI if wrong
#define DEFAULT_RELAY_OUTLET1_PIN   33
#define DEFAULT_RELAY_OUTLET2_PIN   25
#define DEFAULT_RELAY_OUTLET3_PIN   26
#define DEFAULT_RELAY_OUTLET4_PIN   27
#define DEFAULT_SENSOR_UART_TX_PIN  17
#define DEFAULT_SENSOR_UART_RX_PIN  16
#define DEFAULT_BUTTON_PIN          0
#define DEFAULT_LED_PIN             2

// --- Limits ---
#define MAX_SSID_LEN        32
#define MAX_PASSWORD_LEN    64
#define MAX_HOSTNAME_LEN    64
#define MAX_DEVICE_NAME_LEN 32
#define MAX_TIMEZONE_LEN    48
#define MAX_SNTP_HOST_LEN   64
#define NUM_RELAY_SLOTS     4
#define MAX_RELAY_NAME_LEN  16

// --- MQTT defaults ---
#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_REPORT_INTERVAL 6       // seconds
#define DEFAULT_SNTP_PRIMARY    "pool.ntp.org"
#define DEFAULT_SNTP_SECONDARY  "time.nist.gov"

// --- Device config ---
typedef struct {
    // WiFi station
    char     sta_ssid[MAX_SSID_LEN + 1];
    char     sta_password[MAX_PASSWORD_LEN + 1];

    // WiFi AP (auto-generated from MAC if empty)
    char     ap_ssid[MAX_SSID_LEN + 1];
    char     ap_password[MAX_PASSWORD_LEN + 1];

    // MQTT broker
    char     mqtt_host[MAX_HOSTNAME_LEN + 1];
    uint16_t mqtt_port;

    // Device identity
    char     device_name[MAX_DEVICE_NAME_LEN + 1];

    // Timezone (POSIX TZ string)
    char     timezone[MAX_TIMEZONE_LEN + 1];

    // User-assigned outlet labels. Empty string means unassigned/no label.
    char     relay_names[NUM_RELAY_SLOTS][MAX_RELAY_NAME_LEN + 1];

    // Temperature display unit (0=Celsius, 1=Fahrenheit)
    uint8_t  temp_unit;

    // Time source (0=SNTP, 1=Manual browser sync)
    uint8_t  time_src;
    char     sntp_primary[MAX_SNTP_HOST_LEN + 1];
    char     sntp_secondary[MAX_SNTP_HOST_LEN + 1];

    // GPIO pin assignments
    uint8_t  pin_relay_outlet1;  // bit3 — Outlet 1 (GPIO 33 confirmed)
    uint8_t  pin_relay_outlet2;  // bit0 — Outlet 2
    uint8_t  pin_relay_outlet3;  // bit1 — Outlet 3
    uint8_t  pin_relay_outlet4;  // bit2 — Outlet 4
    uint8_t  pin_sensor_uart_tx;
    uint8_t  pin_sensor_uart_rx;
    uint8_t  pin_button;
    uint8_t  pin_led;

    // Sensor calibration offsets
    float    temp_offset;
    float    rh_offset;

    // Reporting cadence
    uint8_t  report_interval_s;

    // MQTT soft-disable: broker config retained but client won't connect
    uint8_t  mqtt_disabled;

    // Relay mode persisted across reboots (0=AUTO, 1=MANUAL)
    uint8_t  relay_mode;

    // Local web UI schedule pause mask. bit0=Outlet1 ... bit3=Outlet4.
    uint8_t  schedule_disabled_mask;
} growhub_config_t;

// Initialize NVS and load config (call once at startup)
void config_init(void);

// Get pointer to current config (read-only in most contexts)
const growhub_config_t *config_get(void);

// Save individual config sections back to NVS
void config_save_wifi(const char *ssid, const char *password);
void config_save_mqtt(const char *host, uint16_t port);
// temp_unit: 0=C, 1=F, -1=no change; time_src: 0=SNTP, 1=Manual, -1=no change
void config_save_device(const char *name, const char *timezone, int temp_unit, int time_src);
void config_save_sntp_servers(const char *primary, const char *secondary);
void config_save_pins(const growhub_config_t *cfg);
void config_save_schedule(const char *json);
bool config_load_schedule(char **json_out, size_t *len_out);
void config_clear_schedule(void);
void config_save_schedule_disabled_mask(uint8_t mask);
void config_save_calibration(float temp_off, float rh_off);
void config_save_mqtt_disabled(bool disabled);
void config_save_relay_mode(uint8_t mode);  // 0=AUTO, 1=MANUAL

// Factory reset — erase all NVS data and reboot
void config_factory_reset(void);

// Get MAC address string (no colons, uppercase, e.g. "FCE8C0XXXXXX")
const char *config_get_mac_str(void);
