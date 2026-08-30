#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t mode_button_init(void);
bool mode_button_pressed(void);
