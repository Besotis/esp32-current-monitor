#pragma once

#include <stdbool.h>
#include "esp_err.h"

/*
 * ST7789 240x240 display driver for Seeed Studio XIAO ESP32-S3.
 * Backlight is PWM controlled and intentionally kept OFF during boot/UI init.
 */
esp_err_t display_st7789_early_backlight_off(void);
esp_err_t display_st7789_init(void);
esp_err_t display_st7789_backlight_set(int percent);
esp_err_t display_st7789_panel_set_visible(bool visible);
