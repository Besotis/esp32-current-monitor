#include "display_ui.h"

#include "board_config.h"
#include "display_st7789.h"
#include "esp_check.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/ui.h"

#include <stdint.h>
#include <stdio.h>

static const char *TAG = "DISPLAY_UI";

#define CHART_POINT_COUNT          60
#define CHART_PHASE_SCALE_X10      10
#define CHART_TOTAL_SCALE_X10      10
#define CHART_PHASE_MAX_X10       300   /* 30.0 A */
#define CHART_TOTAL_MAX_X10       800   /* 80.0 A, matches SquareLine scale */

static lv_chart_series_t *s_chart_l1 = NULL;
static lv_chart_series_t *s_chart_l2 = NULL;
static lv_chart_series_t *s_chart_l3 = NULL;
static lv_chart_series_t *s_chart_total = NULL;

/* SquareLine exports 1-element external arrays for the placeholder data.
 * Replace them with correctly sized buffers before the charts are used. */
static lv_coord_t s_chart_l1_points[CHART_POINT_COUNT];
static lv_coord_t s_chart_l2_points[CHART_POINT_COUNT];
static lv_coord_t s_chart_l3_points[CHART_POINT_COUNT];
static lv_coord_t s_chart_total_points[CHART_POINT_COUNT];


/* ============================================================
 * BATTERY COLORS
 *
 * Keisk tik šitas HEX reikšmes.
 *
 * 0%   = visiškai išsikrovusi baterija
 * 50%  = vidutinė baterija
 * 100% = pilna baterija
 *
 * Tarp spalvų perėjimas atliekamas automatiškai.
 * ============================================================ */

#define BAT_COLOR_0_PCT      0xC50100
#define BAT_COLOR_50_PCT     0xBCE200
#define BAT_COLOR_100_PCT    0x00BF16


/* ============================================================
 * SIGNAL COLORS
 *
 * Keisk tik šitas HEX reikšmes.
 *
 * 1..20%   = 1 stulpelis
 * 21..40%  = 2 stulpeliai
 * 41..60%  = 3 stulpeliai
 * 61..80%  = 4 stulpeliai
 * 81..100% = 5 stulpeliai
 *
 * Antena gauna tokią pačią spalvą kaip aktyvūs stulpeliai.
 *
 * Neaktyvių stulpelių:
 *   background = transparent
 *   border     = paliekamas toks, kokį nustatei SquareLine.
 * ============================================================ */

#define SIGNAL_COLOR_20_PCT      0xD24A00
#define SIGNAL_COLOR_40_PCT      0xD2B700
#define SIGNAL_COLOR_60_PCT      0x9BD200
#define SIGNAL_COLOR_80_PCT      0x61D200
#define SIGNAL_COLOR_100_PCT     0x00D204


static float kva(float current_a)
{
    return NOMINAL_PHASE_VOLTAGE_V * current_a / 1000.0f;
}


static void format_uptime(unsigned seconds, char *out, size_t out_size)
{
    const unsigned hours = seconds / 3600u;
    const unsigned minutes = (seconds / 60u) % 60u;
    const unsigned secs = seconds % 60u;

    snprintf(
        out,
        out_size,
        "%02u:%02u:%02u",
        hours,
        minutes,
        secs
    );
}


/* ============================================================
 * COLOR INTERPOLATION
 *
 * color1 -> color2
 * t = 0   : color1
 * t = 255 : color2
 * ============================================================ */

static lv_color_t interpolate_hex(
    uint32_t color1,
    uint32_t color2,
    uint8_t t
)
{
    const uint8_t r1 = (uint8_t)((color1 >> 16) & 0xFF);
    const uint8_t g1 = (uint8_t)((color1 >> 8)  & 0xFF);
    const uint8_t b1 = (uint8_t)( color1        & 0xFF);

    const uint8_t r2 = (uint8_t)((color2 >> 16) & 0xFF);
    const uint8_t g2 = (uint8_t)((color2 >> 8)  & 0xFF);
    const uint8_t b2 = (uint8_t)( color2        & 0xFF);

    const uint8_t r =
        (uint8_t)((int)r1 + (((int)r2 - (int)r1) * t) / 255);

    const uint8_t g =
        (uint8_t)((int)g1 + (((int)g2 - (int)g1) * t) / 255);

    const uint8_t b =
        (uint8_t)((int)b1 + (((int)b2 - (int)b1) * t) / 255);

    return lv_color_make(r, g, b);
}


/* ============================================================
 * BATTERY COLOR
 *
 * 0..50%:
 * BAT_COLOR_0_PCT -> BAT_COLOR_50_PCT
 *
 * 50..100%:
 * BAT_COLOR_50_PCT -> BAT_COLOR_100_PCT
 * ============================================================ */

static lv_color_t battery_color_from_percent(int percent)
{
    if (percent < 0) {
        percent = 0;
    }

    if (percent > 100) {
        percent = 100;
    }

    if (percent >= 50) {

        const uint8_t t =
            (uint8_t)(((percent - 50) * 255) / 50);

        return interpolate_hex(
            BAT_COLOR_50_PCT,
            BAT_COLOR_100_PCT,
            t
        );
    }

    const uint8_t t =
        (uint8_t)((percent * 255) / 50);

    return interpolate_hex(
        BAT_COLOR_0_PCT,
        BAT_COLOR_50_PCT,
        t
    );
}


/* ============================================================
 * SIGNAL COLOR
 *
 * Kiekvienam stulpelių lygiui galima nurodyti atskirą
 * tikslią HEX spalvą.
 * ============================================================ */

static lv_color_t signal_color_from_percent(int percent)
{
    if (percent <= 20) {
        return lv_color_hex(SIGNAL_COLOR_20_PCT);
    }

    if (percent <= 40) {
        return lv_color_hex(SIGNAL_COLOR_40_PCT);
    }

    if (percent <= 60) {
        return lv_color_hex(SIGNAL_COLOR_60_PCT);
    }

    if (percent <= 80) {
        return lv_color_hex(SIGNAL_COLOR_80_PCT);
    }

    return lv_color_hex(SIGNAL_COLOR_100_PCT);
}


static void chart_fill_none(lv_coord_t *points)
{
    for (int i = 0; i < CHART_POINT_COUNT; ++i) {
        points[i] = LV_CHART_POINT_NONE;
    }
}

static void chart_init_runtime(void)
{
    /* Keep the visual 0..30 A and 0..80 A scales designed in SquareLine,
     * but store values in 0.1 A units so the lines retain useful resolution. */
    lv_chart_set_axis_range(
        ui_L1L2L3Chart,
        LV_CHART_AXIS_PRIMARY_Y,
        0,
        CHART_PHASE_MAX_X10
    );

    lv_chart_set_axis_range(
        ui_FullLoadChart,
        LV_CHART_AXIS_PRIMARY_Y,
        0,
        CHART_TOTAL_MAX_X10
    );

    lv_chart_set_update_mode(
        ui_L1L2L3Chart,
        LV_CHART_UPDATE_MODE_SHIFT
    );
    lv_chart_set_update_mode(
        ui_FullLoadChart,
        LV_CHART_UPDATE_MODE_SHIFT
    );

    s_chart_l1 = lv_chart_get_series_next(ui_L1L2L3Chart, NULL);
    s_chart_l2 = lv_chart_get_series_next(ui_L1L2L3Chart, s_chart_l1);
    s_chart_l3 = lv_chart_get_series_next(ui_L1L2L3Chart, s_chart_l2);
    s_chart_total = lv_chart_get_series_next(ui_FullLoadChart, NULL);

    chart_fill_none(s_chart_l1_points);
    chart_fill_none(s_chart_l2_points);
    chart_fill_none(s_chart_l3_points);
    chart_fill_none(s_chart_total_points);

    if (s_chart_l1 != NULL) {
        lv_chart_set_series_ext_y_array(
            ui_L1L2L3Chart, s_chart_l1, s_chart_l1_points
        );
    }
    if (s_chart_l2 != NULL) {
        lv_chart_set_series_ext_y_array(
            ui_L1L2L3Chart, s_chart_l2, s_chart_l2_points
        );
    }
    if (s_chart_l3 != NULL) {
        lv_chart_set_series_ext_y_array(
            ui_L1L2L3Chart, s_chart_l3, s_chart_l3_points
        );
    }
    if (s_chart_total != NULL) {
        lv_chart_set_series_ext_y_array(
            ui_FullLoadChart, s_chart_total, s_chart_total_points
        );
    }

    lv_chart_refresh(ui_L1L2L3Chart);
    lv_chart_refresh(ui_FullLoadChart);
}

static lv_coord_t chart_current_x10(float amps)
{
    if (amps <= 0.0f) {
        return 0;
    }

    return (lv_coord_t)(amps * 10.0f + 0.5f);
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}


/* ============================================================
 * SIGNAL BAR BACKGROUND
 *
 * active = true:
 *   spalvotas background
 *
 * active = false:
 *   transparent background
 *
 * Border čia išvis neliečiamas.
 * ============================================================ */

static void set_signal_bar(
    lv_obj_t *bar,
    bool active,
    lv_color_t color
)
{
    /*
     * Objektas visada turi būti matomas,
     * nes norime matyti jo border.
     */
    set_hidden(bar, false);

    if (active) {

        lv_obj_set_style_bg_color(
            bar,
            color,
            LV_PART_MAIN | LV_STATE_DEFAULT
        );

        lv_obj_set_style_bg_opa(
            bar,
            LV_OPA_COVER,
            LV_PART_MAIN | LV_STATE_DEFAULT
        );

    } else {

        /*
         * Tik background transparent.
         * Border property neliečiam.
         */
        lv_obj_set_style_bg_opa(
            bar,
            LV_OPA_TRANSP,
            LV_PART_MAIN | LV_STATE_DEFAULT
        );
    }
}


static void update_signal(const display_ui_state_t *state)
{
    set_hidden(ui_SignalLOST, state->online);
    set_hidden(ui_SignalAntena, !state->online);

    lv_obj_t *bars[5] = {
        ui_Signal20percent,
        ui_Signal40percent,
        ui_Signal60percent,
        ui_Signal80percent,
        ui_Signal100percent
    };


    /*
     * OFFLINE:
     *
     * Antena paslėpta.
     * N/C rodomas.
     * Visi signal bars lieka su border,
     * bet jų background transparent.
     */
    if (!state->online) {

        const lv_color_t dummy_color =
            lv_color_hex(SIGNAL_COLOR_20_PCT);

        for (int i = 0; i < 5; i++) {
            set_signal_bar(
                bars[i],
                false,
                dummy_color
            );
        }

        return;
    }


    int p = state->signal_percent;

    if (p < 1) {
        p = 1;
    }

    if (p > 100) {
        p = 100;
    }


    const lv_color_t color =
        signal_color_from_percent(p);


    /*
     * Signal percentage -> aktyvių stulpelių skaičius
     *
     * 1..20%   -> 1
     * 21..40%  -> 2
     * 41..60%  -> 3
     * 61..80%  -> 4
     * 81..100% -> 5
     */

    const int active_bars =
        (p <= 20) ? 1 :
        (p <= 40) ? 2 :
        (p <= 60) ? 3 :
        (p <= 80) ? 4 :
                    5;


    /*
     * Nuspalvinam tik aktyvių barų background.
     *
     * Neaktyvūs lieka:
     *
     * background transparent
     * border matomas
     */

    for (int i = 0; i < 5; i++) {

        set_signal_bar(
            bars[i],
            i < active_bars,
            color
        );
    }


    /*
     * Antena tokios pačios spalvos
     * kaip aktyvūs stulpeliai.
     */
    lv_obj_set_style_text_color(
        ui_SignalAntena,
        color,
        LV_PART_MAIN | LV_STATE_DEFAULT
    );
}


static void update_battery(int percent)
{
    if (percent < 0) {
        percent = 0;
    }

    if (percent > 100) {
        percent = 100;
    }

    lv_bar_set_value(
        ui_Bar1,
        percent,
        LV_ANIM_ON
    );

    lv_obj_set_style_bg_color(
        ui_Bar1,
        battery_color_from_percent(percent),
        LV_PART_INDICATOR | LV_STATE_DEFAULT
    );
}


static void update_view(display_view_t view)
{
    set_hidden(
        ui_ThreePhaseWINDOW,
        view != DISPLAY_VIEW_THREE_PHASE
    );

    set_hidden(
        ui_SinglePhaseWINDOW,
        view != DISPLAY_VIEW_SINGLE_PHASE
    );

    set_hidden(
        ui_L1L2L3Chart,
        view != DISPLAY_VIEW_L1L2L3_CHART
    );

    set_hidden(
        ui_FullLoadChart,
        view != DISPLAY_VIEW_FULL_LOAD_CHART
    );

    /* BottomINFONavigation is intentionally always visible. */
    set_hidden(
        ui_BottomINFONavigation,
        false
    );
}


static void update_measurements(
    const display_ui_state_t *state
)
{
    if (!state->online) {

        lv_label_set_text(
            ui_L1AMPS,
            " --.-- A"
        );

        lv_label_set_text(
            ui_L2AMPS,
            " --.-- A"
        );

        lv_label_set_text(
            ui_L3AMPS,
            "--.-- A"
        );

        lv_label_set_text(
            ui_L1KVA,
            "--.-- kVA"
        );

        lv_label_set_text(
            ui_L2KVA,
            "--.-- kVA"
        );

        lv_label_set_text(
            ui_L3KVA,
            "--.-- kVA"
        );

        lv_label_set_text(
            ui_ThreePhasesFullAMPS,
            "--.-- A"
        );

        lv_label_set_text(
            ui_ThreePhasesFullKVA,
            "--.-- kVA"
        );

        return;
    }


    /*
     * Do not use %f here.
     *
     * LVGL formatter can be built without floating-point
     * formatting support, in which case "%.2f" may be
     * rendered as just "f".
     *
     * Convert values to hundredths and format as integers.
     */

    const int l1_ca =
        (int)(state->l1_a * 100.0f + 0.5f);

    const int l2_ca =
        (int)(state->l2_a * 100.0f + 0.5f);

    const int l3_ca =
        (int)(state->l3_a * 100.0f + 0.5f);


    const int l1_ckva =
        (int)(kva(state->l1_a) * 100.0f + 0.5f);

    const int l2_ckva =
        (int)(kva(state->l2_a) * 100.0f + 0.5f);

    const int l3_ckva =
        (int)(kva(state->l3_a) * 100.0f + 0.5f);


    const float total_a =
        state->l1_a +
        state->l2_a +
        state->l3_a;

    const int total_ca =
        (int)(total_a * 100.0f + 0.5f);

    const int total_ckva =
        (int)(kva(total_a) * 100.0f + 0.5f);


    lv_label_set_text_fmt(
        ui_L1AMPS,
        " %d.%02d A",
        l1_ca / 100,
        l1_ca % 100
    );

    lv_label_set_text_fmt(
        ui_L2AMPS,
        " %d.%02d A",
        l2_ca / 100,
        l2_ca % 100
    );

    lv_label_set_text_fmt(
        ui_L3AMPS,
        " %d.%02d A",
        l3_ca / 100,
        l3_ca % 100
    );


    lv_label_set_text_fmt(
        ui_L1KVA,
        "%d.%02d kVA",
        l1_ckva / 100,
        l1_ckva % 100
    );

    lv_label_set_text_fmt(
        ui_L2KVA,
        "%d.%02d kVA",
        l2_ckva / 100,
        l2_ckva % 100
    );

    lv_label_set_text_fmt(
        ui_L3KVA,
        "%d.%02d kVA",
        l3_ckva / 100,
        l3_ckva % 100
    );


    lv_label_set_text_fmt(
        ui_ThreePhasesFullAMPS,
        "%d.%02d A",
        total_ca / 100,
        total_ca % 100
    );

    lv_label_set_text_fmt(
        ui_ThreePhasesFullKVA,
        "%d.%02d kVA",
        total_ckva / 100,
        total_ckva % 100
    );
}


static void render_locked(
    const display_ui_state_t *state
)
{
    char uptime[20];

    format_uptime(
        state->uptime_seconds,
        uptime,
        sizeof(uptime)
    );

    lv_label_set_text(
        ui_UPtime,
        uptime
    );

    update_signal(state);
    update_battery(state->battery_percent);
    update_view(state->view);
    update_measurements(state);
}


esp_err_t display_ui_init(void)
{
    ESP_RETURN_ON_ERROR(
        display_st7789_init(),
        TAG,
        "ST7789 init failed"
    );

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    ui_init();
    chart_init_runtime();

    /*
     * Initial frame:
     * ThreePhase window,
     * offline,
     * battery unknown / 0%.
     */
    const display_ui_state_t initial = {
        .view = DISPLAY_VIEW_THREE_PHASE,
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

    /*
     * Force first complete LVGL frame while
     * panel/backlight are still hidden.
     */
    lv_refr_now(NULL);

    lvgl_port_unlock();

    vTaskDelay(
        pdMS_TO_TICKS(
            DISPLAY_STARTUP_BLANK_MS
        )
    );

    ESP_RETURN_ON_ERROR(
        display_st7789_panel_set_visible(true),
        TAG,
        "Panel ON failed"
    );

    ESP_RETURN_ON_ERROR(
        display_st7789_backlight_set(
            DISPLAY_STARTUP_BRIGHTNESS_PCT
        ),
        TAG,
        "Backlight ON failed"
    );

    return ESP_OK;
}


esp_err_t display_ui_chart_add_sample(const display_ui_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_chart_l1 == NULL ||
        s_chart_l2 == NULL ||
        s_chart_l3 == NULL ||
        s_chart_total == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (state->online) {
        const float total_a = state->l1_a + state->l2_a + state->l3_a;

        lv_chart_set_next_value(
            ui_L1L2L3Chart, s_chart_l1, chart_current_x10(state->l1_a)
        );
        lv_chart_set_next_value(
            ui_L1L2L3Chart, s_chart_l2, chart_current_x10(state->l2_a)
        );
        lv_chart_set_next_value(
            ui_L1L2L3Chart, s_chart_l3, chart_current_x10(state->l3_a)
        );
        lv_chart_set_next_value(
            ui_FullLoadChart, s_chart_total, chart_current_x10(total_a)
        );
    } else {
        /* A missing minute is a gap, not a fake 0 A measurement. */
        lv_chart_set_next_value(
            ui_L1L2L3Chart, s_chart_l1, LV_CHART_POINT_NONE
        );
        lv_chart_set_next_value(
            ui_L1L2L3Chart, s_chart_l2, LV_CHART_POINT_NONE
        );
        lv_chart_set_next_value(
            ui_L1L2L3Chart, s_chart_l3, LV_CHART_POINT_NONE
        );
        lv_chart_set_next_value(
            ui_FullLoadChart, s_chart_total, LV_CHART_POINT_NONE
        );
    }

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t display_ui_render(
    const display_ui_state_t *state
)
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