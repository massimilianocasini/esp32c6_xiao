#pragma once

#include <esp_err.h>

// Function to reset device to factory defaults
void app_reset_to_factory(void);

// Function to register a callback for reset button
typedef void (*app_reset_callback_t)(void);
void app_reset_button_register(app_reset_callback_t callback);
