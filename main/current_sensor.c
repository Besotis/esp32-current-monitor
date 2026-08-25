#include "current_sensor.h"
#include "board_config.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define ADC_SAMPLE_RATE_HZ        8000
#define RMS_WINDOW_MS             200
#define TARGET_SAMPLES            ((ADC_SAMPLE_RATE_HZ * RMS_WINDOW_MS) / 1000)
#define ADC_READ_BUFFER_SIZE      512

static const char *TAG = "CURRENT_SENSOR";

static adc_continuous_handle_t adc_handle;
static adc_cali_handle_t adc_cali_handle;
static bool adc_calibration_enabled = false;

static float current_history[3] = {0.0f, 0.0f, 0.0f};
static int current_history_count = 0;
static int current_history_index = 0;

static float median3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { float t = b; b = c; c = t; }
    if (a > b) { float t = a; a = b; b = t; }
    return b;
}

static esp_err_t init_adc_calibration(void)
{
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    esp_err_t err = adc_cali_create_scheme_curve_fitting(
        &cali_config,
        &adc_cali_handle
    );

    if (err == ESP_OK) {
        adc_calibration_enabled = true;
        ESP_LOGI(TAG, "ADC calibration: curve fitting enabled");
        return ESP_OK;
    }

    adc_calibration_enabled = false;
    ESP_LOGW(TAG, "ADC calibration unavailable: %s", esp_err_to_name(err));
    return err;
}

esp_err_t current_sensor_init(void)
{
    ESP_LOGI(TAG, "Initializing ADC continuous mode on GPIO%d",
             PIN_CURRENT_SENSOR_ADC);
    ESP_LOGI(TAG, "Sample rate: %d Hz, RMS window: %d ms, target samples: %d",
             ADC_SAMPLE_RATE_HZ, RMS_WINDOW_MS, TARGET_SAMPLES);

    adc_continuous_handle_cfg_t handle_config = {
        .max_store_buf_size = 4096,
        .conv_frame_size = ADC_READ_BUFFER_SIZE,
    };

    ESP_RETURN_ON_ERROR(
        adc_continuous_new_handle(&handle_config, &adc_handle),
        TAG,
        "Failed to create ADC continuous handle"
    );

    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = ADC_CHANNEL_0,
        .unit = ADC_UNIT_1,
        .bit_width = ADC_BITWIDTH_12,
    };

    adc_continuous_config_t adc_config = {
        .sample_freq_hz = ADC_SAMPLE_RATE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .pattern_num = 1,
        .adc_pattern = &adc_pattern,
    };

    ESP_RETURN_ON_ERROR(
        adc_continuous_config(adc_handle, &adc_config),
        TAG,
        "Failed to configure ADC continuous mode"
    );

    (void)init_adc_calibration();

    ESP_RETURN_ON_ERROR(
        adc_continuous_start(adc_handle),
        TAG,
        "Failed to start ADC continuous mode"
    );

    ESP_LOGI(TAG, "ADC continuous mode started");
    return ESP_OK;
}

esp_err_t current_sensor_read(current_measurement_t *measurement)
{
    if (measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[ADC_READ_BUFFER_SIZE];
    uint32_t bytes_read = 0;

    int sample_count = 0;
    int raw_min = 4095;
    int raw_max = 0;

    double voltage_sum_mv = 0.0;
    double voltage_sq_sum_mv = 0.0;

    while (sample_count < TARGET_SAMPLES) {
        esp_err_t err = adc_continuous_read(
            adc_handle,
            buffer,
            sizeof(buffer),
            &bytes_read,
            1000
        );

        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }

        if (err != ESP_OK) {
            return err;
        }

        for (uint32_t i = 0;
             i + sizeof(adc_digi_output_data_t) <= bytes_read;
             i += sizeof(adc_digi_output_data_t)) {

            adc_digi_output_data_t *sample =
                (adc_digi_output_data_t *)&buffer[i];

            uint32_t raw = sample->type2.data;
            uint32_t channel = sample->type2.channel;

            if (channel != ADC_CHANNEL_0) {
                continue;
            }

            if ((int)raw < raw_min) raw_min = (int)raw;
            if ((int)raw > raw_max) raw_max = (int)raw;

            int voltage_mv = 0;

            if (adc_calibration_enabled) {
                esp_err_t cal_err = adc_cali_raw_to_voltage(
                    adc_cali_handle,
                    (int)raw,
                    &voltage_mv
                );

                if (cal_err != ESP_OK) {
                    return cal_err;
                }
            } else {
                voltage_mv = (int)((raw * 3300UL) / 4095UL);
            }

            voltage_sum_mv += (double)voltage_mv;
            voltage_sq_sum_mv +=
                (double)voltage_mv * (double)voltage_mv;

            sample_count++;

            if (sample_count >= TARGET_SAMPLES) {
                break;
            }
        }
    }

    if (sample_count == 0) {
        return ESP_FAIL;
    }

    const double mean_mv =
        voltage_sum_mv / (double)sample_count;

    double variance_mv2 =
        (voltage_sq_sum_mv / (double)sample_count) -
        (mean_mv * mean_mv);

    if (variance_mv2 < 0.0) {
        variance_mv2 = 0.0;
    }

    const float sensor_voltage_rms_v =
        (float)(sqrt(variance_mv2) / 1000.0);

    float corrected_voltage_rms_v = 0.0f;

    if (sensor_voltage_rms_v > SCT_NOISE_RMS_V) {
        const float corrected_sq =
            (sensor_voltage_rms_v * sensor_voltage_rms_v) -
            (SCT_NOISE_RMS_V * SCT_NOISE_RMS_V);

        if (corrected_sq > 0.0f) {
            corrected_voltage_rms_v = sqrtf(corrected_sq);
        }
    }

    float current_rms_a =
        corrected_voltage_rms_v *
        SCT_CURRENT_PER_VOLT_A *
        SCT_CALIBRATION_FACTOR;

    if (current_rms_a < SCT_ZERO_DEADBAND_A) {
        current_rms_a = 0.0f;
    }

    current_history[current_history_index] = current_rms_a;
    current_history_index = (current_history_index + 1) % 3;

    if (current_history_count < 3) {
        current_history_count++;
    }

    float filtered_current_a = current_rms_a;

    if (current_history_count == 3) {
        filtered_current_a = median3(
            current_history[0],
            current_history[1],
            current_history[2]
        );
    }

    measurement->current_rms_a = filtered_current_a;
    measurement->sensor_voltage_rms_v = sensor_voltage_rms_v;
    measurement->offset_voltage_v = (float)(mean_mv / 1000.0);
    measurement->raw_min = raw_min;
    measurement->raw_max = raw_max;
    measurement->sample_count = sample_count;

    return ESP_OK;
}
