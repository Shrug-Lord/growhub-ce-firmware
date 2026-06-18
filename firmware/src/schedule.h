#pragma once

#include <stdbool.h>

// Initialize the schedule engine (starts its own timer task)
void schedule_init(void);

// Load a v3 outlet-condition schedule JSON string from MQTT, NVS, or the web UI.
// Expected format: {"v":3,"outlets":[{"id":1,"conditions":[...]}]}.
bool schedule_load(const char *json, int len);

// Clear the active schedule.
void schedule_clear(void);

// Returns true if a schedule is currently loaded with at least one active entry.
bool schedule_is_active(void);

// ---------------------------------------------------------------------------
// V3 outlet-condition schedule types.
// ---------------------------------------------------------------------------

#define MAX_OUTLET_SCHEDS 4
#define MAX_CONDITIONS_PER_OUTLET 4
#define SCHEDULE_SENSOR_STALE_SECS 120
#define SCHEDULE_SUMMARY_LEN 96

typedef enum {
    SCHED_COND_ALWAYS_ON = 0,
    SCHED_COND_TIME_WINDOW,
    SCHED_COND_RH_LOW_BAND,
    SCHED_COND_RH_HIGH_BAND,
    SCHED_COND_TEMP_LOW_BAND_C,
    SCHED_COND_TEMP_HIGH_BAND_C,
    SCHED_COND_INTERVAL,
    SCHED_COND_COUNT,
} sched_condition_type_t;

typedef struct {
    sched_condition_type_t type;

    // time_window
    char start[6];
    char end[6];
    int  start_mins;
    int  end_mins;

    // rh/temp bands
    float low;
    float high;

    // interval
    int  run_mins;
    int  every_hrs;
    bool has_window;
    char window_start[6];
    char window_end[6];
    int  window_start_mins;
    int  window_end_mins;
} sched_condition_t;

typedef struct {
    int         id;           // outlet number 1–4
    int         condition_count;
    sched_condition_t conditions[MAX_CONDITIONS_PER_OUTLET];
} outlet_sched_t;

// Get the active outlet schedules.
int schedule_get_outlets(outlet_sched_t *out, int max);

// Evaluate immediately when AUTO mode or an accepted schedule should take
// ownership without waiting for the 30-second task tick.
bool schedule_evaluate_now(void);

// Firmware-owned single-line status text for an outlet. Empty in manual mode,
// unassigned outlets, and outlets without an active schedule.
const char *schedule_get_outlet_summary(int outlet_id);

// True when an active AUTO schedule has a wall-clock condition but wall time is
// unavailable. Used by the status LED and clients.
bool schedule_time_sync_required(void);
bool schedule_sensor_data_unavailable(void);

// Last schedule_load() validation error. Empty string / outlet 0 means none.
const char *schedule_last_error_reason(void);
int schedule_last_error_outlet(void);

// Force an interval-mode pump outlet to trigger on the next evaluation tick.
// No-op if outlet_id has no active interval schedule.
void schedule_pump_run_now(int outlet_id);

// True when the pump outlet is actively running its interval.
bool schedule_pump_is_running(int outlet_id);

// True when Run Now can start immediately in the current mode/window/time state.
bool schedule_pump_run_now_allowed(int outlet_id);

// Returns seconds until the next interval run for outlet_id, 0 if due/running,
// or -1 if outlet_id has no active interval schedule.
int schedule_pump_next_run_secs(int outlet_id);
