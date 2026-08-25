#pragma once
#include <stdint.h>
#include "esp_adc/adc_oneshot.h"

#define PIN_CURRENT_SENSOR_ADC       1
#define PIN_BATTERY_ADC              2
#define PIN_MODE_BUTTON              3
#define PIN_DISPLAY_SDA              5
#define PIN_DISPLAY_SCL              6

#define DISPLAY_I2C_FREQ_HZ          400000
#define DISPLAY_I2C_ADDR_PRIMARY     0x3C
#define DISPLAY_I2C_ADDR_SECONDARY   0x3D
#define DISPLAY_NO_SIGNAL_MS         3000

#define BATTERY_DIVIDER_RATIO        2.0f
#define BATTERY_ADC_CHANNEL          ADC_CHANNEL_1

#define SCT_CURRENT_PER_VOLT_A       30.0f
#define SCT_CALIBRATION_FACTOR       0.922f
#define SCT_NOISE_RMS_V              0.0045f
#define SCT_ZERO_DEADBAND_A          0.05f

#define NOMINAL_PHASE_VOLTAGE_V      230.0f
#define ESPNOW_WIFI_CHANNEL          1

static const uint8_t DEVICE_A_MAC[6] = {0xAC,0xA7,0x04,0x2C,0x59,0x64};
static const uint8_t DEVICE_B_MAC[6] = {0x1C,0xDB,0xD4,0x76,0x2A,0x74};
