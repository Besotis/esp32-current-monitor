#include "role_sensor.h"

#include <inttypes.h>

#include "board_config.h"
#include "current_sensor.h"
#include "espnow_comm.h"
#include "protocol.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ROLE_SENSOR";

void role_sensor_start(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "Device role: A - CURRENT SENSOR");
    ESP_LOGI(TAG, "=================================");

    ESP_ERROR_CHECK(current_sensor_init());
    ESP_ERROR_CHECK(espnow_comm_init());
    ESP_ERROR_CHECK(espnow_add_peer(DEVICE_B_MAC));

    const uint32_t session_id = esp_random();
    uint32_t sequence = 0;

    ESP_LOGI(
        TAG,
        "Unicast target B: %02X:%02X:%02X:%02X:%02X:%02X",
        DEVICE_B_MAC[0],
        DEVICE_B_MAC[1],
        DEVICE_B_MAC[2],
        DEVICE_B_MAC[3],
        DEVICE_B_MAC[4],
        DEVICE_B_MAC[5]
    );

    ESP_LOGI(
        TAG,
        "Session ID: 0x%08" PRIX32,
        session_id
    );

    while (1) {
        current_measurement_t measurement;

        esp_err_t err =
            current_sensor_read(&measurement);

        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Current sensor error: %s",
                esp_err_to_name(err)
            );

            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        current_data_packet_t packet = {
            .magic = CURRENT_PROTOCOL_MAGIC,
            .version = CURRENT_PROTOCOL_VERSION,
            .reserved = {0, 0, 0},
            .session_id = session_id,
            .sequence = sequence++,
            .current_rms_a = measurement.current_rms_a,
            .sensor_voltage_rms_v =
                measurement.sensor_voltage_rms_v,
            .offset_voltage_v =
                measurement.offset_voltage_v,
        };

        err = espnow_send_current_to(
            DEVICE_B_MAC,
            &packet
        );

        if (err != ESP_OK) {
            ESP_LOGW(
                TAG,
                "ESP-NOW send error: %s",
                esp_err_to_name(err)
            );
        }

        ESP_LOGI(
            TAG,
            "TX #%" PRIu32
            " | Current=%.3f A"
            " | Sensor=%.4f V RMS"
            " | Offset=%.3f V"
            " | raw=%d..%d"
            " | samples=%d",
            packet.sequence,
            measurement.current_rms_a,
            measurement.sensor_voltage_rms_v,
            measurement.offset_voltage_v,
            measurement.raw_min,
            measurement.raw_max,
            measurement.sample_count
        );

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
