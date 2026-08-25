#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define CH1115_WIDTH   128
#define CH1115_HEIGHT   64
#define CH1115_BUFSIZE (CH1115_WIDTH * CH1115_HEIGHT / 8)

esp_err_t ch1115_init(void);
esp_err_t ch1115_flush(void);
void ch1115_clear(void);

void ch1115_draw_pixel(int x, int y, bool on);
void ch1115_draw_char(int x, int y, char c, int scale);
void ch1115_draw_text(int x, int y, const char *text, int scale);
