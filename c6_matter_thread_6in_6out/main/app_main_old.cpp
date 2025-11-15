/*
 * ESP32C6 XIAO Matter over Thread Device
 * GPIO0: Output digitale ON/OFF (controllabile via Matter)
 * GPIO1: Input digitale (per controllo locale)
 */

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_attribute_utils.h>
#include <esp_matter_feature.h>
#include <esp_matter_endpoint.h>

#include <app_priv.h>
#include <app_reset.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <app/server/OnboardingCodesUtil.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <platform/ESP32/ESP32DeviceInfoProvider.h>

static const char *TAG = "app_main";

// GPIO Definitions per XIAO ESP32C6
#define GPIO_OUTPUT_PIN GPIO_NUM_0  // D0 su XIAO
#define GPIO_INPUT_PIN  GPIO_NUM_1   // D1 su XIAO
#define GPIO_INPUT_PIN_SEL (1ULL << GPIO_INPUT_PIN)

// Variabili globali
static uint16_t light_endpoint_id = 0;
static bool gpio_state = false;
static TaskHandle_t gpio_task_handle = NULL;

// Configurazione GPIO
static void configure_gpio(void)
{
    // Configura GPIO0 come output
    gpio_config_t io_conf_output = {};
    io_conf_output.intr_type = GPIO_INTR_DISABLE;
    io_conf_output.mode = GPIO_MODE_OUTPUT;
    io_conf_output.pin_bit_mask = (1ULL << GPIO_OUTPUT_PIN);
    io_conf_output.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf_output.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf_output);

    // Configura GPIO1 come input con pull-up
    gpio_config_t io_conf_input = {};
    io_conf_input.intr_type = GPIO_INTR_ANYEDGE;
    io_conf_input.mode = GPIO_MODE_INPUT;
    io_conf_input.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf_input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf_input.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf_input);

    // Installa il servizio ISR per GPIO
    gpio_install_isr_service(0);

    ESP_LOGI(TAG, "GPIO configurati: Output su GPIO%d, Input su GPIO%d", 
             GPIO_OUTPUT_PIN, GPIO_INPUT_PIN);
}

// Funzione per impostare lo stato del GPIO output
static void set_gpio_state(bool state)
{
    gpio_state = state;
    gpio_set_level(GPIO_OUTPUT_PIN, state ? 1 : 0);
    ESP_LOGI(TAG, "GPIO%d stato: %s", GPIO_OUTPUT_PIN, state ? "ON" : "OFF");
}

// Task per monitorare l'input GPIO
static void gpio_input_task(void *pvParameter)
{
    static bool last_state = true;
    bool current_state;
    
    while (1) {
        current_state = gpio_get_level(GPIO_INPUT_PIN);
        
        if (current_state != last_state) {
            last_state = current_state;
            
            // Quando il pulsante viene premuto (transizione da HIGH a LOW)
            if (!current_state) {
                ESP_LOGI(TAG, "Pulsante premuto su GPIO%d", GPIO_INPUT_PIN);
                
                // Toggle dello stato
                bool new_state = !gpio_state;
                set_gpio_state(new_state);
                
                // Aggiorna l'attributo Matter
                node_t *node = node::get();
                endpoint_t *endpoint = endpoint::get(node, light_endpoint_id);
                cluster_t *cluster = cluster::get(endpoint, OnOff::Id);
                attribute_t *attribute = attribute::get(cluster, OnOff::Attributes::OnOff::Id);
                
                esp_matter_attr_val_t val = esp_matter_bool(new_state);
                attribute::update(light_endpoint_id, OnOff::Id, 
                                OnOff::Attributes::OnOff::Id, &val);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // Debounce delay
    }
}

// Callback per gli attributi Matter
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                         uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val,
                                         void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        // Callback prima dell'aggiornamento
    } else if (type == POST_UPDATE) {
        // Callback dopo l'aggiornamento
        if (endpoint_id == light_endpoint_id) {
            if (cluster_id == OnOff::Id) {
                if (attribute_id == OnOff::Attributes::OnOff::Id) {
                    ESP_LOGI(TAG, "Ricevuto comando Matter OnOff: %d", val->val.b);
                    set_gpio_state(val->val.b);
                }
            }
        }
    }

    return err;
}

// Callback per identificazione del dispositivo
static esp_err_t app_identification_cb(identification::callback_type_t type,
                                       uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identificazione: tipo=%u, endpoint=%u, effetto=%u, variante=%u", 
             type, endpoint_id, effect_id, effect_variant);
    return ESP_OK;
}

// Stampa i codici di commissioning
static void print_commissioning_codes()
{
    char manualPairingCode[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase45RepresentationLength + 1];
    chip::MutableCharSpan manualPairingCodeSpan(manualPairingCode);
    
    char qrCode[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase45RepresentationLength + 1];
    chip::MutableCharSpan qrCodeSpan(qrCode);
    
    if (chip::GetManualPairingCode(manualPairingCodeSpan) == CHIP_NO_ERROR &&
        chip::GetQRCode(qrCodeSpan) == CHIP_NO_ERROR) {
        
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║                  COMMISSIONING CODES MATTER                 ║");
        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════╣");
        ESP_LOGI(TAG, "║ Manual Pairing Code:                                        ║");
        ESP_LOGI(TAG, "║ %s                                           ║", manualPairingCode);
        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════╣");
        ESP_LOGI(TAG, "║ QR Code:                                                    ║");
        ESP_LOGI(TAG, "║ %s ║", qrCode);
        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════╣");
        ESP_LOGI(TAG, "║ Usa questi codici per aggiungere il dispositivo a Matter    ║");
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    }
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    // Inizializza il dispositivo
    ESP_LOGI(TAG, "Inizializzazione ESP32C6 XIAO Matter Device");
    
    // Configurazione reset
    app_reset_button_register(app_reset_to_factory);

    // Configurazione GPIO
    configure_gpio();
    set_gpio_state(false); // Stato iniziale OFF

    // Configurazione Matter Node
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Errore creazione nodo Matter");
        return;
    }

    // Creazione endpoint on_off_light
    on_off_light::config_t light_config;
    light_config.on_off.on_off = false;
    light_config.on_off.lighting.start_up_on_off = nullptr;
    
    endpoint_t *endpoint = on_off_light::create(node, &light_config, 
                                                ENDPOINT_FLAG_NONE, NULL);
    if (!endpoint) {
        ESP_LOGE(TAG, "Errore creazione endpoint");
        return;
    }

    // Ottieni l'ID dell'endpoint
    light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Endpoint light creato con ID: %u", light_endpoint_id);

    // Configurazione Thread per ESP32C6
    esp_openthread_platform_config_t thread_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    // Set Thread configuration per ESP32C6
    set_openthread_platform_config(&thread_config);

    // Avvia il Matter
    err = esp_matter::start(node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore avvio Matter: %d", err);
        return;
    }

    // Stampa i codici di commissioning
    print_commissioning_codes();

#if CONFIG_ENABLE_ESP_INSIGHTS_TRACE
    esp_insights_config_t config = {
        .log_type = ESP_DIAG_LOG_TYPE_ERROR | ESP_DIAG_LOG_TYPE_WARNING | ESP_DIAG_LOG_TYPE_EVENT,
        .auth_key = ESP_INSIGHTS_AUTH_KEY,
    };
    
    esp_insights_init(&config);
    enable_esp32_factory_data_provider();
    enable_esp32_device_info_provider();
    
    esp32_tracing_init();
#endif

    // Setup console Matter
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::init();

    // OTA requestor Matter
#if CONFIG_ENABLE_OTA_REQUESTOR
    esp_matter_ota_requestor_init();
#endif

    // Avvia il task di monitoraggio GPIO input
    xTaskCreate(gpio_input_task, "gpio_input_task", 2048, NULL, 10, &gpio_task_handle);

    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         ESP32C6 XIAO Matter Device AVVIATO                  ║");
    ESP_LOGI(TAG, "║         Thread Network: ATTIVO                              ║");
    ESP_LOGI(TAG, "║         GPIO0: Output (Controllabile via Matter)            ║");
    ESP_LOGI(TAG, "║         GPIO1: Input (Controllo locale)                     ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
}
