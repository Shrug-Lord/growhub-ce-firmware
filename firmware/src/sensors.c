/*
 * sensors.c — Niwa SH_NP01 sensor board UART driver
 *
 * Protocol reverse-engineered via USB passthrough LA capture (2026-04-26):
 *   Baud:     9600, 8N1, LSB-first
 *   Framing:  [55 AA] [LEN] [TYPE] [DATA...] [SUM mod 256]
 *
 *   Command (ESP32 → sensor, 5 bytes):  55 AA 05 00 04
 *   Response (sensor → ESP32, 11 bytes):
 *     55 AA 0B 10 00 [light] [T_hi T_lo] [RH_hi RH_lo] [sum]
 *       light  : uint8, 0-100 (phototransistor %)
 *       T_raw  : uint16 big-endian → T[°C] = -45 + 175 × raw / 65535
 *       RH_raw : uint16 big-endian → RH[%] = 100 × raw / 65535
 *       sum    : (sum of all preceding bytes) mod 256
 *
 * GPIO assignment: normal builds use the sensor UART pins from config.
 */

#include "sensors.h"
#include "config.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "sensors";

#define SENSOR_UART      UART_NUM_1
#define SENSOR_BAUD      9600

#define CMD_LEN         5
#define RESP_LEN        11
#define RESP_TIMEOUT_MS 500
#define POLL_MS         3000

static const uint8_t CMD[CMD_LEN] = {0x55, 0xAA, 0x05, 0x00, 0x04};

static sensor_reading_t s_latest;
static SemaphoreHandle_t s_mutex;
static volatile bool s_first_read_attempt_done = false;
static int64_t s_last_success_us = 0;

static void mark_first_read_attempt_done(void)
{
    if (!s_first_read_attempt_done) {
        s_first_read_attempt_done = true;
        ESP_LOGI(TAG, "First sensor read attempt complete");
    }
}

static bool verify_checksum(const uint8_t *buf, int len)
{
    uint8_t sum = 0;
    for (int i = 0; i < len - 1; i++) sum += buf[i];
    return (sum == buf[len - 1]);
}

static void parse_response(const uint8_t *buf)
{
    uint8_t  light  = buf[5];
    uint16_t t_raw  = ((uint16_t)buf[6] << 8) | buf[7];
    uint16_t rh_raw = ((uint16_t)buf[8] << 8) | buf[9];

    float temp_c  = -45.0f + 175.0f * (float)t_raw  / 65535.0f;
    float hum_pct =           100.0f * (float)rh_raw / 65535.0f;
    const growhub_config_t *cfg = config_get();
    temp_c += cfg->temp_offset;
    hum_pct += cfg->rh_offset;
    if (hum_pct < 0.0f) hum_pct = 0.0f;
    if (hum_pct > 100.0f) hum_pct = 100.0f;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_latest.temperature = temp_c;
    s_latest.humidity    = hum_pct;
    s_latest.light       = (int)light;
    s_latest.co2         = 0;
    s_latest.temp_valid  = true;
    s_latest.co2_valid   = false;
    s_last_success_us    = esp_timer_get_time();
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "T=%.2f°C  RH=%.1f%%  light=%d", temp_c, hum_pct, (int)light);
}

static void poll_loop(int tx_gpio, int rx_gpio)
{
    uart_config_t cfg = {
        .baud_rate  = SENSOR_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(SENSOR_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(SENSOR_UART, tx_gpio, rx_gpio,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(SENSOR_UART, 256, 0, 0, NULL, 0));

    ESP_LOGI(TAG, "Polling — TX=GPIO%d RX=GPIO%d @ %d baud",
             tx_gpio, rx_gpio, SENSOR_BAUD);

    uint8_t buf[RESP_LEN];
    while (1) {
        uart_flush_input(SENSOR_UART);
        uart_write_bytes(SENSOR_UART, (const char *)CMD, CMD_LEN);

        int total = 0;
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(RESP_TIMEOUT_MS);
        while (total < RESP_LEN && xTaskGetTickCount() < deadline) {
            int got = uart_read_bytes(SENSOR_UART, buf + total,
                                      RESP_LEN - total, pdMS_TO_TICKS(50));
            if (got > 0) total += got;
        }

        if (total < RESP_LEN) {
            ESP_LOGW(TAG, "Short response: got %d of %d bytes", total, RESP_LEN);
        } else if (buf[0] != 0x55 || buf[1] != 0xAA || buf[2] != 0x0B || buf[3] != 0x10) {
            ESP_LOGW(TAG, "Bad header: %02X %02X %02X %02X",
                     buf[0], buf[1], buf[2], buf[3]);
        } else if (!verify_checksum(buf, RESP_LEN)) {
            ESP_LOGW(TAG, "Checksum fail (got 0x%02X)", buf[RESP_LEN - 1]);
        } else {
            parse_response(buf);
        }

        mark_first_read_attempt_done();
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

static void sensor_task(void *arg)
{
    const growhub_config_t *cfg = config_get();
    ESP_LOGI(TAG, "Using configured TX=GPIO%d RX=GPIO%d",
             cfg->pin_sensor_uart_tx, cfg->pin_sensor_uart_rx);
    poll_loop(cfg->pin_sensor_uart_tx, cfg->pin_sensor_uart_rx);
}

void sensors_early_gpio_init(void)
{
    /* Config is not loaded yet, so use the verified default sensor UART pair. */
    const int8_t drive_high[] = {DEFAULT_SENSOR_UART_TX_PIN, DEFAULT_SENSOR_UART_RX_PIN};
    for (int i = 0; i < (int)sizeof(drive_high); i++) {
        gpio_set_direction(drive_high[i], GPIO_MODE_OUTPUT);
        gpio_set_level(drive_high[i], 1);
    }
}

void sensors_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_first_read_attempt_done = false;
    s_last_success_us = 0;
    memset(&s_latest, 0, sizeof(s_latest));
    /* UART driver installed by sensor_task. */
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}

sensor_reading_t sensors_read(void)
{
    sensor_reading_t copy;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    copy = s_latest;
    xSemaphoreGive(s_mutex);
    return copy;
}

bool sensors_first_read_attempt_done(void)
{
    return s_first_read_attempt_done;
}

bool sensors_temp_rh_available(int max_age_s)
{
    if (max_age_s <= 0) return false;

    bool valid = false;
    int64_t last_success_us = 0;

    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    valid = s_latest.temp_valid;
    last_success_us = s_last_success_us;
    if (s_mutex) xSemaphoreGive(s_mutex);

    if (!valid || last_success_us <= 0) return false;

    int64_t age_us = esp_timer_get_time() - last_success_us;
    return age_us <= ((int64_t)max_age_s * 1000000LL);
}
