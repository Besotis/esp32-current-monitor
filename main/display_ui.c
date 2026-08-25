#include "display_ui.h"
#include "board_config.h"
#include "ch1115.h"
#include <stdio.h>
static void fmt_up(unsigned s,char *o,size_t n){snprintf(o,n,"%02u:%02u:%02u",(s/3600)%100,(s/60)%60,s%60);}
static float kva(float a){return NOMINAL_PHASE_VOLTAGE_V*a/1000.0f;}
esp_err_t display_ui_init(void){return ch1115_init();}
esp_err_t display_ui_render(const display_ui_state_t *s){
 if (s == NULL) {
    return ESP_ERR_INVALID_ARG;
}

char up[16];
char top[32];
char line[32];

fmt_up(
    s->uptime_seconds,
    up,
    sizeof(up)
);
 if(s->online)snprintf(top,sizeof(top),"UP%s B%d%% S%d%%",up,s->battery_percent,s->signal_percent);else snprintf(top,sizeof(top),"UP%s B%d%% OFF",up,s->battery_percent);
 ch1115_clear(); ch1115_draw_text(0,0,top,1);
 if(!s->online){ch1115_draw_text(28,24,"--- A",2);ch1115_draw_text(34,50,"OFFLINE",1);return ch1115_flush();}
 if(s->mode==DISPLAY_MODE_GRID){
  snprintf(line,sizeof(line),"L1 %.2fA %.2fkVA",s->l1_a,kva(s->l1_a));ch1115_draw_text(0,17,line,1);
  snprintf(line,sizeof(line),"L2 %.2fA %.2fkVA",s->l2_a,kva(s->l2_a));ch1115_draw_text(0,33,line,1);
  snprintf(line,sizeof(line),"L3 %.2fA %.2fkVA",s->l3_a,kva(s->l3_a));ch1115_draw_text(0,49,line,1);
 }else{
  float a=s->l1_a+s->l2_a+s->l3_a;char ca[20],kv[20];snprintf(ca,sizeof(ca),"%.2f A",a);snprintf(kv,sizeof(kv),"%.2f kVA",kva(a));
  ch1115_draw_text(20,15,ca,2);ch1115_draw_text(14,42,kv,2);
 }
 return ch1115_flush();
}
