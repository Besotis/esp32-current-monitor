#pragma once

#include "esp_err.h"

typedef enum {
    MODE_BUTTON_EVENT_NONE = 0,
    MODE_BUTTON_EVENT_SHORT,
    MODE_BUTTON_EVENT_DOUBLE,
    MODE_BUTTON_EVENT_LONG,
} mode_button_event_t;

esp_err_t mode_button_init(void);
mode_button_event_t mode_button_get_event(void);
