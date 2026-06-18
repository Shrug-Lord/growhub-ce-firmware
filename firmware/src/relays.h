#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Relay bit positions — confirmed GPIO mapping 2026-04-28:
// bit3→Outlet1(GPIO33), bit0→Outlet2(GPIO25), bit1→Outlet3(GPIO26), bit2→Outlet4(GPIO27)
#define RELAY_BIT_OUTLET2   0   // Outlet 2 (GPIO 25)
#define RELAY_BIT_OUTLET3   1   // Outlet 3 (GPIO 26)
#define RELAY_BIT_OUTLET4   2   // Outlet 4 (GPIO 27)
#define RELAY_BIT_OUTLET1   3   // Outlet 1 (GPIO 33)

typedef enum {
    RELAY_MODE_AUTO,
    RELAY_MODE_MANUAL,
} relay_mode_t;

// Initialize relay GPIO pins
void relays_init(void);

// Set mode (manual allows direct control, auto defers to schedule)
void relays_set_mode(relay_mode_t mode);
relay_mode_t relays_get_mode(void);

// Set relay state from bitmask (manual mode only)
// bit0=Outlet2, bit1=Outlet3, bit2=Outlet4, bit3=Outlet1
void relays_set_bitmask(uint8_t mask);

// Get current relay bitmask
uint8_t relays_get_bitmask(void);

// Get actuator state string for MQTT "a" field (9-byte buf, 8 chars + NUL).
// Format: [outlet2][outlet3][outlet4][outlet1][0][0][0][0]
//   pos 0 = bit0 (Outlet2), pos 1 = bit1 (Outlet3),
//   pos 2 = bit2 (Outlet4), pos 3 = bit3 (Outlet1), pos 4-7 = "0000" reserved
//   e.g. Outlet 2 ON only -> "10000000"
void relays_get_actuator_str(char *buf, size_t len);

// Turn all relays off
void relays_all_off(void);
