#include "temperature_monitor.h"

#include <math.h>

#include "board_config.h"
#include "display_adc.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "TEMPERATURE";

esp_err_t temperature_monitor_init(void)
{
    return display_adc_init();
}

esp_err_t temperature_monitor_read(float *temperature_c)
{
    if (temperature_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float adc_voltage_v = 0.0f;
    ESP_RETURN_ON_ERROR(
        display_adc_read_voltage(TEMPERATURE_ADC_CHANNEL,
                                 TEMPERATURE_ADC_SAMPLE_COUNT,
                                 &adc_voltage_v),
        TAG,
        "NTC ADC read failed"
    );

    /* Wiring:
     * 3V3 -> fixed resistor -> GPIO1/ADC -> 100k NTC -> GND
     *
     * Vadc = Vcc * Rntc / (Rfixed + Rntc)
     * Rntc = Rfixed * Vadc / (Vcc - Vadc)
     */
    if (adc_voltage_v <= 0.0f || adc_voltage_v >= NTC_SUPPLY_VOLTAGE_V) {
        ESP_LOGW(TAG, "NTC ADC voltage out of range: %.3f V", adc_voltage_v);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const float r_ntc = NTC_FIXED_RESISTOR_OHM * adc_voltage_v /
                        (NTC_SUPPLY_VOLTAGE_V - adc_voltage_v);

    if (!(r_ntc > 0.0f)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const float t0_k = NTC_NOMINAL_TEMP_C + 273.15f;
    const float inv_t = (1.0f / t0_k) +
                        (1.0f / NTC_BETA_K) *
                        logf(r_ntc / NTC_NOMINAL_RESISTANCE_OHM);
    const float temp_c = (1.0f / inv_t) - 273.15f + NTC_TEMP_OFFSET_C;

    if (!isfinite(temp_c)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *temperature_c = temp_c;
    ESP_LOGI(TAG, "NTC ADC=%.3f V | R=%.0f ohm | Temperature=%.1f C",
             adc_voltage_v, r_ntc, temp_c);
    return ESP_OK;
}
