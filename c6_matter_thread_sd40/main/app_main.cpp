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
#include <driver/i2c.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include <common/Esp32ThreadInit.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <esp_openthread.h>
#endif

#include <setup_payload/OnboardingCodesUtil.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/ESP32Config.h>

static const char *TAG = "app_main";

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

// Endpoint IDs for 2 outputs
static uint16_t output_endpoint_ids[2] = {0, 0};

// Endpoint IDs for 4 inputs
static uint16_t input_endpoint_ids[4] = {0, 0, 0, 0};

// Endpoint IDs for SCD40 sensor
static uint16_t air_quality_endpoint_id = 0;  // Air Quality enum (calculated from CO2)
static uint16_t co2_endpoint_id = 0;           // CO2 concentration in PPM
static uint16_t temperature_endpoint_id = 0;
static uint16_t humidity_endpoint_id = 0;

// GPIO pins arrays
static const gpio_num_t input_pins[4] = {GPIO_INPUT_0, GPIO_INPUT_1, GPIO_INPUT_2, GPIO_INPUT_3};
static const gpio_num_t output_pins[2] = {GPIO_OUTPUT_0, GPIO_OUTPUT_1};

// SCD40 sensor data
static uint16_t scd40_co2 = 0;        // CO2 in ppm
static float scd40_temperature = 0.0;  // Temperature in °C
static float scd40_humidity = 0.0;     // Humidity in %RH
static bool scd40_available = false;  // Flag to track if sensor is available

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
#define SCD40_CMD_START_PERIODIC_MEASUREMENT  0x21b1
#define SCD40_CMD_READ_MEASUREMENT            0xec05
#define SCD40_CMD_STOP_PERIODIC_MEASUREMENT   0x3f86
#define SCD40_CMD_GET_SERIAL_NUMBER           0x3682

// Initialize I2C master
static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = I2C_MASTER_FREQ_HZ,
        },
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C initialized: SDA=GPIO%d, SCL=GPIO%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    return ESP_OK;
}

// Send command to SCD40
static esp_err_t scd40_send_command(uint16_t command)
{
    uint8_t cmd_buf[2];
    cmd_buf[0] = (command >> 8) & 0xFF;
    cmd_buf[1] = command & 0xFF;

    esp_err_t err = i2c_master_write_to_device(I2C_MASTER_NUM, SCD40_I2C_ADDR,
                                                cmd_buf, 2, pdMS_TO_TICKS(1000));
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
    uint8_t data[9];  // 3 words of 2 bytes + 1 CRC byte each

    // Send read command
    esp_err_t err = scd40_send_command(SCD40_CMD_READ_MEASUREMENT);
    if (err != ESP_OK) {
        return err;
    }

    // Wait for measurement to be ready (1ms should be enough)
    vTaskDelay(pdMS_TO_TICKS(1));

    // Read 9 bytes (3x [2 data bytes + 1 CRC])
    err = i2c_master_read_from_device(I2C_MASTER_NUM, SCD40_I2C_ADDR,
                                       data, 9, pdMS_TO_TICKS(1000));
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

// Event callback
static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
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
                    gpio_set_level(output_pins[i], val->val.b ? 1 : 0);
                    ESP_LOGI(TAG, "Output %d (GPIO%d) set to %s", i + 1, output_pins[i],
                             val->val.b ? "ON" : "OFF");
                    break;
                }
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
                // Invert logic: HIGH (pull-up open) = false (closed), LOW (contact) = true (open)
                inverted_state = !current_state;

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
                        val.val.b = inverted_state;  // Inverted: LOW = true (open), HIGH = false (closed)
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

            // Update CO2 concentration attribute (PPM value)
            if (co2_endpoint_id != 0) {
                val.type = ESP_MATTER_VAL_TYPE_NULLABLE_FLOAT;
                val.val.f = (float)co2;
                esp_err_t err_co2 = attribute::update(co2_endpoint_id,
                                 CarbonDioxideConcentrationMeasurement::Id,
                                 CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id,
                                 &val);
                if (err_co2 != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to update CO2 attribute: %s", esp_err_to_name(err_co2));
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
        } else {
            ESP_LOGW(TAG, "Failed to read SCD40 measurement");
        }

        // SCD40 provides new data every 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
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
        // Initialize SCD40 sensor only if I2C works
        err = scd40_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SCD40 sensor initialization failed - sensor disabled");
            ESP_LOGW(TAG, "Device will continue without CO2/Temperature/Humidity sensor");
            scd40_available = false;
        } else {
            ESP_LOGI(TAG, "SCD40 sensor initialized successfully");
            scd40_available = true;
        }
    }

    // Configure GPIOs
    gpio_config_t io_conf = {};

    // Configure 2 outputs (GPIO19, GPIO20)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_OUTPUT_0) | (1ULL << GPIO_OUTPUT_1);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Initialize all outputs to LOW
    for (int i = 0; i < 2; i++) {
        gpio_set_level(output_pins[i], 0);
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
        co2_endpoint_id = air_quality_endpoint_id;  // Use same endpoint for both Air Quality and CO2

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

        // Add CO2 Concentration Measurement cluster to the same Air Quality endpoint
        cluster::concentration_measurement::config_t co2_config;
        co2_config.measurement_medium = 0; // Air
        co2_config.feature_flags = cluster::concentration_measurement::feature::numeric_measurement::get_id();
        co2_config.features.numeric_measurement.measured_value = 0.0f;
        co2_config.features.numeric_measurement.min_measured_value = 400.0f;
        co2_config.features.numeric_measurement.max_measured_value = 5000.0f;
        co2_config.features.numeric_measurement.measurement_unit = 0; // PPM

        cluster_t *co2_cluster = cluster::carbon_dioxide_concentration_measurement::create(
            air_quality_endpoint, &co2_config, CLUSTER_FLAG_SERVER);
        if (!co2_cluster) {
            ESP_LOGE(TAG, "Failed to create CO2 concentration cluster");
            return;
        }

        ESP_LOGI(TAG, "CO2 Concentration cluster added to Air Quality endpoint (both on endpoint %u)", air_quality_endpoint_id);

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

    // Start GPIO input monitoring task
    xTaskCreate(gpio_input_task, "gpio_input", 4096, NULL, 5, NULL);

    // Start SCD40 sensor reading task
    xTaskCreate(scd40_sensor_task, "scd40_sensor", 4096, NULL, 5, NULL);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    // Start Thread status LED monitoring task
    xTaskCreate(thread_status_led_task, "thread_led", 4096, NULL, 5, NULL);
#endif

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
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "");
}
