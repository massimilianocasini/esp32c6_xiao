/*
 * Custom CHIP Project Configuration
 * This file overrides default CHIP configuration values
 */

#ifndef CHIP_PROJECT_CONFIG_H
#define CHIP_PROJECT_CONFIG_H

// Device Information - Define BEFORE including base config
#ifndef CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "VicinoDiCasaDigitale"
#endif

#ifndef CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "Matter Thread 6in/6out"
#endif

// Include base ESP32 CHIP configuration
#include <esp32/CHIPProjectConfig.h>

#endif // CHIP_PROJECT_CONFIG_H
