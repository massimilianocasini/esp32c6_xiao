#include "app_reset.h"
#include "app_priv.h"
#include <esp_log.h>
#include <esp_matter.h>
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
