#include "relays.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "relays";
static uint8_t s_bitmask = 0;
static relay_mode_t s_mode = RELAY_MODE_AUTO;

// Pin mapping (loaded from config at init) — indexed by bit position
static uint8_t s_pins[4];

void relays_init(void)
{
    const growhub_config_t *cfg = config_get();
    s_pins[RELAY_BIT_OUTLET2] = cfg->pin_relay_outlet2;
    s_pins[RELAY_BIT_OUTLET3] = cfg->pin_relay_outlet3;
    s_pins[RELAY_BIT_OUTLET4] = cfg->pin_relay_outlet4;
    s_pins[RELAY_BIT_OUTLET1] = cfg->pin_relay_outlet1;

    for (int i = 0; i < 4; i++) {
        if (s_pins[i] == 0) continue; // skip unconfigured pins
        gpio_reset_pin(s_pins[i]);
        gpio_set_direction(s_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(s_pins[i], 0);
        ESP_LOGI(TAG, "Relay bit%d on GPIO %d", i, s_pins[i]);
    }

    s_bitmask = 0;
    s_mode = (cfg->relay_mode == 1) ? RELAY_MODE_MANUAL : RELAY_MODE_AUTO;
    ESP_LOGI(TAG, "Relay mode restored: %s", s_mode == RELAY_MODE_MANUAL ? "MANUAL" : "AUTO");
}

void relays_set_mode(relay_mode_t mode)
{
    s_mode = mode;
    config_save_relay_mode((uint8_t)mode);
    ESP_LOGI(TAG, "Mode: %s", mode == RELAY_MODE_MANUAL ? "MANUAL" : "AUTO");
}

relay_mode_t relays_get_mode(void)
{
    return s_mode;
}

static void apply_bitmask(void)
{
    for (int i = 0; i < 4; i++) {
        if (s_pins[i] == 0) continue;
        bool on = (s_bitmask >> i) & 1;
        gpio_set_level(s_pins[i], on ? 1 : 0);
    }
    ESP_LOGI(TAG, "Relays: o1=%d o2=%d o3=%d o4=%d (mask=0x%02X)",
             (s_bitmask >> 3) & 1, (s_bitmask >> 0) & 1,
             (s_bitmask >> 1) & 1, (s_bitmask >> 2) & 1, s_bitmask);
}

void relays_set_bitmask(uint8_t mask)
{
    s_bitmask = mask & 0x0F;
    apply_bitmask();
}

uint8_t relays_get_bitmask(void)
{
    return s_bitmask;
}

void relays_get_actuator_str(char *buf, size_t len)
{
    if (len < 9) return;
    snprintf(buf, len, "%d%d%d%d0000",
             (s_bitmask >> 0) & 1,
             (s_bitmask >> 1) & 1,
             (s_bitmask >> 2) & 1,
             (s_bitmask >> 3) & 1);
}

void relays_all_off(void)
{
    s_bitmask = 0;
    apply_bitmask();
}
