#include "role_sensor.h"
#include "current_sensor.h"
#include "espnow_comm.h"
#include "protocol.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ROLE_SENSOR";

void role_sensor_start(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "Device role: A - CURRENT SENSOR");
    ESP_LOGI(TAG, "=================================");

    ESP_ERROR_CHECK(current_sensor_init());
    uint32_t sequence = 0;

    while (1) {
        current_measurement_t measurement;
        esp_err_t err = current_sensor_read(&measurement);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Current: %.3f A | Sensor: %.3f V RMS",
                     measurement.current_rms_a,
                     measurement.sensor_voltage_rms_v);

            current_data_packet_t packet = {
                .version = PROTOCOL_VERSION,
                .sequence = sequence++,
                .current_rms_a = measurement.current_rms_a,
                .sensor_voltage_rms_v = measurement.sensor_voltage_rms_v
            };
            (void)packet;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
