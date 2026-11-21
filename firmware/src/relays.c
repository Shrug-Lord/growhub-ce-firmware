#include "relays.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "relays";
static uint8_t s_bitmask = 0;
static uint8_t s_manual_mask = 0;
static SemaphoreHandle_t s_lock;

// Pin mapping (loaded from config at init) — indexed by bit position
static uint8_t s_pins[4];

void relays_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
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
    s_manual_mask = cfg->relay_manual_mask & 0x0F;
}

static int outlet_bit(int id)
{
    return id >= 1 && id <= 4 ? (id == 1 ? 3 : id - 2) : -1;
}

void relays_set_mode(relay_mode_t mode)
{
    if (mode != RELAY_MODE_AUTO && mode != RELAY_MODE_MANUAL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_manual_mask = mode == RELAY_MODE_MANUAL ? 0x0F : 0;
    config_save_relay_manual_mask(s_manual_mask);
    xSemaphoreGive(s_lock);
}

uint8_t relays_get_manual_mask(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t mask = s_manual_mask;
    xSemaphoreGive(s_lock);
    return mask;
}

relay_mode_t relays_get_mode(void)
{
    return relays_get_manual_mask() == 0x0F ? RELAY_MODE_MANUAL : RELAY_MODE_AUTO;
}

relay_mode_t relays_get_outlet_mode(int id)
{
    int bit = outlet_bit(id);
    return bit >= 0 && (relays_get_manual_mask() & (1 << bit))
        ? RELAY_MODE_MANUAL : RELAY_MODE_AUTO;
}

bool relays_set_outlet_mode(int id, relay_mode_t mode)
{
    int bit = outlet_bit(id);
    if (bit < 0 || (mode != RELAY_MODE_AUTO && mode != RELAY_MODE_MANUAL)) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (mode == RELAY_MODE_MANUAL) s_manual_mask |= 1 << bit;
    else s_manual_mask &= ~(1 << bit);
    config_save_relay_manual_mask(s_manual_mask);
    xSemaphoreGive(s_lock);
    return true;
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

bool relays_set_bitmask(uint8_t mask)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool allowed = s_manual_mask == 0x0F;
    if (allowed) {
        s_bitmask = mask & 0x0F;
        apply_bitmask();
    }
    xSemaphoreGive(s_lock);
    return allowed;
}

bool relays_set_outlet(int id, bool on)
{
    int bit = outlet_bit(id);
    if (bit < 0) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool allowed = (s_manual_mask & (1 << bit)) != 0;
    if (allowed) {
        if (on) s_bitmask |= 1 << bit;
        else s_bitmask &= ~(1 << bit);
        apply_bitmask();
    }
    xSemaphoreGive(s_lock);
    return allowed;
}

bool relays_set_auto_bitmask(uint8_t mask)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t merged = (s_bitmask & s_manual_mask) | (mask & ~s_manual_mask & 0x0F);
    bool changed = s_bitmask != merged;
    if (changed) {
        s_bitmask = merged;
        apply_bitmask();
    }
    xSemaphoreGive(s_lock);
    return changed;
}

uint8_t relays_get_bitmask(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t mask = s_bitmask;
    xSemaphoreGive(s_lock);
    return mask;
}

void relays_get_actuator_str(char *buf, size_t len)
{
    if (len < 9) return;
    uint8_t mask = relays_get_bitmask();
    snprintf(buf, len, "%d%d%d%d0000",
             (mask >> 0) & 1, (mask >> 1) & 1,
             (mask >> 2) & 1, (mask >> 3) & 1);
}

void relays_all_off(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_manual_mask = 0x0F;
    s_bitmask = 0;
    apply_bitmask();
    config_save_relay_manual_mask(s_manual_mask);
    xSemaphoreGive(s_lock);
}
