#include "current_sensor.h"
#include "board_config.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define PER_CHANNEL_SAMPLE_RATE_HZ   8000
#define RMS_WINDOW_MS                200
#define TARGET_SAMPLES_PER_CHANNEL   ((PER_CHANNEL_SAMPLE_RATE_HZ * RMS_WINDOW_MS) / 1000)
#define ADC_READ_BUFFER_SIZE         1024
#define PHASE_COUNT                  3

static const char *TAG = "CURRENT_SENSOR";

typedef struct
{
    bool enabled;
    adc_channel_t channel;
    int gpio;
    float calibration_factor;

    adc_cali_handle_t cali_handle;
    bool cali_enabled;

    float history[3];
    int history_count;
    int history_index;
} phase_cfg_t;

typedef struct
{
    int sample_count;
    int raw_min;
    int raw_max;
    double voltage_sum_mv;
    double voltage_sq_sum_mv;
} phase_acc_t;

static adc_continuous_handle_t s_adc_handle;

static phase_cfg_t s_phase[PHASE_COUNT] = {
    {
        .enabled = SCT_L1_ENABLED,
        .channel = SCT_L1_ADC_CHANNEL,
        .gpio = PIN_SCT_L1_ADC,
        .calibration_factor = SCT_L1_CALIBRATION_FACTOR,
    },
    {
        .enabled = SCT_L2_ENABLED,
        .channel = SCT_L2_ADC_CHANNEL,
        .gpio = PIN_SCT_L2_ADC,
        .calibration_factor = SCT_L2_CALIBRATION_FACTOR,
    },
    {
        .enabled = SCT_L3_ENABLED,
        .channel = SCT_L3_ADC_CHANNEL,
        .gpio = PIN_SCT_L3_ADC,
        .calibration_factor = SCT_L3_CALIBRATION_FACTOR,
    },
};

static float median3(float a, float b, float c)
{
    if (a > b) {
        float t = a;
        a = b;
        b = t;
    }

    if (b > c) {
        float t = b;
        b = c;
        c = t;
    }

    if (a > b) {
        float t = a;
        a = b;
        b = t;
    }

    return b;
}

static int phase_index_from_channel(uint32_t channel)
{
    for (int i = 0; i < PHASE_COUNT; ++i) {
        if (s_phase[i].enabled &&
            channel == (uint32_t)s_phase[i].channel) {
            return i;
        }
    }

    return -1;
}

static esp_err_t init_phase_calibration(phase_cfg_t *phase)
{
    adc_cali_curve_fitting_config_t config = {
        .unit_id = ADC_UNIT_1,
        .chan = phase->channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    esp_err_t err = adc_cali_create_scheme_curve_fitting(
        &config,
        &phase->cali_handle
    );

    if (err == ESP_OK) {
        phase->cali_enabled = true;

        ESP_LOGI(
            TAG,
            "GPIO%d / ADC1_CH%d calibration enabled",
            phase->gpio,
            (int)phase->channel
        );
    } else {
        phase->cali_enabled = false;

        ESP_LOGW(
            TAG,
            "GPIO%d ADC calibration unavailable: %s",
            phase->gpio,
            esp_err_to_name(err)
        );
    }

    return err;
}

esp_err_t current_sensor_init(void)
{
    int enabled_count = 0;

    for (int i = 0; i < PHASE_COUNT; ++i) {
        if (s_phase[i].enabled) {
            enabled_count++;
        }
    }

    if (enabled_count == 0) {
        ESP_LOGE(TAG, "No SCT channels enabled");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(
        TAG,
        "3-phase ADC prepared: L1=%s L2=%s L3=%s",
        SCT_L1_ENABLED ? "ON" : "OFF",
        SCT_L2_ENABLED ? "ON" : "OFF",
        SCT_L3_ENABLED ? "ON" : "OFF"
    );

    ESP_LOGI(
        TAG,
        "Per-channel sample rate: %d Hz, RMS window: %d ms, samples/channel: %d",
        PER_CHANNEL_SAMPLE_RATE_HZ,
        RMS_WINDOW_MS,
        TARGET_SAMPLES_PER_CHANNEL
    );

    adc_continuous_handle_cfg_t handle_config = {
        .max_store_buf_size = 8192,
        .conv_frame_size = ADC_READ_BUFFER_SIZE,
    };

    ESP_RETURN_ON_ERROR(
        adc_continuous_new_handle(
            &handle_config,
            &s_adc_handle
        ),
        TAG,
        "Failed to create ADC continuous handle"
    );

    adc_digi_pattern_config_t pattern[PHASE_COUNT];
    int pattern_count = 0;

    for (int i = 0; i < PHASE_COUNT; ++i) {
        if (!s_phase[i].enabled) {
            continue;
        }

        pattern[pattern_count] = (adc_digi_pattern_config_t) {
            .atten = ADC_ATTEN_DB_12,
            .channel = s_phase[i].channel,
            .unit = ADC_UNIT_1,
            .bit_width = ADC_BITWIDTH_12,
        };

        pattern_count++;
    }

    adc_continuous_config_t adc_config = {
        /*
         * ESP-IDF continuous sample_freq_hz yra bendras konversijų dažnis.
         * Todėl 3 kanalams naudojame 3 * 8 kHz, kad kiekvienas gautų ~8 kHz.
         */
        .sample_freq_hz =
            PER_CHANNEL_SAMPLE_RATE_HZ * pattern_count,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .pattern_num = pattern_count,
        .adc_pattern = pattern,
    };

    ESP_RETURN_ON_ERROR(
        adc_continuous_config(
            s_adc_handle,
            &adc_config
        ),
        TAG,
        "Failed to configure ADC continuous mode"
    );

    for (int i = 0; i < PHASE_COUNT; ++i) {
        if (s_phase[i].enabled) {
            (void)init_phase_calibration(&s_phase[i]);
        }
    }

    ESP_RETURN_ON_ERROR(
        adc_continuous_start(s_adc_handle),
        TAG,
        "Failed to start ADC"
    );

    ESP_LOGI(TAG, "ADC continuous mode started");

    return ESP_OK;
}

static bool all_enabled_channels_complete(
    const phase_acc_t acc[PHASE_COUNT]
)
{
    for (int i = 0; i < PHASE_COUNT; ++i) {
        if (s_phase[i].enabled &&
            acc[i].sample_count < TARGET_SAMPLES_PER_CHANNEL) {
            return false;
        }
    }

    return true;
}

static esp_err_t raw_to_mv(
    phase_cfg_t *phase,
    int raw,
    int *voltage_mv
)
{
    if (phase->cali_enabled) {
        return adc_cali_raw_to_voltage(
            phase->cali_handle,
            raw,
            voltage_mv
        );
    }

    *voltage_mv =
        (raw * 3300) / 4095;

    return ESP_OK;
}

static void finalize_phase(
    int phase_index,
    const phase_acc_t *acc,
    current_phase_measurement_t *out
)
{
    if (!s_phase[phase_index].enabled ||
        acc->sample_count == 0) {

        memset(out, 0, sizeof(*out));
        return;
    }

    const double mean_mv =
        acc->voltage_sum_mv /
        (double)acc->sample_count;

    double variance_mv2 =
        (acc->voltage_sq_sum_mv /
         (double)acc->sample_count) -
        (mean_mv * mean_mv);

    if (variance_mv2 < 0.0) {
        variance_mv2 = 0.0;
    }

    const float sensor_voltage_rms_v =
        (float)(sqrt(variance_mv2) / 1000.0);

    float corrected_voltage_rms_v = 0.0f;

    if (sensor_voltage_rms_v > SCT_NOISE_RMS_V) {
        float corrected_sq =
            sensor_voltage_rms_v *
            sensor_voltage_rms_v -
            SCT_NOISE_RMS_V *
            SCT_NOISE_RMS_V;

        if (corrected_sq > 0.0f) {
            corrected_voltage_rms_v =
                sqrtf(corrected_sq);
        }
    }

    float current_rms_a =
        corrected_voltage_rms_v *
        SCT_CURRENT_PER_VOLT_A *
        s_phase[phase_index].calibration_factor;

    if (current_rms_a < SCT_ZERO_DEADBAND_A) {
        current_rms_a = 0.0f;
    }

    phase_cfg_t *phase =
        &s_phase[phase_index];

    phase->history[phase->history_index] =
        current_rms_a;

    phase->history_index =
        (phase->history_index + 1) % 3;

    if (phase->history_count < 3) {
        phase->history_count++;
    }

    float filtered_current_a =
        current_rms_a;

    if (phase->history_count == 3) {
        filtered_current_a = median3(
            phase->history[0],
            phase->history[1],
            phase->history[2]
        );
    }

    out->current_rms_a =
        filtered_current_a;

    out->sensor_voltage_rms_v =
        sensor_voltage_rms_v;

    out->offset_voltage_v =
        (float)(mean_mv / 1000.0);

    out->raw_min =
        acc->raw_min;

    out->raw_max =
        acc->raw_max;

    out->sample_count =
        acc->sample_count;
}

esp_err_t current_sensor_read(
    current_measurement_t *measurement
)
{
    if (measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[ADC_READ_BUFFER_SIZE];
    uint32_t bytes_read = 0;

    phase_acc_t acc[PHASE_COUNT];

    for (int i = 0; i < PHASE_COUNT; ++i) {
        acc[i] = (phase_acc_t) {
            .sample_count = 0,
            .raw_min = 4095,
            .raw_max = 0,
            .voltage_sum_mv = 0.0,
            .voltage_sq_sum_mv = 0.0,
        };
    }

    while (!all_enabled_channels_complete(acc)) {
        esp_err_t err = adc_continuous_read(
            s_adc_handle,
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

            int phase_index =
                phase_index_from_channel(
                    sample->type2.channel
                );

            if (phase_index < 0) {
                continue;
            }

            phase_acc_t *phase_acc =
                &acc[phase_index];

            if (phase_acc->sample_count >=
                TARGET_SAMPLES_PER_CHANNEL) {
                continue;
            }

            int raw =
                (int)sample->type2.data;

            if (raw < phase_acc->raw_min) {
                phase_acc->raw_min = raw;
            }

            if (raw > phase_acc->raw_max) {
                phase_acc->raw_max = raw;
            }

            int voltage_mv = 0;

            ESP_RETURN_ON_ERROR(
                raw_to_mv(
                    &s_phase[phase_index],
                    raw,
                    &voltage_mv
                ),
                TAG,
                "ADC conversion failed"
            );

            phase_acc->voltage_sum_mv +=
                (double)voltage_mv;

            phase_acc->voltage_sq_sum_mv +=
                (double)voltage_mv *
                (double)voltage_mv;

            phase_acc->sample_count++;
        }
    }

    finalize_phase(
        0,
        &acc[0],
        &measurement->l1
    );

    finalize_phase(
        1,
        &acc[1],
        &measurement->l2
    );

    finalize_phase(
        2,
        &acc[2],
        &measurement->l3
    );

    return ESP_OK;
}
