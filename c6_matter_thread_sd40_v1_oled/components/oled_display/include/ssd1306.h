/*
 * ssd1306.h
 *
 * Low-level SSD1306 OLED driver interface
 *
 * This header provides direct access to SSD1306 controller commands
 * for 128x64 monochrome OLED displays via I2C.
 */

#pragma once

#include <esp_err.h>
#include <driver/i2c_master.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// SSD1306 Commands
// ============================================================================

// Fundamental commands
#define SSD1306_CMD_SET_CONTRAST        0x81
#define SSD1306_CMD_DISPLAY_RAM         0xA4
#define SSD1306_CMD_DISPLAY_ALL_ON      0xA5
#define SSD1306_CMD_NORMAL_DISPLAY      0xA6
#define SSD1306_CMD_INVERT_DISPLAY      0xA7
#define SSD1306_CMD_DISPLAY_OFF         0xAE
#define SSD1306_CMD_DISPLAY_ON          0xAF

// Scrolling commands
#define SSD1306_CMD_SCROLL_H_RIGHT      0x26
#define SSD1306_CMD_SCROLL_H_LEFT       0x27
#define SSD1306_CMD_SCROLL_VH_RIGHT     0x29
#define SSD1306_CMD_SCROLL_VH_LEFT      0x2A
#define SSD1306_CMD_SCROLL_STOP         0x2E
#define SSD1306_CMD_SCROLL_START        0x2F
#define SSD1306_CMD_SET_VSCROLL_AREA    0xA3

// Addressing setting commands
#define SSD1306_CMD_SET_LOWER_COL       0x00
#define SSD1306_CMD_SET_HIGHER_COL      0x10
#define SSD1306_CMD_SET_MEMORY_MODE     0x20
#define SSD1306_CMD_SET_COLUMN_ADDR     0x21
#define SSD1306_CMD_SET_PAGE_ADDR       0x22
#define SSD1306_CMD_SET_PAGE_START      0xB0

// Hardware configuration commands
#define SSD1306_CMD_SET_START_LINE      0x40
#define SSD1306_CMD_SET_SEG_REMAP_0     0xA0
#define SSD1306_CMD_SET_SEG_REMAP_127   0xA1
#define SSD1306_CMD_SET_MUX_RATIO       0xA8
#define SSD1306_CMD_SET_COM_SCAN_INC    0xC0
#define SSD1306_CMD_SET_COM_SCAN_DEC    0xC8
#define SSD1306_CMD_SET_DISPLAY_OFFSET  0xD3
#define SSD1306_CMD_SET_COM_PINS        0xDA

// Timing & driving scheme commands
#define SSD1306_CMD_SET_CLOCK_DIV       0xD5
#define SSD1306_CMD_SET_PRECHARGE       0xD9
#define SSD1306_CMD_SET_VCOMH_DESELECT  0xDB
#define SSD1306_CMD_NOP                 0xE3

// Charge pump commands
#define SSD1306_CMD_SET_CHARGE_PUMP     0x8D

// Control bytes for I2C
#define SSD1306_CTRL_CMD_SINGLE         0x80    // Co=1, D/C#=0: Command
#define SSD1306_CTRL_CMD_STREAM         0x00    // Co=0, D/C#=0: Command stream
#define SSD1306_CTRL_DATA_STREAM        0x40    // Co=0, D/C#=1: Data stream

// Display dimensions
#define SSD1306_WIDTH                   128
#define SSD1306_HEIGHT                  64
#define SSD1306_BUFFER_SIZE             (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

// ============================================================================
// SSD1306 Device Handle
// ============================================================================

typedef struct ssd1306_dev *ssd1306_handle_t;

// ============================================================================
// SSD1306 Configuration
// ============================================================================

typedef struct {
    i2c_master_bus_handle_t i2c_bus;    ///< I2C bus handle
    uint8_t i2c_address;                 ///< I2C device address
    uint8_t width;                       ///< Display width in pixels
    uint8_t height;                      ///< Display height in pixels
    bool flip_horizontal;                ///< Flip display horizontally
    bool flip_vertical;                  ///< Flip display vertically
} ssd1306_config_t;

// ============================================================================
// Low-Level Functions
// ============================================================================

/**
 * @brief Create and initialize SSD1306 device
 * @param[in] config Device configuration
 * @param[out] handle Pointer to receive device handle
 * @return ESP_OK on success
 */
esp_err_t ssd1306_create(const ssd1306_config_t *config, ssd1306_handle_t *handle);

/**
 * @brief Destroy SSD1306 device and free resources
 * @param[in] handle Device handle
 * @return ESP_OK on success
 */
esp_err_t ssd1306_destroy(ssd1306_handle_t handle);

/**
 * @brief Send a command to the SSD1306
 * @param[in] handle Device handle
 * @param[in] cmd Command byte
 * @return ESP_OK on success
 */
esp_err_t ssd1306_send_cmd(ssd1306_handle_t handle, uint8_t cmd);

/**
 * @brief Send command with one argument
 * @param[in] handle Device handle
 * @param[in] cmd Command byte
 * @param[in] arg Argument byte
 * @return ESP_OK on success
 */
esp_err_t ssd1306_send_cmd_arg(ssd1306_handle_t handle, uint8_t cmd, uint8_t arg);

/**
 * @brief Send data to display RAM
 * @param[in] handle Device handle
 * @param[in] data Data buffer
 * @param[in] len Data length
 * @return ESP_OK on success
 */
esp_err_t ssd1306_send_data(ssd1306_handle_t handle, const uint8_t *data, size_t len);

/**
 * @brief Clear display RAM (all pixels off)
 * @param[in] handle Device handle
 * @return ESP_OK on success
 */
esp_err_t ssd1306_clear(ssd1306_handle_t handle);

/**
 * @brief Fill display RAM (all pixels on)
 * @param[in] handle Device handle
 * @return ESP_OK on success
 */
esp_err_t ssd1306_fill(ssd1306_handle_t handle);

/**
 * @brief Set display contrast
 * @param[in] handle Device handle
 * @param[in] contrast Contrast value (0-255)
 * @return ESP_OK on success
 */
esp_err_t ssd1306_set_contrast(ssd1306_handle_t handle, uint8_t contrast);

/**
 * @brief Turn display on
 * @param[in] handle Device handle
 * @return ESP_OK on success
 */
esp_err_t ssd1306_display_on(ssd1306_handle_t handle);

/**
 * @brief Turn display off (sleep mode)
 * @param[in] handle Device handle
 * @return ESP_OK on success
 */
esp_err_t ssd1306_display_off(ssd1306_handle_t handle);

/**
 * @brief Set normal/inverted display mode
 * @param[in] handle Device handle
 * @param[in] invert true for inverted, false for normal
 * @return ESP_OK on success
 */
esp_err_t ssd1306_set_invert(ssd1306_handle_t handle, bool invert);

/**
 * @brief Set memory addressing mode
 * @param[in] handle Device handle
 * @param[in] mode 0=Horizontal, 1=Vertical, 2=Page
 * @return ESP_OK on success
 */
esp_err_t ssd1306_set_memory_mode(ssd1306_handle_t handle, uint8_t mode);

/**
 * @brief Set column address range for horizontal/vertical addressing
 * @param[in] handle Device handle
 * @param[in] start Start column (0-127)
 * @param[in] end End column (0-127)
 * @return ESP_OK on success
 */
esp_err_t ssd1306_set_column_address(ssd1306_handle_t handle, uint8_t start, uint8_t end);

/**
 * @brief Set page address range for horizontal/vertical addressing
 * @param[in] handle Device handle
 * @param[in] start Start page (0-7)
 * @param[in] end End page (0-7)
 * @return ESP_OK on success
 */
esp_err_t ssd1306_set_page_address(ssd1306_handle_t handle, uint8_t start, uint8_t end);

/**
 * @brief Update entire display from buffer
 * @param[in] handle Device handle
 * @param[in] buffer Frame buffer (1024 bytes for 128x64)
 * @return ESP_OK on success
 */
esp_err_t ssd1306_update(ssd1306_handle_t handle, const uint8_t *buffer);

/**
 * @brief Get the internal frame buffer pointer
 * @param[in] handle Device handle
 * @return Pointer to internal buffer, or NULL on error
 */
uint8_t *ssd1306_get_buffer(ssd1306_handle_t handle);

/**
 * @brief Get buffer size
 * @param[in] handle Device handle
 * @return Buffer size in bytes
 */
size_t ssd1306_get_buffer_size(ssd1306_handle_t handle);

#ifdef __cplusplus
}
#endif
