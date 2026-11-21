#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings_snapshot.h"
#include "cJSON.h"
static const char *value(cJSON *object, const char *key) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    assert(cJSON_IsString(item)); return item->valuestring;
}
int main(void) {
    growhub_config_t cfg = {0};
    strcpy(cfg.device_name, "Garden \"A\"");
    strcpy(cfg.sta_password, "PRIVATE_WIFI_PASSWORD");
    strcpy(cfg.ap_password, "PRIVATE_AP_PASSWORD");
    strcpy(cfg.sta_ssid, "Example network");
    strcpy(cfg.timezone, "UTC0");
    strcpy(cfg.relay_names[3], "Light"); strcpy(cfg.relay_names[0], "Water Pump");
    strcpy(cfg.relay_names[1], "Heater"); strcpy(cfg.relay_names[2], "Fan");
    strcpy(cfg.outlet_labels[3], "Lights & air");
    cfg.schedule_disabled_mask = 2; cfg.temp_unit = 1; cfg.operation_led_enabled = 0;
    outlet_sched_t outlets[3] = {{.id=1,.condition_count=1}, {.id=2,.condition_count=1}, {.id=3,.condition_count=1}};
    outlets[0].conditions[0].type = SCHED_COND_TIME_WINDOW;
    strcpy(outlets[0].conditions[0].start, "18:00"); strcpy(outlets[0].conditions[0].end, "06:00");
    sched_condition_t *pump = &outlets[1].conditions[0];
    pump->type = SCHED_COND_INTERVAL; pump->run_mins=12; pump->every_hrs=6; pump->has_window=true;
    strcpy(pump->window_start,"07:00"); strcpy(pump->window_end,"20:00");
    outlets[2].conditions[0].type = SCHED_COND_TEMP_LOW_BAND_C;
    outlets[2].conditions[0].low = 18; outlets[2].conditions[0].high = 21;
    char *json = settings_snapshot_json(&cfg,outlets,3,"AABBCCDDEEFF","1.2.5C");
    assert(json && !strstr(json,"PRIVATE_") && !strstr(json,"password"));
    cJSON *root = cJSON_Parse(json); assert(root);
    assert(!strcmp(value(root,"mac"),"AABBCCDDEEFF"));
    cJSON *fields = cJSON_GetObjectItem(root,"fields");
    assert(!strcmp(value(fields,"dev_name"),"Garden \"A\""));
    assert(!strcmp(value(fields,"outlet_1"),"Light"));
    assert(!strcmp(value(fields,"outlet_2"),"Water Pump"));
    assert(!strcmp(value(fields,"outlet_3"),"Heater"));
    assert(!strcmp(value(fields,"outlet_4"),"Fan"));
    assert(!strcmp(value(fields,"label_1"),"Lights & air"));
    assert(!strcmp(value(fields,"operation_led"),"0"));
    assert(!strcmp(value(fields,"s1_start"),"18:00"));
    assert(!strcmp(value(fields,"s1_end"),"06:00"));
    assert(!strcmp(value(fields,"s2_disabled"),"1"));
    assert(!strcmp(value(fields,"s2_win_en"),"1"));
    assert(!strcmp(value(fields,"s2_every"),"6.0"));
    assert(!strcmp(value(fields,"s3_temp_low"),"64.4"));
    assert(!strcmp(value(fields,"s3_temp_high"),"69.8"));
    assert(!strcmp(value(fields,"s4_always"),""));
    assert(cJSON_IsFalse(cJSON_GetObjectItem(cJSON_GetObjectItem(root,"capabilities"),"co2")));
    cJSON_Delete(root);
    const char *fixture = getenv("SETTINGS_FIXTURE_OUTPUT");
    if (fixture) { FILE *file = fopen(fixture,"w"); assert(file); fputs(json,file); fclose(file); }
    free(json);
    puts("Native settings snapshot tests passed");
    return 0;
}
