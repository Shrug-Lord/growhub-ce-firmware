#include "mqtt.h"
#include "config.h"
#include "relays.h"
#include "schedule.h"
#include "sensors.h"
#include "ota.h"
#include "time_sync.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

// Topic buffers (built once after connect)
static char s_topic_sensor[80];
static char s_topic_control_mode[80];
static char s_topic_control_relay[80];
static char s_topic_config[80];
static char s_topic_grow[80];
static char s_topic_ota[80];
static char s_topic_status[80];
static char s_topic_schedule_action[96];
static char s_topic_schedule_error[96];
static char s_topic_schedule_state[96];
static char s_topic_control_error[96];

static void build_topics(void)
{
    const char *mac = config_get_mac_str();
    snprintf(s_topic_sensor,        sizeof(s_topic_sensor),        "growhub/%s/sensor/live", mac);
    snprintf(s_topic_control_mode,  sizeof(s_topic_control_mode),  "growhub/%s/control/mode", mac);
    snprintf(s_topic_control_relay, sizeof(s_topic_control_relay), "growhub/%s/control/relay", mac);
    snprintf(s_topic_config,        sizeof(s_topic_config),        "growhub/%s/config", mac);
    snprintf(s_topic_grow,          sizeof(s_topic_grow),          "growhub/%s/grow", mac);
    snprintf(s_topic_ota,           sizeof(s_topic_ota),           "growhub/%s/ota", mac);
    snprintf(s_topic_status,        sizeof(s_topic_status),        "growhub/%s/status", mac);
    snprintf(s_topic_schedule_action,sizeof(s_topic_schedule_action),"growhub/%s/schedule/action", mac);
    snprintf(s_topic_schedule_error, sizeof(s_topic_schedule_error), "growhub/%s/schedule/error", mac);
    snprintf(s_topic_schedule_state,sizeof(s_topic_schedule_state),"growhub/%s/schedule/state", mac);
    snprintf(s_topic_control_error, sizeof(s_topic_control_error), "growhub/%s/control/error", mac);
}

static void subscribe_all(void)
{
    esp_mqtt_client_subscribe(s_client, s_topic_control_mode,  1);
    esp_mqtt_client_subscribe(s_client, s_topic_control_relay, 1);
    esp_mqtt_client_subscribe(s_client, s_topic_config,        1);
    esp_mqtt_client_subscribe(s_client, s_topic_grow,          1);
    esp_mqtt_client_subscribe(s_client, s_topic_schedule_action, 1);
    esp_mqtt_client_subscribe(s_client, s_topic_ota,           1);
    ESP_LOGI(TAG, "Subscribed to control/config/grow/schedule-action/ota topics");
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

static bool parse_payload_int(const char *data, int len, int *out)
{
    if (!data || len <= 0 || len > 2) return false;

    int value = 0;
    for (int i = 0; i < len; i++) {
        if (data[i] < '0' || data[i] > '9') return false;
        value = (value * 10) + (data[i] - '0');
    }

    *out = value;
    return true;
}

static void publish_control_error(const char *command, const char *reason)
{
    if (!s_connected || !s_client) return;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "command", command ? command : "");
    cJSON_AddStringToObject(root, "reason", reason ? reason : "invalid_payload");

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) return;

    esp_mqtt_client_publish(s_client, s_topic_control_error, payload, 0, 1, 0);
    free(payload);
}

void mqtt_publish_schedule_error(const char *reason, int outlet, const char *detail)
{
    if (!s_connected || !s_client) return;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "reason", reason ? reason : "invalid_payload");
    if (outlet > 0) cJSON_AddNumberToObject(root, "outlet", outlet);
    if (detail && detail[0]) cJSON_AddStringToObject(root, "detail", detail);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) return;

    esp_mqtt_client_publish(s_client, s_topic_schedule_error, payload, 0, 1, 0);
    free(payload);
}

static bool condition_uses_wall_clock(const sched_condition_t *cond)
{
    if (!cond) return false;
    return cond->type == SCHED_COND_TIME_WINDOW ||
           (cond->type == SCHED_COND_INTERVAL && cond->has_window);
}

static bool condition_uses_sensor(const sched_condition_t *cond)
{
    if (!cond) return false;
    return cond->type == SCHED_COND_RH_LOW_BAND ||
           cond->type == SCHED_COND_RH_HIGH_BAND ||
           cond->type == SCHED_COND_TEMP_LOW_BAND_C ||
           cond->type == SCHED_COND_TEMP_HIGH_BAND_C;
}

static bool outlet_has_condition(const outlet_sched_t *outlet,
                                 bool (*predicate)(const sched_condition_t *))
{
    if (!outlet || !predicate) return false;
    for (int i = 0; i < outlet->condition_count; i++) {
        if (predicate(&outlet->conditions[i])) return true;
    }
    return false;
}

static bool outlet_schedule_disabled(int outlet_id)
{
    if (outlet_id < 1 || outlet_id > 4) return false;
    return (config_get()->schedule_disabled_mask & (1 << (outlet_id - 1))) != 0;
}

static const outlet_sched_t *find_scheduled_outlet(const outlet_sched_t *outlets,
                                                   int outlet_count,
                                                   int outlet_id)
{
    for (int i = 0; i < outlet_count; i++) {
        if (outlets[i].id == outlet_id) return &outlets[i];
    }
    return NULL;
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

static bool window_contains_full_run(const sched_condition_t *cond)
{
    if (!cond || !cond->has_window || !time_sync_wall_time_valid()) return false;

    time_t now_ts;
    time(&now_ts);
    struct tm now_local;
    localtime_r(&now_ts, &now_local);

    int now_mins = now_local.tm_hour * 60 + now_local.tm_min;
    int end_mins = cond->window_end_mins;
    bool in_window = false;
    if (cond->window_start_mins < cond->window_end_mins) {
        in_window = now_mins >= cond->window_start_mins && now_mins < cond->window_end_mins;
    } else {
        in_window = now_mins >= cond->window_start_mins || now_mins < cond->window_end_mins;
    }
    if (!in_window) return false;

    int mins_until_end = end_mins > now_mins ? end_mins - now_mins : (1440 - now_mins) + end_mins;
    return mins_until_end >= cond->run_mins;
}

static void add_condition_json(cJSON *arr, const sched_condition_t *cond)
{
    cJSON *item = cJSON_CreateObject();
    if (!item) return;

    switch (cond->type) {
    case SCHED_COND_ALWAYS_ON:
        cJSON_AddStringToObject(item, "type", "always_on");
        break;
    case SCHED_COND_TIME_WINDOW:
        cJSON_AddStringToObject(item, "type", "time_window");
        cJSON_AddStringToObject(item, "start", cond->start);
        cJSON_AddStringToObject(item, "end", cond->end);
        break;
    case SCHED_COND_RH_LOW_BAND:
        cJSON_AddStringToObject(item, "type", "rh_low_band");
        cJSON_AddNumberToObject(item, "low", cond->low);
        cJSON_AddNumberToObject(item, "high", cond->high);
        break;
    case SCHED_COND_RH_HIGH_BAND:
        cJSON_AddStringToObject(item, "type", "rh_high_band");
        cJSON_AddNumberToObject(item, "low", cond->low);
        cJSON_AddNumberToObject(item, "high", cond->high);
        break;
    case SCHED_COND_TEMP_LOW_BAND_C:
        cJSON_AddStringToObject(item, "type", "temp_low_band_c");
        cJSON_AddNumberToObject(item, "low_c", cond->low);
        cJSON_AddNumberToObject(item, "high_c", cond->high);
        break;
    case SCHED_COND_TEMP_HIGH_BAND_C:
        cJSON_AddStringToObject(item, "type", "temp_high_band_c");
        cJSON_AddNumberToObject(item, "low_c", cond->low);
        cJSON_AddNumberToObject(item, "high_c", cond->high);
        break;
    case SCHED_COND_INTERVAL:
        cJSON_AddStringToObject(item, "type", "interval");
        cJSON_AddNumberToObject(item, "run_mins", cond->run_mins);
        cJSON_AddNumberToObject(item, "every_hrs", cond->every_hrs);
        if (cond->has_window) {
            cJSON *window = cJSON_CreateObject();
            if (window) {
                cJSON_AddStringToObject(window, "start", cond->window_start);
                cJSON_AddStringToObject(window, "end", cond->window_end);
                cJSON_AddItemToObject(item, "window", window);
            }
        }
        break;
    default:
        cJSON_Delete(item);
        return;
    }

    cJSON_AddItemToArray(arr, item);
}

static cJSON *build_schedule_json(const outlet_sched_t *outlets, int outlet_count)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "v", 3);

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "outlets", arr);

    for (int i = 0; i < outlet_count; i++) {
        cJSON *outlet = cJSON_CreateObject();
        cJSON *conditions = cJSON_CreateArray();
        if (!outlet || !conditions) {
            cJSON_Delete(outlet);
            cJSON_Delete(conditions);
            continue;
        }

        cJSON_AddNumberToObject(outlet, "id", outlets[i].id);
        cJSON_AddItemToObject(outlet, "conditions", conditions);
        for (int j = 0; j < outlets[i].condition_count; j++) {
            add_condition_json(conditions, &outlets[i].conditions[j]);
        }
        cJSON_AddItemToArray(arr, outlet);
    }

    return root;
}

static void add_warning(cJSON *warnings,
                        const char *code,
                        const char *message,
                        const char *severity,
                        const int *outlets,
                        int outlet_count)
{
    cJSON *warning = cJSON_CreateObject();
    if (!warning) return;
    cJSON_AddStringToObject(warning, "code", code);
    cJSON_AddStringToObject(warning, "message", message);
    cJSON_AddStringToObject(warning, "severity", severity);

    if (outlets && outlet_count > 0) {
        cJSON *arr = cJSON_CreateArray();
        if (arr) {
            for (int i = 0; i < outlet_count; i++) {
                cJSON_AddItemToArray(arr, cJSON_CreateNumber(outlets[i]));
            }
            cJSON_AddItemToObject(warning, "outlets", arr);
        }
    }

    cJSON_AddItemToArray(warnings, warning);
}

static void handle_control_mode(const char *data, int len)
{
    // "2" = manual, "3" = auto, "7" = all off
    if (len != 1) {
        publish_control_error("control/mode", "invalid_payload");
        return;
    }
    char cmd = data[0];

    switch (cmd) {
    case '2':
        relays_set_mode(RELAY_MODE_MANUAL);
        mqtt_publish_schedule_state("mqtt");
        ESP_LOGI(TAG, "Control: enter manual mode");
        break;
    case '3':
        relays_set_mode(RELAY_MODE_AUTO);
        schedule_evaluate_now();
        mqtt_publish_schedule_state("mqtt");
        ESP_LOGI(TAG, "Control: return to auto mode");
        break;
    case '7':
        relays_set_mode(RELAY_MODE_MANUAL);
        relays_all_off();
        mqtt_publish_schedule_state("mqtt");
        ESP_LOGI(TAG, "Control: all relays off");
        break;
    default:
        publish_control_error("control/mode", "invalid_mode");
        ESP_LOGW(TAG, "Control: unknown mode command '%c'", cmd);
    }
}

static void handle_control_relay(const char *data, int len)
{
    // Payload is a decimal bitmask string: "0"-"15"
    int mask = 0;
    if (!parse_payload_int(data, len, &mask)) {
        publish_control_error("control/relay", "invalid_payload");
        return;
    }
    if (mask < 0 || mask > 15) {
        publish_control_error("control/relay", "invalid_relay_mask");
        return;
    }
    if (relays_get_mode() != RELAY_MODE_MANUAL) {
        publish_control_error("control/relay", "manual_mode_required");
        ESP_LOGW(TAG, "Relay bitmask rejected in AUTO mode");
        return;
    }

    relays_set_bitmask((uint8_t)mask);
    mqtt_publish_schedule_state("mqtt");
    ESP_LOGI(TAG, "Relay bitmask: %d", mask);
}

static void handle_config(const char *data, int len)
{
    // Device config update — parse JSON
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "Config: invalid JSON");
        return;
    }

    bool time_config_changed = false;

    cJSON *tz = cJSON_GetObjectItem(root, "tZ");
    if (tz && cJSON_IsString(tz)) {
        config_save_device(NULL, tz->valuestring, -1, -1);
        ESP_LOGI(TAG, "Config: timezone updated to %s", tz->valuestring);
        time_config_changed = true;
    }

    cJSON *time_src = cJSON_GetObjectItem(root, "timeSrc");
    if (time_src && cJSON_IsString(time_src)) {
        if (strcmp(time_src->valuestring, "sntp") == 0) {
            config_save_device(NULL, NULL, -1, 0);
            time_config_changed = true;
        } else if (strcmp(time_src->valuestring, "manual") == 0) {
            config_save_device(NULL, NULL, -1, 1);
            time_config_changed = true;
        } else {
            ESP_LOGW(TAG, "Config: ignored invalid timeSrc %s", time_src->valuestring);
        }
    }

    cJSON *sntp_primary = cJSON_GetObjectItem(root, "sntpPrimary");
    cJSON *sntp_secondary = cJSON_GetObjectItem(root, "sntpSecondary");
    bool has_sntp_primary = sntp_primary && cJSON_IsString(sntp_primary) && sntp_primary->valuestring[0];
    bool has_sntp_secondary = sntp_secondary && cJSON_IsString(sntp_secondary) && sntp_secondary->valuestring[0];
    if (has_sntp_primary || has_sntp_secondary) {
        config_save_sntp_servers(has_sntp_primary ? sntp_primary->valuestring : NULL,
                                 has_sntp_secondary ? sntp_secondary->valuestring : NULL);
        time_config_changed = true;
    }

    cJSON *tmp_off = cJSON_GetObjectItem(root, "tmpOff");
    cJSON *rh_off  = cJSON_GetObjectItem(root, "rhOff");
    bool has_tmp_off = tmp_off && cJSON_IsNumber(tmp_off);
    bool has_rh_off = rh_off && cJSON_IsNumber(rh_off);
    if ((tmp_off && !has_tmp_off) || (rh_off && !has_rh_off)) {
        ESP_LOGW(TAG, "Config: ignored non-numeric calibration offset");
    }
    if (has_tmp_off || has_rh_off) {
        const growhub_config_t *cfg = config_get();
        config_save_calibration(
            has_tmp_off ? (float)tmp_off->valuedouble : cfg->temp_offset,
            has_rh_off ? (float)rh_off->valuedouble : cfg->rh_offset
        );
    }

    if (time_config_changed) {
        time_sync_apply_config();
    }

    cJSON_Delete(root);
}

static void handle_ota(const char *data, int len)
{
    // OTA URL received — start firmware update
    char url[256] = {0};
    int copy_len = len < (int)sizeof(url) - 1 ? len : (int)sizeof(url) - 1;
    memcpy(url, data, copy_len);
    ESP_LOGI(TAG, "OTA triggered: %s", url);
    if (!ota_start_from_url(url)) {
        publish_control_error("ota", "ota_busy_or_invalid_url");
    }
}

static void handle_schedule_action(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        mqtt_publish_schedule_error("invalid_payload", 0, NULL);
        return;
    }

    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!action || !cJSON_IsString(action)) {
        cJSON_Delete(root);
        mqtt_publish_schedule_error("invalid_payload", 0, NULL);
        return;
    }

    if (strcmp(action->valuestring, "pump_run_now") != 0) {
        cJSON_Delete(root);
        mqtt_publish_schedule_error("unsupported_action", 0, NULL);
        return;
    }

    cJSON *outlet_item = cJSON_GetObjectItem(root, "outlet");
    if (!outlet_item || !cJSON_IsNumber(outlet_item) ||
        outlet_item->valuedouble != (double)outlet_item->valueint ||
        outlet_item->valueint < 1 || outlet_item->valueint > 4) {
        cJSON_Delete(root);
        mqtt_publish_schedule_error("invalid_outlet", 0, NULL);
        return;
    }
    int outlet_id = outlet_item->valueint;
    cJSON_Delete(root);

    if (relays_get_mode() != RELAY_MODE_AUTO) {
        mqtt_publish_schedule_error("auto_mode_required", outlet_id, NULL);
        return;
    }

    outlet_sched_t outlets[MAX_OUTLET_SCHEDS] = {0};
    int outlet_count = schedule_get_outlets(outlets, MAX_OUTLET_SCHEDS);
    const outlet_sched_t *outlet = find_scheduled_outlet(outlets, outlet_count, outlet_id);
    const sched_condition_t *interval = find_interval_condition(outlet);
    if (!interval) {
        mqtt_publish_schedule_error("pump_schedule_required", outlet_id, NULL);
        return;
    }

    if (interval->has_window) {
        if (!time_sync_wall_time_valid()) {
            mqtt_publish_schedule_error("time_sync_required", outlet_id, NULL);
            return;
        }
        if (!window_contains_full_run(interval)) {
            mqtt_publish_schedule_error("pump_window_ineligible", outlet_id, NULL);
            return;
        }
    }

    schedule_pump_run_now(outlet_id);
    mqtt_publish_schedule_state("mqtt");
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Connected to broker");
        // Publish online status (retained)
        esp_mqtt_client_publish(s_client, s_topic_status, "online", 0, 1, 1);
        subscribe_all();
        mqtt_publish_schedule_state("reconnect");
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected from broker");
        break;

    case MQTT_EVENT_DATA: {
        // Null-terminate topic for comparison
        char topic[80] = {0};
        int tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, event->topic, tlen);

        if (strcmp(topic, s_topic_control_mode) == 0) {
            handle_control_mode(event->data, event->data_len);
        } else if (strcmp(topic, s_topic_control_relay) == 0) {
            handle_control_relay(event->data, event->data_len);
        } else if (strcmp(topic, s_topic_config) == 0) {
            handle_config(event->data, event->data_len);
        } else if (strcmp(topic, s_topic_grow) == 0) {
            ESP_LOGI(TAG, "Schedule received from Command Center (%d bytes)", event->data_len);
            if (!schedule_load(event->data, event->data_len)) {
                ESP_LOGW(TAG, "Schedule rejected; not persisted: %s (outlet %d)",
                         schedule_last_error_reason(), schedule_last_error_outlet());
                mqtt_publish_schedule_error(schedule_last_error_reason(),
                                            schedule_last_error_outlet(), NULL);
                break;
            }
            // Persist so schedule survives reboot without Command Center
            char *sched_buf = malloc(event->data_len + 1);
            if (sched_buf) {
                memcpy(sched_buf, event->data, event->data_len);
                sched_buf[event->data_len] = '\0';
                config_save_schedule(sched_buf);
                config_save_schedule_disabled_mask(0);
                free(sched_buf);
                if (relays_get_mode() == RELAY_MODE_AUTO) {
                    schedule_evaluate_now();
                }
                mqtt_publish_schedule_state("mqtt");
            }
        } else if (strcmp(topic, s_topic_schedule_action) == 0) {
            handle_schedule_action(event->data, event->data_len);
        } else if (strcmp(topic, s_topic_ota) == 0) {
            handle_ota(event->data, event->data_len);
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

void mqtt_init(void)
{
    build_topics();

    const growhub_config_t *cfg = config_get();

    if (!cfg->mqtt_host[0] || cfg->mqtt_disabled) {
        ESP_LOGI(TAG, "MQTT %s — skipping init",
                 cfg->mqtt_disabled ? "disabled by user" : "host not configured");
        return;
    }

    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", cfg->mqtt_host, cfg->mqtt_port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = config_get_mac_str(),
        .session.last_will = {
            .topic = s_topic_status,
            .msg = "offline",
            .msg_len = 7,
            .qos = 1,
            .retain = 1,
        },
        .session.keepalive = 30,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    ESP_LOGI(TAG, "MQTT client started — broker: %s", uri);
}

void mqtt_publish_sensor(const char *json_payload)
{
    if (!s_connected || !s_client) return;
    esp_mqtt_client_publish(s_client, s_topic_sensor, json_payload, 0, 0, 0);
}

void mqtt_publish_schedule_state(const char *source)
{
    if (!s_connected || !s_client) return;

    outlet_sched_t outlets[MAX_OUTLET_SCHEDS] = {0};
    int outlet_count = schedule_get_outlets(outlets, MAX_OUTLET_SCHEDS);
    bool active = schedule_is_active() && outlet_count > 0;
    const char *mode = relays_get_mode() == RELAY_MODE_MANUAL ? "manual" : "auto";
    const char *src = (source && source[0]) ? source : "firmware";
    bool auto_mode = relays_get_mode() == RELAY_MODE_AUTO;
    bool time_valid = time_sync_wall_time_valid();
    const char *sntp_status = time_sync_sntp_status_str();
    bool sensor_available = sensors_temp_rh_available(SCHEDULE_SENSOR_STALE_SECS);

    int time_outlets[MAX_OUTLET_SCHEDS] = {0};
    int time_outlet_count = 0;
    int sensor_outlets[MAX_OUTLET_SCHEDS] = {0};
    int sensor_outlet_count = 0;

    if (active && auto_mode) {
        for (int i = 0; i < outlet_count; i++) {
            if (outlet_schedule_disabled(outlets[i].id)) continue;
            if (outlet_has_condition(&outlets[i], condition_uses_wall_clock)) {
                time_outlets[time_outlet_count++] = outlets[i].id;
            }
            if (outlet_has_condition(&outlets[i], condition_uses_sensor)) {
                sensor_outlets[sensor_outlet_count++] = outlets[i].id;
            }
        }
    }

    bool time_sync_required = auto_mode && !time_valid && time_outlet_count > 0;
    bool sensor_unavailable = auto_mode && !sensor_available && sensor_outlet_count > 0;
    bool sntp_unhealthy = time_sync_sntp_unhealthy();

    const char *time_warning = "";
    if (time_sync_required) {
        time_warning = "Time sync required; wall-clock automation is paused";
    } else if (sntp_unhealthy) {
        time_warning = "SNTP sync is unavailable; device time may drift";
    }

    const char *sensor_warning = sensor_unavailable
        ? "Sensor data unavailable; temp/rH automation is paused"
        : "";

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddBoolToObject(root, "active", active);
    cJSON_AddStringToObject(root, "mode", mode);
    cJSON_AddStringToObject(root, "source", src);
    cJSON_AddBoolToObject(root, "time_valid", time_valid);
    cJSON_AddStringToObject(root, "time_source", time_sync_source_str());
    cJSON_AddStringToObject(root, "sntp_status", sntp_status);
    cJSON_AddStringToObject(root, "time_warning", time_warning);
    cJSON_AddStringToObject(root, "sensor_warning", sensor_warning);

    cJSON *warnings = cJSON_CreateArray();
    if (!warnings) {
        cJSON_Delete(root);
        return;
    }
    cJSON_AddItemToObject(root, "warnings", warnings);

    if (time_sync_required) {
        add_warning(warnings,
                    "time_sync_required",
                    "Time sync required; wall-clock automation is paused",
                    "blocking",
                    time_outlets,
                    time_outlet_count);
    }
    if (sensor_unavailable) {
        add_warning(warnings,
                    "sensor_data_unavailable",
                    "Sensor data unavailable; temp/rH automation is paused",
                    "warning",
                    sensor_outlets,
                    sensor_outlet_count);
    }
    if (sntp_unhealthy) {
        add_warning(warnings,
                    "time_sntp_unhealthy",
                    "SNTP sync is unavailable; device time may drift",
                    "warning",
                    NULL,
                    0);
    }

    if (active) {
        cJSON *schedule = build_schedule_json(outlets, outlet_count);
        if (schedule) {
            cJSON_AddItemToObject(root, "schedule", schedule);
        } else {
            cJSON_AddNullToObject(root, "schedule");
        }
    } else {
        cJSON_AddNullToObject(root, "schedule");
    }

    cJSON *outlet_status = cJSON_CreateArray();
    if (!outlet_status) {
        cJSON_Delete(root);
        return;
    }
    cJSON_AddItemToObject(root, "outlet_status", outlet_status);

    uint8_t relay_mask = relays_get_bitmask();
    for (int id = 1; id <= 4; id++) {
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;

        int bit = relay_bit_for_outlet(id);
        bool on = bit >= 0 && ((relay_mask >> bit) & 1);
        cJSON_AddNumberToObject(item, "id", id);
        cJSON_AddStringToObject(item, "state", on ? "on" : "off");
        cJSON_AddStringToObject(item, "summary", schedule_get_outlet_summary(id));
        cJSON_AddItemToArray(outlet_status, item);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        ESP_LOGW(TAG, "Schedule state publish skipped: out of memory");
        return;
    }

    esp_mqtt_client_publish(s_client, s_topic_schedule_state, payload, 0, 1, 1);
    free(payload);
}

bool mqtt_is_connected(void)
{
    return s_connected;
}

void mqtt_stop(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
    ESP_LOGI(TAG, "MQTT client stopped");
}
