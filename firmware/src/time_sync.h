#pragma once

#include <stdbool.h>

// Apply the configured timezone/time source/SNTP servers.
void time_sync_init(void);
void time_sync_apply_config(void);
void time_sync_on_wifi_connected(void);

// Wall time is considered usable once the epoch is beyond the ESP32 reset era.
bool time_sync_wall_time_valid(void);

// Public status strings used by /status and MQTT schedule/state.
const char *time_sync_source_str(void);
const char *time_sync_sntp_status_str(void);

// Drift-risk warning state. This stays false during the startup grace after
// SNTP starts/restarts and becomes true if SNTP never syncs or its last
// successful sync becomes stale while wall time is otherwise valid.
bool time_sync_sntp_unhealthy(void);
