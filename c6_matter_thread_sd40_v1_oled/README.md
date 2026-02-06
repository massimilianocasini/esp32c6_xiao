# ESP32-C6 Matter/Thread Multi-Sensor Board with OLED Display

This project implements a Matter-compatible multi-function sensor and I/O board using the Seeed Studio XIAO ESP32-C6 development board. It features 4 contact sensor inputs, 2 controllable outputs, a Sensirion SCD40 CO2/temperature/humidity sensor, and an SSD1306 OLED display, all communicating over Thread network.

## Features

- **4 Contact Sensor Inputs**: Independent GPIO inputs (GPIO 0, 1, 2, 21) exposed as Matter Contact Sensor endpoints
- **2 Controllable Outputs**: Independent GPIO outputs (GPIO 19, 20) exposed as Matter On/Off Light endpoints
- **SCD40 Sensor Integration**: Reads CO2 (0-40,000 ppm), temperature (-40 to +125 C), and humidity (0-100% RH)
- **Air Quality Calculation**: Automatic air quality level (Good/Fair/Moderate/Poor/VeryPoor/ExtremelyPoor) based on CO2 levels
- **SSD1306 OLED Display**: 128x64 pixel display showing real-time sensor data and network status
- **Thread Role LED Indicator**: Status LED shows network role:
  - Solid ON: End Device (no routing)
  - Single blink: Router (routing enabled)
  - Double blink: Leader (network leader)
  - OFF: Disconnected
- **Antenna Control**: Compile-time selectable internal/external antenna (USE_EXTERNAL_ANTENNA define)
- **Matter Protocol**: Comprehensive Matter device with 17 endpoints:
  - Contact Sensor clusters for 4 inputs (endpoints 1-4)
  - On/Off Light clusters for 2 outputs (endpoints 5-6)
  - Air Quality cluster (0x005B) - Calculated from CO2 (endpoint 7)
  - Carbon Dioxide Concentration Measurement cluster (0x040D) - requires OpenHAB 5.2+ (endpoint 8)
  - Temperature Measurement cluster (0x0402) (endpoint 9)
  - Relative Humidity Measurement cluster (0x0405) (endpoint 10)
  - SCD40 Control endpoints (calibration, ASC, low power mode, persist, self-test, altitude, temp offset) (endpoints 11-17)
- **Thread Networking**: IEEE 802.15.4 mesh networking for reliable connectivity
- **OTA Support**: Over-the-air firmware updates enabled
- **OpenHAB Compatible**: Works with OpenHAB Matter binding for home automation integration (CO2 PPM requires OpenHAB 5.2+, Air Quality works on all versions)
- **Wokwi Simulator Support**: Can be simulated in Wokwi
- **Factory Reset**: Button-based factory reset functionality
- **NVS Persistence**: SCD40 configuration (altitude, temperature offset) persisted across reboots
- **I2C Auto-Recovery**: Automatic I2C bus reinitialization when sensor communication fails (hot-plug support)

## Hardware Requirements

### Components
- Seeed Studio XIAO ESP32-C6 development board
- Sensirion SCD40 CO2/Temperature/Humidity sensor
- SSD1306 128x64 OLED display (I2C interface)
- 4x Contact sensors (optional, for input testing)
- 2x LEDs or relays (optional, for output testing)
- Connecting wires

### Pin Configuration

| Signal | GPIO | Description |
|--------|------|-------------|
| **Inputs** | | |
| Input 1 | GPIO0 | Contact sensor input |
| Input 2 | GPIO1 | Contact sensor input |
| Input 3 | GPIO2 | Contact sensor input |
| Input 4 | GPIO21 | Contact sensor input |
| **Outputs** | | |
| Output 1 | GPIO19 | On/Off light output |
| Output 2 | GPIO20 | On/Off light output |
| **I2C Bus** | | |
| I2C SDA | GPIO22 | I2C data line (shared by SCD40 and OLED) |
| I2C SCL | GPIO23 | I2C clock line (shared by SCD40 and OLED) |
| **System** | | |
| Reset Button | GPIO9 | Factory reset (BOOT button) |
| Status LED | GPIO15 | Thread role indicator LED |
| **Antenna Control** | | |
| RF Enable | GPIO3 | RF switch enable (must be LOW) |
| Antenna Select | GPIO14 | LOW=internal ceramic, HIGH=external UFL |

### I2C Addresses
- SCD40 Sensor: 0x62
- SSD1306 OLED: 0x3C

### Air Quality Thresholds (CO2-based)
| CO2 Level | Air Quality |
|-----------|-------------|
| < 800 ppm | Good |
| 800-999 ppm | Fair |
| 1000-1499 ppm | Moderate |
| 1500-1999 ppm | Poor |
| 2000-4999 ppm | Very Poor |
| >= 5000 ppm | Extremely Poor |

## Display Layout

The SSD1306 OLED display (128x64 pixels, 8 lines of 16 characters) shows:

```
+------------------+
| DD/MM/YY  HH:MM  |  Line 0: Date and time (or "Attesa NTP..." if not synced)
|                  |  Line 1: (empty)
| XXXXppm  Quality |  Line 2: CO2 value + Air quality in Italian
|                  |  Line 3: (empty)
|     25.4 C       |  Line 4: Temperature (centered)
|     65.2 %       |  Line 5: Humidity (centered)
|                  |  Line 6: (empty)
| 1234 12 T:C N:XX |  Line 7: I/O status, Thread status, Node count
+------------------+
```

**Line details:**
- **Line 0**: Date/time in format `DD/MM/YY  HH:MM` (synchronized via NTP)
- **Line 2**: CO2 in ppm + Air quality text in Italian:
  - `Ottima` (Good, <800 ppm)
  - `Buona` (Fair, 800-999 ppm)
  - `Moderata` (Moderate, 1000-1499 ppm)
  - `Scarsa` (Poor, 1500-1999 ppm) - **blinks**
  - `Pessima` (Very Poor, 2000-4999 ppm) - **blinks**
  - `Pericolosa` (Extremely Poor, >=5000 ppm) - **blinks**
- **Line 4-5**: Temperature and humidity centered with units
- **Line 7**: Status line:
  - `1234`: Input states (1-4, shows number if active, `-` if inactive)
  - `12`: Output states (1-2, shows number if on, `-` if off)
  - `T:C`/`T:D`: Thread Connected/Disconnected
  - `N:XX`: Number of nodes in Thread network

## Software Requirements

- ESP-IDF v5.2.1 or later
- ESP-Matter SDK
- OpenThread (included in ESP-IDF)

## NTP Time Synchronization

Thread devices cannot directly reach Internet NTP servers because most Thread Border Routers (like Aqara M100) don't provide a default route to the Internet. The device needs a **local NTP server** reachable via IPv6.

### The Problem

Thread networks use IPv6 only. When the Border Router doesn't advertise a default route (`default:0` in network data), devices cannot reach public NTP servers like `pool.ntp.org` or `time.google.com`.

### Solution: Local NTP Server

If you have a Raspberry Pi, NAS, or Home Assistant in your network, configure it as an NTP server.

#### Installing Chrony on Raspberry Pi / Debian / Ubuntu

```bash
# Install chrony
sudo apt update
sudo apt install chrony

# Edit configuration
sudo nano /etc/chrony/chrony.conf
```

Add these lines to allow NTP requests from your local network:

```conf
# Allow NTP requests from local IPv6 networks
allow fd00::/8
allow fddd::/16
allow fe80::/10

# Allow NTP requests from local IPv4 networks (optional)
allow 192.168.0.0/16
allow 10.0.0.0/8
```

Restart chrony:

```bash
sudo systemctl restart chrony
sudo systemctl enable chrony

# Verify chrony is listening on port 123
sudo ss -ulnp | grep chronyd
```

#### Finding Your Server's IPv6 Address

```bash
ip -6 addr show | grep "fd"
```

Look for an address starting with `fd` (e.g., `fddd:9cf8:e546:0:99b0:6203:5f44:6e2d`).

#### Configuring the Firmware

Edit `main/app_main.cpp` and set your NTP server's IPv6 address:

```c
#define CONFIG_LOCAL_NTP_SERVER_IPV6 "fddd:9cf8:e546:0:99b0:6203:5f44:6e2d"
```

Replace with your actual Raspberry Pi/NAS IPv6 address.

### Verifying NTP Sync

After flashing, check the serial logs:

```
SNTP: Using LOCAL NTP server: fddd:9cf8:e546:0:99b0:6203:5f44:6e2d
SNTP: Started, waiting for time sync...
SNTP: Time synchronized! 2026-01-22 00:15:30
```

The OLED display will show the date and time when synchronized, or "Attesa NTP..." while waiting.

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
- `I2C Master SDA GPIO`: Default GPIO22
- `I2C Master SCL GPIO`: Default GPIO23
- `I2C Master Frequency`: Default 100000 Hz (100kHz standard mode)
- `SCD40 I2C Address`: Default 0x62
- `OLED I2C Address`: Default 0x3C
- `Sensor Read Interval`: Default 5000ms (SCD40 measurement cycle)
- `Display Update Interval`: Default 1000ms
- `Reset Button GPIO`: Default GPIO9 (BOOT button)
- `Thread Status LED GPIO`: Default GPIO15 (built-in USER LED)

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

**OpenHAB Version Compatibility:**
- **OpenHAB 5.1 and earlier**: CO2 concentration cluster (0x040D) shows NULL. Use Air Quality endpoint (7) as fallback
- **OpenHAB 5.2+**: Full support for CO2 concentration measurement in PPM (PR #19897 merged Dec 27, 2025)

### Using chip-tool

```bash
# Commission over Thread
chip-tool pairing ble-thread <node-id> hex:<dataset> 20202021 3840

# Read contact sensor states (endpoints 1-4)
chip-tool booleanstate read state-value <node-id> 1
chip-tool booleanstate read state-value <node-id> 2

# Control outputs (endpoints 5-6)
chip-tool onoff toggle <node-id> 5
chip-tool onoff on <node-id> 6

# Read Air Quality (endpoint 7)
chip-tool airquality read air-quality <node-id> 7

# Read CO2 value (endpoint 8)
chip-tool carbondioxideconcentrationmeasurement read measured-value <node-id> 8

# Read temperature (endpoint 9)
chip-tool temperaturemeasurement read measured-value <node-id> 9

# Read humidity (endpoint 10)
chip-tool relativehumiditymeasurement read measured-value <node-id> 10

# SCD40 Control examples
chip-tool onoff on <node-id> 11          # Trigger forced calibration
chip-tool onoff toggle <node-id> 12      # Toggle ASC
chip-tool levelcontrol move-to-level 100 0 0 0 <node-id> 16  # Set altitude to 1000m
```

## Matter Endpoints

The device exposes the following Matter endpoints (created in this order):

| Endpoint | Type | Description |
|----------|------|-------------|
| 1-4 | Contact Sensors | 4 independent contact sensor inputs (GPIO 0, 1, 2, 21) |
| 5-6 | On/Off Lights | 2 independent controllable outputs (GPIO 19, 20) |
| 7 | Air Quality | Air quality level (Good/Fair/Moderate/Poor/VeryPoor/ExtremelyPoor) |
| 8 | CO2 Sensor | Carbon dioxide concentration (ppm) - requires OpenHAB 5.2+ |
| 9 | Temperature | Temperature measurement (°C) |
| 10 | Humidity | Relative humidity (%) |
| 11 | SCD40 Calibrate | Trigger forced CO2 calibration (On/Off switch) |
| 12 | SCD40 ASC | Enable/disable Automatic Self-Calibration |
| 13 | SCD40 Low Power | Enable low power measurement mode |
| 14 | SCD40 Persist | Save sensor settings to EEPROM |
| 15 | SCD40 Self-Test | Trigger sensor self-test |
| 16 | SCD40 Altitude | Set altitude compensation (Level Control, 0-3000m) |
| 17 | SCD40 Temp Offset | Set temperature offset (Level Control, 0-20°C, default 4°C) |

**Note**: Antenna selection (internal ceramic vs external UFL) is configured at compile time via `USE_EXTERNAL_ANTENNA` define, not via Matter endpoint.

## Factory Reset

To perform a factory reset:
1. Press and hold the BOOT button (GPIO9) for 5 seconds
2. The display will show reset progress
3. Device will restart and enter commissioning mode

## Troubleshooting

### OLED Display Not Working
- Check I2C connections (SDA to GPIO22, SCL to GPIO23)
- Verify I2C address (default 0x3C, some displays use 0x3D)
- Check power supply (3.3V for XIAO)

### SCD40 Sensor Not Responding
- Verify I2C connections (same bus as OLED)
- Check sensor power supply (3.3V)
- Allow 30+ seconds for sensor warm-up after power on
- Ensure nothing blocks the sensor's air intake
- Use the SCD40 Self-Test endpoint to diagnose issues
- **I2C Auto-Recovery**: If the sensor is disconnected and reconnected, the firmware automatically detects I2C errors and reinitializes the entire I2C bus (including OLED) after 3 consecutive failures. Look for these log messages:
  ```
  W (xxx) app_main: Failed to read SCD40 measurement (error 1/3)
  W (xxx) app_main: Failed to read SCD40 measurement (error 2/3)
  W (xxx) app_main: Failed to read SCD40 measurement (error 3/3)
  W (xxx) app_main: Too many consecutive errors - attempting I2C bus recovery...
  I (xxx) app_main: I2C bus reinitialization complete!
  ```

### Thread Connection Issues
- Verify a Thread Border Router is operational
- Check that Thread credentials are correct
- Ensure device is within radio range
- Check the status LED pattern to verify network role
- Try factory reset and recommission
- If using external antenna, verify GPIO14 is set HIGH

### Matter Commissioning Fails
- Verify setup code and discriminator
- Check Thread Border Router connectivity
- Ensure BLE is enabled during commissioning
- Check for interference from other BLE devices

### NTP Time Not Synchronizing
- **"Failed to find valid route for"**: Your Border Router doesn't route to Internet. Use a local NTP server (see NTP section above)
- **"NO DNS SERVERS CONFIGURED"**: Normal for Thread networks. The firmware configures DNS automatically
- **"DNS: Failed to resolve"**: DNS works but server unreachable. Check if your local NTP server's IPv6 is correct
- **Display shows "Attesa NTP..."**: Time not yet synchronized. Check:
  1. Local NTP server (chrony) is running and accepting connections
  2. IPv6 address in firmware matches your NTP server
  3. Chrony config allows your network (`allow fd00::/8`)
  4. Check chrony logs: `sudo journalctl -u chrony -f`

### Contact Sensors Not Responding
- Verify input GPIO connections (GPIO 0, 1, 2, 21)
- Check input pull-up configuration
- Verify proper grounding of sensors

### Outputs Not Working
- Verify output GPIO connections (GPIO 19, 20)
- Check if outputs are configured correctly in Matter controller
- Verify power supply can handle connected loads

## Project Structure

```
c6_matter_thread_sd40_v1_oled/
├── CMakeLists.txt              # Main project CMake file
├── README.md                   # This file
├── build.sh                    # Build script
├── flash.sh                    # Flash script
├── partitions.csv              # Partition table (4MB flash, OTA support)
├── sdkconfig.defaults          # Default SDK configuration
├── idf_component.yml           # Project dependencies
├── wokwi.toml                  # Wokwi simulator configuration
├── main/
│   ├── CMakeLists.txt          # Main component CMake
│   ├── Kconfig.projbuild       # Project configuration options
│   ├── app_main.cpp            # Main application (4 inputs, 2 outputs, SCD40, antenna)
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
        ├── font_8x8.c          # 8x8 pixel font data
        └── include/
            ├── oled_display.h  # Display API header
            ├── ssd1306.h       # SSD1306 driver header
            └── font_8x8.h      # Font header
```

## Wokwi Simulation

The project includes Wokwi simulator support. To simulate:

1. Install the Wokwi VS Code extension
2. Build the project: `./build.sh build`
3. Open `wokwi.toml` in VS Code
4. Start the simulation

Note: Thread networking and Matter commissioning are not fully supported in simulation.

## Background Tasks

The firmware runs several background tasks:

| Task | Stack | Priority | Description |
|------|-------|----------|-------------|
| gpio_input | 4096 | 5 | Monitors 4 GPIO inputs and updates Matter contact sensor states |
| scd40_sensor | 4096 | 5 | Reads CO2/temp/humidity from SCD40, updates Matter clusters and OLED |
| scd40_sync | 4096 | 5 | Syncs SCD40 EEPROM config with Matter attributes (delayed 10s startup) |
| thread_led | 4096 | 5 | Updates status LED based on Thread network role |
| sntp_monitor | 4096 | 5 | Monitors NTP sync status, retries with local server if needed |

## I2C Bus Auto-Recovery

The firmware includes automatic I2C bus recovery to handle sensor disconnection/reconnection scenarios (hot-plug). This is useful during development or if a sensor connection becomes loose.

### How It Works

1. The SCD40 sensor task monitors I2C communication errors
2. After **3 consecutive read failures**, the recovery process starts
3. Recovery steps:
   - Remove SCD40 device from I2C bus
   - Deinitialize OLED display
   - Delete I2C master bus
   - Wait 100ms for hardware stabilization
   - Reinitialize I2C master bus
   - Reinitialize SCD40 sensor
   - Reinitialize OLED display with new bus handle

### Supported Scenarios

- SCD40 sensor temporarily disconnected and reconnected
- OLED display temporarily disconnected and reconnected
- I2C bus locked up due to incomplete transaction
- Power glitch on I2C devices

**Note**: Both I2C devices (SCD40 and OLED) are reinitialized together since they share the same bus.

## Configuration Persistence

SCD40 configuration is persisted in two locations:

1. **SCD40 EEPROM**: Altitude and temperature offset stored in sensor's non-volatile memory
2. **ESP32 NVS**: Backup storage for configuration values

On boot, the firmware:
1. Reads configuration from SCD40 EEPROM
2. Falls back to NVS if EEPROM read fails
3. Uses defaults (altitude=0m, temp_offset=4°C) if neither available
4. Syncs Matter Level Control attributes after 10 seconds (allows OpenHAB to see correct values)

**Factory Reset** clears both NVS configuration and performs SCD40 factory reset.

## License

This project is provided as-is for educational and development purposes.

## Acknowledgments

- Espressif Systems for ESP-IDF and ESP-Matter SDK
- Sensirion for SCD40 sensor documentation
- Connectivity Standards Alliance for Matter specification
