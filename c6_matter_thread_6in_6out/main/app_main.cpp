/*
 * ESP32C6 Matter over Thread - 4 Inputs + 4 Outputs
 * Based on ESP-Matter examples
 *
 * GPIO Configuration:
 * - Inputs: GPIO 0, 1, 2, 21 (Contact Sensors - Independent)
 * - Outputs: GPIO 22, 23, 19, 20 (On/Off Lights - Independent)
 */

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <string.h>

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_attribute_utils.h>

#include <app_priv.h>
#include <app_reset.h>
#include <driver/gpio.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include <common/Esp32ThreadInit.h>
#endif

#include <setup_payload/OnboardingCodesUtil.h>
#include <platform/CHIPDeviceLayer.h>
#include <lib/support/logging/CHIPLogging.h>

static const char *TAG = "app_main";

// GPIO Definitions for ESP32C6
// 4 Inputs
#define GPIO_INPUT_0  GPIO_NUM_0   // Input 1
#define GPIO_INPUT_1  GPIO_NUM_1   // Input 2
#define GPIO_INPUT_2  GPIO_NUM_2   // Input 3
#define GPIO_INPUT_3  GPIO_NUM_21  // Input 4

// 4 Outputs
#define GPIO_OUTPUT_0 GPIO_NUM_22  // Output 1
#define GPIO_OUTPUT_1 GPIO_NUM_23  // Output 2
#define GPIO_OUTPUT_2 GPIO_NUM_19  // Output 3
#define GPIO_OUTPUT_3 GPIO_NUM_20  // Output 4

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// Endpoint IDs for 4 outputs
static uint16_t output_endpoint_ids[4] = {0, 0, 0, 0};

// Endpoint IDs for 4 inputs
static uint16_t input_endpoint_ids[4] = {0, 0, 0, 0};

// GPIO pins arrays
static const gpio_num_t input_pins[4] = {GPIO_INPUT_0, GPIO_INPUT_1, GPIO_INPUT_2, GPIO_INPUT_3};
static const gpio_num_t output_pins[4] = {GPIO_OUTPUT_0, GPIO_OUTPUT_1, GPIO_OUTPUT_2, GPIO_OUTPUT_3};

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
            for (int i = 0; i < 4; i++) {
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

    while (1) {
        // Monitor all 4 inputs
        for (int i = 0; i < 4; i++) {
            current_state = gpio_get_level(input_pins[i]);

            // Detect state change
            if (current_state != last_states[i]) {
                ESP_LOGI(TAG, "Input %d (GPIO%d) changed to %s", i + 1, input_pins[i],
                         current_state ? "HIGH (Open)" : "LOW (Closed)");

                // Update Matter Boolean State attribute
                node_t *node = node::get();
                endpoint_t *endpoint = endpoint::get(node, input_endpoint_ids[i]);

                // Use BooleanState cluster to report contact sensor state
                // StateValue: false = contact (closed), true = no contact (open)
                cluster_t *cluster = cluster::get(endpoint, BooleanState::Id);
                if (cluster) {
                    attribute_t *attribute = attribute::get(cluster, BooleanState::Attributes::StateValue::Id);
                    if (attribute) {
                        esp_matter_attr_val_t val;
                        val.type = ESP_MATTER_VAL_TYPE_BOOLEAN;
                        val.val.b = current_state;  // HIGH = open, LOW = closed
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

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Disable verbose CHIP logs
    // You can disable specific modules: ExchangeManager, DataManagement, etc.
    chip::Logging::SetLogFilter(chip::Logging::kLogCategory_Error);

    // Configure GPIOs
    gpio_config_t io_conf = {};

    // Configure 4 outputs (D4-D7)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_OUTPUT_0) | (1ULL << GPIO_OUTPUT_1) |
                           (1ULL << GPIO_OUTPUT_2) | (1ULL << GPIO_OUTPUT_3);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Initialize all outputs to LOW
    for (int i = 0; i < 4; i++) {
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

    ESP_LOGI(TAG, "GPIOs configured:");
    ESP_LOGI(TAG, "  Inputs: GPIO%d, %d, %d, %d",
             GPIO_INPUT_0, GPIO_INPUT_1, GPIO_INPUT_2, GPIO_INPUT_3);
    ESP_LOGI(TAG, "  Outputs: GPIO%d, %d, %d, %d",
             GPIO_OUTPUT_0, GPIO_OUTPUT_1, GPIO_OUTPUT_2, GPIO_OUTPUT_3);

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
        sensor_config.boolean_state.state_value = true;  // Initial state: open (HIGH with pull-up)

        endpoint_t *input_endpoint = contact_sensor::create(node, &sensor_config, ENDPOINT_FLAG_NONE, NULL);
        if (!input_endpoint) {
            ESP_LOGE(TAG, "Failed to create input endpoint %d", i + 1);
            return;
        }

        input_endpoint_ids[i] = endpoint::get_id(input_endpoint);
        ESP_LOGI(TAG, "Input %d (GPIO%d) endpoint created with id %u",
                 i + 1, input_pins[i], input_endpoint_ids[i]);
    }

    // Create 4 Output Endpoints (On/Off Lights)
    for (int i = 0; i < 4; i++) {
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

    // Update Vendor Name and Product Name AFTER Matter starts
    // These attributes are created by the framework, we just update them
    const char *vendor_name = "VicinoDiCasaDigitale";
    const char *product_name = "Matter Thread 6in/6out";

    esp_matter_attr_val_t vendor_val = esp_matter_char_str((char *)vendor_name, strlen(vendor_name));
    esp_matter_attr_val_t product_val = esp_matter_char_str((char *)product_name, strlen(product_name));

    attribute::update(0, BasicInformation::Id, BasicInformation::Attributes::VendorName::Id, &vendor_val);
    attribute::update(0, BasicInformation::Id, BasicInformation::Attributes::ProductName::Id, &product_val);

    ESP_LOGI(TAG, "Device info updated - Vendor: %s, Product: %s", vendor_name, product_name);

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

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "ESP32C6 Matter Device Started");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  - 4 Input Sensors: GPIO 0,1,2,21");
    ESP_LOGI(TAG, "  - 4 Output Controls: GPIO 22,23,19,20");
    ESP_LOGI(TAG, "  - Protocol: Matter over Thread");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "");
}
