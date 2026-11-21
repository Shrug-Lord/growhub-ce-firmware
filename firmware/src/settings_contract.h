#pragma once

static const struct { const char *label; const char *posix; } TZ_TABLE[] = {
    {"US/Eastern",          "EST5EDT,M3.2.0,M11.1.0"},
    {"US/Central",          "CST6CDT,M3.2.0,M11.1.0"},
    {"US/Mountain",         "MST7MDT,M3.2.0,M11.1.0"},
    {"US/Pacific",          "PST8PDT,M3.2.0,M11.1.0"},
    {"US/Alaska",           "AKST9AKDT,M3.2.0,M11.1.0"},
    {"US/Hawaii",           "HST10"},
    {"UK/London",           "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Amsterdam",    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Australia/Sydney",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth",     "AWST-8"},
    {"Asia/Tokyo",          "JST-9"},
    {"Asia/Singapore",      "SGT-8"},
    {"UTC",                 "UTC0"},
};
#define TZ_COUNT (sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]))

// ---------------------------------------------------------------------------
// Outlet device type options
// ---------------------------------------------------------------------------
static const char *DEVICE_TYPES[] = {
    "None", "Light", "Fan", "Humidifier",
    "Dehumidifier", "Water Pump", "Heater", "AC Controller",
};
#define DEVICE_TYPE_COUNT (sizeof(DEVICE_TYPES) / sizeof(DEVICE_TYPES[0]))
