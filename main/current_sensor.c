#include "current_sensor.h"
#include "esp_log.h"

static const char *TAG = "CURRENT_SENSOR";

esp_err_t current_sensor_init(void)
{
    ESP_LOGI(TAG, "Initializing current sensor");
    /* TODO: ADC continuous mode, GPIO1, RMS calculation, calibration. */
    return ESP_OK;
}

esp_err_t current_sensor_read(current_measurement_t *measurement)
{
    if (measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Temporary values until ADC measurement is implemented. */
    measurement->current_rms_a = 0.0f;
    measurement->sensor_voltage_rms_v = 0.0f;
    return ESP_OK;
}
