#pragma once
#include "config.h"
#include "schedule.h"
// Versioned native-client settings snapshot. Never includes passwords.
// Caller owns the returned heap allocation.
char *settings_snapshot_json(const growhub_config_t *cfg,
    const outlet_sched_t *outlets, int count, const char *mac, const char *version);
