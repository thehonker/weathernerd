/*
 * WeatherNerd — SH1106 OLED driver implementation
 *
 * Minimal I2C driver for 1.3" 128×64 monochrome OLED (SH1106 controller).
 * Uses a 1KB framebuffer in RAM (128×64 / 8 pages = 1024 bytes).
 * Text rendering with embedded 8×8 ASCII font (printable chars 0x20-0x7F).
 *
 * The SH1106 is similar to the SSD1306 but uses page addressing with
 * a column offset of 2 (the SH1106's visible columns start at column 2).
 */

#include "oled.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "oled";

/* ---- 8×8 font (ASCII 0x20-0x7F, 96 characters, 768 bytes) ---- */
/* Each char is 8 bytes, one per row, MSB = leftmost pixel. */
/* Generated from standard font8x8_basic. */
#include "oled_font.h"

/* ---- Display buffer ---- */
static uint8_t s_buffer[OLED_WIDTH * OLED_HEIGHT / 8];  /* 1024 bytes */
static int s_cursor_col = 0;
static int s_cursor_row = 0;

/* ---- I2C helpers ---- */

static esp_err_t oled_write_cmd(i2c_master_dev_handle_t dev, uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };  /* Co=0, D/C=0 → command */
    return i2c_master_transmit(dev, buf, 2, 100);
}

static esp_err_t oled_write_data(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len)
{
    /* Use static buffer to avoid malloc/free per page render.
     * Max page data = 128 bytes + 1 control byte = 129 bytes. */
    static uint8_t s_i2c_buf[OLED_WIDTH + 1];
    if (len > OLED_WIDTH) len = OLED_WIDTH;  /* safety */
    s_i2c_buf[0] = 0x40;  /* Co=0, D/C=1 → data */
    memcpy(&s_i2c_buf[1], data, len);
    return i2c_master_transmit(dev, s_i2c_buf, len + 1, 200);
}

/* ---- Public API ---- */

esp_err_t oled_probe(i2c_master_bus_handle_t bus)
{
    return i2c_master_probe(bus, OLED_I2C_ADDR, 100);
}

esp_err_t oled_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *ret_dev)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = 400000,  /* SH1106 supports up to 400 kHz */
    };

    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, ret_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add OLED to I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_master_dev_handle_t dev = *ret_dev;

    /* SH1106 init sequence */
    const uint8_t init_cmds[] = {
        0xAE,       /* Display OFF */
        0x02,       /* Set column lower nibble start = 2 (SH1106 column offset) */
        0x10,       /* Set column upper nibble start = 0 */
        0x40,       /* Set display start line = 0 */
        0xB0,       /* Set page address = 0 */
        0xA1,       /* SEG remap (column 127 = SEG0) */
        0xC8,       /* COM scan direction (remapped, normal) */
        0x81, 0xCF, /* Set contrast = 0xCF */
        0xA4,       /* Display resume to RAM content */
        0xA6,       /* Normal display (not inverted) */
        0xA8, 0x3F, /* Multiplex ratio = 1/64 (64 rows) */
        0xD3, 0x00, /* Set display offset = 0 */
        0xD5, 0x50, /* Set osc frequency = 0x50 */
        0xD9, 0xF1, /* Set pre-charge period = 0xF1 */
        0xDA, 0x12, /* Set COM pins = 0x12 (sequential) */
        0xDB, 0x40, /* Set VCOMH deselect level = 0x40 */
        0x8D, 0x14, /* Enable charge pump regulator */
        0xAF,       /* Display ON */
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ret = oled_write_cmd(dev, init_cmds[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OLED init cmd %d failed: %s", (int)i, esp_err_to_name(ret));
            return ret;
        }
    }

    oled_clear();
    ESP_LOGI(TAG, "OLED initialized at I2C 0x%02X (%dx%d)", OLED_I2C_ADDR, OLED_WIDTH, OLED_HEIGHT);
    return ESP_OK;
}

void oled_clear(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
    s_cursor_col = 0;
    s_cursor_row = 0;
}

void oled_set_cursor(int col, int row)
{
    if (col < 0) col = 0;
    if (col >= OLED_COLS) col = OLED_COLS - 1;
    if (row < 0) row = 0;
    if (row >= OLED_ROWS) row = OLED_ROWS - 1;
    s_cursor_col = col;
    s_cursor_row = row;
}

void oled_putc(char c)
{
    /* Map printable ASCII to font index */
    if (c < 0x20 || c > 0x7F) c = ' ';
    int idx = c - 0x20;

    /* Draw 8×8 char into buffer at cursor position */
    int x = s_cursor_col * OLED_FONT_WIDTH;
    int page = s_cursor_row;  /* row 0-7 = page 0-7 (8px per page) */

    if (x >= 0 && x < OLED_WIDTH && page >= 0 && page < 8) {
        const uint8_t *glyph = &oled_font[idx * 8];
        for (int i = 0; i < 8 && (x + i) < OLED_WIDTH; i++) {
            s_buffer[page * OLED_WIDTH + x + i] = glyph[i];
        }
    }

    /* Advance cursor */
    s_cursor_col++;
    if (s_cursor_col >= OLED_COLS) {
        s_cursor_col = 0;
        s_cursor_row++;
        if (s_cursor_row >= OLED_ROWS) s_cursor_row = 0;
    }
}

void oled_puts(const char *str)
{
    while (*str) {
        oled_putc(*str++);
    }
}

esp_err_t oled_render(i2c_master_dev_handle_t dev)
{
    /* Send buffer page by page. SH1106 uses page addressing mode. */
    for (int page = 0; page < 8; page++) {
        /* Set page address */
        esp_err_t ret = oled_write_cmd(dev, 0xB0 + page);
        if (ret != ESP_OK) return ret;
        /* Set column offset (lower + upper nibble, offset by 2 for SH1106) */
        ret = oled_write_cmd(dev, 0x02);  /* lower nibble = 2 */
        if (ret != ESP_OK) return ret;
        ret = oled_write_cmd(dev, 0x10);  /* upper nibble = 0 */
        if (ret != ESP_OK) return ret;
        /* Send 128 bytes of page data */
        ret = oled_write_data(dev, &s_buffer[page * OLED_WIDTH], OLED_WIDTH);
        if (ret != ESP_OK) return ret;
    }
    return ESP_OK;
}

esp_err_t oled_display_on(i2c_master_dev_handle_t dev)
{
    return oled_write_cmd(dev, 0xAF);
}

esp_err_t oled_display_off(i2c_master_dev_handle_t dev)
{
    return oled_write_cmd(dev, 0xAE);
}
