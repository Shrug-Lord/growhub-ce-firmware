#pragma once

#include <stdbool.h>

// Initialize MQTT client and connect to broker.
// Call after wifi_init() — will auto-connect when WiFi is up.
void mqtt_init(void);

// Publish a sensor reading. Called by the main sensor loop.
void mqtt_publish_sensor(const char *json_payload);

// Publish retained active schedule state for Command Center mirroring.
void mqtt_publish_schedule_state(const char *source);

// Publish rejected schedule writes/actions. No-op when MQTT is disconnected.
void mqtt_publish_schedule_error(const char *reason, int outlet, const char *detail);

// True when connected to the MQTT broker
bool mqtt_is_connected(void);

// Stop and destroy the MQTT client (used by CC soft-disconnect)
void mqtt_stop(void);
