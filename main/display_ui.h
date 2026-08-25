#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef enum { DISPLAY_MODE_GRID=0, DISPLAY_MODE_GENERATOR=1 } display_mode_t;
typedef struct { display_mode_t mode; bool online; unsigned uptime_seconds; int battery_percent; int signal_percent; float l1_a,l2_a,l3_a; } display_ui_state_t;
esp_err_t display_ui_init(void);
esp_err_t display_ui_render(const display_ui_state_t *state);
