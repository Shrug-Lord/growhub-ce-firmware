#include "schedule.h"
#include "relays.h"
#include "config.h"
#include "sensors.h"
#include "time_sync.h"
#include "mqtt.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "schedule";

// Display outlet N maps to relay_names slot: O1->slot3, O2->slot0,
// O3->slot1, O4->slot2.
static const int OUTLET_SLOT[4] = {3, 0, 1, 2};

static outlet_sched_t s_outlets[MAX_OUTLET_SCHEDS];
static int            s_outlet_count = 0;

typedef struct {
    int64_t last_run_us;
    bool    run_active;
} interval_state_t;

static interval_state_t s_interval[MAX_OUTLET_SCHEDS] = {0};
static bool s_condition_active[MAX_OUTLET_SCHEDS][MAX_CONDITIONS_PER_OUTLET] = {0};
static char s_summaries[4][SCHEDULE_SUMMARY_LEN] = {{0}};
static bool s_time_sync_required = false;
static bool s_sensor_data_unavailable = false;
static bool s_prev_sensor_available = false;
static bool s_sntp_unhealthy = false;

static char s_last_error_reason[32] = "";
static int  s_last_error_outlet = 0;

static void set_error(const char *reason, int outlet_id)
{
    if (!reason) reason = "invalid_payload";
    strncpy(s_last_error_reason, reason, sizeof(s_last_error_reason) - 1);
    s_last_error_reason[sizeof(s_last_error_reason) - 1] = '\0';
    s_last_error_outlet = outlet_id;
}

static void clear_error(void)
{
    s_last_error_reason[0] = '\0';
    s_last_error_outlet = 0;
}

static void reset_active_schedule(void)
{
    memset(s_outlets, 0, sizeof(s_outlets));
    memset(s_interval, 0, sizeof(s_interval));
    memset(s_condition_active, 0, sizeof(s_condition_active));
    memset(s_summaries, 0, sizeof(s_summaries));
    s_time_sync_required = false;
    s_sensor_data_unavailable = false;
    s_prev_sensor_available = false;
    s_sntp_unhealthy = false;
    s_outlet_count = 0;
}

static int relay_bit_for_outlet(int outlet_id)
{
    switch (outlet_id) {
    case 1: return RELAY_BIT_OUTLET1;
    case 2: return RELAY_BIT_OUTLET2;
    case 3: return RELAY_BIT_OUTLET3;
    case 4: return RELAY_BIT_OUTLET4;
    default: return -1;
    }
}

static bool condition_uses_wall_clock(const sched_condition_t *cond)
{
    return cond && (cond->type == SCHED_COND_TIME_WINDOW ||
                    (cond->type == SCHED_COND_INTERVAL && cond->has_window));
}

static bool condition_uses_sensor(const sched_condition_t *cond)
{
    return cond && (cond->type == SCHED_COND_RH_LOW_BAND ||
                    cond->type == SCHED_COND_RH_HIGH_BAND ||
                    cond->type == SCHED_COND_TEMP_LOW_BAND_C ||
                    cond->type == SCHED_COND_TEMP_HIGH_BAND_C);
}

static bool outlet_schedule_disabled(int outlet_id)
{
    if (outlet_id < 1 || outlet_id > 4) return false;
    return (config_get()->schedule_disabled_mask & (1 << (outlet_id - 1))) != 0;
}

static const sched_condition_t *find_interval_condition(const outlet_sched_t *outlet)
{
    if (!outlet) return NULL;
    for (int i = 0; i < outlet->condition_count; i++) {
        if (outlet->conditions[i].type == SCHED_COND_INTERVAL) {
            return &outlet->conditions[i];
        }
    }
    return NULL;
}

static bool minute_in_window(int minute, int start, int end)
{
    if (start < end) return minute >= start && minute < end;
    return minute >= start || minute < end;
}

static bool current_local_minute(int *minute_out)
{
    if (!time_sync_wall_time_valid()) return false;
    time_t now_ts;
    time(&now_ts);
    struct tm now_local;
    localtime_r(&now_ts, &now_local);
    *minute_out = now_local.tm_hour * 60 + now_local.tm_min;
    return true;
}

static void format_hhmm_12(const char *hhmm, char *out, size_t len)
{
    int h = 0;
    int m = 0;
    if (!hhmm || sscanf(hhmm, "%d:%d", &h, &m) != 2) {
        snprintf(out, len, "--:--");
        return;
    }
    const char *ampm = h < 12 ? "AM" : "PM";
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(out, len, "%d:%02d %s", h12, m, ampm);
}

static void format_time_12(time_t ts, char *out, size_t len)
{
    struct tm local;
    localtime_r(&ts, &local);
    int h12 = local.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    const char *ampm = local.tm_hour < 12 ? "AM" : "PM";
    snprintf(out, len, "%d:%02d %s", h12, local.tm_min, ampm);
}

static void append_phrase(char *buf, size_t len, const char *phrase, const char *separator)
{
    if (!phrase || !phrase[0]) return;
    if (!separator) separator = " + ";
    size_t used = strlen(buf);
    if (used >= len - 1) return;
    snprintf(buf + used, len - used, "%s%s", used ? separator : "", phrase);
}

static float display_temp(float temp_c)
{
    if (config_get()->temp_unit == 1) {
        return temp_c * 9.0f / 5.0f + 32.0f;
    }
    return temp_c;
}

static const char *display_temp_unit(void)
{
    return config_get()->temp_unit == 1 ? "F" : "C";
}

static bool parse_hhmm(const char *s, int *mins_out)
{
    if (!s || strlen(s) != 5 || s[2] != ':') return false;
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') return false;
    if (s[3] < '0' || s[3] > '9' || s[4] < '0' || s[4] > '9') return false;

    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;

    *mins_out = h * 60 + m;
    return true;
}

static int window_duration_mins(int start_mins, int end_mins)
{
    if (end_mins > start_mins) return end_mins - start_mins;
    return (24 * 60 - start_mins) + end_mins;
}

static const char *outlet_assignment(int outlet_id)
{
    if (outlet_id < 1 || outlet_id > 4) return "";
    const growhub_config_t *cfg = config_get();
    return cfg->relay_names[OUTLET_SLOT[outlet_id - 1]];
}

static bool streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static bool condition_type_from_string(const char *type, sched_condition_type_t *out)
{
    if (streq(type, "always_on")) {
        *out = SCHED_COND_ALWAYS_ON;
    } else if (streq(type, "time_window")) {
        *out = SCHED_COND_TIME_WINDOW;
    } else if (streq(type, "rh_low_band")) {
        *out = SCHED_COND_RH_LOW_BAND;
    } else if (streq(type, "rh_high_band")) {
        *out = SCHED_COND_RH_HIGH_BAND;
    } else if (streq(type, "temp_low_band_c")) {
        *out = SCHED_COND_TEMP_LOW_BAND_C;
    } else if (streq(type, "temp_high_band_c")) {
        *out = SCHED_COND_TEMP_HIGH_BAND_C;
    } else if (streq(type, "interval")) {
        *out = SCHED_COND_INTERVAL;
    } else {
        return false;
    }
    return true;
}

static bool condition_allowed_for_assignment(sched_condition_type_t type, const char *assignment)
{
    if (!assignment || !assignment[0]) return false;

    switch (type) {
    case SCHED_COND_ALWAYS_ON:
        return streq(assignment, "Light") || streq(assignment, "Fan");
    case SCHED_COND_TIME_WINDOW:
        return streq(assignment, "Light") || streq(assignment, "Fan");
    case SCHED_COND_RH_LOW_BAND:
        return streq(assignment, "Humidifier");
    case SCHED_COND_RH_HIGH_BAND:
        return streq(assignment, "Fan") || streq(assignment, "Dehumidifier");
    case SCHED_COND_TEMP_LOW_BAND_C:
        return streq(assignment, "Heater");
    case SCHED_COND_TEMP_HIGH_BAND_C:
        return streq(assignment, "Fan") || streq(assignment, "AC Controller");
    case SCHED_COND_INTERVAL:
        return streq(assignment, "Water Pump");
    default:
        return false;
    }
}

static bool get_number(cJSON *obj, const char *name, double *out)
{
    cJSON *item = cJSON_GetObjectItem(obj, name);
    if (!item || !cJSON_IsNumber(item)) return false;
    *out = item->valuedouble;
    return true;
}

static bool json_number_is_int(cJSON *item)
{
    return item && cJSON_IsNumber(item) && item->valuedouble == (double)item->valueint;
}

static bool get_int(cJSON *obj, const char *name, int *out)
{
    cJSON *item = cJSON_GetObjectItem(obj, name);
    if (!json_number_is_int(item)) return false;
    *out = item->valueint;
    return true;
}

static bool get_hhmm_field(cJSON *obj, const char *name, char dest[6], int *mins_out)
{
    cJSON *item = cJSON_GetObjectItem(obj, name);
    if (!item || !cJSON_IsString(item) || !parse_hhmm(item->valuestring, mins_out)) {
        return false;
    }
    strncpy(dest, item->valuestring, 5);
    dest[5] = '\0';
    return true;
}

static bool parse_time_window(cJSON *cond, sched_condition_t *out, int outlet_id)
{
    if (!get_hhmm_field(cond, "start", out->start, &out->start_mins) ||
        !get_hhmm_field(cond, "end", out->end, &out->end_mins) ||
        out->start_mins == out->end_mins) {
        set_error("invalid_time_window", outlet_id);
        return false;
    }
    return true;
}

static bool parse_band(cJSON *cond, sched_condition_t *out, bool temp, int outlet_id)
{
    double low = 0.0;
    double high = 0.0;
    const char *low_key = temp ? "low_c" : "low";
    const char *high_key = temp ? "high_c" : "high";

    if (!get_number(cond, low_key, &low) || !get_number(cond, high_key, &high)) {
        set_error("invalid_band", outlet_id);
        return false;
    }

    float min_gap = 2.0f;
    if (temp) {
        min_gap = config_get()->temp_unit == 1 ? (5.0f / 9.0f) : 1.0f;
        if (low < 0.0 || high > 50.0) {
            set_error("invalid_band", outlet_id);
            return false;
        }
    } else if (low < 10.0 || high > 95.0) {
        set_error("invalid_band", outlet_id);
        return false;
    }

    if (high <= low || (float)(high - low) < min_gap) {
        set_error("invalid_band", outlet_id);
        return false;
    }

    out->low = (float)low;
    out->high = (float)high;
    return true;
}

static bool parse_interval(cJSON *cond, sched_condition_t *out, int outlet_id)
{
    if (!get_int(cond, "run_mins", &out->run_mins) ||
        !get_int(cond, "every_hrs", &out->every_hrs) ||
        out->run_mins < 1 || out->run_mins > 240 ||
        out->every_hrs < 1 || out->every_hrs > 168) {
        set_error("invalid_interval", outlet_id);
        return false;
    }

    cJSON *window = cJSON_GetObjectItem(cond, "window");
    if (!window) {
        out->has_window = false;
        return true;
    }

    if (!cJSON_IsObject(window)) {
        set_error("invalid_interval", outlet_id);
        return false;
    }

    out->has_window = true;
    if (!get_hhmm_field(window, "start", out->window_start, &out->window_start_mins) ||
        !get_hhmm_field(window, "end", out->window_end, &out->window_end_mins) ||
        out->window_start_mins == out->window_end_mins) {
        set_error("invalid_interval", outlet_id);
        return false;
    }

    if (window_duration_mins(out->window_start_mins, out->window_end_mins) < out->run_mins) {
        set_error("invalid_interval", outlet_id);
        return false;
    }

    return true;
}

static bool parse_condition(cJSON *cond, const char *assignment, int outlet_id, sched_condition_t *out)
{
    if (!cJSON_IsObject(cond)) {
        set_error("invalid_condition", outlet_id);
        return false;
    }

    cJSON *jtype = cJSON_GetObjectItem(cond, "type");
    if (!jtype || !cJSON_IsString(jtype) ||
        !condition_type_from_string(jtype->valuestring, &out->type)) {
        set_error("invalid_condition", outlet_id);
        return false;
    }

    if (!condition_allowed_for_assignment(out->type, assignment)) {
        set_error("condition_not_allowed", outlet_id);
        return false;
    }

    switch (out->type) {
    case SCHED_COND_ALWAYS_ON:
        return true;
    case SCHED_COND_TIME_WINDOW:
        return parse_time_window(cond, out, outlet_id);
    case SCHED_COND_RH_LOW_BAND:
    case SCHED_COND_RH_HIGH_BAND:
        return parse_band(cond, out, false, outlet_id);
    case SCHED_COND_TEMP_LOW_BAND_C:
    case SCHED_COND_TEMP_HIGH_BAND_C:
        return parse_band(cond, out, true, outlet_id);
    case SCHED_COND_INTERVAL:
        return parse_interval(cond, out, outlet_id);
    default:
        set_error("invalid_condition", outlet_id);
        return false;
    }
}

static bool parse_v3_schedule(cJSON *root, outlet_sched_t parsed[MAX_OUTLET_SCHEDS], int *parsed_count)
{
    cJSON *vfield = cJSON_GetObjectItem(root, "v");
    if (!json_number_is_int(vfield)) {
        set_error("invalid_payload", 0);
        return false;
    }
    if (vfield->valueint != 3) {
        set_error("unsupported_schedule_version", 0);
        return false;
    }

    cJSON *outlets = cJSON_GetObjectItem(root, "outlets");
    if (!outlets || !cJSON_IsArray(outlets)) {
        set_error("invalid_payload", 0);
        return false;
    }

    int outlet_count = cJSON_GetArraySize(outlets);
    if (outlet_count <= 0) {
        set_error("empty_schedule", 0);
        return false;
    }
    if (outlet_count > MAX_OUTLET_SCHEDS) {
        set_error("invalid_outlet", 0);
        return false;
    }

    bool seen_outlet[4] = {0};
    int count = 0;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, outlets) {
        if (!cJSON_IsObject(item)) {
            set_error("invalid_payload", 0);
            return false;
        }

        cJSON *jid = cJSON_GetObjectItem(item, "id");
        if (!json_number_is_int(jid) || jid->valueint < 1 || jid->valueint > 4) {
            set_error("invalid_outlet", 0);
            return false;
        }

        int outlet_id = jid->valueint;
        if (seen_outlet[outlet_id - 1]) {
            set_error("invalid_outlet", outlet_id);
            return false;
        }
        seen_outlet[outlet_id - 1] = true;

        const char *assignment = outlet_assignment(outlet_id);
        cJSON *conditions = cJSON_GetObjectItem(item, "conditions");
        if (!conditions || !cJSON_IsArray(conditions) || cJSON_GetArraySize(conditions) <= 0) {
            set_error("missing_conditions", outlet_id);
            return false;
        }
        if (cJSON_GetArraySize(conditions) > MAX_CONDITIONS_PER_OUTLET) {
            set_error("invalid_condition", outlet_id);
            return false;
        }

        outlet_sched_t *out = &parsed[count];
        memset(out, 0, sizeof(*out));
        out->id = outlet_id;

        bool seen_condition[SCHED_COND_COUNT] = {0};
        bool has_always_on = false;

        cJSON *cond = NULL;
        cJSON_ArrayForEach(cond, conditions) {
            sched_condition_t *parsed_cond = &out->conditions[out->condition_count];
            memset(parsed_cond, 0, sizeof(*parsed_cond));

            if (!parse_condition(cond, assignment, outlet_id, parsed_cond)) {
                return false;
            }

            if (seen_condition[parsed_cond->type]) {
                set_error("duplicate_condition", outlet_id);
                return false;
            }
            seen_condition[parsed_cond->type] = true;
            if (parsed_cond->type == SCHED_COND_ALWAYS_ON) has_always_on = true;

            out->condition_count++;
        }

        if (has_always_on && out->condition_count > 1) {
            set_error("always_on_exclusive", outlet_id);
            return false;
        }

        count++;
    }

    *parsed_count = count;
    return true;
}

bool schedule_load(const char *json, int len)
{
    clear_error();

    if (!json || len <= 0) {
        set_error("invalid_payload", 0);
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        set_error("invalid_payload", 0);
        ESP_LOGW(TAG, "Invalid schedule JSON");
        return false;
    }

    outlet_sched_t parsed[MAX_OUTLET_SCHEDS] = {0};
    int parsed_count = 0;
    bool ok = parse_v3_schedule(root, parsed, &parsed_count);
    cJSON_Delete(root);

    if (!ok) {
        ESP_LOGW(TAG, "Schedule rejected: %s (outlet %d)",
                 schedule_last_error_reason(), schedule_last_error_outlet());
        return false;
    }

    reset_active_schedule();
    memcpy(s_outlets, parsed, sizeof(outlet_sched_t) * parsed_count);
    s_outlet_count = parsed_count;

    int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < s_outlet_count; i++) {
        s_interval[i].last_run_us = now_us;
        s_interval[i].run_active = false;
    }

    clear_error();
    ESP_LOGI(TAG, "V3 schedule loaded: %d outlet(s)", s_outlet_count);
    return true;
}

void schedule_clear(void)
{
    reset_active_schedule();
    clear_error();
    ESP_LOGI(TAG, "Schedule cleared");
}

bool schedule_is_active(void)
{
    return s_outlet_count > 0;
}

int schedule_get_outlets(outlet_sched_t *out, int max)
{
    if (!out || max <= 0) return 0;
    int n = s_outlet_count < max ? s_outlet_count : max;
    memcpy(out, s_outlets, n * sizeof(outlet_sched_t));
    return n;
}

const char *schedule_get_outlet_summary(int outlet_id)
{
    static const char *empty = "";
    if (relays_get_mode() != RELAY_MODE_AUTO) return empty;
    if (outlet_id < 1 || outlet_id > 4) return empty;
    if (outlet_schedule_disabled(outlet_id)) return empty;
    return s_summaries[outlet_id - 1];
}

bool schedule_time_sync_required(void)
{
    return relays_get_mode() == RELAY_MODE_AUTO && s_time_sync_required;
}

bool schedule_sensor_data_unavailable(void)
{
    return relays_get_mode() == RELAY_MODE_AUTO && s_sensor_data_unavailable;
}

const char *schedule_last_error_reason(void)
{
    return s_last_error_reason;
}

int schedule_last_error_outlet(void)
{
    return s_last_error_outlet;
}

static bool interval_window_allows_start(const sched_condition_t *cond, int now_mins)
{
    if (!cond->has_window) return true;
    if (!minute_in_window(now_mins, cond->window_start_mins, cond->window_end_mins)) {
        return false;
    }

    int mins_until_end = cond->window_end_mins > now_mins
        ? cond->window_end_mins - now_mins
        : (1440 - now_mins) + cond->window_end_mins;
    return mins_until_end >= cond->run_mins;
}

static bool evaluate_band_condition(int outlet_index,
                                    int cond_index,
                                    const sched_condition_t *cond,
                                    const sensor_reading_t *sensor,
                                    bool sensor_available)
{
    if (!sensor_available) {
        s_condition_active[outlet_index][cond_index] = false;
        return false;
    }

    bool high_band = cond->type == SCHED_COND_RH_HIGH_BAND ||
                     cond->type == SCHED_COND_TEMP_HIGH_BAND_C;
    float value = (cond->type == SCHED_COND_RH_LOW_BAND ||
                   cond->type == SCHED_COND_RH_HIGH_BAND)
        ? sensor->humidity
        : sensor->temperature;

    bool active = s_condition_active[outlet_index][cond_index];
    if (high_band) {
        if (value > cond->high) active = true;
        else if (value <= cond->low) active = false;
    } else {
        if (value < cond->low) active = true;
        else if (value >= cond->high) active = false;
    }

    s_condition_active[outlet_index][cond_index] = active;
    return active;
}

static void start_interval_run(int outlet_index, int64_t now_us)
{
    s_interval[outlet_index].last_run_us = now_us;
    s_interval[outlet_index].run_active = true;
}

static bool evaluate_interval_condition(int outlet_index,
                                        const sched_condition_t *cond,
                                        bool time_valid,
                                        int now_mins,
                                        int64_t now_us)
{
    int64_t run_us = (int64_t)cond->run_mins * 60LL * 1000000LL;
    int64_t every_us = (int64_t)cond->every_hrs * 3600LL * 1000000LL;

    if (s_interval[outlet_index].run_active) {
        int64_t elapsed_run_us = now_us - s_interval[outlet_index].last_run_us;
        if (elapsed_run_us < run_us) return true;
        s_interval[outlet_index].run_active = false;
    }

    int64_t elapsed_us = now_us - s_interval[outlet_index].last_run_us;
    if (elapsed_us < every_us) return false;

    if (cond->has_window) {
        if (!time_valid || !interval_window_allows_start(cond, now_mins)) {
            return false;
        }
    }

    start_interval_run(outlet_index, now_us);
    return true;
}

static bool evaluate_condition(int outlet_index,
                               int cond_index,
                               const sched_condition_t *cond,
                               const sensor_reading_t *sensor,
                               bool sensor_available,
                               bool time_valid,
                               int now_mins,
                               int64_t now_us)
{
    switch (cond->type) {
    case SCHED_COND_ALWAYS_ON:
        s_condition_active[outlet_index][cond_index] = true;
        return true;
    case SCHED_COND_TIME_WINDOW:
        s_condition_active[outlet_index][cond_index] =
            time_valid && minute_in_window(now_mins, cond->start_mins, cond->end_mins);
        return s_condition_active[outlet_index][cond_index];
    case SCHED_COND_RH_LOW_BAND:
    case SCHED_COND_RH_HIGH_BAND:
    case SCHED_COND_TEMP_LOW_BAND_C:
    case SCHED_COND_TEMP_HIGH_BAND_C:
        return evaluate_band_condition(outlet_index, cond_index, cond, sensor, sensor_available);
    case SCHED_COND_INTERVAL:
        s_condition_active[outlet_index][cond_index] =
            evaluate_interval_condition(outlet_index, cond, time_valid, now_mins, now_us);
        return s_condition_active[outlet_index][cond_index];
    default:
        s_condition_active[outlet_index][cond_index] = false;
        return false;
    }
}

static void condition_waiting_text(const sched_condition_t *cond, char *out, size_t len)
{
    char tbuf[24];
    switch (cond->type) {
    case SCHED_COND_TIME_WINDOW:
        format_hhmm_12(cond->start, tbuf, sizeof(tbuf));
        snprintf(out, len, "%s", tbuf);
        break;
    case SCHED_COND_RH_LOW_BAND:
        snprintf(out, len, "rH < %.0f%%", cond->low);
        break;
    case SCHED_COND_RH_HIGH_BAND:
        snprintf(out, len, "rH > %.0f%%", cond->high);
        break;
    case SCHED_COND_TEMP_LOW_BAND_C:
        snprintf(out, len, "temp < %.1f%s", display_temp(cond->low), display_temp_unit());
        break;
    case SCHED_COND_TEMP_HIGH_BAND_C:
        snprintf(out, len, "temp > %.1f%s", display_temp(cond->high), display_temp_unit());
        break;
    case SCHED_COND_INTERVAL:
        snprintf(out, len, "next pump interval");
        break;
    default:
        out[0] = '\0';
        break;
    }
}

static void condition_active_text(const sched_condition_t *cond, char *out, size_t len)
{
    switch (cond->type) {
    case SCHED_COND_ALWAYS_ON:
        snprintf(out, len, "always on");
        break;
    case SCHED_COND_TIME_WINDOW:
        snprintf(out, len, "time");
        break;
    case SCHED_COND_RH_LOW_BAND:
    case SCHED_COND_RH_HIGH_BAND:
        snprintf(out, len, "rH");
        break;
    case SCHED_COND_TEMP_LOW_BAND_C:
    case SCHED_COND_TEMP_HIGH_BAND_C:
        snprintf(out, len, "temp");
        break;
    case SCHED_COND_INTERVAL:
        snprintf(out, len, "pump");
        break;
    default:
        out[0] = '\0';
        break;
    }
}

static void condition_clear_text(int outlet_index,
                                 const sched_condition_t *cond,
                                 bool time_valid,
                                 int64_t now_us,
                                 char *out,
                                 size_t len)
{
    char tbuf[24];
    switch (cond->type) {
    case SCHED_COND_TIME_WINDOW:
        format_hhmm_12(cond->end, tbuf, sizeof(tbuf));
        snprintf(out, len, "off at %s", tbuf);
        break;
    case SCHED_COND_RH_LOW_BAND:
        snprintf(out, len, "off at rH %.0f%%", cond->high);
        break;
    case SCHED_COND_RH_HIGH_BAND:
        snprintf(out, len, "off at rH %.0f%%", cond->low);
        break;
    case SCHED_COND_TEMP_LOW_BAND_C:
        snprintf(out, len, "off at %.1f%s", display_temp(cond->high), display_temp_unit());
        break;
    case SCHED_COND_TEMP_HIGH_BAND_C:
        snprintf(out, len, "off at %.1f%s", display_temp(cond->low), display_temp_unit());
        break;
    case SCHED_COND_INTERVAL:
        if (time_valid && outlet_index >= 0 && outlet_index < MAX_OUTLET_SCHEDS &&
            s_interval[outlet_index].run_active) {
            int64_t run_us = (int64_t)cond->run_mins * 60LL * 1000000LL;
            int64_t end_us = s_interval[outlet_index].last_run_us + run_us;
            int64_t remaining_us = end_us > now_us ? end_us - now_us : 0;
            time_t now_ts;
            time(&now_ts);
            time_t end_ts = now_ts + (time_t)((remaining_us + 999999LL) / 1000000LL);
            format_time_12(end_ts, tbuf, sizeof(tbuf));
            snprintf(out, len, "off after %d min (%s)", cond->run_mins, tbuf);
        } else {
            snprintf(out, len, "off after %d min", cond->run_mins);
        }
        break;
    default:
        out[0] = '\0';
        break;
    }
}

static void build_summary(int outlet_index,
                          bool outlet_on,
                          bool time_valid,
                          bool sensor_available,
                          int64_t now_us,
                          char out[SCHEDULE_SUMMARY_LEN])
{
    outlet_sched_t *sched = &s_outlets[outlet_index];
    out[0] = '\0';

    if (!outlet_assignment(sched->id)[0]) return;

    bool needs_time = false;
    bool needs_sensor = false;
    int active_count = 0;
    char active[32] = "";
    char waiting[56] = "";
    char single_clear[32] = "";

    for (int i = 0; i < sched->condition_count; i++) {
        const sched_condition_t *cond = &sched->conditions[i];
        if (condition_uses_wall_clock(cond)) needs_time = true;
        if (condition_uses_sensor(cond)) needs_sensor = true;

        if (s_condition_active[outlet_index][i]) {
            char part[20];
            condition_active_text(cond, part, sizeof(part));
            append_phrase(active, sizeof(active), part, " + ");
            active_count++;
            if (active_count == 1) {
                condition_clear_text(outlet_index, cond, time_valid, now_us,
                                     single_clear, sizeof(single_clear));
            }
        } else {
            char part[28];
            condition_waiting_text(cond, part, sizeof(part));
            append_phrase(waiting, sizeof(waiting), part, " or ");
        }
    }

    if (!time_valid && needs_time) {
        if (outlet_on && active[0]) {
            snprintf(out, SCHEDULE_SUMMARY_LEN, "ON - %s active; time sync needed", active);
        } else {
            snprintf(out, SCHEDULE_SUMMARY_LEN, "waiting for time sync");
        }
        return;
    }
    if (!sensor_available && needs_sensor) {
        if (outlet_on && active[0]) {
            snprintf(out, SCHEDULE_SUMMARY_LEN, "ON - %s active; sensor data unavailable", active);
        } else {
            snprintf(out, SCHEDULE_SUMMARY_LEN, "waiting for sensor data");
        }
        return;
    }

    if (outlet_on) {
        if (active_count == 1 && single_clear[0]) {
            snprintf(out, SCHEDULE_SUMMARY_LEN, "ON - %s active; %s", active, single_clear);
        } else if (active[0]) {
            snprintf(out, SCHEDULE_SUMMARY_LEN, "ON - %s active; off when all clear", active);
        }
    } else if (waiting[0]) {
        snprintf(out, SCHEDULE_SUMMARY_LEN, "waiting for %s", waiting);
    }
}

static bool schedule_evaluate_once(void)
{
    bool auto_mode = relays_get_mode() == RELAY_MODE_AUTO;
    bool old_time_sync_required = s_time_sync_required;
    bool old_sntp_unhealthy = s_sntp_unhealthy;
    char old_summaries[4][SCHEDULE_SUMMARY_LEN];
    memcpy(old_summaries, s_summaries, sizeof(old_summaries));
    s_sntp_unhealthy = time_sync_sntp_unhealthy();

    if (!auto_mode) {
        memset(s_summaries, 0, sizeof(s_summaries));
        s_time_sync_required = false;
        return old_time_sync_required ||
               old_sntp_unhealthy != s_sntp_unhealthy ||
               memcmp(old_summaries, s_summaries, sizeof(s_summaries)) != 0;
    }

    sensor_reading_t sensor = sensors_read();
    bool sensor_available = sensors_temp_rh_available(SCHEDULE_SENSOR_STALE_SECS);
    if (!sensor_available || (!s_prev_sensor_available && sensor_available)) {
        memset(s_condition_active, 0, sizeof(s_condition_active));
    }
    s_prev_sensor_available = sensor_available;

    int now_mins = 0;
    bool time_valid = current_local_minute(&now_mins);
    int64_t now_us = esp_timer_get_time();
    uint8_t new_mask = 0;
    s_time_sync_required = false;
    s_sensor_data_unavailable = false;
    memset(s_summaries, 0, sizeof(s_summaries));

    for (int i = 0; i < s_outlet_count; i++) {
        bool outlet_on = false;
        bool outlet_needs_time = false;
        bool outlet_needs_sensor = false;

        if (outlet_schedule_disabled(s_outlets[i].id)) {
            continue;
        }

        for (int j = 0; j < s_outlets[i].condition_count; j++) {
            const sched_condition_t *cond = &s_outlets[i].conditions[j];
            if (condition_uses_wall_clock(cond)) outlet_needs_time = true;
            if (condition_uses_sensor(cond)) outlet_needs_sensor = true;
            if (evaluate_condition(i, j, cond, &sensor, sensor_available, time_valid, now_mins, now_us)) {
                outlet_on = true;
            }
        }

        if (!time_valid && outlet_needs_time) {
            s_time_sync_required = true;
        }
        if (!sensor_available && outlet_needs_sensor) {
            s_sensor_data_unavailable = true;
        }

        int bit = relay_bit_for_outlet(s_outlets[i].id);
        if (outlet_on && bit >= 0) {
            new_mask |= (1 << bit);
        }

        if (s_outlets[i].id >= 1 && s_outlets[i].id <= 4) {
            build_summary(i, outlet_on, time_valid, sensor_available,
                          now_us, s_summaries[s_outlets[i].id - 1]);
        }
    }

    uint8_t old_mask = relays_get_bitmask();
    bool relay_changed = (old_mask & 0x0F) != new_mask;
    if (relay_changed) {
        relays_set_bitmask(new_mask);
    }

    bool summary_changed = memcmp(old_summaries, s_summaries, sizeof(s_summaries)) != 0;
    bool time_changed = old_time_sync_required != s_time_sync_required;
    bool sntp_changed = old_sntp_unhealthy != s_sntp_unhealthy;
    return relay_changed || summary_changed || time_changed || sntp_changed;
}

bool schedule_evaluate_now(void)
{
    return schedule_evaluate_once();
}

static void schedule_task(void *arg)
{
    while (1) {
        if (schedule_evaluate_once()) {
            mqtt_publish_schedule_state("schedule");
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void schedule_init(void)
{
    xTaskCreate(schedule_task, "schedule", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Schedule engine started");
}

void schedule_pump_run_now(int outlet_id)
{
    for (int i = 0; i < s_outlet_count; i++) {
        if (s_outlets[i].id != outlet_id) continue;
        if (s_interval[i].run_active) return;

        const sched_condition_t *interval = find_interval_condition(&s_outlets[i]);
        if (!interval) return;
        if (!schedule_pump_run_now_allowed(outlet_id)) return;

        start_interval_run(i, esp_timer_get_time());
        schedule_evaluate_now();
        return;
    }
}

bool schedule_pump_is_running(int outlet_id)
{
    for (int i = 0; i < s_outlet_count; i++) {
        if (s_outlets[i].id == outlet_id) {
            return s_interval[i].run_active;
        }
    }
    return false;
}

bool schedule_pump_run_now_allowed(int outlet_id)
{
    if (relays_get_mode() != RELAY_MODE_AUTO) return false;
    if (outlet_schedule_disabled(outlet_id)) return false;

    for (int i = 0; i < s_outlet_count; i++) {
        if (s_outlets[i].id != outlet_id) continue;
        if (s_interval[i].run_active) return false;

        const sched_condition_t *interval = find_interval_condition(&s_outlets[i]);
        if (!interval) return false;

        if (interval->has_window) {
            int now_mins = 0;
            return current_local_minute(&now_mins) &&
                   interval_window_allows_start(interval, now_mins);
        }

        return true;
    }
    return false;
}

int schedule_pump_next_run_secs(int outlet_id)
{
    if (outlet_schedule_disabled(outlet_id)) return -1;

    int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < s_outlet_count; i++) {
        if (s_outlets[i].id != outlet_id) continue;
        const sched_condition_t *interval = find_interval_condition(&s_outlets[i]);
        if (!interval) continue;
        if (s_interval[i].run_active) return 0;

        int64_t interval_us = (int64_t)interval->every_hrs * 3600LL * 1000000LL;
        int64_t elapsed_us = now_us - s_interval[i].last_run_us;
        if (elapsed_us >= interval_us) return 0;
        return (int)((interval_us - elapsed_us) / 1000000LL);
    }
    return -1;
}
