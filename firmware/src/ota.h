#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OTA_IDLE,
    OTA_CONNECTING,
    OTA_FLASHING,
    OTA_DONE,
    OTA_FAILED,
} ota_stage_t;

typedef struct {
    ota_stage_t stage;
    int bytes_written;
} ota_progress_t;

// Start OTA update from a URL (runs in its own task, non-blocking).
// Called when an OTA command arrives via MQTT or the web UI.
bool ota_start_from_url(const char *url);

// Returns a snapshot of current OTA progress. Safe to call from any task.
ota_progress_t ota_get_progress(void);

// Update OTA progress state from an external context (e.g. upload handler).
void ota_set_progress(ota_stage_t stage, int bytes);

// Shared guard for URL OTA and browser upload OTA.
bool ota_begin_operation(void);
void ota_end_operation(void);
