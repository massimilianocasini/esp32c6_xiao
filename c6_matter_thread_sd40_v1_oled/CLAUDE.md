# CLAUDE.md - Project Guide for Claude Code

## Project Overview

This is an ESP32-C6 Matter/Thread sensor device firmware for the Seeed Studio XIAO ESP32-C6. It implements a multi-function sensor board with:
- 4 contact sensor inputs (GPIO 0, 1, 2, 21)
- 2 controllable outputs (GPIO 19, 20)
- SCD40 CO2/temperature/humidity sensor (I2C)
- SSD1306 128x64 OLED display (I2C)
- Thread networking with Matter protocol

## Build Environment

```bash
# Source environments (required before any build command)
source /opt/esp/idf/export.sh
source /workspace/esp-matter/export.sh

# Build
idf.py build

# Flash (change port as needed)
idf.py -p /dev/ttyACM0 flash

# Monitor serial output
idf.py -p /dev/ttyACM0 monitor

# Clean build
idf.py fullclean
```

## Project Structure

```
c6_matter_thread_sd40_v1_oled/
├── main/
│   ├── app_main.cpp        # Main application (Matter endpoints, sensors, NTP)
│   ├── app_reset.cpp       # Factory reset handling
│   ├── app_priv.h          # Private definitions
│   └── Kconfig.projbuild   # Configuration options
├── components/
│   └── oled_display/       # OLED display driver
│       ├── oled_display.c  # High-level display API
│       ├── ssd1306.c       # Low-level SSD1306 driver
│       └── font_8x8.c      # Font data
├── sdkconfig               # SDK configuration
├── partitions.csv          # Flash partition table
└── README.md               # User documentation
```

## Key Code Sections in app_main.cpp

| Section | Description |
|---------|-------------|
| Lines 1-180 | Includes, defines, GPIO pins, NTP servers |
| Lines 180-400 | GPIO input/output handling |
| Lines 400-900 | SCD40 sensor functions |
| Lines 900-1200 | NTP/SNTP time synchronization |
| Lines 1200-1500 | Matter attribute callbacks |
| Lines 1500-2000 | Background tasks |
| Lines 2000-2500 | Matter endpoint creation, app_main() |

## Hardware Configuration

| Component | I2C Address | GPIO |
|-----------|-------------|------|
| SCD40 Sensor | 0x62 | SDA=22, SCL=23 |
| SSD1306 OLED | 0x3C | SDA=22, SCL=23 |
| Status LED | - | GPIO 15 |
| Reset Button | - | GPIO 9 |

## Important Technical Notes

### NTP Time Sync
- Thread devices cannot reach Internet directly (Border Router limitation)
- Uses **only local NTP server** (Raspberry Pi with chrony) - no external servers
- Server IPv6 configured in `CONFIG_LOCAL_NTP_SERVER_IPV6`
- No Matter endpoints for NTP configuration (hardcoded local server only)

### OLED Display
- 128x64 pixels = 16 chars x 8 lines (8x8 font)
- Display update in `oled_update_sensor_display()` in `oled_display.c`
- Air quality text in Italian with blinking for poor quality

### Matter Endpoints
- 17 endpoints total (contacts 1-4, outputs 5-6, sensors 7-10, SCD40 control 11-17)
- Endpoint IDs assigned dynamically at creation
- CO2 cluster 0x040D requires OpenHAB 5.2+

### SCD40 Sensor
- 5-second measurement cycle
- Configuration persisted in sensor EEPROM and ESP32 NVS
- Temperature offset default: 4°C
- Altitude compensation: 0-3000m

## Common Tasks

### Adding a new Matter endpoint
1. Define endpoint ID variable: `static uint16_t my_endpoint_id = 0;`
2. Create endpoint in `app_main()` after other endpoints
3. Add attribute callback handling in `app_attribute_update_cb()`
4. Update README.md with new endpoint

### Modifying display layout
1. Edit `oled_update_sensor_display()` in `components/oled_display/oled_display.c`
2. Max 16 chars per line (128px / 8px font)
3. Use `oled_set_cursor(x, y)` where y = line * 8

### Changing NTP server
1. Edit `CONFIG_LOCAL_NTP_SERVER_IPV6` in `main/app_main.cpp`
2. Rebuild and flash the firmware (no runtime configuration via Matter)

## Debugging Tips

- Serial log tags: `app_main`, `oled_display`, `ssd1306`, `OPENTHREAD`, `chip[*]`
- NTP issues: Look for "Failed to find valid route" (Border Router issue)
- I2C issues: "I2C transaction unexpected nack detected"
- Matter issues: Check `chip[DMG]`, `chip[IM]` log tags

## Code Style

- C++ for main application (app_main.cpp)
- C for components (oled_display)
- ESP-IDF logging macros: `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()`
- Italian strings for user-facing OLED text
- English for log messages and code comments
