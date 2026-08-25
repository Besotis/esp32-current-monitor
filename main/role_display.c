#include "role_display.h"
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include "battery_monitor.h"
#include "board_config.h"
#include "display_ui.h"
#include "espnow_comm.h"
#include "mode_button.h"
#include "protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG="ROLE_DISPLAY";
typedef struct{current_data_packet_t packet;uint8_t mac[6];int8_t rssi;} rx_t;
static QueueHandle_t q;
static bool same(const uint8_t a[6],const uint8_t b[6]){return memcmp(a,b,6)==0;}
static int sig(int8_t r){if(r<=-95)return 0;if(r>=-50)return 100;return ((int)r+95)*100/45;}
static void on_packet(const current_data_packet_t *p,const uint8_t mac[6],int8_t rssi){if(!q||!p||!mac||!same(mac,DEVICE_A_MAC))return;rx_t x={.packet=*p,.rssi=rssi};memcpy(x.mac,mac,6);xQueueOverwrite(q,&x);}

void role_display_start(void){
 ESP_LOGI(TAG,"Device role: B - DISPLAY");
 q=xQueueCreate(1,sizeof(rx_t));if(!q){ESP_LOGE(TAG,"Queue failed");return;}
 ESP_ERROR_CHECK(espnow_comm_init());ESP_ERROR_CHECK(battery_monitor_init());ESP_ERROR_CHECK(mode_button_init());ESP_ERROR_CHECK(display_ui_init());espnow_comm_set_receive_callback(on_packet);
 display_ui_state_t ui={.mode=DISPLAY_MODE_GRID,.online=false,.battery_percent=0,.signal_percent=0,.l1_a=0,.l2_a=0,.l3_a=0};
 int64_t boot=esp_timer_get_time(),last_rx=0,last_bat=0,last_draw=0;float fs=0;bool fs_init=false;
 while(1){
  rx_t x;
  if(xQueueReceive(q,&x,pdMS_TO_TICKS(50))==pdTRUE){
   last_rx=esp_timer_get_time();ui.online=true;ui.l1_a=x.packet.current_rms_a;ui.l2_a=0;ui.l3_a=0;
   int ns=sig(x.rssi);if(!fs_init){fs=ns;fs_init=true;}else fs=fs*0.8f+ns*0.2f;ui.signal_percent=(int)(fs+0.5f);
   ESP_LOGI(TAG,"RX #%" PRIu32 " | Current=%.3f A | RSSI=%d dBm | SIG=%d%%",x.packet.sequence,ui.l1_a,x.rssi,ui.signal_percent);
  }
  int64_t now=esp_timer_get_time();if(last_rx==0||now-last_rx>(int64_t)DISPLAY_NO_SIGNAL_MS*1000LL)ui.online=false;
  if(mode_button_pressed()){ui.mode=(ui.mode==DISPLAY_MODE_GRID)?DISPLAY_MODE_GENERATOR:DISPLAY_MODE_GRID;ESP_LOGI(TAG,"Display mode: %s",ui.mode==DISPLAY_MODE_GRID?"GRID":"GENERATOR");last_draw=0;}
  if(last_bat==0||now-last_bat>=1000000LL){float v;int p;if(battery_monitor_read(&v,&p)==ESP_OK){ui.battery_percent=p;ESP_LOGI(TAG,"Battery=%.3f V (%d%%)",v,p);}last_bat=now;}
  ui.uptime_seconds=(unsigned)((now-boot)/1000000LL);
  if(last_draw==0||now-last_draw>=200000LL){ESP_ERROR_CHECK(display_ui_render(&ui));last_draw=now;}
 }
}
