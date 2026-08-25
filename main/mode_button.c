#include "mode_button.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
esp_err_t mode_button_init(void){gpio_config_t c={.pin_bit_mask=(1ULL<<PIN_MODE_BUTTON),.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};return gpio_config(&c);} 
bool mode_button_pressed(void){static int last=1,stable=1;static int64_t changed=0;int raw=gpio_get_level(PIN_MODE_BUTTON);int64_t now=esp_timer_get_time();if(raw!=last){last=raw;changed=now;}if(raw!=stable && now-changed>=40000){stable=raw;if(stable==0)return true;}return false;}
