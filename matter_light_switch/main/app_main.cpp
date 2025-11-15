/*
 * ESP32C6 XIAO Matter over Thread Light Switch
 * Based on ESP-Matter examples
 */

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

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

static const char *TAG = "app_main";

// GPIO Definitions for XIAO ESP32C6
#define GPIO_OUTPUT_PIN GPIO_NUM_0  // D0 on XIAO
#define GPIO_INPUT_PIN  GPIO_NUM_1   // D1 on XIAO

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static uint16_t light_endpoint_id = 0;

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
        if (endpoint_id == light_endpoint_id) {
            if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
                gpio_set_level(GPIO_OUTPUT_PIN, val->val.b ? 1 : 0);
                ESP_LOGI(TAG, "GPIO%d set to %s", GPIO_OUTPUT_PIN, val->val.b ? "ON" : "OFF");
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

// GPIO button task
static void gpio_button_task(void *arg)
{
    bool last_state = true;
    bool current_state;
    
    while (1) {
        current_state = gpio_get_level(GPIO_INPUT_PIN);
        
        if (current_state != last_state && !current_state) {
            // Button pressed (active low)
            ESP_LOGI(TAG, "Button pressed on GPIO%d", GPIO_INPUT_PIN);
            
            // Get current on/off state
            node_t *node = node::get();
            endpoint_t *endpoint = endpoint::get(node, light_endpoint_id);
            cluster_t *cluster = cluster::get(endpoint, OnOff::Id);
            attribute_t *attribute = attribute::get(cluster, OnOff::Attributes::OnOff::Id);
            
            esp_matter_attr_val_t val;
            attribute::get_val(attribute, &val);
            
            // Toggle state
            val.val.b = !val.val.b;
            attribute::update(light_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
        }
        
        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(50));
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

    // Configure GPIOs
    gpio_config_t io_conf = {};
    
    // Configure output
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_OUTPUT_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    
    // Configure input
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_INPUT_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "GPIOs configured: Output=%d, Input=%d", GPIO_OUTPUT_PIN, GPIO_INPUT_PIN);

    // Create Matter node
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return;
    }

    // Create on_off_light endpoint
    on_off_light::config_t light_config;
    light_config.on_off.on_off = false;
    light_config.on_off_lighting.start_up_on_off = nullptr;

    endpoint_t *endpoint = on_off_light::create(node, &light_config, ENDPOINT_FLAG_NONE, NULL);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create endpoint");
        return;
    }

    light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Light endpoint created with id %u", light_endpoint_id);

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

    // Start GPIO button monitoring task
    xTaskCreate(gpio_button_task, "gpio_button", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "ESP32C6 XIAO Matter device started successfully");
}
