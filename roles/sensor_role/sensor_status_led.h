#pragma once

#include "esp_err.h"

esp_err_t sensor_status_led_init(void);
void sensor_status_led_espnow_activity(void);
