#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "protocol.h"
typedef void (*espnow_receive_callback_t)(const current_data_packet_t *packet,const uint8_t sender_mac[6],int8_t rssi_dbm);
esp_err_t espnow_comm_init(void);
void espnow_comm_set_receive_callback(espnow_receive_callback_t callback);
esp_err_t espnow_add_peer(const uint8_t peer_mac[6]);
esp_err_t espnow_send_current_to(const uint8_t peer_mac[6],const current_data_packet_t *packet);
