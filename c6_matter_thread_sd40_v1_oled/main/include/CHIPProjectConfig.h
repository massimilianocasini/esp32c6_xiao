/*
 * CHIPProjectConfig.h
 *
 * Matter/CHIP project configuration for ESP32-C6 SCD40 Air Quality Sensor with OLED
 *
 * This file configures Matter-specific options for the air quality sensor device.
 */

#ifndef CHIP_PROJECT_CONFIG_H
#define CHIP_PROJECT_CONFIG_H

// Device Information - Define BEFORE including base config
#ifndef CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "VicinoDiCasaDigitale"
#endif

#ifndef CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "SCD40 Air Quality Sensor OLED"
#endif

// Include base ESP32 CHIP configuration - REQUIRED for Thread support
#include <esp32/CHIPProjectConfig.h>

#endif // CHIP_PROJECT_CONFIG_H
