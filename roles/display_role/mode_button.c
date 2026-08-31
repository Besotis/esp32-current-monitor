#include "mode_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

/*
 * Button timing tuned for a physical momentary button.
 *
 * Important detail for DOUBLE click:
 * the second click is accepted when the SECOND PRESS starts inside the
 * double-click window. We do not require the second RELEASE to also fit
 * inside that window. This makes double click much easier and more natural.
 */
#define BUTTON_DEBOUNCE_US       25000LL
#define BUTTON_DOUBLE_GAP_US    450000LL
#define BUTTON_LONG_PRESS_US   1200000LL

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
    static bool second_click = false;
    static bool long_consumed = false;

    const int64_t now = esp_timer_get_time();
    const int raw = gpio_get_level(PIN_MODE_BUTTON);

    if (raw != raw_last) {
        raw_last = raw;
        raw_changed_us = now;
    }

    /* Debounced edge. */
    if (raw != stable && (now - raw_changed_us) >= BUTTON_DEBOUNCE_US) {
        stable = raw;

        if (stable == 0) {
            /* Press. */
            press_started_us = now;
            long_consumed = false;

            /*
             * A double click is decided by how quickly the second PRESS
             * follows the first RELEASE. This is much less fussy than timing
             * release-to-release.
             */
            second_click = short_pending &&
                           first_release_us > 0 &&
                           (now - first_release_us) <= BUTTON_DOUBLE_GAP_US;
        } else {
            /* Release. */
            const int64_t held_us = now - press_started_us;

            if (long_consumed || held_us >= BUTTON_LONG_PRESS_US) {
                short_pending = false;
                first_release_us = 0;
                second_click = false;
                long_consumed = true;
                return MODE_BUTTON_EVENT_NONE;
            }

            if (second_click) {
                short_pending = false;
                first_release_us = 0;
                second_click = false;
                return MODE_BUTTON_EVENT_DOUBLE;
            }

            /* First short click: wait briefly to see if a second begins. */
            short_pending = true;
            first_release_us = now;
        }
    }

    /* Long press is reported while the button is still held. */
    if (stable == 0 &&
        !long_consumed &&
        press_started_us > 0 &&
        (now - press_started_us) >= BUTTON_LONG_PRESS_US) {
        long_consumed = true;
        short_pending = false;
        first_release_us = 0;
        second_click = false;
        return MODE_BUTTON_EVENT_LONG;
    }

    /* No second press arrived in time: emit the pending SHORT click. */
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
