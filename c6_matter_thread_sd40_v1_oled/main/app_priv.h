#pragma once

#include <esp_err.h>
#include <stdint.h>

// Callback per il reset del dispositivo
void app_reset_to_factory(void);

// Funzione per registrare il pulsante di reset
typedef void (*app_reset_callback_t)(void);
void app_reset_button_register(app_reset_callback_t callback);

// Erase SCD40 configuration from NVS (called during factory reset)
esp_err_t nvs_erase_scd40_config(void);

// SCD40 sensor functions (called during factory reset)
esp_err_t scd40_send_command(uint16_t cmd);
esp_err_t scd40_perform_factory_reset(void);
