// Exercise real relay and schedule code with host GPIO, NVS and clock substitutes.
#include "relays.h"
#include "schedule.h"
#include "config.h"
#include "sensors.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static growhub_config_t cfg;
static int levels[64];
static bool valid_time = true, valid_sensor = true;
static int64_t now_us = 1000000;
const growhub_config_t *config_get(void) { return &cfg; }
void config_save_relay_manual_mask(uint8_t mask) { cfg.relay_manual_mask = mask; }
void config_save_schedule(const char *json) { (void)json; }
void config_clear_schedule(void) {}
void gpio_reset_pin(int pin) { (void)pin; }
void gpio_set_direction(int pin, int mode) { (void)pin; (void)mode; }
void gpio_set_level(int pin, int level) { assert(pin < 64); levels[pin] = level; }
int64_t esp_timer_get_time(void) { return now_us; }
bool time_sync_wall_time_valid(void) { return valid_time; }
bool time_sync_sntp_unhealthy(void) { return false; }
sensor_reading_t sensors_read(void) { return (sensor_reading_t){.temperature=25,.humidity=50,.temp_valid=true}; }
bool sensors_temp_rh_available(int age) { (void)age; return valid_sensor; }
void mqtt_publish_schedule_state(const char *source) { (void)source; }

static void load(const char *json) { assert(schedule_load(json, (int)strlen(json))); }
static void *schedule_writes(void *arg) {
    (void)arg;
    for (int i=0;i<10000;i++) relays_set_auto_bitmask(i & 7);
    return NULL;
}
int main(void) {
    cfg.pin_relay_outlet1=33; cfg.pin_relay_outlet2=25;
    cfg.pin_relay_outlet3=26; cfg.pin_relay_outlet4=27;
    strcpy(cfg.relay_names[3], "Light");
    strcpy(cfg.relay_names[0], "Light");
    strcpy(cfg.relay_names[1], "Heater");
    strcpy(cfg.relay_names[2], "Water Pump");
    relays_init();
    assert(relays_get_manual_mask()==0 && relays_get_bitmask()==0);
    const char *always = "{\"v\":3,\"outlets\":[{\"id\":1,\"conditions\":[{\"type\":\"always_on\"}]},{\"id\":2,\"conditions\":[{\"type\":\"always_on\"}]}]}";
    load(always); schedule_evaluate_now();
    assert(relays_get_bitmask()==9 && levels[33] && levels[25]);
    assert(!relays_set_outlet(1,false)); // AUTO rejects direct writes.
    assert(relays_set_outlet_mode(1,RELAY_MODE_MANUAL));
    assert(relays_get_bitmask()==9); // Takeover preserves live state.
    assert(relays_set_outlet(1,false));
    schedule_evaluate_now();
    assert(relays_get_bitmask()==1); // O2 remains AUTO; O1 remains manual OFF.
    assert(!relays_set_bitmask(15)); // Legacy whole-mask writes cannot affect AUTO.
    assert(!strcmp(schedule_get_outlet_summary(1), ""));
    assert(relays_set_outlet(1,true));
    schedule_clear(); schedule_evaluate_now();
    assert(relays_get_bitmask()==8); // Clear affects AUTO only.
    load(always); schedule_evaluate_now();
    assert(relays_get_bitmask()==9); // Save/load preserves manual ON.
    assert(relays_set_outlet_mode(1,RELAY_MODE_AUTO));
    schedule_clear(); schedule_evaluate_now();
    assert(relays_get_bitmask()==0); // AUTO with no schedule goes OFF.
    // All physical IDs map correctly; ownership persists, manual state does not.
    for(int id=1;id<=4;id++) {
        relays_set_mode(RELAY_MODE_AUTO);
        assert(relays_set_outlet_mode(id,RELAY_MODE_MANUAL));
        assert(relays_set_outlet(id,true));
        int bit=id==1?3:id-2;
        assert(relays_get_manual_mask()==(1<<bit));
        assert(relays_get_bitmask()==(1<<bit));
        relays_init();
        assert(relays_get_manual_mask()==(1<<bit) && relays_get_bitmask()==0);
    }
    // Manual conditions do not raise automation warnings or permit pump Run Now.
    relays_set_mode(RELAY_MODE_MANUAL);
    valid_time=false; valid_sensor=false;
    load("{\"v\":3,\"outlets\":[{\"id\":1,\"conditions\":[{\"type\":\"time_window\",\"start\":\"06:00\",\"end\":\"22:00\"}]},{\"id\":3,\"conditions\":[{\"type\":\"temp_low_band_c\",\"low_c\":20,\"high_c\":22}]},{\"id\":4,\"conditions\":[{\"type\":\"interval\",\"run_mins\":1,\"every_hrs\":1}]}]}");
    relays_set_outlet_mode(2,RELAY_MODE_AUTO); schedule_evaluate_now();
    assert(!schedule_time_sync_required() && !schedule_sensor_data_unavailable());
    assert(!schedule_pump_run_now_allowed(4));
    relays_set_outlet_mode(1,RELAY_MODE_AUTO);
    relays_set_outlet_mode(3,RELAY_MODE_AUTO); schedule_evaluate_now();
    assert(schedule_time_sync_required() && schedule_sensor_data_unavailable());
    relays_set_outlet_mode(4,RELAY_MODE_AUTO);
    assert(schedule_pump_run_now_allowed(4));
    schedule_pump_run_now(4); assert(relays_get_bitmask() & 4);
    relays_set_outlet_mode(4,RELAY_MODE_MANUAL); schedule_evaluate_now();
    assert(!schedule_pump_is_running(4));
    assert(relays_get_bitmask() & 4); // Takeover preserves relay, cancels run state.
    assert(relays_set_outlet(4,false));
    now_us += 7200000000LL; schedule_evaluate_now();
    assert(!(relays_get_bitmask() & 4));
    // Concurrent schedule writes must never overwrite the manual outlet bit.
    relays_set_mode(RELAY_MODE_AUTO); relays_set_outlet_mode(1,RELAY_MODE_MANUAL);
    pthread_t thread; assert(!pthread_create(&thread,NULL,schedule_writes,NULL));
    for(int i=0;i<10000;i++) {
        assert(relays_set_outlet(1,true)); assert(relays_get_bitmask() & 8);
        assert(relays_set_outlet(1,false)); assert(!(relays_get_bitmask() & 8));
    }
    pthread_join(thread,NULL);
    assert(!relays_set_outlet_mode(0,RELAY_MODE_MANUAL));
    assert(!relays_set_outlet_mode(5,RELAY_MODE_AUTO));
    assert(!relays_set_outlet_mode(1,(relay_mode_t)7));
    relays_all_off();
    assert(relays_get_manual_mask()==15 && relays_get_bitmask()==0);
    assert(relays_set_bitmask(15));
    relays_init(); assert(relays_get_manual_mask()==15 && relays_get_bitmask()==0);
    puts("Per-outlet control and schedule tests passed");
}
