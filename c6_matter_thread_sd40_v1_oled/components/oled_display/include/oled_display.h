/*
 * oled_display.h
 *
 * High-level OLED display interface for SSD1306 128x64 display
 *
 * This module provides a simple interface for displaying sensor data
 * and network status on an SSD1306 OLED display via I2C.
 */

#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>
#include <driver/i2c_master.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Display Configuration
// ============================================================================

#define OLED_WIDTH          128     ///< Display width in pixels
#define OLED_HEIGHT         64      ///< Display height in pixels
#define OLED_PAGES          8       ///< Number of 8-pixel pages (64/8)

// Default I2C configuration for XIAO ESP32-C6
#ifndef CONFIG_OLED_I2C_ADDRESS
#define CONFIG_OLED_I2C_ADDRESS     0x3C
#endif

#ifndef CONFIG_I2C_MASTER_SDA_PIN
#define CONFIG_I2C_MASTER_SDA_PIN   22
#endif

#ifndef CONFIG_I2C_MASTER_SCL_PIN
#define CONFIG_I2C_MASTER_SCL_PIN   23
#endif

#ifndef CONFIG_I2C_MASTER_FREQ_HZ
#define CONFIG_I2C_MASTER_FREQ_HZ   100000
#endif

// ============================================================================
// Display Configuration Structure
// ============================================================================

/**
 * @brief OLED display configuration
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus; ///< Existing I2C bus handle (if NULL, creates new)
    int sda_gpio;           ///< SDA GPIO pin number (only if i2c_bus is NULL)
    int scl_gpio;           ///< SCL GPIO pin number (only if i2c_bus is NULL)
    uint8_t i2c_address;    ///< I2C device address (typically 0x3C or 0x3D)
    uint32_t i2c_freq_hz;   ///< I2C clock frequency in Hz
    int i2c_port;           ///< I2C port number (0 or 1)
} oled_config_t;

/**
 * @brief Default OLED configuration macro
 */
#define OLED_CONFIG_DEFAULT() { \
    .i2c_bus = NULL, \
    .sda_gpio = CONFIG_I2C_MASTER_SDA_PIN, \
    .scl_gpio = CONFIG_I2C_MASTER_SCL_PIN, \
    .i2c_address = CONFIG_OLED_I2C_ADDRESS, \
    .i2c_freq_hz = CONFIG_I2C_MASTER_FREQ_HZ, \
    .i2c_port = 0, \
}

// ============================================================================
// Initialization and Control
// ============================================================================

/**
 * @brief Initialize the OLED display
 *
 * Configures I2C bus and initializes SSD1306 display controller.
 *
 * @param[in] config Display configuration. Pass NULL for defaults.
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t oled_init(const oled_config_t *config);

/**
 * @brief Deinitialize the OLED display
 *
 * Turns off display and releases I2C resources.
 *
 * @return ESP_OK on success
 */
esp_err_t oled_deinit(void);

/**
 * @brief Reinitialize the OLED display with a new I2C bus
 *
 * Used for I2C bus recovery after errors. Destroys the current SSD1306
 * device handle and creates a new one on the provided I2C bus.
 *
 * @param[in] new_i2c_bus New I2C bus handle to use
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t oled_reinit(i2c_master_bus_handle_t new_i2c_bus);

/**
 * @brief Turn the display on
 * @return ESP_OK on success
 */
esp_err_t oled_display_on(void);

/**
 * @brief Turn the display off (low power mode)
 * @return ESP_OK on success
 */
esp_err_t oled_display_off(void);

/**
 * @brief Set display contrast
 * @param[in] contrast Contrast level (0-255)
 * @return ESP_OK on success
 */
esp_err_t oled_set_contrast(uint8_t contrast);

/**
 * @brief Invert display colors
 * @param[in] invert true for inverse, false for normal
 * @return ESP_OK on success
 */
esp_err_t oled_invert(bool invert);

// ============================================================================
// Drawing Functions
// ============================================================================

/**
 * @brief Clear the entire display
 * @return ESP_OK on success
 */
esp_err_t oled_clear(void);

/**
 * @brief Clear a specific line of the display
 * @param[in] line Line number (0-7 for 8-pixel high lines)
 * @return ESP_OK on success
 */
esp_err_t oled_clear_line(uint8_t line);

/**
 * @brief Update display from buffer
 *
 * Sends the internal frame buffer to the display.
 *
 * @return ESP_OK on success
 */
esp_err_t oled_refresh(void);

/**
 * @brief Draw a single pixel
 * @param[in] x X coordinate (0-127)
 * @param[in] y Y coordinate (0-63)
 * @param[in] color true for white, false for black
 * @return ESP_OK on success
 */
esp_err_t oled_draw_pixel(uint8_t x, uint8_t y, bool color);

/**
 * @brief Draw a horizontal line
 * @param[in] x Starting X coordinate
 * @param[in] y Y coordinate
 * @param[in] length Line length in pixels
 * @param[in] color true for white, false for black
 * @return ESP_OK on success
 */
esp_err_t oled_draw_hline(uint8_t x, uint8_t y, uint8_t length, bool color);

/**
 * @brief Draw a vertical line
 * @param[in] x X coordinate
 * @param[in] y Starting Y coordinate
 * @param[in] length Line length in pixels
 * @param[in] color true for white, false for black
 * @return ESP_OK on success
 */
esp_err_t oled_draw_vline(uint8_t x, uint8_t y, uint8_t length, bool color);

/**
 * @brief Draw a rectangle outline
 * @param[in] x Top-left X coordinate
 * @param[in] y Top-left Y coordinate
 * @param[in] width Rectangle width
 * @param[in] height Rectangle height
 * @param[in] color true for white, false for black
 * @return ESP_OK on success
 */
esp_err_t oled_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool color);

/**
 * @brief Draw a filled rectangle
 * @param[in] x Top-left X coordinate
 * @param[in] y Top-left Y coordinate
 * @param[in] width Rectangle width
 * @param[in] height Rectangle height
 * @param[in] color true for white, false for black
 * @return ESP_OK on success
 */
esp_err_t oled_fill_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool color);

// ============================================================================
// Text Functions
// ============================================================================

/**
 * @brief Set cursor position for text output
 * @param[in] x X coordinate in pixels
 * @param[in] y Y coordinate in pixels (should be multiple of 8 for best alignment)
 */
void oled_set_cursor(uint8_t x, uint8_t y);

/**
 * @brief Draw a single character at current cursor position
 *
 * Uses 8x8 pixel font. Cursor advances automatically.
 *
 * @param[in] c Character to draw
 * @return ESP_OK on success
 */
esp_err_t oled_draw_char(char c);

/**
 * @brief Draw a string at current cursor position
 *
 * Uses 8x8 pixel font. Line wrapping is automatic.
 *
 * @param[in] str Null-terminated string to draw
 * @return ESP_OK on success
 */
esp_err_t oled_draw_string(const char *str);

/**
 * @brief Draw a string at specified position
 * @param[in] x X coordinate in pixels
 * @param[in] y Y coordinate in pixels
 * @param[in] str Null-terminated string to draw
 * @return ESP_OK on success
 */
esp_err_t oled_draw_string_at(uint8_t x, uint8_t y, const char *str);

/**
 * @brief Draw formatted text at specified position
 *
 * Similar to printf() but outputs to OLED at specified position.
 *
 * @param[in] x X coordinate
 * @param[in] y Y coordinate
 * @param[in] fmt Format string
 * @param[in] ... Format arguments
 * @return ESP_OK on success
 */
esp_err_t oled_printf_at(uint8_t x, uint8_t y, const char *fmt, ...);

// ============================================================================
// 2x Scaled Text Functions (16x16 pixels per character)
// ============================================================================

/**
 * @brief Draw a single character at 2x scale (16x16 pixels) at current cursor
 *
 * Built from the same 8x8 font by doubling every pixel; no separate font
 * table is used. Cursor advances by 16px automatically.
 *
 * @param[in] c Character to draw
 * @return ESP_OK on success
 */
esp_err_t oled_draw_char_2x(char c);

/**
 * @brief Draw a string at 2x scale (16x16 pixels per character)
 * @param[in] str Null-terminated string to draw
 * @return ESP_OK on success
 */
esp_err_t oled_draw_string_2x(const char *str);

/**
 * @brief Draw a 2x-scaled string at specified position
 * @param[in] x X coordinate in pixels
 * @param[in] y Y coordinate in pixels
 * @param[in] str Null-terminated string to draw
 * @return ESP_OK on success
 */
esp_err_t oled_draw_string_2x_at(uint8_t x, uint8_t y, const char *str);

// ============================================================================
// Rotating Big-Value Display (lines 1-6: label + 2x-scaled value)
// ============================================================================

/**
 * @brief Which sensor value is currently shown large in oled_update_sensor_display()
 *
 * The caller is responsible for timing the rotation (e.g. temperature 5s,
 * humidity 3s, CO2 2s) and passing the corresponding enum value on each call;
 * this module only draws whichever value is selected.
 */
typedef enum {
    OLED_ROTATING_TEMPERATURE = 0,
    OLED_ROTATING_HUMIDITY,
    OLED_ROTATING_CO2,
} oled_rotating_value_t;

// ============================================================================
// High-Level Display Functions for Sensor Data
// ============================================================================

/**
 * @brief Display CO2 value
 *
 * Displays "CO2: XXXX ppm" on the specified line.
 *
 * @param[in] line Display line (0-7)
 * @param[in] co2_ppm CO2 value in parts per million
 * @return ESP_OK on success
 */
esp_err_t oled_show_co2(uint8_t line, uint16_t co2_ppm);

/**
 * @brief Display Thread network status
 *
 * Displays "Thread: Connected" or "Thread: Disconnected" on the specified line.
 *
 * @param[in] line Display line (0-7)
 * @param[in] connected true if connected to Thread network
 * @return ESP_OK on success
 */
esp_err_t oled_show_thread_status(uint8_t line, bool connected);

/**
 * @brief Display node count
 *
 * Displays "Nodes: X" on the specified line.
 *
 * @param[in] line Display line (0-7)
 * @param[in] node_count Number of nodes in network
 * @return ESP_OK on success
 */
esp_err_t oled_show_node_count(uint8_t line, uint8_t node_count);

/**
 * @brief Display temperature value
 *
 * Displays "Temp: XX.X C" on the specified line.
 *
 * @param[in] line Display line (0-7)
 * @param[in] temp_c Temperature in Celsius
 * @return ESP_OK on success
 */
esp_err_t oled_show_temperature(uint8_t line, float temp_c);

/**
 * @brief Display humidity value
 *
 * Displays "RH: XX.X %" on the specified line.
 *
 * @param[in] line Display line (0-7)
 * @param[in] humidity_percent Relative humidity percentage
 * @return ESP_OK on success
 */
esp_err_t oled_show_humidity(uint8_t line, float humidity_percent);

/**
 * @brief Update entire sensor display
 *
 * Display layout:
 * - Line 0: Date and time (DD/MM/YY HH:MM:SS) or "Attesa NTP..." if not synced
 * - Lines 1-6: Rotating big value (2x-scaled) selected by `rotating_value`,
 *   with a small label above it: air quality text for CO2, "TEMPERATURA"
 *   for temperature, "UMIDITA'" for humidity. Entire block blinks when
 *   showing CO2 with poor air quality (index > 3).
 * - Line 7: I/O status, Thread status (T:C/T:D), Node count (N:X)
 *
 * The caller drives the rotation timing (e.g. temperature 5s, humidity 3s,
 * CO2 2s) by calling this function repeatedly with the appropriate
 * `rotating_value`; this function itself does not track time.
 *
 * @param[in] co2_ppm CO2 value in ppm
 * @param[in] temp_c Temperature in Celsius
 * @param[in] humidity Relative humidity percentage
 * @param[in] thread_connected Thread network status
 * @param[in] node_count Number of network nodes
 * @param[in] input_states Bitmask of input states (bit 0-3 for inputs 1-4, 1=closed/active)
 * @param[in] output_states Bitmask of output states (bit 0-1 for outputs 1-2, 1=on)
 * @param[in] day Day of month (1-31)
 * @param[in] month Month (1-12)
 * @param[in] year Year as full value (e.g., 2026)
 * @param[in] hour Hour in 24h format (0-23)
 * @param[in] minute Minute (0-59)
 * @param[in] second Second (0-59)
 * @param[in] air_quality_index Air quality level (1=Good, 2=Fair, 3=Moderate, 4=Poor, 5=VeryPoor, 6=ExtremelyPoor)
 * @param[in] rotating_value Which value (temperature/humidity/CO2) to show large right now
 * @return ESP_OK on success
 */
esp_err_t oled_update_sensor_display(uint16_t co2_ppm, float temp_c, float humidity,
                                      bool thread_connected, uint8_t node_count,
                                      uint8_t input_states, uint8_t output_states,
                                      uint8_t day, uint8_t month, uint16_t year,
                                      uint8_t hour, uint8_t minute, uint8_t second,
                                      uint8_t air_quality_index,
                                      oled_rotating_value_t rotating_value);

/**
 * @brief Display startup splash screen
 *
 * Shows device name and version information.
 *
 * @return ESP_OK on success
 */
esp_err_t oled_show_splash(void);

/**
 * @brief Display commissioning QR code info
 *
 * Shows Matter commissioning information.
 *
 * @param[in] setup_code Manual pairing code
 * @param[in] discriminator Discriminator value
 * @return ESP_OK on success
 */
esp_err_t oled_show_commissioning_info(uint32_t setup_code, uint16_t discriminator);

#ifdef __cplusplus
}
#endif
