/*
 * ssd1306.c
 *
 * Low-level SSD1306 OLED driver implementation
 *
 * This driver uses ESP-IDF's new I2C master driver API for communication
 * with the SSD1306 display controller.
 */

#include "ssd1306.h"

#include <esp_log.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ssd1306";

// ============================================================================
// Internal Structures
// ============================================================================

struct ssd1306_dev {
    i2c_master_dev_handle_t i2c_dev;    ///< I2C device handle
    uint8_t *buffer;                     ///< Frame buffer
    size_t buffer_size;                  ///< Buffer size in bytes
    uint8_t width;                       ///< Display width
    uint8_t height;                      ///< Display height
};

// ============================================================================
// I2C Communication
// ============================================================================

/**
 * @brief Send command bytes to SSD1306
 */
esp_err_t ssd1306_send_cmd(ssd1306_handle_t handle, uint8_t cmd)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[2] = { SSD1306_CTRL_CMD_SINGLE, cmd };
    return i2c_master_transmit(handle->i2c_dev, buf, sizeof(buf), 100);
}

/**
 * @brief Send command with one argument
 */
esp_err_t ssd1306_send_cmd_arg(ssd1306_handle_t handle, uint8_t cmd, uint8_t arg)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[4] = { SSD1306_CTRL_CMD_STREAM, cmd, arg };
    return i2c_master_transmit(handle->i2c_dev, buf, 3, 100);
}

/**
 * @brief Send multiple command bytes
 */
static esp_err_t ssd1306_send_cmd_list(ssd1306_handle_t handle,
                                        const uint8_t *cmds, size_t len)
{
    if (!handle || !cmds) {
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate buffer for control byte + commands
    uint8_t *buf = malloc(len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    buf[0] = SSD1306_CTRL_CMD_STREAM;
    memcpy(buf + 1, cmds, len);

    esp_err_t ret = i2c_master_transmit(handle->i2c_dev, buf, len + 1, 100);
    free(buf);

    return ret;
}

/**
 * @brief Send data to display RAM
 */
esp_err_t ssd1306_send_data(ssd1306_handle_t handle, const uint8_t *data, size_t len)
{
    if (!handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate buffer for control byte + data
    uint8_t *buf = malloc(len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    buf[0] = SSD1306_CTRL_DATA_STREAM;
    memcpy(buf + 1, data, len);

    esp_err_t ret = i2c_master_transmit(handle->i2c_dev, buf, len + 1, 500);
    free(buf);

    return ret;
}

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize SSD1306 with standard 128x64 configuration
 */
static esp_err_t ssd1306_init_display(ssd1306_handle_t handle,
                                       bool flip_h, bool flip_v)
{
    // Initialization sequence for 128x64 OLED
    static const uint8_t init_cmds[] = {
        SSD1306_CMD_DISPLAY_OFF,            // Display off during init

        SSD1306_CMD_SET_CLOCK_DIV, 0x80,    // Clock divide ratio/oscillator frequency
        SSD1306_CMD_SET_MUX_RATIO, 0x3F,    // Multiplex ratio (64-1)
        SSD1306_CMD_SET_DISPLAY_OFFSET, 0x00, // Display offset = 0
        SSD1306_CMD_SET_START_LINE | 0x00,  // Start line = 0

        SSD1306_CMD_SET_CHARGE_PUMP, 0x14,  // Enable charge pump

        SSD1306_CMD_SET_MEMORY_MODE, 0x00,  // Horizontal addressing mode

        SSD1306_CMD_SET_COM_PINS, 0x12,     // COM pins hardware configuration
        SSD1306_CMD_SET_CONTRAST, 0xCF,     // Contrast = 207
        SSD1306_CMD_SET_PRECHARGE, 0xF1,    // Pre-charge period
        SSD1306_CMD_SET_VCOMH_DESELECT, 0x40, // VCOMH deselect level

        SSD1306_CMD_DISPLAY_RAM,            // Display follows RAM content
        SSD1306_CMD_NORMAL_DISPLAY,         // Normal display (not inverted)
        SSD1306_CMD_SCROLL_STOP,            // Disable scrolling
    };

    esp_err_t ret = ssd1306_send_cmd_list(handle, init_cmds, sizeof(init_cmds));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send init commands: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set segment remap (horizontal flip)
    ret = ssd1306_send_cmd(handle,
        flip_h ? SSD1306_CMD_SET_SEG_REMAP_127 : SSD1306_CMD_SET_SEG_REMAP_0);
    if (ret != ESP_OK) return ret;

    // Set COM output scan direction (vertical flip)
    ret = ssd1306_send_cmd(handle,
        flip_v ? SSD1306_CMD_SET_COM_SCAN_DEC : SSD1306_CMD_SET_COM_SCAN_INC);
    if (ret != ESP_OK) return ret;

    // Clear display
    ret = ssd1306_clear(handle);
    if (ret != ESP_OK) return ret;

    // Turn display on
    return ssd1306_display_on(handle);
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t ssd1306_create(const ssd1306_config_t *config, ssd1306_handle_t *handle)
{
    if (!config || !handle || !config->i2c_bus) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Creating SSD1306 device at address 0x%02X", config->i2c_address);

    // Allocate device structure
    struct ssd1306_dev *dev = calloc(1, sizeof(struct ssd1306_dev));
    if (!dev) {
        ESP_LOGE(TAG, "Failed to allocate device structure");
        return ESP_ERR_NO_MEM;
    }

    dev->width = config->width ? config->width : SSD1306_WIDTH;
    dev->height = config->height ? config->height : SSD1306_HEIGHT;
    dev->buffer_size = (dev->width * dev->height) / 8;

    // Allocate frame buffer
    dev->buffer = calloc(1, dev->buffer_size);
    if (!dev->buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
        free(dev);
        return ESP_ERR_NO_MEM;
    }

    // Configure I2C device
    i2c_device_config_t i2c_dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_address,
        .scl_speed_hz = 400000,  // SSD1306 supports fast mode
    };

    esp_err_t ret = i2c_master_bus_add_device(config->i2c_bus, &i2c_dev_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        free(dev->buffer);
        free(dev);
        return ret;
    }

    // Initialize display
    ret = ssd1306_init_display(dev, config->flip_horizontal, config->flip_vertical);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev->i2c_dev);
        free(dev->buffer);
        free(dev);
        return ret;
    }

    *handle = dev;
    ESP_LOGI(TAG, "SSD1306 initialized successfully (%dx%d)", dev->width, dev->height);

    return ESP_OK;
}

esp_err_t ssd1306_destroy(ssd1306_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    // Turn off display
    ssd1306_display_off(handle);

    // Remove I2C device
    if (handle->i2c_dev) {
        i2c_master_bus_rm_device(handle->i2c_dev);
    }

    // Free resources
    free(handle->buffer);
    free(handle);

    ESP_LOGI(TAG, "SSD1306 destroyed");
    return ESP_OK;
}

esp_err_t ssd1306_clear(ssd1306_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle->buffer, 0, handle->buffer_size);
    return ssd1306_update(handle, handle->buffer);
}

esp_err_t ssd1306_fill(ssd1306_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle->buffer, 0xFF, handle->buffer_size);
    return ssd1306_update(handle, handle->buffer);
}

esp_err_t ssd1306_set_contrast(ssd1306_handle_t handle, uint8_t contrast)
{
    return ssd1306_send_cmd_arg(handle, SSD1306_CMD_SET_CONTRAST, contrast);
}

esp_err_t ssd1306_display_on(ssd1306_handle_t handle)
{
    return ssd1306_send_cmd(handle, SSD1306_CMD_DISPLAY_ON);
}

esp_err_t ssd1306_display_off(ssd1306_handle_t handle)
{
    return ssd1306_send_cmd(handle, SSD1306_CMD_DISPLAY_OFF);
}

esp_err_t ssd1306_set_invert(ssd1306_handle_t handle, bool invert)
{
    return ssd1306_send_cmd(handle,
        invert ? SSD1306_CMD_INVERT_DISPLAY : SSD1306_CMD_NORMAL_DISPLAY);
}

esp_err_t ssd1306_set_memory_mode(ssd1306_handle_t handle, uint8_t mode)
{
    return ssd1306_send_cmd_arg(handle, SSD1306_CMD_SET_MEMORY_MODE, mode & 0x03);
}

esp_err_t ssd1306_set_column_address(ssd1306_handle_t handle, uint8_t start, uint8_t end)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmds[] = { SSD1306_CMD_SET_COLUMN_ADDR, start, end };
    return ssd1306_send_cmd_list(handle, cmds, sizeof(cmds));
}

esp_err_t ssd1306_set_page_address(ssd1306_handle_t handle, uint8_t start, uint8_t end)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmds[] = { SSD1306_CMD_SET_PAGE_ADDR, start, end };
    return ssd1306_send_cmd_list(handle, cmds, sizeof(cmds));
}

esp_err_t ssd1306_update(ssd1306_handle_t handle, const uint8_t *buffer)
{
    if (!handle || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set address range for full screen update
    esp_err_t ret = ssd1306_set_column_address(handle, 0, handle->width - 1);
    if (ret != ESP_OK) return ret;

    ret = ssd1306_set_page_address(handle, 0, (handle->height / 8) - 1);
    if (ret != ESP_OK) return ret;

    // Send buffer data
    return ssd1306_send_data(handle, buffer, handle->buffer_size);
}

uint8_t *ssd1306_get_buffer(ssd1306_handle_t handle)
{
    return handle ? handle->buffer : NULL;
}

size_t ssd1306_get_buffer_size(ssd1306_handle_t handle)
{
    return handle ? handle->buffer_size : 0;
}
