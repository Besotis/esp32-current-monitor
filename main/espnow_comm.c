#include "espnow_comm.h"
#include "board_config.h"
#include <string.h>
#include "esp_log.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
static const char *TAG="ESPNOW"; static espnow_receive_callback_t receive_callback=NULL;
static void on_send(const wifi_tx_info_t *tx_info,esp_now_send_status_t status){(void)tx_info;if(status!=ESP_NOW_SEND_SUCCESS)ESP_LOGW(TAG,"Unicast delivery failed");}
static void on_receive(const esp_now_recv_info_t *info,const uint8_t *data,int len){if(!info||!data||len!=(int)sizeof(current_data_packet_t))return;current_data_packet_t p;memcpy(&p,data,sizeof(p));if(p.magic!=CURRENT_PROTOCOL_MAGIC||p.version!=CURRENT_PROTOCOL_VERSION)return;int8_t r=-127;if(info->rx_ctrl)r=info->rx_ctrl->rssi;if(receive_callback)receive_callback(&p,info->src_addr,r);}
static esp_err_t init_nvs(void){esp_err_t e=nvs_flash_init();if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());e=nvs_flash_init();}return e;}
esp_err_t espnow_comm_init(void){ESP_LOGI(TAG,"Initializing Wi-Fi/ESP-NOW on channel %d",ESPNOW_WIFI_CHANNEL);ESP_ERROR_CHECK(init_nvs());esp_err_t e=esp_netif_init();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return e;e=esp_event_loop_create_default();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return e;wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&cfg));ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));ESP_ERROR_CHECK(esp_wifi_start());ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL,WIFI_SECOND_CHAN_NONE));uint8_t mac[6];ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA,mac));ESP_LOGI(TAG,"STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);ESP_ERROR_CHECK(esp_now_init());ESP_ERROR_CHECK(esp_now_register_send_cb(on_send));ESP_ERROR_CHECK(esp_now_register_recv_cb(on_receive));ESP_LOGI(TAG,"ESP-NOW initialized");return ESP_OK;}
void espnow_comm_set_receive_callback(espnow_receive_callback_t cb){receive_callback=cb;}
esp_err_t espnow_add_peer(const uint8_t mac[6]){if(!mac)return ESP_ERR_INVALID_ARG;if(esp_now_is_peer_exist(mac))return ESP_OK;esp_now_peer_info_t p={0};memcpy(p.peer_addr,mac,6);p.channel=ESPNOW_WIFI_CHANNEL;p.ifidx=WIFI_IF_STA;p.encrypt=false;return esp_now_add_peer(&p);}
esp_err_t espnow_send_current_to(const uint8_t mac[6],const current_data_packet_t *packet){if(!mac||!packet)return ESP_ERR_INVALID_ARG;return esp_now_send(mac,(const uint8_t*)packet,sizeof(*packet));}
