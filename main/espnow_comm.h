#pragma once
#include "esp_err.h"
#include "protocol.h"

esp_err_t espnow_comm_init(void);
esp_err_t espnow_send_current(const current_data_packet_t *packet);
