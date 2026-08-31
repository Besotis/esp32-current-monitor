#include "display_adc.h"

#include <stdbool.h>

#include "board_config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "DISPLAY_ADC";

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_battery_cali = NULL;
static adc_cali_handle_t s_temperature_cali = NULL;
static bool s_battery_cali_ok = false;
static bool s_temperature_cali_ok = false;
static bool s_initialized = false;

static esp_err_t create_channel_calibration(adc_channel_t channel,
                                            adc_cali_handle_t *handle,
                                            bool *ok)
{
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cfg, handle);
    if (err == ESP_OK) {
        *ok = true;
        return ESP_OK;
    }

    *ok = false;
    ESP_LOGW(TAG, "ADC calibration unavailable for channel %d: %s",
             (int)channel, esp_err_to_name(err));
    return ESP_OK;
}

esp_err_t display_adc_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG,
                        "ADC1 init failed");

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &chan_cfg),
                        TAG, "Battery ADC channel config failed");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, TEMPERATURE_ADC_CHANNEL, &chan_cfg),
                        TAG, "Temperature ADC channel config failed");

    ESP_RETURN_ON_ERROR(create_channel_calibration(BATTERY_ADC_CHANNEL,
                                                   &s_battery_cali,
                                                   &s_battery_cali_ok),
                        TAG, "Battery calibration init failed");
    ESP_RETURN_ON_ERROR(create_channel_calibration(TEMPERATURE_ADC_CHANNEL,
                                                   &s_temperature_cali,
                                                   &s_temperature_cali_ok),
                        TAG, "Temperature calibration init failed");

    s_initialized = true;
    ESP_LOGI(TAG, "Shared ADC1 ready: battery GPIO%d/CH%d, NTC GPIO%d/CH%d",
             PIN_BATTERY_ADC, (int)BATTERY_ADC_CHANNEL,
             PIN_TEMPERATURE_ADC, (int)TEMPERATURE_ADC_CHANNEL);
    return ESP_OK;
}

esp_err_t display_adc_read_voltage(adc_channel_t channel, int sample_count, float *voltage_v)
{
    if (voltage_v == NULL || sample_count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_adc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    adc_cali_handle_t cali = NULL;
    bool cali_ok = false;
    if (channel == BATTERY_ADC_CHANNEL) {
        cali = s_battery_cali;
        cali_ok = s_battery_cali_ok;
    } else if (channel == TEMPERATURE_ADC_CHANNEL) {
        cali = s_temperature_cali;
        cali_ok = s_temperature_cali_ok;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t sum_mv = 0;
    for (int i = 0; i < sample_count; ++i) {
        int raw = 0;
        int mv = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, channel, &raw), TAG,
                            "ADC read failed");

        if (cali_ok) {
            ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(cali, raw, &mv), TAG,
                                "ADC calibration failed");
        } else {
            /* Fallback only.  Curve-fitting calibration is expected on ESP32-S3. */
            mv = (raw * 3300) / 4095;
        }
        sum_mv += mv;
    }

    *voltage_v = ((float)sum_mv / (float)sample_count) / 1000.0f;
    return ESP_OK;
}
