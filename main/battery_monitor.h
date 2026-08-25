#pragma once
#include "esp_err.h"
esp_err_t battery_monitor_init(void);
esp_err_t battery_monitor_read(float *voltage_v, int *percent);
