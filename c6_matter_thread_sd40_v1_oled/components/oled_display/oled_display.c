/*
 * oled_display.c
 *
 * High-level OLED display interface implementation
 *
 * Provides a simple API for displaying text and graphics on an SSD1306
 * OLED display, with specific functions for sensor data display.
 */

#include "oled_display.h"
#include "ssd1306.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "oled_display";

// ============================================================================
// External Font Data
// ============================================================================

extern const uint8_t font_8x8[95][8];
const uint8_t *font_8x8_get_char(char c);

// ============================================================================
// Module State
// ============================================================================

static struct {
    ssd1306_handle_t ssd1306;           ///< SSD1306 driver handle
    i2c_master_bus_handle_t i2c_bus;    ///< I2C bus handle
    bool owns_bus;                       ///< True if we created the bus
    uint8_t *buffer;                     ///< Frame buffer pointer
    uint8_t cursor_x;                    ///< Current cursor X position
    uint8_t cursor_y;                    ///< Current cursor Y position
    bool initialized;                    ///< Initialization flag
} s_oled = {0};

// ============================================================================
// Initialization
// ============================================================================

esp_err_t oled_init(const oled_config_t *config)
{
    if (s_oled.initialized) {
        ESP_LOGW(TAG, "OLED already initialized");
        return ESP_OK;
    }

    // Use default config if not provided
    oled_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        cfg = (oled_config_t)OLED_CONFIG_DEFAULT();
    }

    ESP_LOGI(TAG, "Initializing OLED display");
    ESP_LOGI(TAG, "  I2C address: 0x%02X, Frequency: %lu Hz",
             cfg.i2c_address, (unsigned long)cfg.i2c_freq_hz);

    esp_err_t ret;

    // Use existing bus or create new one
    if (cfg.i2c_bus != NULL) {
        ESP_LOGI(TAG, "  Using existing I2C bus");
        s_oled.i2c_bus = cfg.i2c_bus;
        s_oled.owns_bus = false;
    } else {
        ESP_LOGI(TAG, "  Creating new I2C bus on SDA: GPIO%d, SCL: GPIO%d",
                 cfg.sda_gpio, cfg.scl_gpio);

        // Configure I2C bus
        i2c_master_bus_config_t i2c_bus_cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = cfg.i2c_port,
            .sda_io_num = cfg.sda_gpio,
            .scl_io_num = cfg.scl_gpio,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };

        ret = i2c_new_master_bus(&i2c_bus_cfg, &s_oled.i2c_bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
            return ret;
        }
        s_oled.owns_bus = true;
    }

    // Configure SSD1306
    ssd1306_config_t ssd_cfg = {
        .i2c_bus = s_oled.i2c_bus,
        .i2c_address = cfg.i2c_address,
        .width = OLED_WIDTH,
        .height = OLED_HEIGHT,
        .flip_horizontal = true,    // Typical orientation
        .flip_vertical = true,
    };

    ret = ssd1306_create(&ssd_cfg, &s_oled.ssd1306);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SSD1306: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_oled.i2c_bus);
        return ret;
    }

    // Get buffer pointer
    s_oled.buffer = ssd1306_get_buffer(s_oled.ssd1306);
    if (!s_oled.buffer) {
        ESP_LOGE(TAG, "Failed to get frame buffer");
        ssd1306_destroy(s_oled.ssd1306);
        i2c_del_master_bus(s_oled.i2c_bus);
        return ESP_FAIL;
    }

    s_oled.cursor_x = 0;
    s_oled.cursor_y = 0;
    s_oled.initialized = true;

    ESP_LOGI(TAG, "OLED display initialized successfully");

    // Show splash screen
    oled_show_splash();

    return ESP_OK;
}

esp_err_t oled_deinit(void)
{
    if (!s_oled.initialized) {
        return ESP_OK;
    }

    ssd1306_destroy(s_oled.ssd1306);

    // Only delete the bus if we created it
    if (s_oled.owns_bus) {
        i2c_del_master_bus(s_oled.i2c_bus);
    }

    memset(&s_oled, 0, sizeof(s_oled));

    ESP_LOGI(TAG, "OLED display deinitialized");
    return ESP_OK;
}

// ============================================================================
// Display Control
// ============================================================================

esp_err_t oled_display_on(void)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;
    return ssd1306_display_on(s_oled.ssd1306);
}

esp_err_t oled_display_off(void)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;
    return ssd1306_display_off(s_oled.ssd1306);
}

esp_err_t oled_set_contrast(uint8_t contrast)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;
    return ssd1306_set_contrast(s_oled.ssd1306, contrast);
}

esp_err_t oled_invert(bool invert)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;
    return ssd1306_set_invert(s_oled.ssd1306, invert);
}

// ============================================================================
// Drawing Functions
// ============================================================================

esp_err_t oled_clear(void)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    memset(s_oled.buffer, 0, SSD1306_BUFFER_SIZE);
    s_oled.cursor_x = 0;
    s_oled.cursor_y = 0;

    return ssd1306_update(s_oled.ssd1306, s_oled.buffer);
}

esp_err_t oled_clear_line(uint8_t line)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;
    if (line >= OLED_PAGES) return ESP_ERR_INVALID_ARG;

    // Clear one page (8 rows of pixels)
    memset(s_oled.buffer + (line * OLED_WIDTH), 0, OLED_WIDTH);

    return ESP_OK;
}

esp_err_t oled_refresh(void)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;
    return ssd1306_update(s_oled.ssd1306, s_oled.buffer);
}

esp_err_t oled_draw_pixel(uint8_t x, uint8_t y, bool color)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return ESP_ERR_INVALID_ARG;

    uint16_t byte_idx = x + (y / 8) * OLED_WIDTH;
    uint8_t bit_pos = y % 8;

    if (color) {
        s_oled.buffer[byte_idx] |= (1 << bit_pos);
    } else {
        s_oled.buffer[byte_idx] &= ~(1 << bit_pos);
    }

    return ESP_OK;
}

esp_err_t oled_draw_hline(uint8_t x, uint8_t y, uint8_t length, bool color)
{
    for (uint8_t i = 0; i < length && (x + i) < OLED_WIDTH; i++) {
        oled_draw_pixel(x + i, y, color);
    }
    return ESP_OK;
}

esp_err_t oled_draw_vline(uint8_t x, uint8_t y, uint8_t length, bool color)
{
    for (uint8_t i = 0; i < length && (y + i) < OLED_HEIGHT; i++) {
        oled_draw_pixel(x, y + i, color);
    }
    return ESP_OK;
}

esp_err_t oled_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool color)
{
    oled_draw_hline(x, y, width, color);                    // Top
    oled_draw_hline(x, y + height - 1, width, color);       // Bottom
    oled_draw_vline(x, y, height, color);                   // Left
    oled_draw_vline(x + width - 1, y, height, color);       // Right
    return ESP_OK;
}

esp_err_t oled_fill_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool color)
{
    for (uint8_t j = 0; j < height; j++) {
        oled_draw_hline(x, y + j, width, color);
    }
    return ESP_OK;
}

// ============================================================================
// Text Functions
// ============================================================================

void oled_set_cursor(uint8_t x, uint8_t y)
{
    s_oled.cursor_x = x;
    s_oled.cursor_y = y;
}

esp_err_t oled_draw_char(char c)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    // Handle special characters
    if (c == '\n') {
        s_oled.cursor_x = 0;
        s_oled.cursor_y += 8;
        return ESP_OK;
    }
    if (c == '\r') {
        s_oled.cursor_x = 0;
        return ESP_OK;
    }

    // Check bounds
    if (s_oled.cursor_x + 8 > OLED_WIDTH) {
        s_oled.cursor_x = 0;
        s_oled.cursor_y += 8;
    }
    if (s_oled.cursor_y + 8 > OLED_HEIGHT) {
        s_oled.cursor_y = 0;  // Wrap to top
    }

    // Get font data
    const uint8_t *glyph = font_8x8_get_char(c);

    // Calculate buffer position
    uint8_t page = s_oled.cursor_y / 8;
    uint8_t bit_offset = s_oled.cursor_y % 8;

    // Draw character
    for (uint8_t col = 0; col < 8; col++) {
        uint16_t x = s_oled.cursor_x + col;
        if (x >= OLED_WIDTH) break;

        uint8_t data = glyph[col];

        // Handle page-aligned case (common, faster)
        if (bit_offset == 0) {
            s_oled.buffer[page * OLED_WIDTH + x] |= data;
        } else {
            // Character spans two pages
            s_oled.buffer[page * OLED_WIDTH + x] |= (data << bit_offset);
            if (page + 1 < OLED_PAGES) {
                s_oled.buffer[(page + 1) * OLED_WIDTH + x] |= (data >> (8 - bit_offset));
            }
        }
    }

    // Advance cursor
    s_oled.cursor_x += 8;

    return ESP_OK;
}

esp_err_t oled_draw_string(const char *str)
{
    if (!str) return ESP_ERR_INVALID_ARG;

    while (*str) {
        esp_err_t ret = oled_draw_char(*str++);
        if (ret != ESP_OK) return ret;
    }

    return ESP_OK;
}

esp_err_t oled_draw_string_at(uint8_t x, uint8_t y, const char *str)
{
    oled_set_cursor(x, y);
    return oled_draw_string(str);
}

esp_err_t oled_printf_at(uint8_t x, uint8_t y, const char *fmt, ...)
{
    char buf[64];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    return oled_draw_string_at(x, y, buf);
}

// ============================================================================
// High-Level Display Functions
// ============================================================================

esp_err_t oled_show_co2(uint8_t line, uint16_t co2_ppm)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    oled_clear_line(line);

    char buf[20];
    snprintf(buf, sizeof(buf), "CO2: %u ppm", co2_ppm);

    oled_set_cursor(0, line * 8);
    oled_draw_string(buf);

    return ESP_OK;
}

esp_err_t oled_show_thread_status(uint8_t line, bool connected)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    oled_clear_line(line);

    const char *status = connected ? "Connected" : "Disconnected";
    char buf[24];
    snprintf(buf, sizeof(buf), "Thread: %s", status);

    oled_set_cursor(0, line * 8);
    oled_draw_string(buf);

    return ESP_OK;
}

esp_err_t oled_show_node_count(uint8_t line, uint8_t node_count)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    oled_clear_line(line);

    char buf[16];
    snprintf(buf, sizeof(buf), "Nodes: %u", node_count);

    oled_set_cursor(0, line * 8);
    oled_draw_string(buf);

    return ESP_OK;
}

esp_err_t oled_show_temperature(uint8_t line, float temp_c)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    oled_clear_line(line);

    char buf[20];
    snprintf(buf, sizeof(buf), "Temp: %.1f C", temp_c);

    oled_set_cursor(0, line * 8);
    oled_draw_string(buf);

    return ESP_OK;
}

esp_err_t oled_show_humidity(uint8_t line, float humidity_percent)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    oled_clear_line(line);

    char buf[16];
    snprintf(buf, sizeof(buf), "RH: %.1f %%", humidity_percent);

    oled_set_cursor(0, line * 8);
    oled_draw_string(buf);

    return ESP_OK;
}

esp_err_t oled_update_sensor_display(uint16_t co2_ppm, float temp_c, float humidity,
                                      bool thread_connected, uint8_t node_count)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    // Clear buffer
    memset(s_oled.buffer, 0, SSD1306_BUFFER_SIZE);

    // Line 0: CO2 (large, important)
    oled_show_co2(0, co2_ppm);

    // Line 2: Thread status (skip line 1 for spacing)
    oled_show_thread_status(2, thread_connected);

    // Line 3: Node count
    oled_show_node_count(3, node_count);

    // Line 5: Temperature (skip line 4 for spacing)
    oled_show_temperature(5, temp_c);

    // Line 6: Humidity
    oled_show_humidity(6, humidity);

    // Update display
    return oled_refresh();
}

esp_err_t oled_show_splash(void)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    // Clear display
    memset(s_oled.buffer, 0, SSD1306_BUFFER_SIZE);

    // Draw splash screen
    oled_set_cursor(16, 8);
    oled_draw_string("SCD40 Sensor");

    oled_set_cursor(8, 24);
    oled_draw_string("Matter/Thread");

    oled_set_cursor(24, 40);
    oled_draw_string("v1.0.0");

    oled_set_cursor(8, 56);
    oled_draw_string("Initializing...");

    return oled_refresh();
}

esp_err_t oled_show_commissioning_info(uint32_t setup_code, uint16_t discriminator)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    // Clear display
    memset(s_oled.buffer, 0, SSD1306_BUFFER_SIZE);

    // Draw commissioning info
    oled_set_cursor(0, 0);
    oled_draw_string("Commission Me!");

    char buf[24];
    snprintf(buf, sizeof(buf), "Code: %08lu", (unsigned long)setup_code);
    oled_set_cursor(0, 16);
    oled_draw_string(buf);

    snprintf(buf, sizeof(buf), "Disc: %04X", discriminator);
    oled_set_cursor(0, 32);
    oled_draw_string(buf);

    oled_set_cursor(0, 48);
    oled_draw_string("Waiting...");

    return oled_refresh();
}
