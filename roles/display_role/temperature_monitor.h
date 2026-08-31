#pragma once

#include "esp_err.h"

esp_err_t temperature_monitor_init(void);
esp_err_t temperature_monitor_read(float *temperature_c);
