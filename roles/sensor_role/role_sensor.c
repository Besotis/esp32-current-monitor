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
    ESP_LOGI(TAG, "Device role: A - 3-PHASE SENSOR");
    ESP_LOGI(TAG, "=================================");

    ESP_ERROR_CHECK(current_sensor_init());
    ESP_ERROR_CHECK(espnow_comm_init());
    ESP_ERROR_CHECK(espnow_add_peer(DEVICE_B_MAC));

    const uint32_t session_id =
        esp_random();

    uint32_t sequence = 0;

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

            vTaskDelay(
                pdMS_TO_TICKS(500)
            );

            continue;
        }

        current_data_packet_t packet = {
            .magic = CURRENT_PROTOCOL_MAGIC,
            .version = CURRENT_PROTOCOL_VERSION,
            .reserved = {0, 0, 0},

            .session_id = session_id,
            .sequence = sequence++,

            .current_l1_a =
                measurement.l1.current_rms_a,

            .current_l2_a =
                measurement.l2.current_rms_a,

            .current_l3_a =
                measurement.l3.current_rms_a,

            .sensor_l1_voltage_rms_v =
                measurement.l1.sensor_voltage_rms_v,

            .sensor_l2_voltage_rms_v =
                measurement.l2.sensor_voltage_rms_v,

            .sensor_l3_voltage_rms_v =
                measurement.l3.sensor_voltage_rms_v,

            .offset_l1_voltage_v =
                measurement.l1.offset_voltage_v,

            .offset_l2_voltage_v =
                measurement.l2.offset_voltage_v,

            .offset_l3_voltage_v =
                measurement.l3.offset_voltage_v,
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
            " | L1=%.4fA %.5fVrms off=%.3fV raw=%d..%d"
            " | L2=%.4fA %.5fVrms off=%.3fV raw=%d..%d"
            " | L3=%.4fA %.5fVrms off=%.3fV raw=%d..%d",
            packet.sequence,
            packet.current_l1_a,
            packet.sensor_l1_voltage_rms_v,
            packet.offset_l1_voltage_v,
            measurement.l1.raw_min,
            measurement.l1.raw_max,
            packet.current_l2_a,
            packet.sensor_l2_voltage_rms_v,
            packet.offset_l2_voltage_v,
            measurement.l2.raw_min,
            measurement.l2.raw_max,
            packet.current_l3_a,
            packet.sensor_l3_voltage_rms_v,
            packet.offset_l3_voltage_v,
            measurement.l3.raw_min,
            measurement.l3.raw_max
        );

        ESP_LOGD(
            TAG,
            "L1 %.4fVrms %.3fV raw=%d..%d n=%d"
            " | L2 %.4fVrms %.3fV raw=%d..%d n=%d"
            " | L3 %.4fVrms %.3fV raw=%d..%d n=%d",
            measurement.l1.sensor_voltage_rms_v,
            measurement.l1.offset_voltage_v,
            measurement.l1.raw_min,
            measurement.l1.raw_max,
            measurement.l1.sample_count,

            measurement.l2.sensor_voltage_rms_v,
            measurement.l2.offset_voltage_v,
            measurement.l2.raw_min,
            measurement.l2.raw_max,
            measurement.l2.sample_count,

            measurement.l3.sensor_voltage_rms_v,
            measurement.l3.offset_voltage_v,
            measurement.l3.raw_min,
            measurement.l3.raw_max,
            measurement.l3.sample_count
        );

        vTaskDelay(
            pdMS_TO_TICKS(150)
        );
    }
}
