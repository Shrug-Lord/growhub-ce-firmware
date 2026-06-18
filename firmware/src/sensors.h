#pragma once

#include <stdbool.h>

typedef struct {
    float temperature;  // degrees C
    float humidity;     // relative humidity %
    int   co2;          // ppm (0 if no sensor connected)
    int   light;        // 0-100% (phototransistor on SH_NP01 sensor board)
    bool  temp_valid;
    bool  co2_valid;
} sensor_reading_t;

// Drive sensor UART GPIOs HIGH early — call as first thing in app_main()
void sensors_early_gpio_init(void);

// Initialize sensor UART interface
void sensors_init(void);

// Returns the latest reading (invalid fields flagged).
sensor_reading_t sensors_read(void);

// True when temp/rH has a valid successful read within max_age_s.
bool sensors_temp_rh_available(int max_age_s);

// True after the sensor task completes its first UART read attempt.
bool sensors_first_read_attempt_done(void);
