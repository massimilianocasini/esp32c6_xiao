#pragma once

#include <esp_err.h>

// Callback per il reset del dispositivo
void app_reset_to_factory(void);

// Funzione per registrare il pulsante di reset
typedef void (*app_reset_callback_t)(void);
void app_reset_button_register(app_reset_callback_t callback);
