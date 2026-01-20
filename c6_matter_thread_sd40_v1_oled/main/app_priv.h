/*
 * app_priv.h
 *
 * Private application header for Matter SCD40 Air Quality Sensor with OLED
 *
 * Contains function declarations and global state definitions used
 * throughout the application.
 */

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <stdint.h>
#include <driver/i2c_master.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Sensor Data Structure
// ============================================================================

/**
 * @brief Structure to hold SCD40 sensor readings
 */
typedef struct {
    uint16_t co2_ppm;           ///< CO2 concentration in parts per million
    float temperature_c;         ///< Temperature in degrees Celsius
    float humidity_percent;      ///< Relative humidity in percent
    bool data_ready;            ///< True if valid data is available
    int64_t timestamp_ms;       ///< Timestamp of last reading
} scd40_data_t;

// ============================================================================
// Thread Network Status
// ============================================================================

/**
 * @brief Thread network connection status
 */
typedef enum {
    THREAD_STATUS_DETACHED = 0,     ///< Not connected to any network
    THREAD_STATUS_CHILD,            ///< Connected as child device
    THREAD_STATUS_ROUTER,           ///< Connected as router (FTD only)
    THREAD_STATUS_LEADER,           ///< Network leader (FTD only)
    THREAD_STATUS_DISABLED,         ///< Thread stack disabled
} thread_status_t;

/**
 * @brief Thread network information structure
 */
typedef struct {
    thread_status_t status;         ///< Current connection status
    uint8_t router_count;           ///< Number of routers in network
    uint8_t child_count;            ///< Number of children (if router)
    uint16_t rloc16;                ///< RLOC16 address
    char network_name[17];          ///< Network name (max 16 chars + null)
    uint8_t channel;                ///< Current channel
    int8_t rssi;                    ///< Signal strength
} thread_info_t;

// ============================================================================
// Application Functions
// ============================================================================

/**
 * @brief Initialize the I2C bus for SCD40 and OLED
 * @param[out] bus_handle Pointer to store the created bus handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_i2c_init(i2c_master_bus_handle_t *bus_handle);

/**
 * @brief Initialize the SCD40 CO2 sensor
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_scd40_init(void);

/**
 * @brief Read current sensor values from SCD40
 * @param[out] data Pointer to structure to receive sensor data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_scd40_read(scd40_data_t *data);

/**
 * @brief Get current Thread network information
 * @param[out] info Pointer to structure to receive network info
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_thread_get_info(thread_info_t *info);

/**
 * @brief Get total number of nodes visible in Thread network
 * @return Number of nodes (routers + children)
 */
uint8_t app_thread_get_node_count(void);

/**
 * @brief Update Matter attributes with latest sensor readings
 * @param[in] endpoint_id Matter endpoint ID for air quality sensor
 * @param[in] data Current sensor readings
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_matter_update_attributes(uint16_t endpoint_id, const scd40_data_t *data);

/**
 * @brief Application factory reset handler
 *
 * Called when factory reset is triggered. Erases all configuration
 * and restarts the device.
 */
void app_reset_factory(void);

/**
 * @brief Start the factory reset countdown
 *
 * Call this when the reset button is long-pressed.
 */
void app_reset_start(void);

/**
 * @brief Cancel any pending factory reset
 */
void app_reset_cancel(void);

// ============================================================================
// External Variables (defined in app_main.cpp)
// ============================================================================

/**
 * @brief Global sensor data - updated by sensor task, read by display/Matter
 */
extern volatile scd40_data_t g_sensor_data;

/**
 * @brief Global Thread info - updated by network event handler
 */
extern volatile thread_info_t g_thread_info;

/**
 * @brief Matter air quality sensor endpoint ID
 */
extern uint16_t g_air_quality_endpoint_id;

#ifdef __cplusplus
}
#endif

// ============================================================================
// OpenThread Configuration Macros (C++ linkage - outside extern "C")
// ============================================================================

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"

#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }

// Forward declaration for OpenThread platform config function (C++ linkage)
esp_err_t set_openthread_platform_config(esp_openthread_platform_config_t * config);

#endif
