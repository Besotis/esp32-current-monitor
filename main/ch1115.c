#include "ch1115.h"
#include "board_config.h"

#include <stddef.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "CH1115";

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t s_addr = 0;
static uint8_t s_fb[CH1115_BUFSIZE];

static const uint8_t *glyph5x7(char c)
{
    /* 5 columns, LSB = top pixel */
    static const uint8_t blank[5] = {0,0,0,0,0};
    static const uint8_t A[5] = {0x7E,0x11,0x11,0x11,0x7E};
    static const uint8_t C[5] = {0x3E,0x41,0x41,0x41,0x22};
    static const uint8_t E[5] = {0x7F,0x49,0x49,0x49,0x41};
    static const uint8_t I[5] = {0x00,0x41,0x7F,0x41,0x00};
    static const uint8_t K[5] = {0x7F,0x08,0x14,0x22,0x41};
    static const uint8_t L[5] = {0x7F,0x40,0x40,0x40,0x40};
    static const uint8_t N[5] = {0x7F,0x02,0x0C,0x10,0x7F};
    static const uint8_t O[5] = {0x3E,0x41,0x41,0x41,0x3E};
    static const uint8_t R[5] = {0x7F,0x09,0x19,0x29,0x46};
    static const uint8_t S[5] = {0x46,0x49,0x49,0x49,0x31};
    static const uint8_t T[5] = {0x01,0x01,0x7F,0x01,0x01};
    static const uint8_t U[5] = {0x3F,0x40,0x40,0x40,0x3F};
    static const uint8_t G[5] = {0x3E,0x41,0x49,0x49,0x7A};
    static const uint8_t D[5] = {0x7F,0x41,0x41,0x22,0x1C};
    static const uint8_t M[5] = {0x7F,0x02,0x0C,0x02,0x7F};
    static const uint8_t F[5] = {0x7F,0x09,0x09,0x09,0x01};
    static const uint8_t P[5] = {0x7F,0x09,0x09,0x09,0x06};
    static const uint8_t V[5] = {0x1F,0x20,0x40,0x20,0x1F};
    static const uint8_t X[5] = {0x63,0x14,0x08,0x14,0x63};
    static const uint8_t Y[5] = {0x03,0x04,0x78,0x04,0x03};
    static const uint8_t H[5] = {0x7F,0x08,0x08,0x08,0x7F};
    static const uint8_t B[5] = {0x7F,0x49,0x49,0x49,0x36};
    static const uint8_t W[5] = {0x3F,0x40,0x38,0x40,0x3F};

    static const uint8_t n0[5] = {0x3E,0x51,0x49,0x45,0x3E};
    static const uint8_t n1[5] = {0x00,0x42,0x7F,0x40,0x00};
    static const uint8_t n2[5] = {0x42,0x61,0x51,0x49,0x46};
    static const uint8_t n3[5] = {0x21,0x41,0x45,0x4B,0x31};
    static const uint8_t n4[5] = {0x18,0x14,0x12,0x7F,0x10};
    static const uint8_t n5[5] = {0x27,0x45,0x45,0x45,0x39};
    static const uint8_t n6[5] = {0x3C,0x4A,0x49,0x49,0x30};
    static const uint8_t n7[5] = {0x01,0x71,0x09,0x05,0x03};
    static const uint8_t n8[5] = {0x36,0x49,0x49,0x49,0x36};
    static const uint8_t n9[5] = {0x06,0x49,0x49,0x29,0x1E};
    static const uint8_t dot[5] = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t pct[5] = {0x63,0x13,0x08,0x64,0x63};
    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};

    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');

    switch (c) {
        case 'A': return A; case 'B': return B; case 'C': return C;
        case 'D': return D; case 'E': return E; case 'F': return F;
        case 'G': return G; case 'H': return H; case 'I': return I;
        case 'K': return K; case 'L': return L; case 'M': return M;
        case 'N': return N; case 'O': return O; case 'P': return P;
        case 'R': return R; case 'S': return S; case 'T': return T;
        case 'U': return U; case 'V': return V; case 'W': return W;
        case 'X': return X; case 'Y': return Y;
        case '0': return n0; case '1': return n1; case '2': return n2;
        case '3': return n3; case '4': return n4; case '5': return n5;
        case '6': return n6; case '7': return n7; case '8': return n8;
        case '9': return n9;
        case '.': return dot; case '-': return dash; case '%': return pct;
        case ':': return colon;
        default: return blank;
    }
}

static esp_err_t write_cmds(const uint8_t *cmds, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[32];
    if (len + 1 > sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    buf[0] = 0x00; /* control byte: commands */
    memcpy(&buf[1], cmds, len);
    return i2c_master_transmit(s_dev, buf, len + 1, 100);
}

static esp_err_t write_data(const uint8_t *data, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    /* 1 control byte + up to 128 data bytes */
    uint8_t buf[129];
    if (len > 128) return ESP_ERR_INVALID_SIZE;
    buf[0] = 0x40; /* control byte: display data */
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(s_dev, buf, len + 1, 100);
}

esp_err_t ch1115_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_DISPLAY_SDA,
        .scl_io_num = PIN_DISPLAY_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    uint8_t candidates[] = {
        DISPLAY_I2C_ADDR_PRIMARY,
        DISPLAY_I2C_ADDR_SECONDARY
    };

    esp_err_t probe = ESP_FAIL;
    for (size_t i = 0; i < sizeof(candidates); ++i) {
        probe = i2c_master_probe(s_bus, candidates[i], 100);
        if (probe == ESP_OK) {
            s_addr = candidates[i];
            break;
        }
    }

    if (probe != ESP_OK) {
        ESP_LOGE(TAG, "CH1115 not found at 0x3C or 0x3D");
        return probe;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_addr,
        .scl_speed_hz = DISPLAY_I2C_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));

    /*
     * CH1115 128x64 init sequence.
     * Internal DC-DC enabled; page addressing used for framebuffer flush.
     */
    static const uint8_t init1[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide / oscillator */
        0xA8, 0x3F, /* multiplex 1/64 */
        0xD3, 0x00, /* display offset */
        0x40,       /* start line */
        0xAD, 0x8B, /* CH1115 DC-DC on */
        0xA1,       /* segment remap */
        0xC8,       /* COM scan direction */
        0xDA, 0x12, /* COM pins */
        0x81, 0x7F, /* contrast */
        0xD9, 0x22, /* pre-charge */
        0xDB, 0x20, /* VCOMH */
        0xA4,       /* RAM display */
        0xA6        /* normal display */
    };

    ESP_ERROR_CHECK(write_cmds(init1, sizeof(init1)));
    ch1115_clear();
    ESP_ERROR_CHECK(ch1115_flush());

    const uint8_t on = 0xAF;
    ESP_ERROR_CHECK(write_cmds(&on, 1));

    ESP_LOGI(TAG, "CH1115 ready: addr=0x%02X SDA=GPIO%d SCL=GPIO%d",
             s_addr, PIN_DISPLAY_SDA, PIN_DISPLAY_SCL);

    return ESP_OK;
}

void ch1115_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void ch1115_draw_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= CH1115_WIDTH || y < 0 || y >= CH1115_HEIGHT) return;

    size_t index = (size_t)x + (size_t)(y / 8) * CH1115_WIDTH;
    uint8_t mask = (uint8_t)(1u << (y & 7));

    if (on) s_fb[index] |= mask;
    else    s_fb[index] &= (uint8_t)~mask;
}

void ch1115_draw_char(int x, int y, char c, int scale)
{
    if (scale < 1) scale = 1;
    const uint8_t *g = glyph5x7(c);

    for (int col = 0; col < 5; ++col) {
        for (int row = 0; row < 7; ++row) {
            if ((g[col] >> row) & 0x01) {
                for (int sx = 0; sx < scale; ++sx) {
                    for (int sy = 0; sy < scale; ++sy) {
                        ch1115_draw_pixel(
                            x + col * scale + sx,
                            y + row * scale + sy,
                            true
                        );
                    }
                }
            }
        }
    }
}

void ch1115_draw_text(int x, int y, const char *text, int scale)
{
    if (!text) return;
    int cursor = x;
    int advance = 6 * (scale < 1 ? 1 : scale);

    while (*text) {
        ch1115_draw_char(cursor, y, *text++, scale);
        cursor += advance;
    }
}

esp_err_t ch1115_flush(void)
{
    /*
     * Most 128x64 CH1115 modules use a 2-column offset.
     * Page addressing: B0..B7, low column 0x02, high column 0x10.
     */
    for (int page = 0; page < 8; ++page) {
        uint8_t cmds[3] = {
            (uint8_t)(0xB0 + page),
            0x02,
            0x10
        };

        ESP_RETURN_ON_ERROR(
            write_cmds(cmds, sizeof(cmds)),
            TAG,
            "Failed to set page"
        );

        ESP_RETURN_ON_ERROR(
            write_data(&s_fb[page * CH1115_WIDTH], CH1115_WIDTH),
            TAG,
            "Failed to write page"
        );
    }

    return ESP_OK;
}
