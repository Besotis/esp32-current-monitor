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

#define CHART_POINT_COUNT         180
#define CHART_PHASE_SCALE_X10      10
#define CHART_TOTAL_SCALE_X10      10
#define CHART_DEFAULT_MAX_X10      50   /* Start at 0..5 A */
#define CHART_X_DIV_LINES_NORMAL    7   /* 15/30/60 min: 6 equal X intervals */
#define CHART_X_DIV_LINES_5MIN     11   /* 0..5 min: 10 grid intervals = 30 s each */
#define CHART_Y_LABEL_COUNT         4
#define CHART_Y_LABEL_COLOR    0xE2E2E2
#define CHART_Y_LABEL_X_PAD_PX      4
#define CHART_Y_PLOT_TOP_PAD_PX      4
#define CHART_Y_PLOT_BOTTOM_PAD_PX   12
#define CHART_X_LABEL_COUNT         3
#define CHART_X_LABEL_COLOR    0xE2E2E2
#define CHART_X_PLOT_LEFT_PAD_PX     4
#define CHART_X_PLOT_RIGHT_PAD_PX   12
#define CHART_X_LABEL_Y_OFFSET_PX   10
#define CHART_X_LAST_LABEL_OFFSET_PX -4

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

static lv_obj_t *s_phase_y_labels[CHART_Y_LABEL_COUNT];
static lv_obj_t *s_total_y_labels[CHART_Y_LABEL_COUNT];
static lv_obj_t *s_phase_x_labels[CHART_X_LABEL_COUNT];
static lv_obj_t *s_total_x_labels[CHART_X_LABEL_COUNT];
static int s_chart_time_window_min = -1;
static int s_phase_scale_max_a = -1;
static int s_total_scale_max_a = -1;
static int s_chart_sample_count = 0;


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

typedef struct {
    int max_a;
    int div_lines;
    int label_values[CHART_Y_LABEL_COUNT];
    int label_count;
} chart_scale_t;

static chart_scale_t chart_scale_for_max_x10(lv_coord_t max_x10)
{
    /* The scale steps are intentionally fixed so the chart does not jump
     * through arbitrary ranges and the grid always lands on useful values. */
    if (max_x10 <= 50) {
        return (chart_scale_t){5, 6, {1, 3, 5, 0}, 3};
    }
    if (max_x10 <= 100) {
        return (chart_scale_t){10, 5, {5, 10, 0, 0}, 2};
    }
    if (max_x10 <= 200) {
        return (chart_scale_t){20, 5, {10, 20, 0, 0}, 2};
    }
    if (max_x10 <= 300) {
        return (chart_scale_t){30, 4, {10, 20, 30, 0}, 3};
    }
    if (max_x10 <= 500) {
        return (chart_scale_t){50, 6, {10, 30, 50, 0}, 3};
    }
    if (max_x10 <= 800) {
        return (chart_scale_t){80, 5, {20, 40, 60, 80}, 4};
    }

    /* Safety fallback for a possible 3 x 30 A full-load value above 80 A. */
    return (chart_scale_t){100, 6, {20, 40, 60, 100}, 4};
}

static lv_coord_t chart_points_max(const lv_coord_t *a,
                                   const lv_coord_t *b,
                                   const lv_coord_t *c)
{
    lv_coord_t max_value = 0;

    for (int i = 0; i < CHART_POINT_COUNT; ++i) {
        const lv_coord_t values[3] = {
            a != NULL ? a[i] : LV_CHART_POINT_NONE,
            b != NULL ? b[i] : LV_CHART_POINT_NONE,
            c != NULL ? c[i] : LV_CHART_POINT_NONE,
        };

        for (int j = 0; j < 3; ++j) {
            if (values[j] != LV_CHART_POINT_NONE && values[j] > max_value) {
                max_value = values[j];
            }
        }
    }

    return max_value;
}

static void chart_create_y_labels(lv_obj_t *chart, lv_obj_t **labels)
{
    for (int i = 0; i < CHART_Y_LABEL_COUNT; ++i) {
        labels[i] = lv_label_create(chart);
        lv_obj_set_size(labels[i], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(labels[i], lv_color_hex(CHART_Y_LABEL_COLOR),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(labels[i], LV_OPA_COVER,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(labels[i], &lv_font_montserrat_12,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_transform_rotation(labels[i], 900,
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void chart_apply_scale(lv_obj_t *chart,
                              lv_obj_t **labels,
                              int *current_max_a,
                              chart_scale_t scale)
{
    if (*current_max_a == scale.max_a) {
        return;
    }

    *current_max_a = scale.max_a;

    /* Chart values are stored in 0.1 A units. */
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, scale.max_a * 10);
    lv_chart_set_div_line_count(chart, scale.div_lines,
                                s_chart_time_window_min == 5 ? CHART_X_DIV_LINES_5MIN
                                                             : CHART_X_DIV_LINES_NORMAL);

    for (int i = 0; i < CHART_Y_LABEL_COUNT; ++i) {
        if (i >= scale.label_count) {
            lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const int value_a = scale.label_values[i];
        lv_label_set_text_fmt(labels[i], "%dA", value_a);

        /* Match the label center to the real chart plot area.
         *
         * The chart object is 236 x 166 px, but the horizontal grid does not
         * start at object Y=0 or end at Y=165.  On the physical display the
         * top and bottom grid lines are both inset by 8 px.  For a 166 px
         * chart this gives a real Y plot span of 8..157 (149 px).
         *
         * Keeping these as explicit plot paddings makes every dynamic scale
         * (5/10/20/30/50/80 A) use the same calibrated geometry.  The
         * existing SquareLine "0" label is intentionally left untouched. */
        lv_obj_set_align(labels[i], LV_ALIGN_TOP_LEFT);
        lv_obj_update_layout(chart);
        lv_obj_update_layout(labels[i]);

        const int32_t chart_h = lv_obj_get_height(chart);
        const int32_t plot_top = CHART_Y_PLOT_TOP_PAD_PX;
        const int32_t plot_bottom = chart_h > 0
            ? chart_h - 1 - CHART_Y_PLOT_BOTTOM_PAD_PX
            : 0;
        const int32_t plot_h = plot_bottom > plot_top
            ? plot_bottom - plot_top
            : 0;
        const int32_t label_h = lv_obj_get_height(labels[i]);
        const int32_t line_y = plot_top +
            (int32_t)(((int64_t)(scale.max_a - value_a) * plot_h
                       + (scale.max_a / 2)) / scale.max_a);

        lv_obj_set_x(labels[i], CHART_Y_LABEL_X_PAD_PX);
        lv_obj_set_y(labels[i], line_y - (label_h / 2));
        lv_obj_remove_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_chart_refresh(chart);
}

static void chart_create_x_labels(lv_obj_t *chart, lv_obj_t **labels)
{
    for (int i = 0; i < CHART_X_LABEL_COUNT; ++i) {
        labels[i] = lv_label_create(chart);
        lv_obj_set_size(labels[i], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(labels[i], lv_color_hex(CHART_X_LABEL_COLOR),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(labels[i], LV_OPA_COVER,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(labels[i], &lv_font_montserrat_12,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_align(labels[i], LV_ALIGN_BOTTOM_LEFT);
        lv_obj_set_y(labels[i], CHART_X_LABEL_Y_OFFSET_PX);
    }
}

static void chart_position_x_labels(lv_obj_t *chart, lv_obj_t **labels,
                                    int window_min)
{
    const int values_5[3]  = {1, 3, 5};
    const int values_15[3] = {5, 10, 15};
    const int values_30[3] = {10, 20, 30};
    const int values_60[3] = {20, 40, 60};
    const int *values = window_min <= 5 ? values_5
                      : window_min <= 15 ? values_15
                      : window_min <= 30 ? values_30 : values_60;

    lv_obj_update_layout(chart);
    const int32_t chart_w = lv_obj_get_width(chart);
    const int32_t plot_left = CHART_X_PLOT_LEFT_PAD_PX;
    const int32_t plot_right = chart_w > 0
        ? chart_w - 1 - CHART_X_PLOT_RIGHT_PAD_PX : 0;
    const int32_t plot_w = plot_right > plot_left ? plot_right - plot_left : 0;

    for (int i = 0; i < CHART_X_LABEL_COUNT; ++i) {
        if (i == CHART_X_LABEL_COUNT - 1) {
            /* Last label intentionally has no "min" so it fits on 240 px. */
            lv_label_set_text_fmt(labels[i], "%d", values[i]);
        } else {
            lv_label_set_text_fmt(labels[i], "%dmin", values[i]);
        }
        lv_obj_update_layout(labels[i]);
        const int32_t label_w = lv_obj_get_width(labels[i]);
        const int32_t x = plot_left + ((int32_t)values[i] * plot_w) / window_min;
        const int32_t last_offset = (i == CHART_X_LABEL_COUNT - 1)
            ? CHART_X_LAST_LABEL_OFFSET_PX : 0;
        lv_obj_set_x(labels[i], x - label_w / 2 + last_offset);
    }
}

static void chart_update_time_window(void)
{
    /* Samples arrive every 20 s: 15=5 min, 45=15 min, 90=30 min, 180=60 min. */
    const int window_min = s_chart_sample_count <= 15 ? 5
                         : s_chart_sample_count <= 45 ? 15
                         : s_chart_sample_count <= 90 ? 30 : 60;
    if (window_min == s_chart_time_window_min) {
        return;
    }
    s_chart_time_window_min = window_min;

    /* Change X spacing for the active time window while keeping the full
     * 180-point / 60-minute history buffers intact. */
    const int visible_points = window_min * 3; /* one point every 20 s */
    lv_chart_set_point_count(ui_L1L2L3Chart, visible_points);
    lv_chart_set_point_count(ui_FullLoadChart, visible_points);

    const int x_div_lines = window_min == 5 ? CHART_X_DIV_LINES_5MIN
                                             : CHART_X_DIV_LINES_NORMAL;
    /* Preserve each chart's current dynamic Y grid while changing only X grid density. */
    const chart_scale_t phase_scale = chart_scale_for_max_x10(chart_points_max(
        s_chart_l1_points, s_chart_l2_points, s_chart_l3_points));
    const chart_scale_t total_scale = chart_scale_for_max_x10(chart_points_max(
        s_chart_total_points, NULL, NULL));
    lv_chart_set_div_line_count(ui_L1L2L3Chart, phase_scale.div_lines, x_div_lines);
    lv_chart_set_div_line_count(ui_FullLoadChart, total_scale.div_lines, x_div_lines);

    /* Re-bind external arrays after point-count changes. */
    if (s_chart_l1 != NULL) lv_chart_set_series_ext_y_array(ui_L1L2L3Chart, s_chart_l1, s_chart_l1_points);
    if (s_chart_l2 != NULL) lv_chart_set_series_ext_y_array(ui_L1L2L3Chart, s_chart_l2, s_chart_l2_points);
    if (s_chart_l3 != NULL) lv_chart_set_series_ext_y_array(ui_L1L2L3Chart, s_chart_l3, s_chart_l3_points);
    if (s_chart_total != NULL) lv_chart_set_series_ext_y_array(ui_FullLoadChart, s_chart_total, s_chart_total_points);

    chart_position_x_labels(ui_L1L2L3Chart, s_phase_x_labels, window_min);
    chart_position_x_labels(ui_FullLoadChart, s_total_x_labels, window_min);
    lv_chart_refresh(ui_L1L2L3Chart);
    lv_chart_refresh(ui_FullLoadChart);
}

static void chart_update_dynamic_scales(void)
{
    const lv_coord_t phase_max = chart_points_max(
        s_chart_l1_points, s_chart_l2_points, s_chart_l3_points);
    const lv_coord_t total_max = chart_points_max(
        s_chart_total_points, NULL, NULL);

    chart_apply_scale(ui_L1L2L3Chart, s_phase_y_labels, &s_phase_scale_max_a,
                      chart_scale_for_max_x10(phase_max));
    chart_apply_scale(ui_FullLoadChart, s_total_y_labels, &s_total_scale_max_a,
                      chart_scale_for_max_x10(total_max));
}

static void chart_init_runtime(void)
{
    lv_chart_set_axis_range(
        ui_L1L2L3Chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_DEFAULT_MAX_X10);
    lv_chart_set_axis_range(
        ui_FullLoadChart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_DEFAULT_MAX_X10);

    lv_chart_set_update_mode(ui_L1L2L3Chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_update_mode(ui_FullLoadChart, LV_CHART_UPDATE_MODE_SHIFT);

    s_chart_l1 = lv_chart_get_series_next(ui_L1L2L3Chart, NULL);
    s_chart_l2 = lv_chart_get_series_next(ui_L1L2L3Chart, s_chart_l1);
    s_chart_l3 = lv_chart_get_series_next(ui_L1L2L3Chart, s_chart_l2);
    s_chart_total = lv_chart_get_series_next(ui_FullLoadChart, NULL);

    chart_fill_none(s_chart_l1_points);
    chart_fill_none(s_chart_l2_points);
    chart_fill_none(s_chart_l3_points);
    chart_fill_none(s_chart_total_points);

    if (s_chart_l1 != NULL) {
        lv_chart_set_series_ext_y_array(ui_L1L2L3Chart, s_chart_l1, s_chart_l1_points);
    }
    if (s_chart_l2 != NULL) {
        lv_chart_set_series_ext_y_array(ui_L1L2L3Chart, s_chart_l2, s_chart_l2_points);
    }
    if (s_chart_l3 != NULL) {
        lv_chart_set_series_ext_y_array(ui_L1L2L3Chart, s_chart_l3, s_chart_l3_points);
    }
    if (s_chart_total != NULL) {
        lv_chart_set_series_ext_y_array(ui_FullLoadChart, s_chart_total, s_chart_total_points);
    }

    chart_create_y_labels(ui_L1L2L3Chart, s_phase_y_labels);
    chart_create_y_labels(ui_FullLoadChart, s_total_y_labels);
    chart_create_x_labels(ui_L1L2L3Chart, s_phase_x_labels);
    chart_create_x_labels(ui_FullLoadChart, s_total_x_labels);
    chart_update_dynamic_scales();
    chart_update_time_window();
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


static void update_temperature(const display_ui_state_t *state)
{
    if (!state->temperature_valid) {
        lv_label_set_text(ui_Temperature, "--.-°C");
        return;
    }

    /* LVGL printf is intentionally kept integer-only in this project. */
    int temp_tenths = (int)(state->temperature_c * 10.0f +
                            (state->temperature_c >= 0.0f ? 0.5f : -0.5f));
    const bool negative = temp_tenths < 0;
    if (negative) {
        temp_tenths = -temp_tenths;
    }

    lv_label_set_text_fmt(ui_Temperature, "%s%d.%d°C",
                          negative ? "-" : "",
                          temp_tenths / 10,
                          temp_tenths % 10);
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
    update_temperature(state);
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
        .temperature_valid = false,
        .temperature_c = 0.0f,
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


esp_err_t display_ui_chart_add_sample(float l1_a, float l2_a, float l3_a, float total_a, bool valid)
{

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

    /* Fill from left to right while the first 60 minutes are collected.
     * At 20 s/sample, 180 points = 60 minutes. Only then shift oldest out. */
    int index;
    if (s_chart_sample_count < CHART_POINT_COUNT) {
        index = s_chart_sample_count++;
    } else {
        for (int i = 1; i < CHART_POINT_COUNT; ++i) {
            s_chart_l1_points[i - 1] = s_chart_l1_points[i];
            s_chart_l2_points[i - 1] = s_chart_l2_points[i];
            s_chart_l3_points[i - 1] = s_chart_l3_points[i];
            s_chart_total_points[i - 1] = s_chart_total_points[i];
        }
        index = CHART_POINT_COUNT - 1;
    }

    if (valid) {
        s_chart_l1_points[index] = chart_current_x10(l1_a);
        s_chart_l2_points[index] = chart_current_x10(l2_a);
        s_chart_l3_points[index] = chart_current_x10(l3_a);
        s_chart_total_points[index] = chart_current_x10(total_a);
    } else {
        /* No received measurement in this 20 s bucket: draw a gap, not fake 0 A. */
        s_chart_l1_points[index] = LV_CHART_POINT_NONE;
        s_chart_l2_points[index] = LV_CHART_POINT_NONE;
        s_chart_l3_points[index] = LV_CHART_POINT_NONE;
        s_chart_total_points[index] = LV_CHART_POINT_NONE;
    }

    chart_update_dynamic_scales();
    chart_update_time_window();
    lv_chart_refresh(ui_L1L2L3Chart);
    lv_chart_refresh(ui_FullLoadChart);

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