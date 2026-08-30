#include "display_ui.h"

#include "board_config.h"
#include "display_st7789.h"
#include "esp_check.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/ui.h"

#include <stdio.h>

static const char *TAG = "DISPLAY_UI";

static float kva(float current_a)
{
    return NOMINAL_PHASE_VOLTAGE_V * current_a / 1000.0f;
}

static void format_uptime(unsigned seconds, char *out, size_t out_size)
{
    const unsigned hours = seconds / 3600u;
    const unsigned minutes = (seconds / 60u) % 60u;
    const unsigned secs = seconds % 60u;
    snprintf(out, out_size, "%02u:%02u:%02u", hours, minutes, secs);
}

static lv_color_t battery_color_from_percent(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    if (percent >= 50) {
        /* 50..100%: yellow -> green */
        const uint8_t t = (uint8_t)(((percent - 50) * 255) / 50);
        return lv_color_make((uint8_t)(255 - t), 255, 0);
    }

    /* 0..50%: red -> yellow */
    const uint8_t t = (uint8_t)((percent * 255) / 50);
    return lv_color_make(255, t, 0);
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_signal(const display_ui_state_t *state)
{
    set_hidden(ui_SignalLOST, state->online);
    set_hidden(ui_SignalAntena, !state->online);

    if (!state->online) {
        return;
    }

    int p = state->signal_percent;
    if (p < 1) p = 1;
    if (p > 100) p = 100;

    /* At least one bar while packets are being received. */
    set_hidden(ui_Signal20percent, false);
    set_hidden(ui_Signal40percent, p <= 20);
    set_hidden(ui_Signal60percent, p <= 40);
    set_hidden(ui_Signal80percent, p <= 60);
    set_hidden(ui_Signal100percent, p <= 80);
}

static void update_battery(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    lv_bar_set_value(ui_Bar1, percent, LV_ANIM_ON);
    lv_obj_set_style_bg_color(
        ui_Bar1,
        battery_color_from_percent(percent),
        LV_PART_INDICATOR | LV_STATE_DEFAULT
    );
}

static void update_mode(display_mode_t mode)
{
    const bool three_phase = (mode == DISPLAY_MODE_GRID);

    set_hidden(ui_ThreePhaseWINDOW, !three_phase);
    set_hidden(ui_SinglePhaseWINDOW, three_phase);

    /* Exact SquareLine-object behavior requested by the user. */
    set_hidden(ui_ButtomINFOSinglePhase, !three_phase);
    set_hidden(ui_BottomINFOFullLoad, three_phase);
}

static void update_measurements(const display_ui_state_t *state)
{
    if (!state->online) {
        lv_label_set_text(ui_L1AMPS, "L1 : --.-- A");
        lv_label_set_text(ui_L2AMPS, "L2 : --.-- A");
        lv_label_set_text(ui_L3AMPS, "L3 : --.-- A");
        lv_label_set_text(ui_L1KVA, "--.-- KVA");
        lv_label_set_text(ui_L2KVA, "--.-- KVA");
        lv_label_set_text(ui_L3KVA, "--.-- KVA");
        lv_label_set_text(ui_ThreePhasesFullAMPS, "--.-- A");
        lv_label_set_text(ui_ThreePhasesFullKVA, "--.-- KVA");
        return;
    }

    lv_label_set_text_fmt(ui_L1AMPS, "L1 : %.2f A", state->l1_a);
    lv_label_set_text_fmt(ui_L2AMPS, "L2 : %.2f A", state->l2_a);
    lv_label_set_text_fmt(ui_L3AMPS, "L3 : %.2f A", state->l3_a);

    lv_label_set_text_fmt(ui_L1KVA, "%.2f KVA", kva(state->l1_a));
    lv_label_set_text_fmt(ui_L2KVA, "%.2f KVA", kva(state->l2_a));
    lv_label_set_text_fmt(ui_L3KVA, "%.2f KVA", kva(state->l3_a));

    const float total_a = state->l1_a + state->l2_a + state->l3_a;
    lv_label_set_text_fmt(ui_ThreePhasesFullAMPS, "%.2f A", total_a);
    lv_label_set_text_fmt(ui_ThreePhasesFullKVA, "%.2f KVA", kva(total_a));
}

static void render_locked(const display_ui_state_t *state)
{
    char uptime[20];
    format_uptime(state->uptime_seconds, uptime, sizeof(uptime));
    lv_label_set_text(ui_UPtime, uptime);

    update_signal(state);
    update_battery(state->battery_percent);
    update_mode(state->mode);
    update_measurements(state);
}

esp_err_t display_ui_init(void)
{
    ESP_RETURN_ON_ERROR(display_st7789_init(), TAG, "ST7789 init failed");

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    ui_init();

    /* Initial frame: ThreePhase window, offline, battery unknown/0%. */
    const display_ui_state_t initial = {
        .mode = DISPLAY_MODE_GRID,
        .online = false,
        .uptime_seconds = 0,
        .battery_percent = 0,
        .signal_percent = 0,
        .rssi_dbm = 0,
        .l1_a = 0.0f,
        .l2_a = 0.0f,
        .l3_a = 0.0f,
    };
    render_locked(&initial);

    /* Force first complete LVGL frame while the panel/backlight are still hidden. */
    lv_refr_now(NULL);
    lvgl_port_unlock();

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_STARTUP_BLANK_MS));
    ESP_RETURN_ON_ERROR(display_st7789_panel_set_visible(true), TAG, "Panel ON failed");
    ESP_RETURN_ON_ERROR(
        display_st7789_backlight_set(DISPLAY_STARTUP_BRIGHTNESS_PCT),
        TAG,
        "Backlight ON failed"
    );

    return ESP_OK;
}

esp_err_t display_ui_render(const display_ui_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    render_locked(state);
    lvgl_port_unlock();
    return ESP_OK;
}
