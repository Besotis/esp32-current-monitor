#include "espnow_comm.h"
#include "esp_log.h"

static const char *TAG = "ESPNOW";

esp_err_t espnow_comm_init(void)
{
    ESP_LOGI(TAG, "ESP-NOW initialization placeholder");
    /* TODO: Wi-Fi init, ESP-NOW init, peer registration. */
    return ESP_OK;
}

esp_err_t espnow_send_current(const current_data_packet_t *packet)
{
    if (packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* TODO: esp_now_send(). */
    return ESP_OK;
}
