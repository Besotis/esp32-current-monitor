#include "battery_monitor.h"
#include "board_config.h"
#include <stdbool.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG="BATTERY";
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static bool cali_ok=false;

static int percent_from_v(float v)
{
    typedef struct
    {
        float v;
        int p;
    } point_t;

    static const point_t t[] =
    {
        {4.20f, 100},
        {4.10f,  90},
        {4.00f,  80},
        {3.90f,  70},
        {3.80f,  55},
        {3.70f,  40},
        {3.60f,  25},
        {3.50f,  15},
        {3.40f,   8},
        {3.30f,   3},
        {3.20f,   0},
    };

    if (v >= 4.20f) {
        return 100;
    }

    if (v <= 3.20f) {
        return 0;
    }

    for (int i = 0; i < 10; i++) {
        if (v <= t[i].v && v >= t[i + 1].v) {
            float x =
                (v - t[i + 1].v) /
                (t[i].v - t[i + 1].v);

            return (int)(
                t[i + 1].p +
                x * (t[i].p - t[i + 1].p) +
                0.5f
            );
        }
    }

    return 0;
}

esp_err_t battery_monitor_init(void){
    adc_oneshot_unit_init_cfg_t u={.unit_id=ADC_UNIT_1,.ulp_mode=ADC_ULP_MODE_DISABLE};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&u,&adc));
    adc_oneshot_chan_cfg_t c={.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc,BATTERY_ADC_CHANNEL,&c));
    adc_cali_curve_fitting_config_t cc={.unit_id=ADC_UNIT_1,.chan=BATTERY_ADC_CHANNEL,.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12};
    if(adc_cali_create_scheme_curve_fitting(&cc,&cali)==ESP_OK){cali_ok=true;ESP_LOGI(TAG,"Battery ADC calibration enabled");}
    return ESP_OK;
}

esp_err_t battery_monitor_read(float *voltage_v,int *percent){
    if(!voltage_v||!percent)return ESP_ERR_INVALID_ARG;
    int64_t sum=0; const int n=32;
    for(int i=0;i<n;i++){int raw=0,mv=0;ESP_ERROR_CHECK(adc_oneshot_read(adc,BATTERY_ADC_CHANNEL,&raw));if(cali_ok)ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali,raw,&mv));else mv=(raw*3300)/4095;sum+=mv;}
    float v=((float)sum/n)/1000.0f*BATTERY_DIVIDER_RATIO;*voltage_v=v;*percent=percent_from_v(v);return ESP_OK;
}
