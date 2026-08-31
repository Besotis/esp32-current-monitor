#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

/* Shared ADC1 owner for the DISPLAY board.  Both the battery divider and the
 * NTC live on ADC1, so the unit must be created only once. */
esp_err_t display_adc_init(void);
esp_err_t display_adc_read_voltage(adc_channel_t channel, int sample_count, float *voltage_v);
