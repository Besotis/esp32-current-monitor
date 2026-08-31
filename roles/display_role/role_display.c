#include "role_display.h"
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include "battery_monitor.h"
#include "board_config.h"
#include "display_ui.h"
#include "display_st7789.h"
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
static int sig(int8_t r)
{
    /*
     * User-facing LR signal curve:
     *   >= -80 dBm -> 100%
     *      -85 dBm ->  90%
     *      -90 dBm ->  50%
     *     -100 dBm ->   1%
     *
     * Piecewise interpolation is used between these points.
     * 0% is never shown while packets are still arriving.
     */
    if (r >= -80) {
        return 100;
    }

    if (r >= -85) {
        /* -80..-85 dBm: 100..90% */
        return 100 - ((-80 - (int)r) * 10) / 5;
    }

    if (r >= -90) {
        /* -85..-90 dBm: 90..50% */
        return 90 - ((-85 - (int)r) * 40) / 5;
    }

    if (r > -100) {
        /* -90..-100 dBm: 50..1% */
        int pct = 50 - ((-90 - (int)r) * 49) / 10;
        return (pct < 1) ? 1 : pct;
    }

    return 1;
}
static void on_packet(const current_data_packet_t *p,const uint8_t mac[6],int8_t rssi){if(!q||!p||!mac||!same(mac,DEVICE_A_MAC))return;rx_t x={.packet=*p,.rssi=rssi};memcpy(x.mac,mac,6);xQueueOverwrite(q,&x);}

void role_display_start(void){
 /* Hide TFT immediately, before Wi-Fi/ESP-NOW init, to suppress boot artifacts. */
 ESP_ERROR_CHECK(display_st7789_early_backlight_off());
 ESP_LOGI(TAG,"Device role: B - DISPLAY");
 q=xQueueCreate(1,sizeof(rx_t));if(!q){ESP_LOGE(TAG,"Queue failed");return;}
 ESP_ERROR_CHECK(espnow_comm_init());ESP_ERROR_CHECK(battery_monitor_init());ESP_ERROR_CHECK(mode_button_init());ESP_ERROR_CHECK(display_ui_init());espnow_comm_set_receive_callback(on_packet);
 display_ui_state_t ui={.view=DISPLAY_VIEW_THREE_PHASE,.online=false,.battery_percent=0,.signal_percent=0,.rssi_dbm=0,.l1_a=0,.l2_a=0,.l3_a=0};
 int64_t boot=esp_timer_get_time(),last_rx=0,last_bat=0,last_draw=0,last_chart_sample=0;float fs=0;bool fs_init=false;
 while(1){
  rx_t x;
  if(xQueueReceive(q,&x,pdMS_TO_TICKS(10))==pdTRUE){
   last_rx=esp_timer_get_time();ui.online=true;ui.rssi_dbm=x.rssi;ui.l1_a=x.packet.current_l1_a;ui.l2_a=x.packet.current_l2_a;ui.l3_a=x.packet.current_l3_a;
   int ns=sig(x.rssi);if(!fs_init){fs=ns;fs_init=true;}else fs=fs*0.8f+ns*0.2f;ui.signal_percent=(int)(fs+0.5f);
   ESP_LOGI(TAG,"RX #%" PRIu32 " | L1=%.3f A | L2=%.3f A | L3=%.3f A | RSSI=%d dBm | SIG=%d%%",x.packet.sequence,ui.l1_a,ui.l2_a,ui.l3_a,x.rssi,ui.signal_percent);
  }
  int64_t now=esp_timer_get_time();if(last_rx==0||now-last_rx>(int64_t)DISPLAY_NO_SIGNAL_MS*1000LL)ui.online=false;
  const mode_button_event_t button_event = mode_button_get_event();
  if(button_event == MODE_BUTTON_EVENT_SHORT){
   switch(ui.view){
    case DISPLAY_VIEW_THREE_PHASE:
     ui.view=DISPLAY_VIEW_SINGLE_PHASE;
     ESP_LOGI(TAG,"View: SINGLE_PHASE");
     break;
    case DISPLAY_VIEW_SINGLE_PHASE:
     ui.view=DISPLAY_VIEW_THREE_PHASE;
     ESP_LOGI(TAG,"View: THREE_PHASE");
     break;
    case DISPLAY_VIEW_L1L2L3_CHART:
     ui.view=DISPLAY_VIEW_THREE_PHASE;
     ESP_LOGI(TAG,"View: THREE_PHASE (back from L1/L2/L3 chart)");
     break;
    case DISPLAY_VIEW_FULL_LOAD_CHART:
     ui.view=DISPLAY_VIEW_SINGLE_PHASE;
     ESP_LOGI(TAG,"View: SINGLE_PHASE (back from Full Load chart)");
     break;
   }
   last_draw=0;
  }else if(button_event == MODE_BUTTON_EVENT_DOUBLE){
   if(ui.view==DISPLAY_VIEW_THREE_PHASE){
    ui.view=DISPLAY_VIEW_L1L2L3_CHART;
    ESP_LOGI(TAG,"View: L1/L2/L3 CHART");
    last_draw=0;
   }else if(ui.view==DISPLAY_VIEW_SINGLE_PHASE){
    ui.view=DISPLAY_VIEW_FULL_LOAD_CHART;
    ESP_LOGI(TAG,"View: FULL LOAD CHART");
    last_draw=0;
   }
  }else if(button_event == MODE_BUTTON_EVENT_LONG){
   /* Reserved for deep sleep. Do not change the current view. */
   ESP_LOGI(TAG,"Long press detected (deep sleep not implemented yet)");
  }
  if(last_bat==0||now-last_bat>=1000000LL){float v;int p;if(battery_monitor_read(&v,&p)==ESP_OK){ui.battery_percent=p;}last_bat=now;}

  /* 60 points x 60 s = one hour of history.
   * Take the first real sample as soon as data arrives, then one sample/min. */
  if((last_chart_sample==0 && ui.online) ||
     (last_chart_sample!=0 && now-last_chart_sample>=60000000LL)){
   ESP_ERROR_CHECK(display_ui_chart_add_sample(&ui));
   last_chart_sample=now;
  }
  ui.uptime_seconds=(unsigned)((now-boot)/1000000LL);
  if(last_draw==0||now-last_draw>=200000LL){ESP_ERROR_CHECK(display_ui_render(&ui));last_draw=now;}
 }
}
