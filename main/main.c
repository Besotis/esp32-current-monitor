#include "esp_log.h"
#include "role_sensor.h"
#include "role_display.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Current Monitor starting...");

#if CONFIG_DEVICE_ROLE_SENSOR
    role_sensor_start();
#elif CONFIG_DEVICE_ROLE_DISPLAY
    role_display_start();
#else
#error "Device role not configured"
#endif
}
