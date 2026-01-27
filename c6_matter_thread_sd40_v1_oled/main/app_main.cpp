/*
 * ESP32C6 Matter over Thread - 4 Inputs + 2 Outputs + SCD40 Sensor + Antenna Control
 * Based on ESP-Matter examples
 *
 * GPIO Configuration:
 * - Inputs: GPIO 0, 1, 2, 21 (Contact Sensors - Independent)
 * - Outputs: GPIO 19, 20 (On/Off Lights - Independent)
 * - I2C: GPIO 22 (SDA), GPIO 23 (SCL) - SCD40 Sensor (0x62)
 * - Status LED: GPIO 15 (Thread Role Indicator)
 *   * Solid ON: End Device (no routing)
 *   * Single blink (1x): Router (routing enabled)
 *   * Double blink (2x): Leader (routing + network leader)
 *   * OFF: Disconnected/Disabled
 *
 * SCD40 Sensor (Sensirion CO2/Temperature/Humidity):
 * - I2C Address: 0x62
 * - Measurements: CO2 (ppm), Temperature (°C), Humidity (%RH)
 * - Published via Matter clusters
 *
 * XIAO ESP32C6 Antenna Control:
 * - GPIO 3: RF switch enable (LOW to activate)
 * - GPIO 14: Antenna selection (LOW=internal, HIGH=external)
 * - Matter Control: Virtual On/Off endpoint (ON=External, OFF=Internal)
 */

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_attribute_utils.h>

#include <app/clusters/air-quality-server/air-quality-server.h>
#include <platform/CHIPDeviceLayer.h>

#include <app_priv.h>
#include <app_reset.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include <common/Esp32ThreadInit.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <openthread/netdata.h>
#include <esp_openthread.h>
#endif

#include <setup_payload/OnboardingCodesUtil.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/ESP32Config.h>

// OLED Display
#include "oled_display.h"

// Time functions
#include <time.h>
#include <sys/time.h>

// SNTP for time synchronization
#include <esp_sntp.h>
#include <esp_netif_sntp.h>

// DNS and network diagnostics
#include <lwip/dns.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

static const char *TAG = "app_main";

// OLED display available flag
static bool oled_available = false;

// GPIO Definitions for ESP32C6
// 4 Inputs
#define GPIO_INPUT_0  GPIO_NUM_0   // Input 1
#define GPIO_INPUT_1  GPIO_NUM_1   // Input 2
#define GPIO_INPUT_2  GPIO_NUM_2   // Input 3
#define GPIO_INPUT_3  GPIO_NUM_21  // Input 4

// 2 Outputs (GPIO22 and GPIO23 now used for I2C)
#define GPIO_OUTPUT_0 GPIO_NUM_19  // Output 1
#define GPIO_OUTPUT_1 GPIO_NUM_20  // Output 2

// I2C for SCD40 Sensor
#define I2C_MASTER_SCL_IO GPIO_NUM_23  // I2C SCL
#define I2C_MASTER_SDA_IO GPIO_NUM_22  // I2C SDA
#define I2C_MASTER_NUM I2C_NUM_0       // I2C port number
#define I2C_MASTER_FREQ_HZ 100000      // I2C clock frequency
#define SCD40_I2C_ADDR 0x62            // SCD40 I2C address

// XIAO ESP32C6 Status LED
#define GPIO_STATUS_LED       GPIO_NUM_15  // USER LED - Thread role indicator

// XIAO ESP32C6 Antenna Configuration
// Set to 1 for external antenna (UFL connector), 0 for internal ceramic antenna
#define USE_EXTERNAL_ANTENNA  0

// Antenna control GPIOs (XIAO ESP32C6 specific)
#define GPIO_WIFI_ENABLE      GPIO_NUM_3   // RF switch enable (must be LOW)
#define GPIO_WIFI_ANT_CONFIG  GPIO_NUM_14  // Antenna selection: LOW=internal, HIGH=external
// Note: Try GPIO14 (official docs), GPIO15, or GPIO16 if GPIO18 doesn't work

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// Forward declare Air Quality Instance pointer
static chip::app::Clusters::AirQuality::Instance * gAirQualityInstance = nullptr;

// Boot initialization flag - prevents Matter from overwriting EEPROM values during startup
static bool device_initialization_complete = false;

// SCD40 configuration loaded from EEPROM at boot - used for OpenHAB sync
static uint16_t loaded_altitude_meters = 0;
static float loaded_temp_offset_celsius = 4.0f;

// Endpoint IDs for 2 outputs
static uint16_t output_endpoint_ids[2] = {0, 0};

// Endpoint IDs for 4 inputs
static uint16_t input_endpoint_ids[4] = {0, 0, 0, 0};

// Endpoint IDs for SCD40 sensor
static uint16_t air_quality_endpoint_id = 0;  // Air Quality enum (calculated from CO2)
static uint16_t co2_endpoint_id = 0;           // CO2 concentration in PPM
static uint16_t temperature_endpoint_id = 0;
static uint16_t humidity_endpoint_id = 0;

// Endpoint IDs for SCD40 control (virtual switches for OpenHAB)
static uint16_t scd40_calibrate_endpoint_id = 0;     // Trigger forced calibration
static uint16_t scd40_asc_enable_endpoint_id = 0;    // Enable/disable ASC
static uint16_t scd40_low_power_endpoint_id = 0;     // Switch to low power mode
static uint16_t scd40_persist_endpoint_id = 0;       // Trigger persist settings
static uint16_t scd40_self_test_endpoint_id = 0;     // Trigger self test
static uint16_t scd40_altitude_endpoint_id = 0;      // Set altitude (level control)
static uint16_t scd40_temp_offset_endpoint_id = 0;   // Set temperature offset (level control)

// SNTP configuration
static bool sntp_initialized = false;
static bool sntp_time_synced = false;

// Local NTP server IPv6 address (only internal NTP server is used)
// This is the Raspberry Pi/Home Assistant IPv6 address reachable from Thread network
// Change this to match your local NTP server's IPv6 address
#ifndef CONFIG_LOCAL_NTP_SERVER_IPV6
#define CONFIG_LOCAL_NTP_SERVER_IPV6 "fddd:9cf8:e546:0:99b0:6203:5f44:6e2d"
#endif

// GPIO pins arrays
static const gpio_num_t input_pins[4] = {GPIO_INPUT_0, GPIO_INPUT_1, GPIO_INPUT_2, GPIO_INPUT_3};
static const gpio_num_t output_pins[2] = {GPIO_OUTPUT_0, GPIO_OUTPUT_1};

// SCD40 sensor data
static uint16_t scd40_co2 = 0;        // CO2 in ppm
static float scd40_temperature = 0.0;  // Temperature in °C
static float scd40_humidity = 0.0;     // Humidity in %RH
static bool scd40_available = false;  // Flag to track if sensor is available

// I2C handles (new API)
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t scd40_dev_handle = NULL;

// Helper function to calculate Air Quality from CO2 level
// Based on indoor air quality standards
static uint8_t calculate_air_quality_from_co2(uint16_t co2_ppm) {
    // AirQualityEnum values:
    // 0x00 = Unknown, 0x01 = Good, 0x02 = Fair, 0x03 = Moderate
    // 0x04 = Poor, 0x05 = VeryPoor, 0x06 = ExtremelyPoor

    if (co2_ppm < 800) {
        return 0x01;  // Good
    } else if (co2_ppm < 1000) {
        return 0x02;  // Fair
    } else if (co2_ppm < 1500) {
        return 0x03;  // Moderate
    } else if (co2_ppm < 2000) {
        return 0x04;  // Poor
    } else if (co2_ppm < 5000) {
        return 0x05;  // VeryPoor
    } else {
        return 0x06;  // ExtremelyPoor
    }
}

// SCD40 commands (Sensirion SCD40 datasheet)
// Basic Commands
#define SCD40_CMD_START_PERIODIC_MEASUREMENT  0x21b1
#define SCD40_CMD_READ_MEASUREMENT            0xec05
#define SCD40_CMD_STOP_PERIODIC_MEASUREMENT   0x3f86

// On-chip output signal compensation
#define SCD40_CMD_SET_TEMPERATURE_OFFSET      0x241d
#define SCD40_CMD_GET_TEMPERATURE_OFFSET      0x2318
#define SCD40_CMD_SET_SENSOR_ALTITUDE         0x2427
#define SCD40_CMD_GET_SENSOR_ALTITUDE         0x2322
#define SCD40_CMD_SET_AMBIENT_PRESSURE        0xe000

// Field calibration
#define SCD40_CMD_PERFORM_FORCED_RECALIBRATION           0x362f
#define SCD40_CMD_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED 0x2416
#define SCD40_CMD_GET_AUTOMATIC_SELF_CALIBRATION_ENABLED 0x2313

// Low power
#define SCD40_CMD_START_LOW_POWER_PERIODIC_MEASUREMENT   0x21ac
#define SCD40_CMD_GET_DATA_READY_STATUS                  0xe4b8

// Advanced features
#define SCD40_CMD_PERSIST_SETTINGS            0x3615
#define SCD40_CMD_GET_SERIAL_NUMBER           0x3682
#define SCD40_CMD_PERFORM_SELF_TEST           0x3639
#define SCD40_CMD_PERFORM_FACTORY_RESET       0x3632
#define SCD40_CMD_REINIT                      0x3646

// Low power single shot (SCD41 only)
#define SCD40_CMD_MEASURE_SINGLE_SHOT         0x219d
#define SCD40_CMD_MEASURE_SINGLE_SHOT_RHT_ONLY 0x2196

// Initialize I2C master (new API)
static esp_err_t i2c_master_init(void)
{
    // Configure I2C master bus
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &i2c_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C master bus creation failed: %s", esp_err_to_name(err));
        return err;
    }

    // Add SCD40 device to the bus
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SCD40_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    err = i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &scd40_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SCD40 device to I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C initialized: SDA=GPIO%d, SCL=GPIO%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    return ESP_OK;
}

// Send command to SCD40
esp_err_t scd40_send_command(uint16_t command)
{
    if (scd40_dev_handle == NULL) {
        ESP_LOGE(TAG, "SCD40 command 0x%04x failed: I2C handle is NULL (not initialized or corrupted)", command);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cmd_buf[2];
    cmd_buf[0] = (command >> 8) & 0xFF;
    cmd_buf[1] = command & 0xFF;

    esp_err_t err = i2c_master_transmit(scd40_dev_handle, cmd_buf, 2, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SCD40 command 0x%04x failed: %s", command, esp_err_to_name(err));
    }
    return err;
}

// Read data from SCD40 with CRC check (Sensirion CRC-8)
static uint8_t scd40_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

// Initialize SCD40 sensor
static esp_err_t scd40_init(void)
{
    ESP_LOGI(TAG, "Initializing SCD40 sensor...");

    // Stop any ongoing measurement
    scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Start periodic measurement
    esp_err_t err = scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start SCD40 periodic measurement");
        return err;
    }

    ESP_LOGI(TAG, "SCD40 periodic measurement started (readings every 5 seconds)");
    return ESP_OK;
}

// Read measurement from SCD40
static esp_err_t scd40_read_measurement(uint16_t *co2, float *temperature, float *humidity)
{
    if (scd40_dev_handle == NULL) {
        ESP_LOGE(TAG, "scd40_read_measurement failed: I2C handle is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[9];  // 3 words of 2 bytes + 1 CRC byte each

    // Send read command
    esp_err_t err = scd40_send_command(SCD40_CMD_READ_MEASUREMENT);
    if (err != ESP_OK) {
        return err;
    }

    // Wait for measurement to be ready (1ms should be enough)
    vTaskDelay(pdMS_TO_TICKS(1));

    // Read 9 bytes (3x [2 data bytes + 1 CRC])
    err = i2c_master_receive(scd40_dev_handle, data, 9, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SCD40 data: %s", esp_err_to_name(err));
        return err;
    }

    // Verify CRC for each word
    if (scd40_crc8(&data[0], 2) != data[2]) {
        ESP_LOGE(TAG, "SCD40 CRC error for CO2");
        return ESP_ERR_INVALID_CRC;
    }
    if (scd40_crc8(&data[3], 2) != data[5]) {
        ESP_LOGE(TAG, "SCD40 CRC error for temperature");
        return ESP_ERR_INVALID_CRC;
    }
    if (scd40_crc8(&data[6], 2) != data[8]) {
        ESP_LOGE(TAG, "SCD40 CRC error for humidity");
        return ESP_ERR_INVALID_CRC;
    }

    // Parse data
    *co2 = (data[0] << 8) | data[1];
    uint16_t temp_raw = (data[3] << 8) | data[4];
    uint16_t hum_raw = (data[6] << 8) | data[7];

    // Convert to physical values (from SCD40 datasheet)
    *temperature = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);
    *humidity = 100.0f * ((float)hum_raw / 65535.0f);

    return ESP_OK;
}

// ============================================================================
// ADDITIONAL SCD40 COMMANDS - Full API Implementation
// ============================================================================

// Helper function: Send command with data word (16-bit) and CRC
static esp_err_t scd40_send_command_with_data(uint16_t command, uint16_t data)
{
    if (scd40_dev_handle == NULL) {
        ESP_LOGE(TAG, "SCD40 command with data 0x%04x failed: I2C handle is NULL", command);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cmd_buf[5];
    uint8_t data_bytes[2];

    // Command bytes
    cmd_buf[0] = (command >> 8) & 0xFF;
    cmd_buf[1] = command & 0xFF;

    // Data bytes
    data_bytes[0] = (data >> 8) & 0xFF;
    data_bytes[1] = data & 0xFF;
    cmd_buf[2] = data_bytes[0];
    cmd_buf[3] = data_bytes[1];

    // CRC for data
    cmd_buf[4] = scd40_crc8(data_bytes, 2);

    esp_err_t err = i2c_master_transmit(scd40_dev_handle, cmd_buf, 5, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SCD40 command with data 0x%04x failed: %s", command, esp_err_to_name(err));
    }
    return err;
}

/// Helper function: Read single word (16-bit) with CRC verification
static esp_err_t scd40_read_word(uint16_t *value)
{
    if (scd40_dev_handle == NULL) {
        ESP_LOGE(TAG, "scd40_read_word failed: I2C handle is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[3]; // 2 bytes data + 1 byte CRC

    esp_err_t err = i2c_master_receive(scd40_dev_handle, data, 3, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read word from SCD40: %s", esp_err_to_name(err));
        return err;
    }

    // Verify CRC
    if (scd40_crc8(&data[0], 2) != data[2]) {
        ESP_LOGE(TAG, "SCD40 CRC error reading word");
        return ESP_ERR_INVALID_CRC;
    }

    *value = (data[0] << 8) | data[1];
    return ESP_OK;
}

// ============================================================================
// ON-CHIP OUTPUT SIGNAL COMPENSATION COMMANDS
// ============================================================================

// Set temperature offset (in °C)
// Temperature offset has no influence on SCD40 CO2 accuracy
// Default offset is 4°C
esp_err_t scd40_set_temperature_offset(float offset_celsius)
{
    // Convert °C to raw value: raw = (offset * 2^16) / 175
    uint16_t offset_raw = (uint16_t)((offset_celsius * 65536.0f) / 175.0f);

    ESP_LOGI(TAG, "Setting SCD40 temperature offset: %.2f°C (raw: 0x%04x)", offset_celsius, offset_raw);
    return scd40_send_command_with_data(SCD40_CMD_SET_TEMPERATURE_OFFSET, offset_raw);
}

// Get temperature offset (in °C)
esp_err_t scd40_get_temperature_offset(float *offset_celsius)
{
    esp_err_t err = scd40_send_command(SCD40_CMD_GET_TEMPERATURE_OFFSET);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(1));

    uint16_t offset_raw;
    err = scd40_read_word(&offset_raw);
    if (err != ESP_OK) return err;

    // Convert raw to °C: offset = (raw * 175) / 2^16
    *offset_celsius = (offset_raw * 175.0f) / 65536.0f;

    ESP_LOGI(TAG, "SCD40 temperature offset: %.2f°C (raw: 0x%04x)", *offset_celsius, offset_raw);
    return ESP_OK;
}

// Set sensor altitude (in meters above sea level)
// Used for pressure compensation. Default is 0m
esp_err_t scd40_set_sensor_altitude(uint16_t altitude_meters)
{
    ESP_LOGI(TAG, "Setting SCD40 sensor altitude: %u meters", altitude_meters);
    return scd40_send_command_with_data(SCD40_CMD_SET_SENSOR_ALTITUDE, altitude_meters);
}

// Get sensor altitude (in meters above sea level)
esp_err_t scd40_get_sensor_altitude(uint16_t *altitude_meters)
{
    esp_err_t err = scd40_send_command(SCD40_CMD_GET_SENSOR_ALTITUDE);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(1));

    err = scd40_read_word(altitude_meters);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SCD40 sensor altitude: %u meters", *altitude_meters);
    }
    return err;
}

// Set ambient pressure (in Pascals)
// Can be called during periodic measurement for continuous pressure compensation
// Overrides altitude-based compensation
esp_err_t scd40_set_ambient_pressure(uint32_t pressure_pascal)
{
    // Convert Pa to hectopascals (hPa): 1 hPa = 100 Pa
    // Command expects pressure in hectopascals
    uint16_t pressure_hpa = (uint16_t)(pressure_pascal / 100);

    ESP_LOGI(TAG, "Setting SCD40 ambient pressure: %lu Pa (%u hPa)", pressure_pascal, pressure_hpa);
    return scd40_send_command_with_data(SCD40_CMD_SET_AMBIENT_PRESSURE, pressure_hpa);
}

// ============================================================================
// FIELD CALIBRATION COMMANDS
// ============================================================================

// Perform forced recalibration (FRC)
// target_co2_ppm: Reference CO2 concentration (typically 400 ppm for outdoor air)
// Returns: FRC correction value (ppm), or 0xFFFF if failed
esp_err_t scd40_perform_forced_recalibration(uint16_t target_co2_ppm, int16_t *frc_correction)
{
    ESP_LOGI(TAG, "Performing SCD40 forced recalibration with target: %u ppm", target_co2_ppm);

    esp_err_t err = scd40_send_command_with_data(SCD40_CMD_PERFORM_FORCED_RECALIBRATION, target_co2_ppm);
    if (err != ESP_OK) return err;

    // Wait for command execution (400ms according to datasheet)
    vTaskDelay(pdMS_TO_TICKS(400));

    uint16_t correction_raw;
    err = scd40_read_word(&correction_raw);
    if (err != ESP_OK) return err;

    if (correction_raw == 0xFFFF) {
        ESP_LOGE(TAG, "SCD40 forced recalibration FAILED");
        *frc_correction = 0;
        return ESP_FAIL;
    }

    // Convert to signed correction value
    *frc_correction = (int16_t)correction_raw - 0x8000;

    ESP_LOGI(TAG, "SCD40 forced recalibration SUCCESS. Correction: %d ppm", *frc_correction);
    return ESP_OK;
}

// Enable/disable automatic self-calibration (ASC)
// ASC is enabled by default and works when sensor is exposed to fresh air (400 ppm) at least once per week
esp_err_t scd40_set_automatic_self_calibration_enabled(bool enabled)
{
    uint16_t value = enabled ? 1 : 0;
    ESP_LOGI(TAG, "Setting SCD40 automatic self-calibration: %s", enabled ? "ENABLED" : "DISABLED");
    return scd40_send_command_with_data(SCD40_CMD_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED, value);
}

// Get automatic self-calibration status
esp_err_t scd40_get_automatic_self_calibration_enabled(bool *enabled)
{
    esp_err_t err = scd40_send_command(SCD40_CMD_GET_AUTOMATIC_SELF_CALIBRATION_ENABLED);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(1));

    uint16_t value;
    err = scd40_read_word(&value);
    if (err != ESP_OK) return err;

    *enabled = (value == 1);
    ESP_LOGI(TAG, "SCD40 automatic self-calibration: %s", *enabled ? "ENABLED" : "DISABLED");
    return ESP_OK;
}

// ============================================================================
// LOW POWER COMMANDS
// ============================================================================

// Start low power periodic measurement (30 second interval instead of 5 seconds)
// Saves power but has longer response time
esp_err_t scd40_start_low_power_periodic_measurement(void)
{
    ESP_LOGI(TAG, "Starting SCD40 low power periodic measurement (30s interval)");
    return scd40_send_command(SCD40_CMD_START_LOW_POWER_PERIODIC_MEASUREMENT);
}

// Get data ready status
// Returns: true if data is ready to be read
esp_err_t scd40_get_data_ready_status(bool *data_ready)
{
    esp_err_t err = scd40_send_command(SCD40_CMD_GET_DATA_READY_STATUS);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(1));

    uint16_t status;
    err = scd40_read_word(&status);
    if (err != ESP_OK) return err;

    // Data is ready if last 11 bits are NOT all 0
    *data_ready = ((status & 0x07FF) != 0);

    return ESP_OK;
}

// ============================================================================
// ADVANCED FEATURES COMMANDS
// ============================================================================

// Persist settings to EEPROM
// Saves temperature offset, sensor altitude, and ASC enabled/disabled state
// EEPROM is guaranteed for at least 2000 write cycles
esp_err_t scd40_persist_settings(void)
{
    ESP_LOGI(TAG, "Persisting SCD40 settings to EEPROM...");
    esp_err_t err = scd40_send_command(SCD40_CMD_PERSIST_SETTINGS);
    if (err == ESP_OK) {
        // Wait for command execution (800ms according to datasheet)
        vTaskDelay(pdMS_TO_TICKS(800));
        ESP_LOGI(TAG, "SCD40 settings persisted successfully");
    }
    return err;
}

// Get serial number
// Returns 48-bit serial number
esp_err_t scd40_get_serial_number(uint64_t *serial_number)
{
    if (scd40_dev_handle == NULL) {
        ESP_LOGE(TAG, "scd40_get_serial_number failed: I2C handle is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = scd40_send_command(SCD40_CMD_GET_SERIAL_NUMBER);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t data[9]; // 3 words: 2 bytes + CRC each
    err = i2c_master_receive(scd40_dev_handle, data, 9, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SCD40 serial number: %s", esp_err_to_name(err));
        return err;
    }

    // Verify CRC for each word
    if (scd40_crc8(&data[0], 2) != data[2] ||
        scd40_crc8(&data[3], 2) != data[5] ||
        scd40_crc8(&data[6], 2) != data[8]) {
        ESP_LOGE(TAG, "SCD40 CRC error reading serial number");
        return ESP_ERR_INVALID_CRC;
    }

    // Combine 3 words into 48-bit serial number
    uint64_t word0 = ((uint64_t)data[0] << 8) | data[1];
    uint64_t word1 = ((uint64_t)data[3] << 8) | data[4];
    uint64_t word2 = ((uint64_t)data[6] << 8) | data[7];

    *serial_number = (word0 << 32) | (word1 << 16) | word2;

    ESP_LOGI(TAG, "SCD40 Serial Number: 0x%012llX", *serial_number);
    return ESP_OK;
}

// Perform self-test
// Returns: true if no malfunction detected
esp_err_t scd40_perform_self_test(bool *test_passed)
{
    ESP_LOGI(TAG, "Performing SCD40 self-test (takes ~5.5 seconds)...");

    esp_err_t err = scd40_send_command(SCD40_CMD_PERFORM_SELF_TEST);
    if (err != ESP_OK) return err;

    // Wait for command execution (5500ms according to datasheet)
    vTaskDelay(pdMS_TO_TICKS(5500));

    uint16_t result;
    err = scd40_read_word(&result);
    if (err != ESP_OK) return err;

    *test_passed = (result == 0);

    if (*test_passed) {
        ESP_LOGI(TAG, "SCD40 self-test PASSED");
    } else {
        ESP_LOGE(TAG, "SCD40 self-test FAILED (result: 0x%04x)", result);
    }

    return ESP_OK;
}

// Perform factory reset
// Resets all configuration settings and erases FRC and ASC history
esp_err_t scd40_perform_factory_reset(void)
{
    ESP_LOGW(TAG, "Performing SCD40 factory reset - all settings will be erased!");

    esp_err_t err = scd40_send_command(SCD40_CMD_PERFORM_FACTORY_RESET);
    if (err == ESP_OK) {
        // Wait for command execution (1200ms according to datasheet)
        vTaskDelay(pdMS_TO_TICKS(1200));
        ESP_LOGI(TAG, "SCD40 factory reset complete");
    }
    return err;
}

// Reinitialize sensor
// Reloads user settings from EEPROM
// Must be in idle mode (stop periodic measurement first)
esp_err_t scd40_reinit(void)
{
    ESP_LOGI(TAG, "Reinitializing SCD40 sensor...");

    esp_err_t err = scd40_send_command(SCD40_CMD_REINIT);
    if (err == ESP_OK) {
        // Wait for command execution (20ms according to datasheet)
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "SCD40 reinitialized successfully");
    }
    return err;
}

// ============================================================================
// LOW POWER SINGLE SHOT COMMANDS (SCD41 only)
// ============================================================================

// Measure single shot (on-demand measurement)
// Measures CO2, temperature, and humidity
// SCD41 only - takes ~1.35 seconds
esp_err_t scd40_measure_single_shot(void)
{
    ESP_LOGI(TAG, "SCD40/41 single shot measurement (takes ~1.35s)");

    esp_err_t err = scd40_send_command(SCD40_CMD_MEASURE_SINGLE_SHOT);
    if (err == ESP_OK) {
        // Wait for measurement (1350ms according to datasheet)
        vTaskDelay(pdMS_TO_TICKS(1350));
    }
    return err;
}

// Measure single shot - RH and T only (no CO2)
// SCD41 only - takes ~50ms
// CO2 output will be 0 ppm
esp_err_t scd40_measure_single_shot_rht_only(void)
{
    ESP_LOGI(TAG, "SCD40/41 single shot RH/T measurement (takes ~50ms)");

    esp_err_t err = scd40_send_command(SCD40_CMD_MEASURE_SINGLE_SHOT_RHT_ONLY);
    if (err == ESP_OK) {
        // Wait for measurement (50ms according to datasheet)
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return err;
}

// ============================================================================
// END OF SCD40 ADDITIONAL COMMANDS
// ============================================================================

// Configure antenna selection for XIAO ESP32C6
static void configure_antenna(void)
{
    // Configure GPIO3 (WIFI_ENABLE) - must be LOW to enable RF switch control
    gpio_config_t ant_enable_conf = {};
    ant_enable_conf.intr_type = GPIO_INTR_DISABLE;
    ant_enable_conf.mode = GPIO_MODE_OUTPUT;
    ant_enable_conf.pin_bit_mask = (1ULL << GPIO_WIFI_ENABLE);
    ant_enable_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ant_enable_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&ant_enable_conf);
    gpio_set_level(GPIO_WIFI_ENABLE, 0);  // Set LOW to activate RF switch

    // Small delay for RF switch activation
    vTaskDelay(pdMS_TO_TICKS(10));

    // Configure WIFI_ANT_CONFIG GPIO - antenna selection
    gpio_config_t ant_config_conf = {};
    ant_config_conf.intr_type = GPIO_INTR_DISABLE;
    ant_config_conf.mode = GPIO_MODE_OUTPUT;
    ant_config_conf.pin_bit_mask = (1ULL << GPIO_WIFI_ANT_CONFIG);
    ant_config_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ant_config_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&ant_config_conf);

#if USE_EXTERNAL_ANTENNA
    gpio_set_level(GPIO_WIFI_ANT_CONFIG, 1);  // HIGH = External antenna (UFL)
    ESP_LOGI(TAG, "Antenna configured: EXTERNAL (UFL connector)");
#else
    gpio_set_level(GPIO_WIFI_ANT_CONFIG, 0);  // LOW = Internal ceramic antenna
    ESP_LOGI(TAG, "Antenna configured: INTERNAL (ceramic)");
#endif
}

// Switch antenna at runtime (call this function to change antenna)
void switch_antenna(bool use_external)
{
    if (use_external) {
        gpio_set_level(GPIO_WIFI_ANT_CONFIG, 1);  // HIGH = External antenna
        ESP_LOGI(TAG, "Switched to EXTERNAL antenna (UFL)");
    } else {
        gpio_set_level(GPIO_WIFI_ANT_CONFIG, 0);  // LOW = Internal antenna
        ESP_LOGI(TAG, "Switched to INTERNAL antenna (ceramic)");
    }
}

// ==============================================================================
// NVS Helper Functions for SCD40 Configuration Persistence
// ==============================================================================

#define NVS_NAMESPACE "scd40_cfg"
#define NVS_KEY_ALTITUDE "altitude"
#define NVS_KEY_TEMP_OFFSET "temp_offset"

// Save altitude to NVS
static esp_err_t nvs_save_altitude(uint16_t altitude)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to open namespace '%s' for writing altitude: %s", NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u16(handle, NVS_KEY_ALTITUDE, altitude);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to set altitude value: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to commit altitude: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    ESP_LOGI(TAG, "NVS: ✓ Saved altitude %u meters (handle=0x%lx)", altitude, (unsigned long)handle);
    nvs_close(handle);
    return ESP_OK;
}

// Load altitude from NVS
static esp_err_t nvs_load_altitude(uint16_t *altitude)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;  // Not found or error
    }

    err = nvs_get_u16(handle, NVS_KEY_ALTITUDE, altitude);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS: Loaded altitude %u meters", *altitude);
    }
    return err;
}

// Save temperature offset to NVS (stored as int16_t in centesimi: 4.5°C = 450)
static esp_err_t nvs_save_temp_offset(float offset_celsius)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to open namespace '%s' for writing temp offset: %s", NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    // Convert float to int16_t centesimi (4.5°C → 450)
    int16_t offset_centesimi = (int16_t)(offset_celsius * 100.0f);

    err = nvs_set_i16(handle, NVS_KEY_TEMP_OFFSET, offset_centesimi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to set temp offset value: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to commit temp offset: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    ESP_LOGI(TAG, "NVS: ✓ Saved temp offset %.2f°C (%d centesimi, handle=0x%lx)", offset_celsius, offset_centesimi, (unsigned long)handle);
    nvs_close(handle);
    return ESP_OK;
}

// Load temperature offset from NVS
static esp_err_t nvs_load_temp_offset(float *offset_celsius)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;  // Not found or error
    }

    int16_t offset_centesimi;
    err = nvs_get_i16(handle, NVS_KEY_TEMP_OFFSET, &offset_centesimi);
    nvs_close(handle);

    if (err == ESP_OK) {
        *offset_celsius = offset_centesimi / 100.0f;
        ESP_LOGI(TAG, "NVS: Loaded temp offset %.2f°C (%d centesimi)", *offset_celsius, offset_centesimi);
    }
    return err;
}

// Erase SCD40 configuration from NVS (used during factory reset)
esp_err_t nvs_erase_scd40_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS: Namespace %s not found during erase (already clean)", NVS_NAMESPACE);
        return ESP_OK;  // If namespace doesn't exist, consider it already erased
    }

    // Erase entire namespace
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "NVS: Erased all SCD40 configuration (altitude, temp offset)");
        } else {
            ESP_LOGE(TAG, "NVS: Failed to commit erase: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "NVS: Failed to erase namespace: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

// Event callback
static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        // Trigger NTP resync when IP address changes (network may now be available)
        if (sntp_initialized && !sntp_time_synced) {
            ESP_LOGI(TAG, "Triggering NTP resync after IP address change");
            esp_sntp_stop();
            esp_sntp_init();
        }
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;
    case chip::DeviceLayer::DeviceEventType::kThreadStateChange:
        ESP_LOGI(TAG, "Thread state changed");
        break;
    case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
        ESP_LOGI(TAG, "Thread connectivity changed");
        // Trigger NTP resync when Thread connectivity changes
        // This ensures time sync happens after Thread network is available
        if (sntp_initialized && !sntp_time_synced) {
            ESP_LOGI(TAG, "Triggering NTP resync after Thread connectivity change");
            esp_sntp_stop();
            esp_sntp_init();
        }
        break;
    case chip::DeviceLayer::DeviceEventType::kCHIPoBLEConnectionEstablished:
        ESP_LOGI(TAG, "CHIPoBLE connection established");
        break;
    case chip::DeviceLayer::DeviceEventType::kCHIPoBLEConnectionClosed:
        ESP_LOGI(TAG, "CHIPoBLE connection closed");
        break;
    default:
        ESP_LOGD(TAG, "Event type: %d", event->Type);
        break;
    }
}

// ==============================================================================
// SNTP Time Synchronization
// ==============================================================================

// SNTP time sync callback
static void sntp_sync_time_cb(struct timeval *tv) {
    sntp_time_synced = true;

    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG, "SNTP: Time synchronized! %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

// Initialize SNTP with local NTP server only
static void sntp_init_local(void) {
    const char* local_ntp = CONFIG_LOCAL_NTP_SERVER_IPV6;

    ESP_LOGI(TAG, "SNTP: Initializing with local NTP server: %s", local_ntp);

    // Set timezone to Europe/Rome (CET-1CEST,M3.5.0,M10.5.0/3)
    // CET = Central European Time (UTC+1)
    // CEST = Central European Summer Time (UTC+2)
    // M3.5.0 = DST starts last Sunday of March at 2:00
    // M10.5.0/3 = DST ends last Sunday of October at 3:00
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    if (sntp_initialized) {
        // Stop existing SNTP
        esp_sntp_stop();
    }

    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // Use only local NTP server (IPv6 address reachable from Thread network)
    ESP_LOGI(TAG, "SNTP: Using LOCAL NTP server: %s", local_ntp);
    esp_sntp_setservername(0, local_ntp);

    esp_sntp_set_time_sync_notification_cb(sntp_sync_time_cb);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    // Start SNTP
    esp_sntp_init();
    sntp_initialized = true;

    ESP_LOGI(TAG, "SNTP: Started, waiting for time sync...");
}

// Get SNTP sync status string
static const char* sntp_sync_status_str(sntp_sync_status_t status) {
    switch (status) {
        case SNTP_SYNC_STATUS_RESET: return "RESET (waiting)";
        case SNTP_SYNC_STATUS_COMPLETED: return "COMPLETED";
        case SNTP_SYNC_STATUS_IN_PROGRESS: return "IN_PROGRESS";
        default: return "UNKNOWN";
    }
}

// Log Thread network info
static void log_thread_network_info(void) {
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    otInstance *ot_instance = esp_openthread_get_instance();
    if (!ot_instance) {
        ESP_LOGE(TAG, "Thread: No OpenThread instance");
        return;
    }

    ESP_LOGW(TAG, "Thread Network Info:");

    // Device role
    otDeviceRole role = otThreadGetDeviceRole(ot_instance);
    const char* role_str = "Unknown";
    switch(role) {
        case OT_DEVICE_ROLE_DISABLED: role_str = "Disabled"; break;
        case OT_DEVICE_ROLE_DETACHED: role_str = "Detached"; break;
        case OT_DEVICE_ROLE_CHILD: role_str = "Child"; break;
        case OT_DEVICE_ROLE_ROUTER: role_str = "Router"; break;
        case OT_DEVICE_ROLE_LEADER: role_str = "Leader"; break;
    }
    ESP_LOGW(TAG, "  Role: %s", role_str);

    // Get mesh-local addresses
    const otNetifAddress *addr = otIp6GetUnicastAddresses(ot_instance);
    ESP_LOGW(TAG, "  IPv6 Addresses:");
    while (addr) {
        char addr_str[64];
        otIp6AddressToString(&addr->mAddress, addr_str, sizeof(addr_str));
        ESP_LOGW(TAG, "    %s", addr_str);
        addr = addr->mNext;
    }

    // Check if we have a border router
    otBorderRouterConfig config;
    otNetworkDataIterator iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    bool has_border_router = false;
    ESP_LOGW(TAG, "  Border Routers in Network Data:");
    while (otNetDataGetNextOnMeshPrefix(ot_instance, &iterator, &config) == OT_ERROR_NONE) {
        has_border_router = true;
        char prefix_str[64];
        otIp6PrefixToString(&config.mPrefix, prefix_str, sizeof(prefix_str));
        ESP_LOGW(TAG, "    Prefix: %s (default:%d, slaac:%d, dhcp:%d, onMesh:%d)",
                 prefix_str,
                 config.mDefaultRoute ? 1 : 0,
                 config.mSlaac ? 1 : 0,
                 config.mDhcp ? 1 : 0,
                 config.mOnMesh ? 1 : 0);
    }
    if (!has_border_router) {
        ESP_LOGE(TAG, "    NO BORDER ROUTER PREFIX! Cannot reach Internet.");
    }
#endif
}

// SNTP monitoring task - periodically checks and retries sync with local server only
static void sntp_monitor_task(void *arg) {
    ESP_LOGI(TAG, "SNTP monitor task started (local server only: %s)", CONFIG_LOCAL_NTP_SERVER_IPV6);

    int retry_count = 0;
    const int MAX_RETRIES = 10;  // Maximum retries before extended wait

    // Wait for initial Thread connection
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1) {
        // Check current SNTP status
        sntp_sync_status_t status = sntp_get_sync_status();

        // Get current time to check if it's reasonable
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        int year = timeinfo.tm_year + 1900;

        // Check if Thread is connected
        bool thread_connected = false;
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
        otInstance *ot_instance = esp_openthread_get_instance();
        if (ot_instance) {
            otDeviceRole role = otThreadGetDeviceRole(ot_instance);
            thread_connected = (role >= OT_DEVICE_ROLE_CHILD);
        }
#endif

        if (sntp_time_synced && year >= 2020) {
            // Time is synced, just log periodically
            ESP_LOGI(TAG, "SNTP: Status=%s, Time=%04d-%02d-%02d %02d:%02d:%02d, Server=%s",
                     sntp_sync_status_str(status),
                     year, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                     CONFIG_LOCAL_NTP_SERVER_IPV6);
            retry_count = 0;  // Reset retry count on success
            // Check every 5 minutes once synced
            vTaskDelay(pdMS_TO_TICKS(300000));
        } else {
            // Not synced yet
            ESP_LOGW(TAG, "SNTP: NOT SYNCED - Status=%s, Year=%d, Thread=%s, Server=%s, Retry=%d",
                     sntp_sync_status_str(status),
                     year,
                     thread_connected ? "Connected" : "Disconnected",
                     CONFIG_LOCAL_NTP_SERVER_IPV6,
                     retry_count);

            if (thread_connected) {
                retry_count++;

                // Run network diagnostics on first retry
                if (retry_count == 1) {
                    ESP_LOGW(TAG, "========================================");
                    ESP_LOGW(TAG, "=== SNTP Network Diagnostics ===");
                    ESP_LOGW(TAG, "========================================");
                    log_thread_network_info();
                    ESP_LOGW(TAG, "========================================");
                }

                // Force resync with local server
                if (retry_count <= MAX_RETRIES) {
                    ESP_LOGI(TAG, "SNTP: Forcing resync attempt with local server...");
                    esp_sntp_stop();
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_sntp_init();
                } else {
                    // After max retries, wait longer between attempts
                    ESP_LOGW(TAG, "SNTP: Max retries reached, waiting 2 minutes before next attempt");
                    vTaskDelay(pdMS_TO_TICKS(90000));  // Additional wait
                }
            }

            // Retry every 30 seconds when not synced
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }
}


// Attribute update callback
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                         uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val,
                                         void *priv_data)
{
    if (type == POST_UPDATE) {
        // Check if it's an OnOff cluster update
        if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
            // Check which output endpoint it is
            for (int i = 0; i < 2; i++) {
                if (endpoint_id == output_endpoint_ids[i]) {
                    // Active-low logic: Matter ON (true) = GPIO LOW (0)
                    gpio_set_level(output_pins[i], val->val.b ? 0 : 1);
                    ESP_LOGI(TAG, "Output %d (GPIO%d) set to %s (GPIO=%d)", i + 1, output_pins[i],
                             val->val.b ? "ON" : "OFF", val->val.b ? 0 : 1);
                    break;
                }
            }

            // SCD40 Control Commands via Matter/OpenHAB
            if (!scd40_available) {
                ESP_LOGW(TAG, "SCD40 not available - ignoring command");
                return ESP_OK;
            }

            // Trigger Forced Calibration (400 ppm fresh air)
            if (endpoint_id == scd40_calibrate_endpoint_id && val->val.b) {
                ESP_LOGI(TAG, "OpenHAB: Trigger SCD40 forced calibration (400 ppm)");
                scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
                vTaskDelay(pdMS_TO_TICKS(500));

                int16_t correction;
                esp_err_t err = scd40_perform_forced_recalibration(400, &correction);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Calibration completed. Correction: %d ppm", correction);
                } else {
                    ESP_LOGE(TAG, "Calibration FAILED!");
                }

                scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);

                // Auto-reset switch to OFF
                val->val.b = false;
                attribute::update(endpoint_id, cluster_id, attribute_id, val);
            }

            // Enable/Disable Automatic Self-Calibration
            if (endpoint_id == scd40_asc_enable_endpoint_id) {
                ESP_LOGI(TAG, "OpenHAB: Set SCD40 ASC %s", val->val.b ? "ENABLED" : "DISABLED");
                scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
                vTaskDelay(pdMS_TO_TICKS(500));

                scd40_set_automatic_self_calibration_enabled(val->val.b);

                scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);
            }

            // Switch Low Power Mode
            if (endpoint_id == scd40_low_power_endpoint_id) {
                ESP_LOGI(TAG, "OpenHAB: Set SCD40 %s mode", val->val.b ? "LOW POWER" : "NORMAL");
                scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
                vTaskDelay(pdMS_TO_TICKS(500));

                if (val->val.b) {
                    scd40_start_low_power_periodic_measurement();  // 30s interval
                } else {
                    scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);  // 5s interval
                }
            }

            // Trigger Persist Settings
            if (endpoint_id == scd40_persist_endpoint_id && val->val.b) {
                ESP_LOGI(TAG, "OpenHAB: Trigger SCD40 persist settings to EEPROM");
                scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
                vTaskDelay(pdMS_TO_TICKS(500));

                scd40_persist_settings();

                scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);

                // Auto-reset switch to OFF
                val->val.b = false;
                attribute::update(endpoint_id, cluster_id, attribute_id, val);
            }

            // Trigger Self Test
            if (endpoint_id == scd40_self_test_endpoint_id && val->val.b) {
                ESP_LOGI(TAG, "OpenHAB: Trigger SCD40 self-test (takes 5.5 seconds)");
                scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
                vTaskDelay(pdMS_TO_TICKS(500));

                bool test_passed;
                esp_err_t err = scd40_perform_self_test(&test_passed);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Self-test: %s", test_passed ? "PASSED" : "FAILED");
                }

                scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);

                // Auto-reset switch to OFF
                val->val.b = false;
                attribute::update(endpoint_id, cluster_id, attribute_id, val);
            }
        }

        // On/Off cluster (for altitude, temperature offset virtual dimmers)
        // Matter automatically tries to set on/off when level changes - handle it gracefully
        if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
            if (endpoint_id == scd40_altitude_endpoint_id || endpoint_id == scd40_temp_offset_endpoint_id) {
                // These are virtual dimmers - always keep them "on", silently ignore state changes
                return ESP_OK;
            }
        }

        // Level Control cluster (for altitude, temperature offset)
        if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            // IMPORTANT: Ignore attribute updates during boot initialization
            // This prevents Matter SDK from overwriting EEPROM values with defaults
            if (!device_initialization_complete) {
                ESP_LOGD(TAG, "Ignoring Level Control update during boot initialization (endpoint %u, level %u)",
                         endpoint_id, val->val.u8);
                return ESP_OK;
            }

            // SCD40 endpoints require sensor to be available
            if (!scd40_available) {
                if (endpoint_id == scd40_altitude_endpoint_id || endpoint_id == scd40_temp_offset_endpoint_id) {
                    ESP_LOGW(TAG, "SCD40 not available - ignoring command");
                }
                return ESP_OK;
            }

            // Set Altitude (0-5000 meters, mapped to 1-255 level)
            if (endpoint_id == scd40_altitude_endpoint_id) {
                // Map level (1-255) to altitude (0-5000m): level 1=0m, level 255=5000m
                uint16_t altitude = ((val->val.u8 - 1) * 5000) / 254;
                ESP_LOGI(TAG, "OpenHAB: Set SCD40 altitude %u meters", altitude);

                scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
                vTaskDelay(pdMS_TO_TICKS(500));

                scd40_set_sensor_altitude(altitude);

                scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);

                // Save immediately to NVS for persistence across reboots
                nvs_save_altitude(altitude);

                return ESP_OK;
            }

            // Set Temperature Offset (0-10°C, mapped to 1-255 level)
            if (endpoint_id == scd40_temp_offset_endpoint_id) {
                // Map level (1-255) to offset (0-10°C): level 1=0°C, level 255=10°C
                float offset = ((val->val.u8 - 1) * 10.0f) / 254.0f;
                ESP_LOGI(TAG, "OpenHAB: Set SCD40 temperature offset %.2f°C", offset);

                scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
                vTaskDelay(pdMS_TO_TICKS(500));

                scd40_set_temperature_offset(offset);

                scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);

                // Save immediately to NVS for persistence across reboots
                nvs_save_temp_offset(offset);

                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}

// Identification callback
static esp_err_t app_identification_cb(identification::callback_type_t type,
                                       uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type=%u, endpoint=%u", type, endpoint_id);
    return ESP_OK;
}

// GPIO input monitoring task
// Monitors 4 independent inputs and reports state changes via Matter
static void gpio_input_task(void *arg)
{
    bool last_states[4] = {true, true, true, true};  // Assume pull-up, initial HIGH
    bool current_state;
    bool inverted_state;

    while (1) {
        // Monitor all 4 inputs
        for (int i = 0; i < 4; i++) {
            current_state = gpio_get_level(input_pins[i]);

            // Detect state change
            if (current_state != last_states[i]) {
                // Logic: LOW (contact closed) = false (closed), HIGH (open) = true (open)
                inverted_state = current_state;

                ESP_LOGI(TAG, "Input %d (GPIO%d) changed to %s", i + 1, input_pins[i],
                         inverted_state ? "OPEN" : "CLOSED");

                // Update Matter Boolean State attribute
                node_t *node = node::get();
                endpoint_t *endpoint = endpoint::get(node, input_endpoint_ids[i]);

                // Use BooleanState cluster to report contact sensor state
                // StateValue inverted: HIGH (open physically) = false (closed in Matter)
                cluster_t *cluster = cluster::get(endpoint, BooleanState::Id);
                if (cluster) {
                    attribute_t *attribute = attribute::get(cluster, BooleanState::Attributes::StateValue::Id);
                    if (attribute) {
                        esp_matter_attr_val_t val;
                        val.type = ESP_MATTER_VAL_TYPE_BOOLEAN;
                        val.val.b = inverted_state;  // LOW = false (closed), HIGH = true (open)
                        attribute::update(input_endpoint_ids[i], BooleanState::Id,
                                         BooleanState::Attributes::StateValue::Id, &val);
                    }
                }

                last_states[i] = current_state;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms polling interval with debounce
    }
}

// SCD40 sensor reading task
// Reads CO2, temperature, and humidity every 5 seconds and updates Matter attributes
static void scd40_sensor_task(void *arg)
{
    ESP_LOGI(TAG, "SCD40 sensor task started");

    // Check if sensor is available
    if (!scd40_available) {
        ESP_LOGW(TAG, "SCD40 sensor not available - task will not read sensor");
        ESP_LOGI(TAG, "SCD40 sensor task terminating (sensor not connected)");
        vTaskDelete(NULL);  // Terminate task if sensor not available
        return;
    }

    // Wait for first measurement to be ready (SCD40 needs 5 seconds after start)
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "SCD40 sensor task: endpoint IDs - CO2=%u, Temp=%u, Hum=%u",
             co2_endpoint_id, temperature_endpoint_id, humidity_endpoint_id);

    while (1) {
        uint16_t co2;
        float temperature, humidity;

        // Read measurement from SCD40
        esp_err_t err = scd40_read_measurement(&co2, &temperature, &humidity);

        if (err == ESP_OK) {
            // Update global variables
            scd40_co2 = co2;
            scd40_temperature = temperature;
            scd40_humidity = humidity;

            ESP_LOGI(TAG, "SCD40: CO2=%u ppm, Temp=%.2f°C, Humidity=%.2f%%RH",
                     co2, temperature, humidity);

            // Update Matter attributes
            esp_matter_attr_val_t val;

            // Update Air Quality enum (using Air Quality Instance API with proper locking)
            if (gAirQualityInstance) {
                uint8_t air_quality_val = calculate_air_quality_from_co2(co2);
                chip::app::Clusters::AirQuality::AirQualityEnum air_quality_enum;

                // Convert uint8_t to AirQualityEnum
                switch (air_quality_val) {
                    case 0x01: air_quality_enum = chip::app::Clusters::AirQuality::AirQualityEnum::kGood; break;
                    case 0x02: air_quality_enum = chip::app::Clusters::AirQuality::AirQualityEnum::kFair; break;
                    case 0x03: air_quality_enum = chip::app::Clusters::AirQuality::AirQualityEnum::kModerate; break;
                    case 0x04: air_quality_enum = chip::app::Clusters::AirQuality::AirQualityEnum::kPoor; break;
                    case 0x05: air_quality_enum = chip::app::Clusters::AirQuality::AirQualityEnum::kVeryPoor; break;
                    case 0x06: air_quality_enum = chip::app::Clusters::AirQuality::AirQualityEnum::kExtremelyPoor; break;
                    default: air_quality_enum = chip::app::Clusters::AirQuality::AirQualityEnum::kUnknown; break;
                }

                // Acquire Matter stack lock before updating Air Quality
                chip::DeviceLayer::PlatformMgr().LockChipStack();
                auto status = gAirQualityInstance->UpdateAirQuality(air_quality_enum);
                chip::DeviceLayer::PlatformMgr().UnlockChipStack();

                if (status == chip::Protocols::InteractionModel::Status::Success) {
                    ESP_LOGI(TAG, "Air Quality updated to: %u (CO2: %u ppm)", air_quality_val, co2);
                } else {
                    ESP_LOGW(TAG, "Failed to update Air Quality: status %d", static_cast<int>(status));
                }
            }

            // Update CO2 concentration value (cluster 0x040D)
            if (co2_endpoint_id != 0) {
                val.type = ESP_MATTER_VAL_TYPE_NULLABLE_FLOAT;
                val.val.f = (float)co2;  // CO2 PPM value (400-5000 range)
                esp_err_t err_co2 = attribute::update(co2_endpoint_id,
                                 CarbonDioxideConcentrationMeasurement::Id,
                                 CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id,
                                 &val);
                if (err_co2 != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to update CO2 concentration attribute: %s", esp_err_to_name(err_co2));
                }
            }

            // Update Temperature attribute (in °C * 100 for Matter)
            if (temperature_endpoint_id != 0) {
                val.type = ESP_MATTER_VAL_TYPE_NULLABLE_INT16;
                val.val.i16 = (int16_t)(temperature * 100);
                esp_err_t err_temp = attribute::update(temperature_endpoint_id,
                                 TemperatureMeasurement::Id,
                                 TemperatureMeasurement::Attributes::MeasuredValue::Id,
                                 &val);
                if (err_temp != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to update temperature attribute: %s", esp_err_to_name(err_temp));
                }
            }

            // Update Humidity attribute (in %RH * 100 for Matter)
            if (humidity_endpoint_id != 0) {
                val.type = ESP_MATTER_VAL_TYPE_NULLABLE_UINT16;
                val.val.u16 = (uint16_t)(humidity * 100);
                esp_err_t err_hum = attribute::update(humidity_endpoint_id,
                                 RelativeHumidityMeasurement::Id,
                                 RelativeHumidityMeasurement::Attributes::MeasuredValue::Id,
                                 &val);
                if (err_hum != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to update humidity attribute: %s", esp_err_to_name(err_hum));
                }
            }

            // Update OLED display with sensor data and Thread status
            if (oled_available) {
                // Get Thread connection status
                bool thread_connected = false;
                uint8_t node_count = 0;
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
                otInstance *ot_instance = esp_openthread_get_instance();
                if (ot_instance) {
                    otDeviceRole role = otThreadGetDeviceRole(ot_instance);
                    thread_connected = (role >= OT_DEVICE_ROLE_CHILD);
                    if (thread_connected) {
                        node_count = 1;  // Start with self

                        // Count neighbor nodes from neighbor table
                        otNeighborInfoIterator iterator = OT_NEIGHBOR_INFO_ITERATOR_INIT;
                        otNeighborInfo neighborInfo;
                        while (otThreadGetNextNeighborInfo(ot_instance, &iterator, &neighborInfo) == OT_ERROR_NONE) {
                            node_count++;
                        }
                    }
                }
#endif
                // Get current I/O states
                uint8_t input_states = 0;
                uint8_t output_states = 0;
                for (int i = 0; i < 4; i++) {
                    if (gpio_get_level(input_pins[i]) == 0) {  // Active low (contact closed)
                        input_states |= (1 << i);
                    }
                }
                for (int i = 0; i < 2; i++) {
                    // Active-low logic: GPIO LOW (0) = output ON
                    if (gpio_get_level(output_pins[i]) == 0) {
                        output_states |= (1 << i);
                    }
                }

                // Get current time
                time_t now;
                struct tm timeinfo;
                time(&now);
                localtime_r(&now, &timeinfo);

                // Calculate air quality for OLED display
                uint8_t oled_air_quality = calculate_air_quality_from_co2(co2);

                oled_update_sensor_display(co2, temperature, humidity,
                                           thread_connected, node_count,
                                           input_states, output_states,
                                           timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                                           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                                           oled_air_quality);
            }
        } else {
            ESP_LOGW(TAG, "Failed to read SCD40 measurement");
        }

        // SCD40 provides new data every 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ==============================================================================
// SCD40 Configuration Sync Task
// ==============================================================================
// Syncs altitude and temperature offset values from EEPROM to OpenHAB after boot
// Delayed to ensure Matter connection is fully established
static void scd40_config_sync_task(void *arg)
{
    // Wait 10 seconds for Matter/Thread connection to stabilize
    ESP_LOGI(TAG, "SCD40 config sync task started - waiting 10s for Matter connection...");
    vTaskDelay(pdMS_TO_TICKS(10000));

    if (!scd40_available) {
        ESP_LOGI(TAG, "SCD40 not available - skipping config sync");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Syncing SCD40 configuration to OpenHAB...");

    // Update altitude attribute (Level Control cluster)
    if (scd40_altitude_endpoint_id != 0) {
        uint8_t altitude_level = (uint8_t)(((loaded_altitude_meters * 254) + 2500) / 5000) + 1;
        esp_matter_attr_val_t altitude_val = esp_matter_invalid(NULL);
        altitude_val.type = ESP_MATTER_VAL_TYPE_NULLABLE_UINT8;
        altitude_val.val.u8 = altitude_level;

        esp_err_t err = attribute::update(scd40_altitude_endpoint_id,
                        LevelControl::Id,
                        LevelControl::Attributes::CurrentLevel::Id,
                        &altitude_val);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "→ OpenHAB: Synced altitude %um (level %u)", loaded_altitude_meters, altitude_level);
        } else {
            ESP_LOGW(TAG, "Failed to sync altitude: %s", esp_err_to_name(err));
        }
    }

    // Update temperature offset attribute (Level Control cluster)
    if (scd40_temp_offset_endpoint_id != 0) {
        uint8_t temp_offset_level = (uint8_t)(((loaded_temp_offset_celsius * 254.0f) + 5.0f) / 10.0f) + 1;
        esp_matter_attr_val_t temp_offset_val = esp_matter_invalid(NULL);
        temp_offset_val.type = ESP_MATTER_VAL_TYPE_NULLABLE_UINT8;
        temp_offset_val.val.u8 = temp_offset_level;

        esp_err_t err = attribute::update(scd40_temp_offset_endpoint_id,
                        LevelControl::Id,
                        LevelControl::Attributes::CurrentLevel::Id,
                        &temp_offset_val);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "→ OpenHAB: Synced temp offset %.2f°C (level %u)", loaded_temp_offset_celsius, temp_offset_level);
        } else {
            ESP_LOGW(TAG, "Failed to sync temp offset: %s", esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "✓ SCD40 configuration synced to OpenHAB");

    // Task completes - delete itself
    vTaskDelete(NULL);
}

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
// Thread role monitoring task - Controls status LED
// LED patterns indicate Thread device role in mesh network
static void thread_status_led_task(void *arg)
{
    otDeviceRole last_role = OT_DEVICE_ROLE_DISABLED;

    ESP_LOGI(TAG, "Thread status LED task started on GPIO%d", GPIO_STATUS_LED);

    while (1) {
        // Get OpenThread instance
        otInstance *instance = esp_openthread_get_instance();
        if (instance) {
            otDeviceRole role = otThreadGetDeviceRole(instance);

            // Log role changes
            if (role != last_role) {
                const char *role_str = "UNKNOWN";
                switch (role) {
                    case OT_DEVICE_ROLE_DISABLED:  role_str = "DISABLED"; break;
                    case OT_DEVICE_ROLE_DETACHED:  role_str = "DETACHED"; break;
                    case OT_DEVICE_ROLE_CHILD:     role_str = "END DEVICE (Child)"; break;
                    case OT_DEVICE_ROLE_ROUTER:    role_str = "ROUTER"; break;
                    case OT_DEVICE_ROLE_LEADER:    role_str = "LEADER (Router)"; break;
                }
                ESP_LOGI(TAG, "Thread role changed: %s", role_str);
                last_role = role;
            }

            // Determine LED pattern based on role
            switch (role) {
                case OT_DEVICE_ROLE_DISABLED:
                case OT_DEVICE_ROLE_DETACHED:
                    // Not connected - LED OFF
                    gpio_set_level(GPIO_STATUS_LED, 0);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    break;

                case OT_DEVICE_ROLE_CHILD:
                    // End Device (no routing) - LED ON (solid)
                    gpio_set_level(GPIO_STATUS_LED, 1);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    break;

                case OT_DEVICE_ROLE_ROUTER:
                    // Router - Single blink (250ms ON, 750ms OFF = 1 second cycle)
                    gpio_set_level(GPIO_STATUS_LED, 1);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    gpio_set_level(GPIO_STATUS_LED, 0);
                    vTaskDelay(pdMS_TO_TICKS(250));
                    break;

                case OT_DEVICE_ROLE_LEADER:
                    // Leader - Double blink (200ms ON, 200ms OFF, 200ms ON, 600ms OFF = 1200ms cycle)
                    // First blink
                    gpio_set_level(GPIO_STATUS_LED, 0);
                    vTaskDelay(pdMS_TO_TICKS(250));
                    gpio_set_level(GPIO_STATUS_LED, 1);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    // Second blink
                    gpio_set_level(GPIO_STATUS_LED, 0);
                    vTaskDelay(pdMS_TO_TICKS(250));
                    gpio_set_level(GPIO_STATUS_LED, 1);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    break;

                default:
                    // Unknown - Fast blink
                    gpio_set_level(GPIO_STATUS_LED, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_set_level(GPIO_STATUS_LED, 0);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    break;
            }
        } else {
            // OpenThread not available - LED OFF
            gpio_set_level(GPIO_STATUS_LED, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD

extern "C" void app_main()
{
    // EARLY DEBUG: Test USB Serial output immediately
    printf("\n\n");
    printf("==============================================\n");
    printf("ESP32C6 XIAO - USB SERIAL TEST\n");
    printf("==============================================\n");
    printf("If you see this, USB Serial is working!\n\n");

    esp_err_t err = ESP_OK;

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Set custom Vendor Name and Product Name in NVS BEFORE Matter starts
    const char *vendor_name = "VicinoDiCasaDigitale";
    const char *product_name = "Matter Thread 4in/2out+SCD40";

    chip::DeviceLayer::Internal::ESP32Config::WriteConfigValueStr(
        chip::DeviceLayer::Internal::ESP32Config::kConfigKey_VendorName, vendor_name);
    chip::DeviceLayer::Internal::ESP32Config::WriteConfigValueStr(
        chip::DeviceLayer::Internal::ESP32Config::kConfigKey_ProductName, product_name);

    ESP_LOGI(TAG, "Device info configured in NVS: Vendor=%s, Product=%s", vendor_name, product_name);

    // Configure antenna (XIAO ESP32C6 specific - must be done BEFORE radio operations)
    configure_antenna();

    // Initialize I2C for SCD40 sensor (optional - allows boot without sensor)
    err = i2c_master_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C initialization failed - SCD40 sensor disabled");
        ESP_LOGW(TAG, "Device will continue without CO2/Temperature/Humidity sensor");
        scd40_available = false;
    } else {
        // Wait for SCD40 power-on (datasheet requires min 1000ms after power-up)
        ESP_LOGI(TAG, "Waiting for SCD40 power-on (3000ms)...");
        vTaskDelay(pdMS_TO_TICKS(3000));

        // Initialize SCD40 sensor only if I2C works
        err = scd40_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SCD40 sensor initialization failed - sensor disabled");
            ESP_LOGW(TAG, "Device will continue without CO2/Temperature/Humidity sensor");
            scd40_available = false;
        } else {
            ESP_LOGI(TAG, "SCD40 sensor initialized successfully");
            scd40_available = true;

            // ==============================================================================
            // EARLY EEPROM READ: Load SCD40 configuration before Matter endpoints are created
            // This prevents I2C handle corruption that can occur after heavy memory allocation
            // ==============================================================================
            ESP_LOGI(TAG, "Reading configuration from SCD40 EEPROM (early read)...");

            // Stop periodic measurement to allow EEPROM read
            scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
            vTaskDelay(pdMS_TO_TICKS(500));

            // Try to read altitude from SCD40 EEPROM
            uint16_t eeprom_altitude = 0;
            if (scd40_get_sensor_altitude(&eeprom_altitude) == ESP_OK) {
                loaded_altitude_meters = eeprom_altitude;
                ESP_LOGI(TAG, "✓ Altitude from SCD40 EEPROM: %um", loaded_altitude_meters);
                nvs_save_altitude(loaded_altitude_meters);
            } else {
                // Fallback to NVS
                uint16_t nvs_altitude = 0;
                if (nvs_load_altitude(&nvs_altitude) == ESP_OK) {
                    loaded_altitude_meters = nvs_altitude;
                    ESP_LOGI(TAG, "Altitude from NVS: %um", loaded_altitude_meters);
                } else {
                    loaded_altitude_meters = 0;
                    ESP_LOGI(TAG, "No altitude found, using default: 0m");
                }
            }

            // Try to read temperature offset from SCD40 EEPROM
            float eeprom_temp_offset = 0.0f;
            if (scd40_get_temperature_offset(&eeprom_temp_offset) == ESP_OK) {
                loaded_temp_offset_celsius = eeprom_temp_offset;
                ESP_LOGI(TAG, "✓ Temp offset from SCD40 EEPROM: %.2f°C", loaded_temp_offset_celsius);
                nvs_save_temp_offset(loaded_temp_offset_celsius);
            } else {
                // Fallback to NVS
                float nvs_temp_offset = 4.0f;
                if (nvs_load_temp_offset(&nvs_temp_offset) == ESP_OK) {
                    loaded_temp_offset_celsius = nvs_temp_offset;
                    ESP_LOGI(TAG, "Temp offset from NVS: %.2f°C", loaded_temp_offset_celsius);
                } else {
                    loaded_temp_offset_celsius = 4.0f;
                    ESP_LOGI(TAG, "No temp offset found, using default: 4.0°C");
                }
            }

            // Apply configuration to SCD40 immediately
            ESP_LOGI(TAG, "Applying configuration to SCD40...");
            scd40_set_temperature_offset(loaded_temp_offset_celsius);
            scd40_set_sensor_altitude(loaded_altitude_meters);

            // Restart periodic measurement
            scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);
            ESP_LOGI(TAG, "SCD40 configuration loaded and applied successfully");
        }

        // Initialize OLED display with shared I2C bus
        oled_config_t oled_cfg = OLED_CONFIG_DEFAULT();
        oled_cfg.i2c_bus = i2c_bus_handle;  // Use the same bus as SCD40
        err = oled_init(&oled_cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "OLED initialization failed - continuing without display");
            oled_available = false;
        } else {
            ESP_LOGI(TAG, "OLED display initialized successfully");
            oled_available = true;
            // Show splash screen
            oled_show_splash();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Configure GPIOs
    gpio_config_t io_conf = {};

    // Configure 2 outputs (GPIO19, GPIO20)
    // Use INPUT_OUTPUT mode to allow reading back the output state for display
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_OUTPUT_0) | (1ULL << GPIO_OUTPUT_1);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Initialize all outputs to OFF (HIGH for active-low logic)
    for (int i = 0; i < 2; i++) {
        gpio_set_level(output_pins[i], 1);
    }

    // Configure 4 inputs (D0-D3)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_INPUT_0) | (1ULL << GPIO_INPUT_1) |
                           (1ULL << GPIO_INPUT_2) | (1ULL << GPIO_INPUT_3);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // Configure Status LED (USER LED on GPIO15)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_STATUS_LED);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(GPIO_STATUS_LED, 0);  // Initially OFF

    ESP_LOGI(TAG, "GPIOs configured:");
    ESP_LOGI(TAG, "  Inputs: GPIO%d, %d, %d, %d",
             GPIO_INPUT_0, GPIO_INPUT_1, GPIO_INPUT_2, GPIO_INPUT_3);
    ESP_LOGI(TAG, "  Outputs: GPIO%d, %d",
             GPIO_OUTPUT_0, GPIO_OUTPUT_1);
    ESP_LOGI(TAG, "  I2C: SDA=GPIO%d, SCL=GPIO%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    ESP_LOGI(TAG, "  Status LED: GPIO%d (Thread role indicator)", GPIO_STATUS_LED);

    // Create Matter node
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return;
    }

    ESP_LOGI(TAG, "Creating Matter endpoints...");

    // Create 4 Input Endpoints (Contact Sensors)
    for (int i = 0; i < 4; i++) {
        contact_sensor::config_t sensor_config;
        // With pull-up: HIGH=open, we report as false (contact/closed)
        sensor_config.boolean_state.state_value = false;  // Initial state: closed (HIGH with pull-up inverted)

        endpoint_t *input_endpoint = contact_sensor::create(node, &sensor_config, ENDPOINT_FLAG_NONE, NULL);
        if (!input_endpoint) {
            ESP_LOGE(TAG, "Failed to create input endpoint %d", i + 1);
            return;
        }

        input_endpoint_ids[i] = endpoint::get_id(input_endpoint);
        ESP_LOGI(TAG, "Input %d (GPIO%d) endpoint created with id %u",
                 i + 1, input_pins[i], input_endpoint_ids[i]);
    }

    // Create 2 Output Endpoints (On/Off Lights)
    for (int i = 0; i < 2; i++) {
        on_off_light::config_t light_config;
        light_config.on_off.on_off = false;  // Initial state: OFF
        light_config.on_off_lighting.start_up_on_off = nullptr;

        endpoint_t *output_endpoint = on_off_light::create(node, &light_config, ENDPOINT_FLAG_NONE, NULL);
        if (!output_endpoint) {
            ESP_LOGE(TAG, "Failed to create output endpoint %d", i + 1);
            return;
        }

        output_endpoint_ids[i] = endpoint::get_id(output_endpoint);
        ESP_LOGI(TAG, "Output %d (GPIO%d) endpoint created with id %u",
                 i + 1, output_pins[i], output_endpoint_ids[i]);
    }

    // Create SCD40 sensor endpoints only if sensor is available
    if (scd40_available) {
        ESP_LOGI(TAG, "Creating SCD40 sensor endpoints (Air Quality with CO2, Temperature, Humidity)...");

        // Create Air Quality Sensor endpoint (device type 0x002C)
        // This endpoint will contain BOTH Air Quality enum AND CO2 PPM measurement
        air_quality_sensor::config_t air_quality_config;
        endpoint_t *air_quality_endpoint = air_quality_sensor::create(node, &air_quality_config, ENDPOINT_FLAG_NONE, NULL);
        if (!air_quality_endpoint) {
            ESP_LOGE(TAG, "Failed to create Air Quality Sensor endpoint");
            return;
        }

        air_quality_endpoint_id = endpoint::get_id(air_quality_endpoint);

        ESP_LOGI(TAG, "Air Quality Sensor endpoint created with id %u", air_quality_endpoint_id);

        // Create Air Quality Instance for programmatic control
        gAirQualityInstance = new chip::app::Clusters::AirQuality::Instance(
            air_quality_endpoint_id,
            chip::BitMask<chip::app::Clusters::AirQuality::Feature>(
                chip::app::Clusters::AirQuality::Feature::kFair,
                chip::app::Clusters::AirQuality::Feature::kModerate,
                chip::app::Clusters::AirQuality::Feature::kVeryPoor,
                chip::app::Clusters::AirQuality::Feature::kExtremelyPoor
            )
        );

        if (gAirQualityInstance) {
            CHIP_ERROR err = gAirQualityInstance->Init();
            if (err != CHIP_NO_ERROR) {
                ESP_LOGE(TAG, "Failed to initialize Air Quality Instance");
                delete gAirQualityInstance;
                gAirQualityInstance = nullptr;
            }
            // Note: Air Quality will be updated by the sensor task once readings start
        }

        // Create SEPARATE CO2 endpoint using Air Quality Sensor as base
        // Uses Carbon Dioxide Concentration Measurement cluster (0x040D) - Matter standard for CO2 sensors
        // NOTE: OpenHAB 5.1 doesn't support this cluster yet (will show NULL)
        //       OpenHAB 5.2+ will support it (PR #19897 merged Dec 27, 2025)
        //       Air Quality enum (endpoint 7) works on all versions as fallback
        air_quality_sensor::config_t co2_base_config;
        endpoint_t *co2_endpoint = air_quality_sensor::create(node, &co2_base_config, ENDPOINT_FLAG_NONE, NULL);
        if (!co2_endpoint) {
            ESP_LOGE(TAG, "Failed to create CO2 base endpoint");
            return;
        }

        // Add Carbon Dioxide Concentration Measurement cluster (0x040D)
        cluster::concentration_measurement::config_t co2_config;
        co2_config.measurement_medium = 0; // Air
        co2_config.feature_flags = cluster::concentration_measurement::feature::numeric_measurement::get_id();
        co2_config.features.numeric_measurement.measured_value = 400.0f;
        co2_config.features.numeric_measurement.min_measured_value = 400.0f;
        co2_config.features.numeric_measurement.max_measured_value = 5000.0f;
        co2_config.features.numeric_measurement.measurement_unit = 0; // PPM

        cluster_t *co2_cluster = cluster::carbon_dioxide_concentration_measurement::create(
            co2_endpoint, &co2_config, CLUSTER_FLAG_SERVER);
        if (!co2_cluster) {
            ESP_LOGE(TAG, "Failed to create CO2 concentration cluster");
            return;
        }

        co2_endpoint_id = endpoint::get_id(co2_endpoint);
        ESP_LOGI(TAG, "CO2 endpoint created with id %u (cluster 0x040D - requires OpenHAB 5.2+)", co2_endpoint_id);

        // Create Temperature Sensor Endpoint
        temperature_sensor::config_t temp_config;
        temp_config.temperature_measurement.measured_value = 2000; // 20.00°C initial
        temp_config.temperature_measurement.min_measured_value = -4000; // -40.00°C
        temp_config.temperature_measurement.max_measured_value = 12500; // 125.00°C

        endpoint_t *temp_endpoint = temperature_sensor::create(node, &temp_config, ENDPOINT_FLAG_NONE, NULL);
        if (!temp_endpoint) {
            ESP_LOGE(TAG, "Failed to create temperature sensor endpoint");
            return;
        }

        temperature_endpoint_id = endpoint::get_id(temp_endpoint);
        ESP_LOGI(TAG, "Temperature sensor endpoint created with id %u", temperature_endpoint_id);

        // Create Humidity Sensor Endpoint
        humidity_sensor::config_t hum_config;
        hum_config.relative_humidity_measurement.measured_value = 5000; // 50.00%RH initial
        hum_config.relative_humidity_measurement.min_measured_value = (uint16_t)0; // 0.00%RH
        hum_config.relative_humidity_measurement.max_measured_value = (uint16_t)10000; // 100.00%RH

        endpoint_t *hum_endpoint = humidity_sensor::create(node, &hum_config, ENDPOINT_FLAG_NONE, NULL);
        if (!hum_endpoint) {
            ESP_LOGE(TAG, "Failed to create humidity sensor endpoint");
            return;
        }

        humidity_endpoint_id = endpoint::get_id(hum_endpoint);
        ESP_LOGI(TAG, "Humidity sensor endpoint created with id %u", humidity_endpoint_id);

        // ==================================================================
        // Create SCD40 Control Endpoints (Virtual switches for OpenHAB)
        // ==================================================================
        ESP_LOGI(TAG, "Creating SCD40 control endpoints for OpenHAB...");

        // 1. Trigger Forced Calibration (On/Off Light - momentary switch)
        on_off_light::config_t calibrate_config;
        calibrate_config.on_off.on_off = false;
        calibrate_config.on_off_lighting.start_up_on_off = nullptr;

        endpoint_t *calibrate_endpoint = on_off_light::create(node, &calibrate_config, ENDPOINT_FLAG_NONE, NULL);
        if (calibrate_endpoint) {
            scd40_calibrate_endpoint_id = endpoint::get_id(calibrate_endpoint);
            ESP_LOGI(TAG, "SCD40 Calibrate switch created with id %u", scd40_calibrate_endpoint_id);
        }

        // 2. ASC Enable/Disable (On/Off Light - persistent switch)
        on_off_light::config_t asc_config;
        asc_config.on_off.on_off = true;  // ASC enabled by default
        asc_config.on_off_lighting.start_up_on_off = nullptr;

        endpoint_t *asc_endpoint = on_off_light::create(node, &asc_config, ENDPOINT_FLAG_NONE, NULL);
        if (asc_endpoint) {
            scd40_asc_enable_endpoint_id = endpoint::get_id(asc_endpoint);
            ESP_LOGI(TAG, "SCD40 ASC Enable switch created with id %u", scd40_asc_enable_endpoint_id);
        }

        // 3. Low Power Mode (On/Off Light - persistent switch)
        on_off_light::config_t low_power_config;
        low_power_config.on_off.on_off = false;  // Normal mode by default
        low_power_config.on_off_lighting.start_up_on_off = nullptr;

        endpoint_t *low_power_endpoint = on_off_light::create(node, &low_power_config, ENDPOINT_FLAG_NONE, NULL);
        if (low_power_endpoint) {
            scd40_low_power_endpoint_id = endpoint::get_id(low_power_endpoint);
            ESP_LOGI(TAG, "SCD40 Low Power switch created with id %u", scd40_low_power_endpoint_id);
        }

        // 4. Trigger Persist Settings (On/Off Light - momentary switch)
        on_off_light::config_t persist_config;
        persist_config.on_off.on_off = false;
        persist_config.on_off_lighting.start_up_on_off = nullptr;

        endpoint_t *persist_endpoint = on_off_light::create(node, &persist_config, ENDPOINT_FLAG_NONE, NULL);
        if (persist_endpoint) {
            scd40_persist_endpoint_id = endpoint::get_id(persist_endpoint);
            ESP_LOGI(TAG, "SCD40 Persist Settings switch created with id %u", scd40_persist_endpoint_id);
        }

        // 5. Trigger Self Test (On/Off Light - momentary switch)
        on_off_light::config_t self_test_config;
        self_test_config.on_off.on_off = false;
        self_test_config.on_off_lighting.start_up_on_off = nullptr;

        endpoint_t *self_test_endpoint = on_off_light::create(node, &self_test_config, ENDPOINT_FLAG_NONE, NULL);
        if (self_test_endpoint) {
            scd40_self_test_endpoint_id = endpoint::get_id(self_test_endpoint);
            ESP_LOGI(TAG, "SCD40 Self Test switch created with id %u", scd40_self_test_endpoint_id);
        }

        // ==============================================================================
        // Create Altitude and Temperature Offset control endpoints
        // NOTE: SCD40 configuration was already loaded and applied early in init,
        // using globals loaded_altitude_meters and loaded_temp_offset_celsius
        // ==============================================================================

        // 6. Set Altitude (Dimmable Light with Level Control - 0-5000 meters mapped to 1-255)
        // Formula: level = (altitude * 254 / 5000) + 1, with rounding
        uint8_t altitude_level = (uint8_t)(((loaded_altitude_meters * 254) + 2500) / 5000) + 1;
        dimmable_light::config_t altitude_config;
        altitude_config.on_off.on_off = true;
        altitude_config.on_off_lighting.start_up_on_off = nullptr;
        altitude_config.level_control.current_level = altitude_level;

        endpoint_t *altitude_endpoint = dimmable_light::create(node, &altitude_config, ENDPOINT_FLAG_NONE, NULL);
        if (altitude_endpoint) {
            scd40_altitude_endpoint_id = endpoint::get_id(altitude_endpoint);
            ESP_LOGI(TAG, "SCD40 Altitude control created with id %u (initial: %um, level %u)",
                     scd40_altitude_endpoint_id, loaded_altitude_meters, altitude_level);
        }

        // 7. Set Temperature Offset (Dimmable Light with Level Control - 0-10°C mapped to 1-255)
        // Formula: level = (offset * 254 / 10) + 1, with rounding
        uint8_t temp_offset_level = (uint8_t)(((loaded_temp_offset_celsius * 254.0f) + 5.0f) / 10.0f) + 1;
        dimmable_light::config_t temp_offset_config;
        temp_offset_config.on_off.on_off = true;
        temp_offset_config.on_off_lighting.start_up_on_off = nullptr;
        temp_offset_config.level_control.current_level = temp_offset_level;

        endpoint_t *temp_offset_endpoint = dimmable_light::create(node, &temp_offset_config, ENDPOINT_FLAG_NONE, NULL);
        if (temp_offset_endpoint) {
            scd40_temp_offset_endpoint_id = endpoint::get_id(temp_offset_endpoint);
            ESP_LOGI(TAG, "SCD40 Temp Offset control created with id %u (initial: %.2f°C, level %u)",
                     scd40_temp_offset_endpoint_id, loaded_temp_offset_celsius, temp_offset_level);
        }

        ESP_LOGI(TAG, "All SCD40 control endpoints created successfully");

    } else {
        ESP_LOGW(TAG, "SCD40 sensor not available - skipping sensor endpoints creation");
    }

    ESP_LOGI(TAG, "All Matter endpoints created successfully");

    // Setup reset button handler
    app_reset_button_register(app_reset_to_factory);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    // Set OpenThread platform config
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    // Start Matter
    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter start failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Matter started successfully");

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    // Print Thread network status
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "   THREAD NETWORK STATUS");
    ESP_LOGI(TAG, "====================================");

    auto & threadStackMgr = chip::DeviceLayer::ThreadStackMgr();
    bool isThreadProvisioned = threadStackMgr.IsThreadProvisioned();
    bool isThreadEnabled = threadStackMgr.IsThreadEnabled();
    bool isThreadAttached = threadStackMgr.IsThreadAttached();

    ESP_LOGI(TAG, "Thread Provisioned: %s", isThreadProvisioned ? "YES" : "NO");
    ESP_LOGI(TAG, "Thread Enabled: %s", isThreadEnabled ? "YES" : "NO");
    ESP_LOGI(TAG, "Thread Attached: %s", isThreadAttached ? "YES" : "NO");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "");
#endif

    // Print commissioning information (QR code and manual pairing code)
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "   COMMISSIONING INFORMATION");
    ESP_LOGI(TAG, "====================================");
    PrintOnboardingCodes(chip::RendezvousInformationFlag::kBLE);
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "");

    // Show commissioning info on OLED
    if (oled_available) {
        oled_show_commissioning_info(
            CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE,
            CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR
        );
    }

    // Start GPIO input monitoring task
    xTaskCreate(gpio_input_task, "gpio_input", 4096, NULL, 5, NULL);

    // Start SCD40 sensor reading task
    xTaskCreate(scd40_sensor_task, "scd40_sensor", 4096, NULL, 5, NULL);

    // Start SCD40 configuration sync task (delayed 10s to ensure Matter connection)
    if (scd40_available) {
        xTaskCreate(scd40_config_sync_task, "scd40_sync", 4096, NULL, 5, NULL);
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    // Start Thread status LED monitoring task
    xTaskCreate(thread_status_led_task, "thread_led", 4096, NULL, 5, NULL);
#endif

    // Mark initialization as complete - now allow Matter attribute updates
    // This prevents the SDK from overwriting EEPROM values during startup
    device_initialization_complete = true;
    ESP_LOGI(TAG, "Device initialization complete - ready for operation");

    // Initialize SNTP for time synchronization (local server only)
    ESP_LOGI(TAG, "Starting SNTP with local server: %s", CONFIG_LOCAL_NTP_SERVER_IPV6);
    sntp_init_local();

    // Start SNTP monitoring task for automatic retry
    xTaskCreate(sntp_monitor_task, "sntp_monitor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "ESP32C6 Matter Device Started");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  - 4 Input Sensors: GPIO 0,1,2,21");
    ESP_LOGI(TAG, "  - 2 Output Controls: GPIO 19,20");
    ESP_LOGI(TAG, "  - SCD40 Sensor: I2C (SDA=GPIO22, SCL=GPIO23)");
    ESP_LOGI(TAG, "    * CO2 (ppm)");
    ESP_LOGI(TAG, "    * Temperature (°C)");
    ESP_LOGI(TAG, "    * Humidity (%%RH)");
    ESP_LOGI(TAG, "  - Status LED: GPIO 15 (Thread role indicator)");
    ESP_LOGI(TAG, "  - Protocol: Matter over Thread");
    ESP_LOGI(TAG, "  - NTP Server: %s (local only)", CONFIG_LOCAL_NTP_SERVER_IPV6);
    ESP_LOGI(TAG, "  - Timezone: Europe/Rome (CET/CEST)");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "");
}
