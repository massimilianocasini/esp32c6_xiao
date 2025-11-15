#pragma once

#include <esp_err.h>

// Funzione per resettare il dispositivo ai valori di fabbrica
void app_reset_to_factory(void);

// Funzione per registrare un callback al pulsante di reset  
typedef void (*app_reset_callback_t)(void);
void app_reset_button_register(app_reset_callback_t callback);
