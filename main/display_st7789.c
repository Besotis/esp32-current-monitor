#include "display_st7789.h"
#include "board_config.h"

#include <stdbool.h>
#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "ST7789";

static esp_lcd_panel_io_handle_t s_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static bool s_backlight_pwm_ready = false;

#define DISPLAY_SPI_HOST     SPI2_HOST

#define BL_PWM_TIMER       LEDC_TIMER_0
#define BL_PWM_MODE        LEDC_LOW_SPEED_MODE
#define BL_PWM_CHANNEL     LEDC_CHANNEL_0
#define BL_PWM_FREQ_HZ     5000
#define BL_PWM_RESOLUTION  LEDC_TIMER_10_BIT
#define BL_PWM_MAX_DUTY    ((1U << 10) - 1U)

esp_err_t display_st7789_early_backlight_off(void)
{
    /*
     * Do this as early as possible.  For absolutely artifact-free cold boot,
     * a small external pulldown (for example 10 kOhm) on BLK is still useful,
     * because software cannot control the pin before the ESP32 starts.
     */
    ESP_RETURN_ON_ERROR(gpio_reset_pin(PIN_DISPLAY_BLK), TAG, "BLK reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(PIN_DISPLAY_BLK, GPIO_MODE_OUTPUT), TAG, "BLK direction failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(PIN_DISPLAY_BLK, 0), TAG, "BLK off failed");
    return ESP_OK;
}

static esp_err_t backlight_pwm_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = BL_PWM_MODE,
        .duty_resolution = BL_PWM_RESOLUTION,
        .timer_num = BL_PWM_TIMER,
        .freq_hz = BL_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "LEDC timer init failed");

    const ledc_channel_config_t channel_cfg = {
        .gpio_num = PIN_DISPLAY_BLK,
        .speed_mode = BL_PWM_MODE,
        .channel = BL_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_cfg), TAG, "LEDC channel init failed");
    s_backlight_pwm_ready = true;
    return ESP_OK;
}

static esp_err_t lcd_bus_init(void)
{
    const spi_bus_config_t bus_cfg = {
        .sclk_io_num = PIN_DISPLAY_SCLK,
        .mosi_io_num = PIN_DISPLAY_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_LVGL_BUFFER_LINES * 2,
    };

    return spi_bus_initialize(DISPLAY_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
}

static esp_err_t lcd_panel_init(void)
{
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DISPLAY_DC,
        .cs_gpio_num = -1, /* GMT130-V1.0 7-pin module has no exposed CS */
        .pclk_hz = DISPLAY_SPI_FREQ_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = DISPLAY_SPI_MODE,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI_HOST, &io_cfg, &s_io),
        TAG,
        "Panel IO init failed"
    );

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_DISPLAY_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .vendor_config = NULL,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel), TAG, "ST7789 create failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "Panel invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, DISPLAY_X_GAP, DISPLAY_Y_GAP), TAG, "Panel gap failed");

    /* Keep controller output disabled until SquareLine has rendered the first frame. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, false), TAG, "Panel OFF failed");
    return ESP_OK;
}

static esp_err_t lvgl_display_init(void)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL port init failed");

    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = s_io,
        .panel_handle = s_panel,
        .buffer_size = DISPLAY_WIDTH * DISPLAY_LVGL_BUFFER_LINES,
        .double_buffer = true,
        .hres = DISPLAY_WIDTH,
        .vres = DISPLAY_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = true,
        },
        .flags = {
            .buff_dma = true,
        },
    };

    if (lvgl_port_add_disp(&display_cfg) == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_disp() failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t display_st7789_init(void)
{
    ESP_LOGI(TAG, "ST7789 init: 240x240, SPI mode %d, %d MHz, gap %d/%d",
             DISPLAY_SPI_MODE,
             DISPLAY_SPI_FREQ_HZ / 1000000,
             DISPLAY_X_GAP,
             DISPLAY_Y_GAP);

    ESP_RETURN_ON_ERROR(display_st7789_early_backlight_off(), TAG, "Early BLK off failed");
    ESP_RETURN_ON_ERROR(backlight_pwm_init(), TAG, "Backlight PWM init failed");
    ESP_RETURN_ON_ERROR(lcd_bus_init(), TAG, "SPI bus init failed");
    ESP_RETURN_ON_ERROR(lcd_panel_init(), TAG, "LCD panel init failed");
    ESP_RETURN_ON_ERROR(lvgl_display_init(), TAG, "LVGL display init failed");
    return ESP_OK;
}

esp_err_t display_st7789_backlight_set(int percent)
{
    if (!s_backlight_pwm_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    const uint32_t duty = (BL_PWM_MAX_DUTY * (uint32_t)percent + 50U) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(BL_PWM_MODE, BL_PWM_CHANNEL, duty), TAG, "Set BL duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(BL_PWM_MODE, BL_PWM_CHANNEL), TAG, "Update BL duty failed");

    ESP_LOGI(TAG, "Backlight: %d%% (duty=%" PRIu32 ")", percent, duty);
    return ESP_OK;
}

esp_err_t display_st7789_panel_set_visible(bool visible)
{
    if (s_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_disp_on_off(s_panel, visible);
}
