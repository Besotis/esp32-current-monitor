#include "sensor_status_led.h"

#include "board_config.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SENSOR_LED";

static TaskHandle_t s_activity_task = NULL;

static void heartbeat_task(void *arg)
{
    (void)arg;

    bool on = true;

    while (1) {
        gpio_set_level(PIN_SENSOR_LED_GREEN, on ? 1 : 0);
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(SENSOR_LED_HEARTBEAT_MS));
    }
}

static void activity_task(void *arg)
{
    (void)arg;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        gpio_set_level(PIN_SENSOR_LED_YELLOW, 1);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_LED_ESPNOW_PULSE_MS));
        gpio_set_level(PIN_SENSOR_LED_YELLOW, 0);
    }
}

esp_err_t sensor_status_led_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask =
            (1ULL << PIN_SENSOR_LED_GREEN) |
            (1ULL << PIN_SENSOR_LED_YELLOW),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(PIN_SENSOR_LED_GREEN, 0);
    gpio_set_level(PIN_SENSOR_LED_YELLOW, 0);

    BaseType_t ok = xTaskCreate(
        heartbeat_task,
        "sensor_heartbeat",
        2048,
        NULL,
        2,
        NULL
    );

    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ok = xTaskCreate(
        activity_task,
        "sensor_espnow_led",
        2048,
        NULL,
        2,
        &s_activity_task
    );

    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Status LEDs ready: green GPIO%d heartbeat, yellow GPIO%d ESP-NOW activity",
        PIN_SENSOR_LED_GREEN,
        PIN_SENSOR_LED_YELLOW
    );

    return ESP_OK;
}

void sensor_status_led_espnow_activity(void)
{
    if (s_activity_task != NULL) {
        xTaskNotifyGive(s_activity_task);
    }
}
