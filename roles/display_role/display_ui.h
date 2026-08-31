#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    DISPLAY_VIEW_THREE_PHASE = 0,
    DISPLAY_VIEW_SINGLE_PHASE,
    DISPLAY_VIEW_L1L2L3_CHART,
    DISPLAY_VIEW_FULL_LOAD_CHART,
} display_view_t;

typedef struct {
    display_view_t view;
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
esp_err_t display_ui_chart_add_sample(float l1_a, float l2_a, float l3_a, float total_a, bool valid);
