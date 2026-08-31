#include "battery_monitor.h"
#include "board_config.h"
#include "display_adc.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG="BATTERY";

static int percent_from_v(float v)
{
    typedef struct
    {
        float v;
        int p;
    } point_t;

    static const point_t t[] =
    {
        /*
         * Praktinė 1S LiPo indikacijos kreivė.
         * Pakoreguota pagal realų matavimą, kad apie 4.04 V
         * vartotojo ekrane būtų rodoma maždaug 84 %.
         */
        {4.20f, 100},
        {4.10f,  93},
        {4.00f,  84},
        {3.90f,  73},
        {3.80f,  58},
        {3.70f,  42},
        {3.60f,  27},
        {3.50f,  16},
        {3.40f,   8},
        {3.30f,   3},
        {3.20f,   0},
    };

    if (v >= 4.20f) {
        return 100;
    }

    if (v <= 3.20f) {
        return 0;
    }

    for (int i = 0; i < 10; i++) {
        if (v <= t[i].v && v >= t[i + 1].v) {
            float x =
                (v - t[i + 1].v) /
                (t[i].v - t[i + 1].v);

            return (int)(
                t[i + 1].p +
                x * (t[i].p - t[i + 1].p) +
                0.5f
            );
        }
    }

    return 0;
}

esp_err_t battery_monitor_init(void)
{
    return display_adc_init();
}

esp_err_t battery_monitor_read(float *voltage_v, int *percent)
{
    if (voltage_v == NULL || percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float adc_voltage_v = 0.0f;
    ESP_RETURN_ON_ERROR(
        display_adc_read_voltage(BATTERY_ADC_CHANNEL, 32, &adc_voltage_v),
        TAG,
        "Battery ADC read failed"
    );

    float battery_voltage_v =
        adc_voltage_v *
        BATTERY_DIVIDER_RATIO;

    *voltage_v = battery_voltage_v;
    *percent = percent_from_v(battery_voltage_v);

    ESP_LOGI(
        TAG,
        "BAT ADC=%.3f V | Battery=%.3f V | %d%% | divider=%.3f",
        adc_voltage_v,
        battery_voltage_v,
        *percent,
        BATTERY_DIVIDER_RATIO
    );

    return ESP_OK;
}
