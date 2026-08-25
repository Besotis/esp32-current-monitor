#pragma once
#include "esp_err.h"

typedef struct {
    float current_rms_a;
    float sensor_voltage_rms_v;
} current_measurement_t;

esp_err_t current_sensor_init(void);
esp_err_t current_sensor_read(current_measurement_t *measurement);
