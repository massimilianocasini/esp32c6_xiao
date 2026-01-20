/*
 * app_main.cpp
 *
 * ESP32-C6 Matter/Thread Air Quality Sensor with OLED Display
 *
 * This application creates a Matter air quality sensor device that:
 * - Reads CO2, temperature, and humidity from an SCD40 sensor
 * - Displays readings on an SSD1306 128x64 OLED display
 * - Communicates over Thread network using Matter protocol
 * - Shows Thread network status and node count on display
 *
 * Hardware:
 * - Seeed Studio XIAO ESP32-C6
 * - Sensirion SCD40 CO2/Temp/Humidity sensor (I2C)
 * - SSD1306 128x64 OLED display (I2C)
 *
 * I2C Bus Configuration:
 * - SDA: GPIO22
 * - SCL: GPIO23
 * - SCD40 address: 0x62
 * - OLED address: 0x3C
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

// ESP-IDF includes
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// OpenThread includes
#include <esp_openthread.h>
#include <esp_openthread_types.h>
#include <openthread/thread.h>
#include <openthread/instance.h>
#include <openthread/dataset.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include <common/Esp32ThreadInit.h>
#endif

// ESP-Matter includes
#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_cluster.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_attribute.h>
#include <esp_matter_console.h>

// Application includes
#include "app_priv.h"
#include "app_reset.h"
#include "oled_display.h"

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::cluster;
using namespace esp_matter::endpoint;

// ============================================================================
// Configuration
// ============================================================================

// I2C Configuration
#define I2C_MASTER_SDA_PIN      CONFIG_I2C_MASTER_SDA_PIN
#define I2C_MASTER_SCL_PIN      CONFIG_I2C_MASTER_SCL_PIN
#define I2C_MASTER_FREQ_HZ      CONFIG_I2C_MASTER_FREQ_HZ

// SCD40 Configuration
#define SCD40_I2C_ADDRESS       CONFIG_SCD40_I2C_ADDRESS
#define SENSOR_READ_INTERVAL_MS CONFIG_SENSOR_READ_INTERVAL_MS

// Display Configuration
#define DISPLAY_UPDATE_MS       CONFIG_DISPLAY_UPDATE_INTERVAL_MS

// ============================================================================
// SCD40 Commands
// ============================================================================

#define SCD40_CMD_START_PERIODIC        0x21B1
#define SCD40_CMD_STOP_PERIODIC         0x3F86
#define SCD40_CMD_READ_MEASUREMENT      0xEC05
#define SCD40_CMD_GET_DATA_READY        0xE4B8
#define SCD40_CMD_REINIT                0x3646
#define SCD40_CMD_GET_SERIAL            0x3682
#define SCD40_CMD_PERFORM_SELF_TEST     0x3639
#define SCD40_CMD_FACTORY_RESET         0x3632

// ============================================================================
// Global State
// ============================================================================

// Sensor data - shared between tasks
volatile scd40_data_t g_sensor_data = {0};

// Thread network info - updated by event handler
volatile thread_info_t g_thread_info = {
    .status = THREAD_STATUS_DETACHED,
    .router_count = 0,
    .child_count = 0,
    .rloc16 = 0,
    .network_name = "",
    .channel = 0,
    .rssi = 0,
};

// Matter endpoint ID for air quality sensor
uint16_t g_air_quality_endpoint_id = 0;

// I2C handles
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_scd40_dev = NULL;

// Synchronization
static SemaphoreHandle_t s_sensor_mutex = NULL;
static SemaphoreHandle_t s_thread_mutex = NULL;

// Task handles
static TaskHandle_t s_sensor_task = NULL;
static TaskHandle_t s_display_task = NULL;

// ============================================================================
// CRC-8 for SCD40 communication
// ============================================================================

static uint8_t scd40_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// ============================================================================
// SCD40 I2C Communication
// ============================================================================

static esp_err_t scd40_write_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_master_transmit(s_scd40_dev, buf, sizeof(buf), 100);
}

static esp_err_t scd40_read_data(uint8_t *data, size_t len)
{
    return i2c_master_receive(s_scd40_dev, data, len, 100);
}

// ============================================================================
// I2C Initialization
// ============================================================================

esp_err_t app_i2c_init(i2c_master_bus_handle_t *bus_handle)
{
    ESP_LOGI(TAG, "Initializing I2C bus");
    ESP_LOGI(TAG, "  SDA: GPIO%d, SCL: GPIO%d, Freq: %d Hz",
             I2C_MASTER_SDA_PIN, I2C_MASTER_SCL_PIN, I2C_MASTER_FREQ_HZ);

    // Configure I2C master bus
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)I2C_MASTER_SDA_PIN,
        .scl_io_num = (gpio_num_t)I2C_MASTER_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add SCD40 device
    i2c_device_config_t scd40_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SCD40_I2C_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &scd40_cfg, &s_scd40_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SCD40 device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Return bus handle if requested
    if (bus_handle) {
        *bus_handle = s_i2c_bus;
    }

    ESP_LOGI(TAG, "I2C bus initialized successfully");
    return ESP_OK;
}

// ============================================================================
// SCD40 Sensor Functions
// ============================================================================

esp_err_t app_scd40_init(void)
{
    ESP_LOGI(TAG, "Initializing SCD40 sensor at address 0x%02X", SCD40_I2C_ADDRESS);

    // Stop any ongoing measurement
    esp_err_t ret = scd40_write_cmd(SCD40_CMD_STOP_PERIODIC);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Stop periodic measurement failed (may be OK): %s",
                 esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Reinitialize sensor
    ret = scd40_write_cmd(SCD40_CMD_REINIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(30));

    // Read serial number to verify communication
    ret = scd40_write_cmd(SCD40_CMD_GET_SERIAL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get serial failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t serial_buf[9];
    ret = scd40_read_data(serial_buf, sizeof(serial_buf));
    if (ret == ESP_OK) {
        uint64_t serial = 0;
        for (int i = 0; i < 3; i++) {
            uint8_t *word = &serial_buf[i * 3];
            if (scd40_crc8(word, 2) == word[2]) {
                serial = (serial << 16) | (word[0] << 8) | word[1];
            }
        }
        ESP_LOGI(TAG, "SCD40 serial number: %012llX", serial);
    }

    // Start periodic measurement
    ret = scd40_write_cmd(SCD40_CMD_START_PERIODIC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start periodic measurement failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SCD40 initialized - periodic measurement started");
    return ESP_OK;
}

esp_err_t app_scd40_read(scd40_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if data is ready
    esp_err_t ret = scd40_write_cmd(SCD40_CMD_GET_DATA_READY);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t ready_buf[3];
    ret = scd40_read_data(ready_buf, sizeof(ready_buf));
    if (ret != ESP_OK) {
        return ret;
    }

    // Check CRC
    if (scd40_crc8(ready_buf, 2) != ready_buf[2]) {
        ESP_LOGW(TAG, "Data ready CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    // Check if data is ready (lower 11 bits != 0)
    uint16_t ready_status = (ready_buf[0] << 8) | ready_buf[1];
    if ((ready_status & 0x07FF) == 0) {
        // Data not ready yet
        data->data_ready = false;
        return ESP_OK;
    }

    // Read measurement
    ret = scd40_write_cmd(SCD40_CMD_READ_MEASUREMENT);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t meas_buf[9];  // 3 values x 3 bytes (2 data + 1 CRC)
    ret = scd40_read_data(meas_buf, sizeof(meas_buf));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read measurement failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify CRCs and extract data
    bool crc_ok = true;

    // CO2 (word 0)
    if (scd40_crc8(&meas_buf[0], 2) == meas_buf[2]) {
        data->co2_ppm = (meas_buf[0] << 8) | meas_buf[1];
    } else {
        ESP_LOGW(TAG, "CO2 CRC mismatch");
        crc_ok = false;
    }

    // Temperature (word 1)
    if (scd40_crc8(&meas_buf[3], 2) == meas_buf[5]) {
        uint16_t temp_raw = (meas_buf[3] << 8) | meas_buf[4];
        data->temperature_c = -45.0f + 175.0f * ((float)temp_raw / 65536.0f);
    } else {
        ESP_LOGW(TAG, "Temperature CRC mismatch");
        crc_ok = false;
    }

    // Humidity (word 2)
    if (scd40_crc8(&meas_buf[6], 2) == meas_buf[8]) {
        uint16_t hum_raw = (meas_buf[6] << 8) | meas_buf[7];
        data->humidity_percent = 100.0f * ((float)hum_raw / 65536.0f);
    } else {
        ESP_LOGW(TAG, "Humidity CRC mismatch");
        crc_ok = false;
    }

    data->data_ready = crc_ok;
    data->timestamp_ms = esp_timer_get_time() / 1000;

    if (data->data_ready) {
        ESP_LOGI(TAG, "SCD40: CO2=%u ppm, Temp=%.1f C, RH=%.1f %%",
                 data->co2_ppm, data->temperature_c, data->humidity_percent);
    }

    return ESP_OK;
}

// ============================================================================
// Thread Network Status
// ============================================================================

esp_err_t app_thread_get_info(thread_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    otInstance *ot_instance = esp_openthread_get_instance();
    if (!ot_instance) {
        info->status = THREAD_STATUS_DISABLED;
        return ESP_OK;
    }

    // Get device role
    otDeviceRole role = otThreadGetDeviceRole(ot_instance);
    switch (role) {
        case OT_DEVICE_ROLE_DISABLED:
            info->status = THREAD_STATUS_DISABLED;
            break;
        case OT_DEVICE_ROLE_DETACHED:
            info->status = THREAD_STATUS_DETACHED;
            break;
        case OT_DEVICE_ROLE_CHILD:
            info->status = THREAD_STATUS_CHILD;
            break;
        case OT_DEVICE_ROLE_ROUTER:
            info->status = THREAD_STATUS_ROUTER;
            break;
        case OT_DEVICE_ROLE_LEADER:
            info->status = THREAD_STATUS_LEADER;
            break;
        default:
            info->status = THREAD_STATUS_DETACHED;
    }

    // Get RLOC16
    info->rloc16 = otThreadGetRloc16(ot_instance);

    // Get network name
    const char *network_name = otThreadGetNetworkName(ot_instance);
    if (network_name) {
        strncpy(info->network_name, network_name, sizeof(info->network_name) - 1);
        info->network_name[sizeof(info->network_name) - 1] = '\0';
    }

    // Get channel
    info->channel = otLinkGetChannel(ot_instance);

    // Get router/child count from leader data if available
    // Note: otThreadGetRouterInfo not available in this OpenThread version
    // otRouterInfo router_info;
    info->router_count = 0;
    // for (uint8_t i = 0; i <= 62; i++) {  // Router IDs 0-62
    //     if (otThreadGetRouterInfo(ot_instance, i, &router_info) == OT_ERROR_NONE) {
    //         info->router_count++;
    //     }
    // }

    return ESP_OK;
}

uint8_t app_thread_get_node_count(void)
{
    thread_info_t info;
    if (app_thread_get_info(&info) != ESP_OK) {
        return 0;
    }

    // Return router count + 1 for self (simplified node count)
    if (info.status >= THREAD_STATUS_CHILD) {
        return info.router_count + 1;
    }
    return 0;
}

// ============================================================================
// Sensor Reading Task
// ============================================================================

static void sensor_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor task started");

    scd40_data_t local_data;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        // Read sensor
        esp_err_t ret = app_scd40_read(&local_data);

        if (ret == ESP_OK && local_data.data_ready) {
            // Update global data with mutex protection
            if (xSemaphoreTake(s_sensor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_sensor_data.co2_ppm = local_data.co2_ppm;
                g_sensor_data.temperature_c = local_data.temperature_c;
                g_sensor_data.humidity_percent = local_data.humidity_percent;
                g_sensor_data.data_ready = true;
                g_sensor_data.timestamp_ms = local_data.timestamp_ms;
                xSemaphoreGive(s_sensor_mutex);
            }

            // Update Matter attributes
            app_matter_update_attributes(g_air_quality_endpoint_id, &local_data);
        }

        // Wait for next reading interval
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

// ============================================================================
// Display Update Task
// ============================================================================

static void display_task(void *arg)
{
    ESP_LOGI(TAG, "Display task started");

    scd40_data_t sensor_snapshot;
    thread_info_t thread_snapshot;
    TickType_t last_wake_time = xTaskGetTickCount();

    // Wait a bit for initial sensor reading
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Display task entering main loop");

    while (1) {
        // Get sensor data snapshot
        if (xSemaphoreTake(s_sensor_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            memcpy(&sensor_snapshot, (const void *)&g_sensor_data, sizeof(sensor_snapshot));
            xSemaphoreGive(s_sensor_mutex);
            ESP_LOGI(TAG, "Display: Got sensor data, data_ready=%d, CO2=%u",
                     sensor_snapshot.data_ready, sensor_snapshot.co2_ppm);
        } else {
            ESP_LOGW(TAG, "Display: Failed to get sensor mutex");
        }

        // Get Thread status
        app_thread_get_info(&thread_snapshot);

        // Determine connection status
        bool thread_connected = (thread_snapshot.status >= THREAD_STATUS_CHILD);
        uint8_t node_count = thread_connected ? (thread_snapshot.router_count + 1) : 0;

        // Update display
        if (sensor_snapshot.data_ready) {
            ESP_LOGI(TAG, "Display: Updating display with sensor data");
            oled_update_sensor_display(
                sensor_snapshot.co2_ppm,
                sensor_snapshot.temperature_c,
                sensor_snapshot.humidity_percent,
                thread_connected,
                node_count
            );
            ESP_LOGI(TAG, "Display: Update complete");
        } else {
            // Show waiting message if no sensor data yet
            ESP_LOGI(TAG, "Display: Showing waiting message");
            oled_clear();
            oled_draw_string_at(0, 0, "CO2: ---");
            oled_draw_string_at(0, 16, thread_connected ? "Thread: Connected" : "Thread: Disconnected");
            oled_printf_at(0, 24, "Nodes: %u", node_count);
            oled_draw_string_at(0, 40, "Waiting for");
            oled_draw_string_at(0, 48, "sensor data...");
            oled_refresh();
        }

        // Wait for next update interval
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(DISPLAY_UPDATE_MS));
    }
}

// ============================================================================
// Matter Attribute Update
// ============================================================================

esp_err_t app_matter_update_attributes(uint16_t endpoint_id, const scd40_data_t *data)
{
    if (!data || !data->data_ready || endpoint_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    esp_matter_attr_val_t val;

    // Update CO2 concentration (Carbon Dioxide Concentration Measurement cluster)
    // Cluster ID: 0x040D, Attribute: MeasuredValue (0x0000)
    // Value is in ppm, stored as float
    val.type = ESP_MATTER_VAL_TYPE_NULLABLE_FLOAT;
    val.val.f = (float)data->co2_ppm;
    ret = attribute::update(endpoint_id, 0x040D, 0x0000, &val);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update CO2 attribute: %s", esp_err_to_name(ret));
    }

    // Update temperature (Temperature Measurement cluster)
    // Cluster ID: 0x0402, Attribute: MeasuredValue (0x0000)
    // Value is in 0.01 degrees Celsius
    val.type = ESP_MATTER_VAL_TYPE_NULLABLE_INT16;
    val.val.i16 = (int16_t)(data->temperature_c * 100);
    ret = attribute::update(endpoint_id, 0x0402, 0x0000, &val);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update temperature attribute: %s", esp_err_to_name(ret));
    }

    // Update humidity (Relative Humidity Measurement cluster)
    // Cluster ID: 0x0405, Attribute: MeasuredValue (0x0000)
    // Value is in 0.01 percent
    val.type = ESP_MATTER_VAL_TYPE_NULLABLE_UINT16;
    val.val.u16 = (uint16_t)(data->humidity_percent * 100);
    ret = attribute::update(endpoint_id, 0x0405, 0x0000, &val);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update humidity attribute: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

// ============================================================================
// Matter Callbacks
// ============================================================================

/**
 * @brief Matter attribute update callback
 */
static esp_err_t matter_attribute_update_cb(
    attribute::callback_type_t type,
    uint16_t endpoint_id,
    uint32_t cluster_id,
    uint32_t attribute_id,
    esp_matter_attr_val_t *val,
    void *priv_data)
{
    ESP_LOGI(TAG, "Attribute update: endpoint=%u, cluster=0x%04lX, attr=0x%04lX, type=%d",
             endpoint_id, (unsigned long)cluster_id, (unsigned long)attribute_id, type);

    // Handle write callbacks if needed
    // For sensor device, most attributes are read-only

    return ESP_OK;
}

/**
 * @brief Matter identification callback
 */
static esp_err_t matter_identification_cb(
    identification::callback_type_t type,
    uint16_t endpoint_id,
    uint8_t effect_id,
    uint8_t effect_variant,
    void *priv_data)
{
    ESP_LOGI(TAG, "Identification: endpoint=%u, effect=%u, variant=%u",
             endpoint_id, effect_id, effect_variant);

    // Flash the display or LED for identification
    if (type == identification::callback_type_t::START) {
        oled_invert(true);
        ESP_LOGI(TAG, "Identification started");
    } else if (type == identification::callback_type_t::STOP) {
        oled_invert(false);
        ESP_LOGI(TAG, "Identification stopped");
    }

    return ESP_OK;
}

/**
 * @brief Matter event callback
 */
static void matter_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    if (event == nullptr) {
        return;
    }

    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
            ESP_LOGI(TAG, "Commissioning complete!");
            oled_clear();
            oled_draw_string_at(0, 24, "Commissioned!");
            oled_refresh();
            vTaskDelay(pdMS_TO_TICKS(2000));
            break;

        case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
            ESP_LOGW(TAG, "Commissioning failed - failsafe timer expired");
            break;

        case chip::DeviceLayer::DeviceEventType::kThreadStateChange:
            ESP_LOGI(TAG, "Thread state changed");
            break;

        case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
            ESP_LOGI(TAG, "Thread connectivity changed");
            break;

        default:
            break;
    }
}

// ============================================================================
// Matter Device Setup
// ============================================================================

static esp_err_t create_matter_device(void)
{
    ESP_LOGI(TAG, "Creating Matter air quality sensor device");

    // Create node
    node::config_t node_cfg;
    node_t *node = node::create(&node_cfg, matter_attribute_update_cb,
                                 matter_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    // Create air quality sensor endpoint
    // Using a combination of clusters for environmental sensor
    air_quality_sensor::config_t sensor_cfg;
    endpoint_t *sensor_ep = air_quality_sensor::create(node, &sensor_cfg,
                                                        ENDPOINT_FLAG_NONE, NULL);
    if (!sensor_ep) {
        ESP_LOGE(TAG, "Failed to create air quality sensor endpoint");
        return ESP_FAIL;
    }

    g_air_quality_endpoint_id = endpoint::get_id(sensor_ep);
    ESP_LOGI(TAG, "Air quality sensor endpoint ID: %u", g_air_quality_endpoint_id);

    // Add Temperature Measurement cluster (0x0402)
    temperature_measurement::config_t temp_cfg;
    temp_cfg.measured_value = nullable<int16_t>(2500);  // 25.00 C
    temp_cfg.min_measured_value = nullable<int16_t>(-4000);  // -40 C
    temp_cfg.max_measured_value = nullable<int16_t>(8500);   // 85 C
    cluster_t *temp_cluster = temperature_measurement::create(sensor_ep, &temp_cfg,
                                                               CLUSTER_FLAG_SERVER);
    if (!temp_cluster) {
        ESP_LOGW(TAG, "Failed to create temperature cluster");
    }

    // Add Relative Humidity Measurement cluster (0x0405)
    relative_humidity_measurement::config_t hum_cfg;
    hum_cfg.measured_value = nullable<uint16_t>(5000);  // 50.00%
    hum_cfg.min_measured_value = nullable<uint16_t>(0);
    hum_cfg.max_measured_value = nullable<uint16_t>(10000);  // 100%
    cluster_t *hum_cluster = relative_humidity_measurement::create(sensor_ep, &hum_cfg,
                                                                    CLUSTER_FLAG_SERVER);
    if (!hum_cluster) {
        ESP_LOGW(TAG, "Failed to create humidity cluster");
    }

    // Add Carbon Dioxide Concentration Measurement cluster (0x040D)
    cluster::concentration_measurement::config_t co2_cfg;
    co2_cfg.measurement_medium = 0; // Air
    co2_cfg.feature_flags = cluster::concentration_measurement::feature::numeric_measurement::get_id();
    co2_cfg.features.numeric_measurement.measured_value = 400.0f;
    co2_cfg.features.numeric_measurement.min_measured_value = 0.0f;
    co2_cfg.features.numeric_measurement.max_measured_value = 5000.0f;
    co2_cfg.features.numeric_measurement.measurement_unit = 0; // PPM
    cluster_t *co2_cluster = cluster::carbon_dioxide_concentration_measurement::create(
        sensor_ep, &co2_cfg, CLUSTER_FLAG_SERVER);
    if (!co2_cluster) {
        ESP_LOGW(TAG, "Failed to create CO2 cluster");
    }

    ESP_LOGI(TAG, "Matter device created successfully");
    return ESP_OK;
}

// ============================================================================
// Main Application Entry Point
// ============================================================================

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  SCD40 Air Quality Sensor with OLED Display  ");
    ESP_LOGI(TAG, "  Matter/Thread - ESP32-C6 XIAO               ");
    ESP_LOGI(TAG, "==============================================");

    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create mutexes
    s_sensor_mutex = xSemaphoreCreateMutex();
    s_thread_mutex = xSemaphoreCreateMutex();
    if (!s_sensor_mutex || !s_thread_mutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return;
    }

    // Initialize I2C bus (shared between SCD40 and OLED)
    i2c_master_bus_handle_t i2c_bus = NULL;
    ret = app_i2c_init(&i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed");
        return;
    }

    // Initialize OLED display with shared I2C bus
    oled_config_t oled_cfg = OLED_CONFIG_DEFAULT();
    oled_cfg.i2c_bus = i2c_bus;  // Use the same bus as SCD40
    ret = oled_init(&oled_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OLED initialization failed - continuing without display");
        // Continue without display - sensor still works
    }

    // Initialize SCD40 sensor
    ret = app_scd40_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SCD40 initialization failed");
        oled_clear();
        oled_draw_string_at(0, 16, "SCD40 Error!");
        oled_draw_string_at(0, 32, "Check wiring");
        oled_refresh();
        // Continue anyway - Matter still works, just no sensor data
    }

    // Setup reset button handler
    app_reset_button_register(app_reset_to_factory);

    // Create Matter device BEFORE starting Matter
    ret = create_matter_device();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Matter device creation failed");
        return;
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    // Set OpenThread platform config BEFORE starting Matter
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
    ESP_LOGI(TAG, "OpenThread platform config set");
#endif

    // Start Matter (after creating endpoints and setting Thread config)
    ESP_LOGI(TAG, "Starting ESP-Matter");

    ret = esp_matter::start(matter_event_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-Matter start failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Matter started successfully");

#if CONFIG_ENABLE_CHIP_SHELL
    // Initialize Matter console for debugging
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::init();
#endif

    // Show commissioning info on display
    ESP_LOGI(TAG, "Showing commissioning info on display");
    ret = oled_show_commissioning_info(
        CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE,
        CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR
    );
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to show commissioning info: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Commissioning info displayed successfully");
    }

    // Start sensor reading task
    ESP_LOGI(TAG, "Creating sensor task");
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, &s_sensor_task);

    // Start display update task
    ESP_LOGI(TAG, "Creating display task");
    xTaskCreate(display_task, "display_task", 4096, NULL, 4, &s_display_task);

    ESP_LOGI(TAG, "Application started successfully");
    ESP_LOGI(TAG, "Commissioning code: %lu", (unsigned long)CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE);
    ESP_LOGI(TAG, "Discriminator: 0x%04X", CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR);
}
