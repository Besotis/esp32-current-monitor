#include "role_sensor.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>

#include "board_config.h"
#include "current_sensor.h"
#include "espnow_comm.h"
#include "protocol.h"
#include "sensor_status_led.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ROLE_SENSOR";
static volatile bool display_ready = false;
static atomic_bool hello_sent;
static atomic_uint consecutive_send_failures;

static void on_control(espnow_control_type_t type,const uint8_t mac[6],int8_t rssi)
{
    if (mac == NULL || memcmp(mac, DEVICE_B_MAC, 6) != 0) return;
    if (type == ESPNOW_CONTROL_READY) {
        /* A READY is valid only after this SENSOR has sent HELLO in the
         * current handshake cycle.  This prevents an already-running DISPLAY
         * from making a freshly rebooted SENSOR skip HELLO. */
        if (!atomic_load(&hello_sent)) {
            ESP_LOGW(TAG, "Ignoring READY received before HELLO, RSSI=%d dBm", rssi);
            return;
        }

        if (!display_ready) {
            ESP_LOGI(TAG, "DISPLAY READY received, RSSI=%d dBm", rssi);
        }
        atomic_store(&consecutive_send_failures, 0);
        display_ready = true;
    }
}

static void on_send_result(bool success)
{
    /* Delivery callbacks run from the Wi-Fi task.  Only track failures while
     * the application-level link is CONNECTED; HELLO attempts while waiting
     * for DISPLAY are intentionally ignored here. */
    if (!display_ready) {
        return;
    }

    if (success) {
        atomic_store(&consecutive_send_failures, 0);
    } else {
        atomic_fetch_add(&consecutive_send_failures, 1);
    }
}

static void wait_for_display_ready(void)
{
    ESP_LOGI(TAG, "Waiting for DISPLAY READY...");

    while (!display_ready) {
        /* Arm READY acceptance before queueing HELLO.  ESP-NOW send is
         * asynchronous, so the peer cannot receive and answer this HELLO
         * before espnow_send_control_to() has queued it. */
        atomic_store(&hello_sent, true);
        esp_err_t hello_err = espnow_send_control_to(DEVICE_B_MAC, ESPNOW_CONTROL_HELLO);
        if (hello_err != ESP_OK) {
            atomic_store(&hello_sent, false);
            ESP_LOGW(TAG, "HELLO send error: %s", esp_err_to_name(hello_err));
        } else {
            ESP_LOGI(TAG, "HELLO -> DISPLAY");
        }
        vTaskDelay(pdMS_TO_TICKS(ESPNOW_HANDSHAKE_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "ESP-NOW link ready; starting current measurements");
}


void role_sensor_start(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "Device role: A - 3-PHASE SENSOR");
    ESP_LOGI(TAG, "=================================");

    ESP_ERROR_CHECK(sensor_status_led_init());
    ESP_ERROR_CHECK(current_sensor_init());
    ESP_ERROR_CHECK(espnow_comm_init());
    atomic_init(&hello_sent, false);
    atomic_init(&consecutive_send_failures, 0);
    espnow_comm_set_activity_callback(sensor_status_led_espnow_activity);
    espnow_comm_set_control_callback(on_control);
    espnow_comm_set_send_result_callback(on_send_result);
    ESP_ERROR_CHECK(espnow_add_peer(DEVICE_B_MAC));

    wait_for_display_ready();

    const uint32_t session_id =
        esp_random();

    uint32_t sequence = 0;

    while (1) {
        const unsigned fail_count = atomic_load(&consecutive_send_failures);
        if (fail_count >= ESPNOW_LINK_LOSS_FAIL_COUNT) {
            ESP_LOGW(
                TAG,
                "DISPLAY link lost after %u consecutive delivery failures",
                fail_count
            );

            display_ready = false;
            atomic_store(&hello_sent, false);
            atomic_store(&consecutive_send_failures, 0);
            wait_for_display_ready();
        }

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
            if (display_ready) {
                atomic_fetch_add(&consecutive_send_failures, 1);
            }
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
