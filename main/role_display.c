#include "role_display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ROLE_DISPLAY";

void role_display_start(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "Device role: B - DISPLAY");
    ESP_LOGI(TAG, "=================================");

    while (1) {
        ESP_LOGI(TAG, "Waiting for current data...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
