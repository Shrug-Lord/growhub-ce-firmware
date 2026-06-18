#pragma once

#include <stdbool.h>

// Initialize WiFi subsystem.
// If station credentials are configured, connects as station.
// Always starts AP mode in parallel for config access.
void wifi_init(void);

// True when connected to the home WiFi network (station mode)
bool wifi_is_connected(void);

// Reconnect station with current config (call after saving new credentials)
void wifi_reconnect(void);

// WiFi Recovery Mode: entered after retry exhaustion when configured SSID is
// not visible in scan. Performs an hourly scan and auto-reconnects when found.
bool wifi_is_in_recovery_mode(void);
void wifi_enter_recovery_mode(void);

// Exit recovery mode, disconnect STA, and remain AP-only. If clear_creds, also forgets WiFi creds.
// On the button: call with false (keep creds, user can re-engage recovery later).
// From web "Forget WiFi": call with true.
void wifi_exit_recovery_mode(bool clear_creds);
