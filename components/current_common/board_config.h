#pragma once

#include <stdint.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"

/*
 * Seeed Studio XIAO ESP32-S3
 *
 * A / SENSOR (final calibrated wiring):
 *   L1 -> D2 / GPIO3 / ADC1_CH2
 *   L2 -> D3 / GPIO4 / ADC1_CH3
 *   L3 -> D8 / GPIO7 / ADC1_CH6
 */
#define SCT_L1_ENABLED                 1
#define SCT_L2_ENABLED                 1
#define SCT_L3_ENABLED                 1

#define PIN_SCT_L1_ADC                 3
#define PIN_SCT_L2_ADC                 4
#define PIN_SCT_L3_ADC                 7

#define SCT_L1_ADC_CHANNEL             ADC_CHANNEL_2
#define SCT_L2_ADC_CHANNEL             ADC_CHANNEL_3
#define SCT_L3_ADC_CHANNEL             ADC_CHANNEL_6

/*
 * B / DISPLAY board
 *
 * Existing inputs:
 *   Battery ADC -> D1 / GPIO2
 *   Mode button -> D2 / GPIO3, button to GND, internal pull-up
 *
 * GMT130-V1.0 / ST7789 240x240 (7-pin):
 *   VCC      -> XIAO 3V3
 *   GND      -> XIAO GND
 *   SCK      -> D5  / GPIO6
 *   SDA/MOSI -> D4  / GPIO5
 *   RES/RST  -> D3  / GPIO4
 *   DC       -> D10 / GPIO9
 *   BLK      -> D9  / GPIO8
 *
 * The module has no exposed CS pin, so the driver uses CS=-1.
 */
#define PIN_TEMPERATURE_ADC            1
#define PIN_BATTERY_ADC                2
#define PIN_MODE_BUTTON                3

#define PIN_DISPLAY_MOSI               5
#define PIN_DISPLAY_SCLK               6
#define PIN_DISPLAY_RST                4
#define PIN_DISPLAY_DC                 9
#define PIN_DISPLAY_BLK                8

#define DISPLAY_SPI_FREQ_HZ            (40 * 1000 * 1000)
#define DISPLAY_SPI_MODE               3
#define DISPLAY_WIDTH                  240
#define DISPLAY_HEIGHT                 240
#define DISPLAY_X_GAP                  80
#define DISPLAY_Y_GAP                  0
#define DISPLAY_LVGL_BUFFER_LINES      40
#define DISPLAY_STARTUP_BRIGHTNESS_PCT 50
#define DISPLAY_STARTUP_BLANK_MS       100
#define DISPLAY_NO_SIGNAL_MS           3000

#define BATTERY_DIVIDER_RATIO          1.999f
#define BATTERY_ADC_CHANNEL            ADC_CHANNEL_1

/* 100k NTC on GPIO1 / ADC1_CH0.
 * Wiring: 3V3 -> fixed resistor -> GPIO1 -> NTC -> GND.
 * A 100 nF capacitor from GPIO1 to GND is recommended. */
#define TEMPERATURE_ADC_CHANNEL        ADC_CHANNEL_0
#define TEMPERATURE_ADC_SAMPLE_COUNT   96
#define NTC_SUPPLY_VOLTAGE_V            3.300f
#define NTC_FIXED_RESISTOR_OHM     100200.0f
#define NTC_NOMINAL_RESISTANCE_OHM 100000.0f
#define NTC_NOMINAL_TEMP_C              25.0f
#define NTC_BETA_K                    3950.0f
#define NTC_TEMP_OFFSET_C                0.0f

/* SCT-013-030: 30 A / 1 V RMS. */
#define SCT_CURRENT_PER_VOLT_A         30.0f
#define SCT_L1_CALIBRATION_FACTOR      0.990f
#define SCT_L2_CALIBRATION_FACTOR      0.990f
#define SCT_L3_CALIBRATION_FACTOR      0.990f

#define SCT_L1_NOISE_RMS_V             0.00403f
#define SCT_L2_NOISE_RMS_V             0.00413f
#define SCT_L3_NOISE_RMS_V             0.00418f
#define SCT_ZERO_DEADBAND_A            0.06f

#define NOMINAL_PHASE_VOLTAGE_V        230.0f

/* ESP-NOW */
#define ESPNOW_WIFI_CHANNEL            1

static const uint8_t DEVICE_A_MAC[6] = {
    0xAC, 0xA7, 0x04, 0x2C, 0x59, 0x64
};

static const uint8_t DEVICE_B_MAC[6] = {
    0x1C, 0xDB, 0xD4, 0x76, 0x2A, 0x74
};
