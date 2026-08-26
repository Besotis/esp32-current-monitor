#pragma once

#include <stdint.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"

/*
 * Seeed Studio XIAO ESP32-S3
 *
 * A / SENSOR:
 *   L1 -> D0 / GPIO1 / ADC1_CH0
 *   L2 -> D1 / GPIO2 / ADC1_CH1
 *   L3 -> D2 / GPIO3 / ADC1_CH2
 *
 * Kol L2/L3 SCT fiziškai neprijungti, paliekame juos išjungtus,
 * kad plaukiojantys ADC įėjimai nerodytų netikrų srovių.
 */
#define SCT_L1_ENABLED                 1
#define SCT_L2_ENABLED                 1
#define SCT_L3_ENABLED                 1

#define PIN_SCT_L1_ADC                 1
#define PIN_SCT_L2_ADC                 2
#define PIN_SCT_L3_ADC                 3

#define SCT_L1_ADC_CHANNEL             ADC_CHANNEL_0
#define SCT_L2_ADC_CHANNEL             ADC_CHANNEL_1
#define SCT_L3_ADC_CHANNEL             ADC_CHANNEL_2

/* B / DISPLAY */
#define PIN_BATTERY_ADC                2
#define PIN_MODE_BUTTON                3
#define PIN_DISPLAY_SDA                5
#define PIN_DISPLAY_SCL                6

#define DISPLAY_I2C_FREQ_HZ            400000
#define DISPLAY_I2C_ADDR_PRIMARY       0x3C
#define DISPLAY_I2C_ADDR_SECONDARY     0x3D
#define DISPLAY_NO_SIGNAL_MS           3000

#define BATTERY_DIVIDER_RATIO          2.010f
#define BATTERY_ADC_CHANNEL            ADC_CHANNEL_1

/*
 * SCT-013-030: 30 A / 1 V RMS.
 *
 * L1 jau sukalibruotas pagal dabartinius testus.
 * L2/L3 pradžioje naudojame tą patį faktorių, bet galutiniame
 * variante kiekvieną SCT bus galima sukalibruoti atskirai.
 */
#define SCT_CURRENT_PER_VOLT_A         30.0f
#define SCT_L1_CALIBRATION_FACTOR      0.900f
#define SCT_L2_CALIBRATION_FACTOR      0.900f
#define SCT_L3_CALIBRATION_FACTOR      0.900f

#define SCT_NOISE_RMS_V                0.0045f
#define SCT_ZERO_DEADBAND_A            0.05f

#define NOMINAL_PHASE_VOLTAGE_V        230.0f

/* ESP-NOW */
#define ESPNOW_WIFI_CHANNEL            1

static const uint8_t DEVICE_A_MAC[6] = {
    0xAC, 0xA7, 0x04, 0x2C, 0x59, 0x64
};

static const uint8_t DEVICE_B_MAC[6] = {
    0x1C, 0xDB, 0xD4, 0x76, 0x2A, 0x74
};
