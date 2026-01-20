# ESP32-C6 Matter/Thread Air Quality Sensor with OLED Display

This project implements a Matter-compatible air quality sensor using the ESP32-C6 XIAO development board. It reads CO2, temperature, and humidity data from a Sensirion SCD40 sensor and displays the values on an SSD1306 OLED display while communicating over Thread network.

## Features

- **SCD40 Sensor Integration**: Reads CO2 (0-40,000 ppm), temperature (-40 to +125 C), and humidity (0-100% RH)
- **SSD1306 OLED Display**: 128x64 pixel display showing real-time sensor data and network status
- **Matter Protocol**: Standard Matter air quality sensor device type with:
  - Carbon Dioxide Concentration Measurement cluster (0x040D)
  - Temperature Measurement cluster (0x0402)
  - Relative Humidity Measurement cluster (0x0405)
- **Thread Networking**: IEEE 802.15.4 mesh networking for reliable connectivity
- **OpenHAB Compatible**: Works with OpenHAB Matter binding for home automation integration
- **Factory Reset**: Button-based factory reset functionality

## Hardware Requirements

### Components
- Seeed Studio XIAO ESP32-C6 development board
- Sensirion SCD40 CO2/Temperature/Humidity sensor
- SSD1306 128x64 OLED display (I2C interface)
- Connecting wires

### Pin Configuration

| Signal | GPIO | Description |
|--------|------|-------------|
| I2C SDA | GPIO6 | I2C data line (shared by SCD40 and OLED) |
| I2C SCL | GPIO7 | I2C clock line (shared by SCD40 and OLED) |
| Reset Button | GPIO9 | Factory reset (BOOT button) |
| Status LED | GPIO15 | Thread connection status (optional) |

### I2C Addresses
- SCD40 Sensor: 0x62
- SSD1306 OLED: 0x3C

## Display Layout

The OLED display shows:
```
Line 0: CO2: XXXX ppm
Line 2: Thread: Connected/Disconnected
Line 3: Nodes: X
Line 5: Temp: XX.X C
Line 6: RH: XX.X %
```

## Software Requirements

- ESP-IDF v5.2.1 or later
- ESP-Matter SDK
- OpenThread (included in ESP-IDF)

## Building the Project

### 1. Set Up Environment

```bash
# Source ESP-IDF
source $IDF_PATH/export.sh

# Source ESP-Matter
source $ESP_MATTER_PATH/export.sh
```

### 2. Configure Project

```bash
# Open menuconfig to adjust settings
./build.sh menuconfig
```

Key configuration options:
- `I2C Master SDA GPIO`: Default GPIO6
- `I2C Master SCL GPIO`: Default GPIO7
- `SCD40 I2C Address`: Default 0x62
- `OLED I2C Address`: Default 0x3C
- `Sensor Read Interval`: Default 5000ms
- `Display Update Interval`: Default 1000ms

### 3. Build

```bash
./build.sh build
```

### 4. Flash

```bash
./build.sh flash
# Or specify port:
PORT=/dev/ttyACM0 ./build.sh flash
```

### 5. Monitor

```bash
./build.sh monitor
```

## Commissioning

### Matter Commissioning Code
- **Setup Code**: 20202021
- **Discriminator**: 0xF00

### Using OpenHAB

1. Ensure your OpenHAB instance has the Matter binding installed
2. Add a Thread Border Router to your network (Apple HomePod Mini, Google Nest Hub, etc.)
3. Use the OpenHAB Matter binding to discover and commission the device
4. Enter the setup code when prompted: `20202021`

### Using chip-tool

```bash
# Commission over Thread
chip-tool pairing ble-thread <node-id> hex:<dataset> 20202021 3840

# Read CO2 value
chip-tool carbonDioxideConcentrationMeasurement read measured-value <node-id> 1

# Read temperature
chip-tool temperaturemeasurement read measured-value <node-id> 1

# Read humidity
chip-tool relativehumiditymeasurement read measured-value <node-id> 1
```

## Factory Reset

To perform a factory reset:
1. Press and hold the BOOT button (GPIO9) for 5 seconds
2. The display will show reset progress
3. Device will restart and enter commissioning mode

## Troubleshooting

### OLED Display Not Working
- Check I2C connections (SDA to GPIO6, SCL to GPIO7)
- Verify I2C address (default 0x3C, some displays use 0x3D)
- Check power supply (3.3V for XIAO)

### SCD40 Sensor Not Responding
- Verify I2C connections
- Check sensor power supply (3.3V)
- Allow 30+ seconds for sensor warm-up after power on
- Ensure nothing blocks the sensor's air intake

### Thread Connection Issues
- Verify a Thread Border Router is operational
- Check that Thread credentials are correct
- Ensure device is within radio range
- Try factory reset and recommission

### Matter Commissioning Fails
- Verify setup code and discriminator
- Check Thread Border Router connectivity
- Ensure BLE is enabled during commissioning
- Check for interference from other BLE devices

## Project Structure

```
c6_matter_thread_sd40_v1_oled/
├── CMakeLists.txt              # Main project CMake file
├── README.md                   # This file
├── build.sh                    # Build script
├── flash.sh                    # Flash script
├── partitions.csv              # Partition table
├── sdkconfig.defaults          # Default SDK configuration
├── idf_component.yml           # Project dependencies
├── main/
│   ├── CMakeLists.txt          # Main component CMake
│   ├── Kconfig.projbuild       # Project configuration options
│   ├── app_main.cpp            # Main application
│   ├── app_reset.cpp           # Factory reset handling
│   ├── app_reset.h             # Reset header
│   ├── app_priv.h              # Application private header
│   ├── idf_component.yml       # Main component dependencies
│   └── include/
│       └── CHIPProjectConfig.h # Matter configuration
└── components/
    └── oled_display/           # OLED display component
        ├── CMakeLists.txt
        ├── oled_display.c      # High-level display API
        ├── ssd1306.c           # Low-level SSD1306 driver
        ├── font_8x8.c          # 8x8 pixel font
        └── include/
            ├── oled_display.h  # Display API header
            └── ssd1306.h       # SSD1306 driver header
```

## License

This project is provided as-is for educational and development purposes.

## Acknowledgments

- Espressif Systems for ESP-IDF and ESP-Matter SDK
- Sensirion for SCD40 sensor documentation
- Connectivity Standards Alliance for Matter specification
