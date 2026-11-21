#include "settings_snapshot.h"
#include "settings_contract.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static void number(cJSON *fields, const char *key, double value) {
    char text[32]; snprintf(text, sizeof(text), "%.1f", value);
    cJSON_AddStringToObject(fields, key, text);
}
static void integer(cJSON *fields, const char *key, unsigned value) {
    char text[24]; snprintf(text, sizeof(text), "%u", value);
    cJSON_AddStringToObject(fields, key, text);
}
static const sched_condition_t *condition(const outlet_sched_t *outlet, sched_condition_type_t type) {
    if (outlet) for (int i = 0; i < outlet->condition_count; i++)
        if (outlet->conditions[i].type == type) return &outlet->conditions[i];
    return NULL;
}
static void choice(cJSON *options, const char *value, const char *label) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return;
    cJSON_AddStringToObject(item, "value", value);
    cJSON_AddStringToObject(item, "label", label);
    cJSON_AddItemToArray(options, item);
}
char *settings_snapshot_json(const growhub_config_t *cfg,
    const outlet_sched_t *outlets, int count, const char *mac, const char *version) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "api", 1);
    cJSON_AddStringToObject(root, "mac", mac);
    cJSON_AddStringToObject(root, "fw", version);
    cJSON *fields = cJSON_AddObjectToObject(root, "fields");
    cJSON *choices = cJSON_AddObjectToObject(root, "choices");
    cJSON *capabilities = cJSON_AddObjectToObject(root, "capabilities");
    if (!fields || !choices || !capabilities) { cJSON_Delete(root); return NULL; }
    cJSON_AddBoolToObject(capabilities, "outlet_modes", true);
    cJSON_AddBoolToObject(capabilities, "temperature", true);
    cJSON_AddBoolToObject(capabilities, "humidity", true);
    cJSON_AddBoolToObject(capabilities, "light", true);
    cJSON_AddBoolToObject(capabilities, "co2", false);
    cJSON_AddBoolToObject(capabilities, "schedule_v3", true);
    cJSON_AddBoolToObject(capabilities, "file_ota", true);
    cJSON_AddBoolToObject(capabilities, "release_updates", true);
    cJSON_AddStringToObject(fields, "dev_name", cfg->device_name);
    cJSON_AddStringToObject(fields, "timezone", cfg->timezone);
    cJSON_AddStringToObject(fields, "ssid", cfg->sta_ssid);
    cJSON_AddStringToObject(fields, "mqtt_host", cfg->mqtt_host);
    integer(fields, "mqtt_port", cfg->mqtt_port);
    integer(fields, "temp_unit", cfg->temp_unit);
    integer(fields, "time_src", cfg->time_src);
    integer(fields, "keep_ap", cfg->keep_ap_active != 0);
    integer(fields, "operation_led", cfg->operation_led_enabled != 0);
    integer(fields, "mqtt_disabled", cfg->mqtt_disabled != 0);
    integer(fields, "operation_led_pin", cfg->pin_led);
    integer(fields, "error_led_pin", cfg->pin_error_led);
    cJSON_AddStringToObject(fields, "sntp_primary", cfg->sntp_primary);
    cJSON_AddStringToObject(fields, "sntp_secondary", cfg->sntp_secondary);
    cJSON_AddStringToObject(fields, "sched_v3", "1");
    cJSON *zones = cJSON_AddArrayToObject(choices, "timezone");
    for (size_t i = 0; i < TZ_COUNT; i++) choice(zones, TZ_TABLE[i].posix, TZ_TABLE[i].label);
    cJSON *types = cJSON_AddArrayToObject(choices, "outlet_type");
    for (size_t i = 0; i < DEVICE_TYPE_COUNT; i++) choice(types, i ? DEVICE_TYPES[i] : "", DEVICE_TYPES[i]);
    static const int slots[] = {3, 0, 1, 2};
    for (int id = 1; id <= 4; id++) {
        char key[32];
        const char *type = cfg->relay_names[slots[id - 1]];
        snprintf(key, sizeof(key), "outlet_%d", id);
        cJSON_AddStringToObject(fields, key, type);
        snprintf(key, sizeof(key), "label_%d", id);
        cJSON_AddStringToObject(fields, key, cfg->outlet_labels[slots[id - 1]]);
        const outlet_sched_t *outlet = NULL;
        for (int i = 0; i < count; i++) if (outlets[i].id == id) { outlet = &outlets[i]; break; }
        const sched_condition_t *always = condition(outlet, SCHED_COND_ALWAYS_ON);
        const sched_condition_t *time = condition(outlet, SCHED_COND_TIME_WINDOW);
        const sched_condition_t *rh = condition(outlet, !strcmp(type, "Humidifier") ? SCHED_COND_RH_LOW_BAND : SCHED_COND_RH_HIGH_BAND);
        const sched_condition_t *temp = condition(outlet, !strcmp(type, "Heater") ? SCHED_COND_TEMP_LOW_BAND_C : SCHED_COND_TEMP_HIGH_BAND_C);
        const sched_condition_t *interval = condition(outlet, SCHED_COND_INTERVAL);
        #define FIELD(suffix, value) do { snprintf(key, sizeof(key), "s%d_" suffix, id); cJSON_AddStringToObject(fields, key, (value)); } while (0)
        #define NUM(suffix, value) do { snprintf(key, sizeof(key), "s%d_" suffix, id); number(fields, key, (value)); } while (0)
        FIELD("disabled", (cfg->schedule_disabled_mask & (1 << (id - 1))) ? "1" : "0");
        FIELD("always", always ? "1" : "");
        FIELD("time_en", time ? "1" : "");
        FIELD("start", time ? time->start : "06:00");
        FIELD("end", time ? time->end : "22:00");
        FIELD("rh_en", rh ? "1" : "");
        NUM("rh_low", rh ? rh->low : !strcmp(type, "Humidifier") ? 50 : 55);
        NUM("rh_high", rh ? rh->high : !strcmp(type, "Humidifier") ? 60 : 65);
        FIELD("temp_en", temp ? "1" : "");
        float low = temp ? temp->low : !strcmp(type, "Heater") ? 18 : 24;
        float high = temp ? temp->high : !strcmp(type, "Heater") ? 21 : 27;
        NUM("temp_low", cfg->temp_unit ? low * 9 / 5 + 32 : low);
        NUM("temp_high", cfg->temp_unit ? high * 9 / 5 + 32 : high);
        FIELD("interval_en", interval ? "1" : "");
        NUM("run", interval ? interval->run_mins : 15);
        NUM("every", interval ? interval->every_hrs : 4);
        FIELD("win_en", interval && interval->has_window ? "1" : "");
        FIELD("wstart", interval && interval->has_window ? interval->window_start : "06:00");
        FIELD("wend", interval && interval->has_window ? interval->window_end : "22:00");
        #undef FIELD
        #undef NUM
    }
    char *result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}
