#include "display_ui.h"
#include "board_config.h"
#include "ch1115.h"

#include <stdio.h>
#include <stddef.h>

static void format_uptime(
    unsigned seconds,
    char *out,
    size_t out_size
)
{
    if (seconds < 60u) {
        snprintf(
            out,
            out_size,
            "%u",
            seconds
        );
        return;
    }

    if (seconds < 3600u) {
        unsigned minutes = seconds / 60u;
        unsigned secs = seconds % 60u;

        snprintf(
            out,
            out_size,
            "%u:%02u",
            minutes,
            secs
        );
        return;
    }

    unsigned hours = seconds / 3600u;
    unsigned minutes = (seconds / 60u) % 60u;
    unsigned secs = seconds % 60u;

    snprintf(
        out,
        out_size,
        "%u:%02u:%02u",
        hours,
        minutes,
        secs
    );
}

static float kva(float current_a)
{
    return
        NOMINAL_PHASE_VOLTAGE_V *
        current_a /
        1000.0f;
}

/*
 * Mini antenos ikona 8x7 px.
 * Piešiama viršutinėje statuso juostoje.
 */
static void draw_signal_icon(
    int x,
    int y
)
{
    /*
     * 4 vertikalios juostos kaip klasikinė signal strength ikona.
     * Plotis ~9 px, aukštis 7 px.
     */
    const int heights[4] = {2, 4, 6, 7};

    for (int bar = 0; bar < 4; ++bar) {
        int bx = x + bar * 2;

        for (int py = 0; py < heights[bar]; ++py) {
            ch1115_draw_pixel(
                bx,
                y + 6 - py,
                true
            );
        }
    }
}

/*
 * Mini baterijos ikona 11x7 px.
 * Vidus užpildomas pagal battery_percent.
 */
static void draw_battery_icon(
    int x,
    int y,
    int battery_percent
)
{
    if (battery_percent < 0) {
        battery_percent = 0;
    }

    if (battery_percent > 100) {
        battery_percent = 100;
    }

    /* korpusas 9x7 */
    for (int px = 0; px <= 8; ++px) {
        ch1115_draw_pixel(x + px, y + 0, true);
        ch1115_draw_pixel(x + px, y + 6, true);
    }

    for (int py = 0; py <= 6; ++py) {
        ch1115_draw_pixel(x + 0, y + py, true);
        ch1115_draw_pixel(x + 8, y + py, true);
    }

    /* baterijos kontaktas */
    ch1115_draw_pixel(x + 9, y + 2, true);
    ch1115_draw_pixel(x + 10, y + 2, true);
    ch1115_draw_pixel(x + 9, y + 3, true);
    ch1115_draw_pixel(x + 10, y + 3, true);
    ch1115_draw_pixel(x + 9, y + 4, true);
    ch1115_draw_pixel(x + 10, y + 4, true);

    /*
     * 7 vidiniai stulpeliai.
     * 100 % -> visi 7 užpildyti.
     */
    int fill =
        (battery_percent * 7 + 99) / 100;

    for (int px = 0; px < fill; ++px) {
        for (int py = 2; py <= 4; ++py) {
            ch1115_draw_pixel(
                x + 1 + px,
                y + py,
                true
            );
        }
    }
}

/*
 * Viršutinė statuso juosta:
 *
 * LAIKAS                    SIGNALAS                  BATERIJA
 * 1:02:33                  [ant] 92%                [bat] 87%
 *
 * Naudojamas tas pats įskaitomas 5x7 fontas.
 */
static void draw_status_bar(
    const display_ui_state_t *state
)
{
    char uptime[16];
    char signal_text[12];
    char battery_text[8];

    format_uptime(
        state->uptime_seconds,
        uptime,
        sizeof(uptime)
    );

    snprintf(
        battery_text,
        sizeof(battery_text),
        "%d%%",
        state->battery_percent
    );

    /*
     * Uptime visada prie kairio krašto.
     */
    ch1115_draw_text(
        0,
        0,
        uptime,
        1
    );

    /*
     * Signalas maždaug ekrano viduryje.
     * Kai ryšio nėra, vietoje procento rodoma OFF.
     */
    if (state->online) {
        snprintf(
            signal_text,
            sizeof(signal_text),
            "%d%%",
            state->signal_percent
        );

        /*
         * RSSI test mode:
         * rodome tikrą ESP-NOW priimto paketo RSSI dBm.
         */
        draw_signal_icon(
            47,
            0
        );

        ch1115_draw_text(
            56,
            0,
            signal_text,
            1
        );
    } else {
        /*
         * Offline: antenos ikona lieka matoma,
         * o signalo procentą pakeičia "- -".
         */
        draw_signal_icon(
            47,
            0
        );

        ch1115_draw_text(
            56,
            0,
            "...",
            1
        );
    }

    /*
     * Baterija lygiuojama prie dešinio krašto.
     * Ikona: x=91..101
     * Procentas: nuo x=104
     */
    draw_battery_icon(
        91,
        0,
        state->battery_percent
    );

    ch1115_draw_text(
        104,
        0,
        battery_text,
        1
    );
}

esp_err_t display_ui_init(void)
{
    return ch1115_init();
}

esp_err_t display_ui_render(
    const display_ui_state_t *state
)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char line[32];

    ch1115_clear();

    draw_status_bar(state);

    if (!state->online) {
        ch1115_draw_text(
            28,
            25,
            "--- A",
            2
        );

        ch1115_draw_text(
            34,
            51,
            "OFFLINE",
            1
        );

        return ch1115_flush();
    }

    if (state->mode == DISPLAY_MODE_GRID) {
        /*
         * L1/L2/L3 paliekami 5 px nuo kairio krašto.
         * Blokas nuleistas žemiau statuso juostos.
         */
        snprintf(
            line,
            sizeof(line),
            "L1 %.2fA %.2fkVA",
            state->l1_a,
            kva(state->l1_a)
        );

        ch1115_draw_text(
            10,
            21,
            line,
            1
        );

        snprintf(
            line,
            sizeof(line),
            "L2 %.2fA %.2fkVA",
            state->l2_a,
            kva(state->l2_a)
        );

        ch1115_draw_text(
            10,
            39,
            line,
            1
        );

        snprintf(
            line,
            sizeof(line),
            "L3 %.2fA %.2fkVA",
            state->l3_a,
            kva(state->l3_a)
        );

        ch1115_draw_text(
            10,
            57,
            line,
            1
        );
    } else {
        float total_a =
            state->l1_a +
            state->l2_a +
            state->l3_a;

        char current[20];
        char kva_text[20];

        snprintf(
            current,
            sizeof(current),
            "%.2f A",
            total_a
        );

        snprintf(
            kva_text,
            sizeof(kva_text),
            "%.2f kVA",
            kva(total_a)
        );

        ch1115_draw_text(
            20,
            17,
            current,
            2
        );

        ch1115_draw_text(
            14,
            43,
            kva_text,
            2
        );
    }

    return ch1115_flush();
}
