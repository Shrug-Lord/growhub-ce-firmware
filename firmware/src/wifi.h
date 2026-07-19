#pragma once

#include <stdbool.h>
#include <stdint.h>

// Initialize WiFi subsystem.
// If station credentials are configured, connects as station.
// Starts the setup AP according to provisioning and persisted preference state.
void wifi_init(void);

// True when connected to the home WiFi network (station mode)
bool wifi_is_connected(void);
bool wifi_is_ap_active(void);
const char *wifi_ap_reason(void);
uint32_t wifi_ap_fallback_seconds(void);
const char *wifi_get_sta_ip(void);
void wifi_set_keep_ap_active(bool keep_active);

// Reconnect station with current config (call after saving new credentials)
void wifi_reconnect(void);

// Session-scoped physical-button setup AP override. The compatibility recovery
// flag is also true while the automatic fallback AP is active.
bool wifi_is_in_recovery_mode(void);
bool wifi_is_manual_ap_override(void);
void wifi_enter_recovery_mode(void);

// Exit the manual override. If clear_creds, forget station credentials and
// enter provisioning AP mode while preserving the AP preference.
void wifi_exit_recovery_mode(bool clear_creds);
