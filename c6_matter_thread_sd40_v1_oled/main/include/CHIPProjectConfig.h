/*
 * CHIPProjectConfig.h
 *
 * Matter/CHIP project configuration for ESP32-C6 SCD40 Air Quality Sensor
 *
 * This file configures Matter-specific options for the air quality sensor device.
 */

#pragma once

// Device identification
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID 0xFFF1          // Test vendor ID
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID 0x8001         // Air quality sensor product
#define CHIP_DEVICE_CONFIG_DEVICE_HARDWARE_VERSION 1
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "1.0"
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING "1.0.0"
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION 1

// Device product name shown during commissioning
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "SCD40 Air Quality Sensor"

// Transport protocol configuration (Thread, WiFi, BLE) is managed by platform config
// These are automatically configured based on target and build settings

// Thread device type: Sleepy End Device for power efficiency
// Note: Thread configuration is enabled via sdkconfig and platform defaults
#define CHIP_DEVICE_CONFIG_THREAD_DEVICE_TYPE 2  // MTD Sleepy End Device

// Commissioning flow settings
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR 0xF00
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE 20202021

// Enable diagnostic logs
#define CHIP_CONFIG_LOG_LEVEL 4  // Detail level

// Memory optimization for ESP32-C6
#define CHIP_CONFIG_IM_STATUS_CODE_VERBOSE_FORMAT 0
