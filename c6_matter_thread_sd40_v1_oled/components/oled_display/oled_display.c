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
    bool blink_state;                    ///< Blink state for poor air quality warning
} s_oled = {0};

// ============================================================================
// Air Quality Italian Text
// ============================================================================

/**
 * @brief Get air quality description in Italian
 * @param[in] air_quality_index Index (1-6): Good, Fair, Moderate, Poor, VeryPoor, ExtremelyPoor
 * @return Italian string description
 */
static const char* get_air_quality_text_it(uint8_t air_quality_index) {
    switch (air_quality_index) {
        case 1: return "Ottima";
        case 2: return "Buona";
        case 3: return "Moderata";
        case 4: return "Scarsa";
        case 5: return "Pessima";
        case 6: return "Pericolosa";
        default: return "N/D";
    }
}

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
        // Only delete bus if we created it
        if (s_oled.owns_bus) {
            i2c_del_master_bus(s_oled.i2c_bus);
        }
        return ret;
    }

    // Get buffer pointer
    s_oled.buffer = ssd1306_get_buffer(s_oled.ssd1306);
    if (!s_oled.buffer) {
        ESP_LOGE(TAG, "Failed to get frame buffer");
        ssd1306_destroy(s_oled.ssd1306);
        // Only delete bus if we created it
        if (s_oled.owns_bus) {
            i2c_del_master_bus(s_oled.i2c_bus);
        }
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

esp_err_t oled_reinit(i2c_master_bus_handle_t new_i2c_bus)
{
    ESP_LOGI(TAG, "Reinitializing OLED display with new I2C bus...");

    // Destroy existing SSD1306 handle if present
    if (s_oled.ssd1306) {
        ssd1306_destroy(s_oled.ssd1306);
        s_oled.ssd1306 = NULL;
    }

    // Clear internal state but keep owns_bus=false since we don't own the bus
    s_oled.buffer = NULL;
    s_oled.initialized = false;

    // Update to new bus (we never own it in reinit scenario)
    s_oled.i2c_bus = new_i2c_bus;
    s_oled.owns_bus = false;

    // Configure SSD1306 with new bus
    ssd1306_config_t ssd_cfg = {
        .i2c_bus = s_oled.i2c_bus,
        .i2c_address = CONFIG_OLED_I2C_ADDRESS,
        .width = OLED_WIDTH,
        .height = OLED_HEIGHT,
        .flip_horizontal = true,
        .flip_vertical = true,
    };

    esp_err_t ret = ssd1306_create(&ssd_cfg, &s_oled.ssd1306);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to recreate SSD1306: %s", esp_err_to_name(ret));
        return ret;
    }

    // Get buffer pointer
    s_oled.buffer = ssd1306_get_buffer(s_oled.ssd1306);
    if (!s_oled.buffer) {
        ESP_LOGE(TAG, "Failed to get frame buffer during reinit");
        ssd1306_destroy(s_oled.ssd1306);
        s_oled.ssd1306 = NULL;
        return ESP_FAIL;
    }

    s_oled.cursor_x = 0;
    s_oled.cursor_y = 0;
    s_oled.initialized = true;

    ESP_LOGI(TAG, "OLED display reinitialized successfully");
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

// ============================================================================
// 2x Scaled Text Functions (16x16 pixels per character)
// ============================================================================

/**
 * @brief Expand one 8-bit column into a 16-bit column (each pixel -> 2x2 block)
 *
 * Doubles every bit vertically: bit N of the input becomes bits 2N and 2N+1
 * of the output. Used to build a 16px-tall glyph from the 8px font without
 * a separate font table.
 */
static uint16_t oled_expand_byte_2x(uint8_t b)
{
    uint16_t out = 0;
    for (uint8_t bit = 0; bit < 8; bit++) {
        if (b & (1 << bit)) {
            out |= (0x3 << (bit * 2));  // set both bits of the doubled pixel
        }
    }
    return out;
}

esp_err_t oled_draw_char_2x(char c)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    if (c == '\n') {
        s_oled.cursor_x = 0;
        s_oled.cursor_y += 16;
        return ESP_OK;
    }
    if (c == '\r') {
        s_oled.cursor_x = 0;
        return ESP_OK;
    }

    // Check bounds (16x16 glyph)
    if (s_oled.cursor_x + 16 > OLED_WIDTH) {
        s_oled.cursor_x = 0;
        s_oled.cursor_y += 16;
    }
    if (s_oled.cursor_y + 16 > OLED_HEIGHT) {
        s_oled.cursor_y = 0;  // Wrap to top
    }

    const uint8_t *glyph = font_8x8_get_char(c);
    uint8_t page = s_oled.cursor_y / 8;
    uint8_t bit_offset = s_oled.cursor_y % 8;

    for (uint8_t col = 0; col < 8; col++) {
        uint16_t data16 = oled_expand_byte_2x(glyph[col]);
        uint8_t data_lo = data16 & 0xFF;         // top 8 rows of the glyph
        uint8_t data_hi = (data16 >> 8) & 0xFF;  // bottom 8 rows of the glyph

        // Each source column becomes two adjacent destination columns
        for (uint8_t rep = 0; rep < 2; rep++) {
            uint16_t x = s_oled.cursor_x + col * 2 + rep;
            if (x >= OLED_WIDTH) continue;

            if (bit_offset == 0) {
                // Page-aligned: data_lo goes in 'page', data_hi in 'page+1'
                s_oled.buffer[page * OLED_WIDTH + x] |= data_lo;
                if (page + 1 < OLED_PAGES) {
                    s_oled.buffer[(page + 1) * OLED_WIDTH + x] |= data_hi;
                }
            } else {
                // Glyph spans up to 3 pages once shifted
                s_oled.buffer[page * OLED_WIDTH + x] |= (data_lo << bit_offset);
                if (page + 1 < OLED_PAGES) {
                    s_oled.buffer[(page + 1) * OLED_WIDTH + x] |=
                        (data_lo >> (8 - bit_offset)) | (data_hi << bit_offset);
                }
                if (page + 2 < OLED_PAGES) {
                    s_oled.buffer[(page + 2) * OLED_WIDTH + x] |= (data_hi >> (8 - bit_offset));
                }
            }
        }
    }

    s_oled.cursor_x += 16;

    return ESP_OK;
}

esp_err_t oled_draw_string_2x(const char *str)
{
    if (!str) return ESP_ERR_INVALID_ARG;

    while (*str) {
        esp_err_t ret = oled_draw_char_2x(*str++);
        if (ret != ESP_OK) return ret;
    }

    return ESP_OK;
}

esp_err_t oled_draw_string_2x_at(uint8_t x, uint8_t y, const char *str)
{
    oled_set_cursor(x, y);
    return oled_draw_string_2x(str);
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
                                      bool thread_connected, uint8_t node_count,
                                      uint8_t input_states, uint8_t output_states,
                                      uint8_t day, uint8_t month, uint16_t year,
                                      uint8_t hour, uint8_t minute, uint8_t second,
                                      uint8_t air_quality_index,
                                      oled_rotating_value_t rotating_value)
{
    if (!s_oled.initialized) return ESP_ERR_INVALID_STATE;

    // Clear buffer
    memset(s_oled.buffer, 0, SSD1306_BUFFER_SIZE);

    // Line 0: Date and time "DD/MM/YY  HH:MM" or "Attesa NTP..." if not synced
    // Note: 16 chars max per line (128px / 8px font = 16 chars)
    char datetime_buf[20];
    if (year >= 2020) {
        // Time is synchronized - show date and time (no seconds to fit in 16 chars)
        snprintf(datetime_buf, sizeof(datetime_buf), "%02d/%02d/%02d  %02d:%02d",
                 day, month, year % 100, hour, minute);
    } else {
        // Time not synchronized yet (epoch time) - show waiting message in Italian
        snprintf(datetime_buf, sizeof(datetime_buf), " Attesa NTP...  ");
    }
    oled_set_cursor(0, 0);
    oled_draw_string(datetime_buf);

    // Lines 1-6: Rotating big value (2x-scaled), selected by `rotating_value`.
    // Layout within the 1-6 block (pixel rows 8-63, 56px tall):
    //   - Label line (small font, row 16) centered above the value
    //   - Big value (16px tall, rows 32-47) centered horizontally
    // Blink the whole block when showing CO2 with poor air quality (index > 3),
    // same behaviour the old fixed PPM/air-quality line had.
    s_oled.blink_state = !s_oled.blink_state;
    bool is_co2_poor = (rotating_value == OLED_ROTATING_CO2) && (air_quality_index > 3);
    bool show_block = !is_co2_poor || s_oled.blink_state;

    if (show_block) {
        char label_buf[20];
        char value_buf[16];

        switch (rotating_value) {
            case OLED_ROTATING_TEMPERATURE:
                snprintf(label_buf, sizeof(label_buf), "TEMPERATURA");
                snprintf(value_buf, sizeof(value_buf), "%.1f C", temp_c);
                break;
            case OLED_ROTATING_HUMIDITY:
                snprintf(label_buf, sizeof(label_buf), "UMIDITA'");
                snprintf(value_buf, sizeof(value_buf), "%.1f %%", humidity);
                break;
            case OLED_ROTATING_CO2:
            default:
                snprintf(label_buf, sizeof(label_buf), "%s",
                         get_air_quality_text_it(air_quality_index));
                snprintf(value_buf, sizeof(value_buf), "%u ppm", co2_ppm);
                break;
        }

        // Label: small font (8px), centered on row 16 (line 2)
        uint8_t label_len = strlen(label_buf);
        uint8_t label_x = (label_len * 8 < OLED_WIDTH) ? (OLED_WIDTH - label_len * 8) / 2 : 0;
        oled_set_cursor(label_x, 2 * 8);
        oled_draw_string(label_buf);

        // Value: 2x-scaled font (16px), centered on rows 32-47 (lines 4-5)
        uint8_t value_len = strlen(value_buf);
        uint8_t value_x = (value_len * 16 < OLED_WIDTH) ? (OLED_WIDTH - value_len * 16) / 2 : 0;
        oled_set_cursor(value_x, 4 * 8);
        oled_draw_string_2x(value_buf);
    }

    // Line 7: I/O status + Thread status + Node count
    // Compact format to fit in 16 chars: "1234 12 T:C N:XX"
    // Input/output digits only when active, dash when inactive
    char status_buf[20];
    snprintf(status_buf, sizeof(status_buf), "%c%c%c%c %c%c T:%c N:%u",
             (input_states & 0x01) ? '1' : '-',
             (input_states & 0x02) ? '2' : '-',
             (input_states & 0x04) ? '3' : '-',
             (input_states & 0x08) ? '4' : '-',
             (output_states & 0x01) ? '1' : '-',
             (output_states & 0x02) ? '2' : '-',
             thread_connected ? 'C' : 'D',
             node_count);
    oled_set_cursor(0, 7 * 8);
    oled_draw_string(status_buf);

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
