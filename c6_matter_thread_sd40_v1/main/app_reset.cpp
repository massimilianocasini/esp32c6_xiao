#include "app_reset.h"
#include "app_priv.h"
#include <esp_log.h>
#include <esp_matter.h>
#include <nvs_flash.h>
#include <esp_partition.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "app_reset";

// GPIO per il pulsante di reset (usa il pulsante BOOT su XIAO ESP32C6)
#define RESET_BUTTON_GPIO GPIO_NUM_9

static app_reset_callback_t reset_callback = NULL;

void app_reset_to_factory(void)
{
    ESP_LOGI(TAG, "Performing factory reset");

    // Erase SCD40 configuration from NVS before Matter factory reset
    ESP_LOGI(TAG, "Erasing SCD40 configuration from NVS...");
    esp_err_t err = nvs_erase_scd40_config();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to erase SCD40 NVS config (may not exist): %s", esp_err_to_name(err));
    }

    // Perform SCD40 sensor factory reset to erase EEPROM
    ESP_LOGI(TAG, "Performing SCD40 sensor factory reset (erasing EEPROM)...");

    // Stop sensor before factory reset
    #define SCD40_CMD_STOP_PERIODIC_MEASUREMENT 0x3F86
    scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));

    err = scd40_perform_factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset SCD40 sensor: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SCD40 sensor factory reset completed (EEPROM erased)");
    }

    // Erase OpenThread storage partition (this is where Matter saves persistent attributes!)
    ESP_LOGI(TAG, "Erasing OpenThread storage partition...");
    const esp_partition_t *ot_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x3a, "ot_storage");
    if (ot_partition != NULL) {
        err = esp_partition_erase_range(ot_partition, 0, ot_partition->size);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OpenThread storage partition erased successfully");
        } else {
            ESP_LOGE(TAG, "Failed to erase OpenThread storage: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "OpenThread storage partition not found");
    }

    // Erase factory NVS partition (may contain Matter persistent data)
    ESP_LOGI(TAG, "Erasing factory NVS partition...");
    const esp_partition_t *fctry_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "fctry");
    if (fctry_partition != NULL) {
        err = esp_partition_erase_range(fctry_partition, 0, fctry_partition->size);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Factory NVS partition erased successfully");
        } else {
            ESP_LOGE(TAG, "Failed to erase factory NVS: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "Factory NVS partition not found");
    }

    // Erase ALL NVS to remove other persistent data
    ESP_LOGI(TAG, "Erasing ALL NVS flash (complete factory reset)...");
    err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS flash: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "NVS flash erased successfully");
    }

    // Perform Matter factory reset (erases chip-* namespaces) and reboot
    esp_matter::factory_reset();
}

static void reset_button_task(void *pvParameter)
{
    int button_pressed_time = 0;
    bool button_state = false;
    bool last_button_state = false;
    
    while (1) {
        button_state = (gpio_get_level(RESET_BUTTON_GPIO) == 0); // Active low
        
        if (button_state && !last_button_state) {
            // Button just pressed
            button_pressed_time = 0;
            ESP_LOGI(TAG, "Reset button pressed, hold for 5 seconds to factory reset");
        } else if (button_state && last_button_state) {
            // Button still pressed
            button_pressed_time += 100;
            
            if (button_pressed_time >= 5000) { // 5 seconds
                ESP_LOGI(TAG, "Factory reset initiated");
                if (reset_callback) {
                    reset_callback();
                }
                button_pressed_time = 0;
            }
        } else if (!button_state && last_button_state) {
            // Button released
            if (button_pressed_time > 0 && button_pressed_time < 5000) {
                ESP_LOGI(TAG, "Button released too early (%.1f seconds)", 
                         button_pressed_time / 1000.0);
            }
            button_pressed_time = 0;
        }
        
        last_button_state = button_state;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_reset_button_register(app_reset_callback_t callback)
{
    reset_callback = callback;
    
    // Configure GPIO for button
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << RESET_BUTTON_GPIO);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    // Create monitoring task
    xTaskCreate(reset_button_task, "reset_button_task", 2048, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Reset button registered on GPIO%d", RESET_BUTTON_GPIO);
}
