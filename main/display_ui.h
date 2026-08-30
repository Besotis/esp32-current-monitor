#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    DISPLAY_MODE_GRID = 0,       /* ThreePhaseWINDOW */
    DISPLAY_MODE_GENERATOR = 1,  /* SinglePhaseWINDOW / total load */
} display_mode_t;

typedef struct {
    display_mode_t mode;
    bool online;
    unsigned uptime_seconds;
    int battery_percent;
    int signal_percent;
    int rssi_dbm;
    float l1_a;
    float l2_a;
    float l3_a;
} display_ui_state_t;

esp_err_t display_ui_init(void);
esp_err_t display_ui_render(const display_ui_state_t *state);
