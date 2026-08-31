#include "mode_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define BUTTON_DEBOUNCE_US      40000LL
#define BUTTON_DOUBLE_GAP_US   350000LL
#define BUTTON_LONG_PRESS_US  1200000LL

esp_err_t mode_button_init(void)
{
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << PIN_MODE_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&c);
}

mode_button_event_t mode_button_get_event(void)
{
    static int raw_last = 1;
    static int stable = 1;
    static int64_t raw_changed_us = 0;
    static int64_t press_started_us = 0;
    static int64_t first_release_us = 0;
    static bool short_pending = false;
    static bool long_consumed = false;

    const int64_t now = esp_timer_get_time();
    const int raw = gpio_get_level(PIN_MODE_BUTTON);

    if (raw != raw_last) {
        raw_last = raw;
        raw_changed_us = now;
    }

    if (raw != stable && (now - raw_changed_us) >= BUTTON_DEBOUNCE_US) {
        stable = raw;

        if (stable == 0) {
            press_started_us = now;
            long_consumed = false;
        } else {
            const int64_t held_us = now - press_started_us;

            if (long_consumed || held_us >= BUTTON_LONG_PRESS_US) {
                short_pending = false;
                first_release_us = 0;
                long_consumed = true;
                return MODE_BUTTON_EVENT_NONE;
            }

            if (short_pending &&
                first_release_us > 0 &&
                (now - first_release_us) <= BUTTON_DOUBLE_GAP_US) {
                short_pending = false;
                first_release_us = 0;
                return MODE_BUTTON_EVENT_DOUBLE;
            }

            short_pending = true;
            first_release_us = now;
        }
    }

    if (stable == 0 &&
        !long_consumed &&
        press_started_us > 0 &&
        (now - press_started_us) >= BUTTON_LONG_PRESS_US) {
        long_consumed = true;
        short_pending = false;
        first_release_us = 0;
        return MODE_BUTTON_EVENT_LONG;
    }

    if (short_pending &&
        stable == 1 &&
        first_release_us > 0 &&
        (now - first_release_us) > BUTTON_DOUBLE_GAP_US) {
        short_pending = false;
        first_release_us = 0;
        return MODE_BUTTON_EVENT_SHORT;
    }

    return MODE_BUTTON_EVENT_NONE;
}
