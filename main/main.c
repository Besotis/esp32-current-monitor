#include "esp_log.h"

#if CONFIG_DEVICE_ROLE_SENSOR
#include "role_sensor.h"
#elif CONFIG_DEVICE_ROLE_DISPLAY
#include "role_display.h"
#else
#error "Device role not configured"
#endif

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Current Monitor starting...");

#if CONFIG_DEVICE_ROLE_SENSOR
    role_sensor_start();
#elif CONFIG_DEVICE_ROLE_DISPLAY
    role_display_start();
#endif
}
