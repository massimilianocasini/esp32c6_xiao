#!/bin/bash
#
# build.sh - Build script for Matter SCD40 OLED project
#
# This script handles the build process for the ESP32-C6 Matter/Thread
# air quality sensor with OLED display.
#
# Prerequisites:
# - ESP-IDF v5.2.1+ installed and configured
# - ESP-Matter SDK installed
# - Environment variables set (run export.sh from IDF and ESP-Matter)
#
# Usage:
#   ./build.sh              # Full build
#   ./build.sh clean        # Clean build
#   ./build.sh menuconfig   # Open menuconfig
#   ./build.sh flash        # Flash to device
#   ./build.sh monitor      # Open serial monitor
#   ./build.sh all          # Build, flash, and monitor

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Project directory (where this script is located)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}  Matter SCD40 OLED Build Script${NC}"
echo -e "${GREEN}============================================${NC}"

# Check if IDF_PATH is set
if [ -z "$IDF_PATH" ]; then
    echo -e "${RED}Error: IDF_PATH is not set${NC}"
    echo "Please run: source \$IDF_PATH/export.sh"
    exit 1
fi

# Check if ESP_MATTER_PATH is set
if [ -z "$ESP_MATTER_PATH" ]; then
    echo -e "${YELLOW}Warning: ESP_MATTER_PATH is not set${NC}"
    echo "Attempting to set default path..."
    export ESP_MATTER_PATH="$HOME/esp/esp-matter"
    if [ ! -d "$ESP_MATTER_PATH" ]; then
        echo -e "${RED}Error: ESP-Matter not found at $ESP_MATTER_PATH${NC}"
        echo "Please set ESP_MATTER_PATH to your ESP-Matter installation"
        exit 1
    fi
fi

echo "IDF_PATH: $IDF_PATH"
echo "ESP_MATTER_PATH: $ESP_MATTER_PATH"
echo "Project: $PROJECT_DIR"
echo ""

# Change to project directory
cd "$PROJECT_DIR"

# Process command line argument
case "${1:-build}" in
    clean)
        echo -e "${YELLOW}Cleaning build...${NC}"
        rm -rf build
        echo -e "${GREEN}Clean complete${NC}"
        ;;

    fullclean)
        echo -e "${YELLOW}Full clean (including managed components)...${NC}"
        rm -rf build
        rm -rf managed_components
        echo -e "${GREEN}Full clean complete${NC}"
        ;;

    menuconfig)
        echo -e "${YELLOW}Opening menuconfig...${NC}"
        idf.py menuconfig
        ;;

    build)
        echo -e "${YELLOW}Building project...${NC}"
        idf.py build
        echo -e "${GREEN}Build complete!${NC}"
        ;;

    flash)
        echo -e "${YELLOW}Flashing to device...${NC}"
        idf.py -p "${PORT:-/dev/ttyUSB0}" flash
        echo -e "${GREEN}Flash complete!${NC}"
        ;;

    monitor)
        echo -e "${YELLOW}Opening serial monitor...${NC}"
        idf.py -p "${PORT:-/dev/ttyUSB0}" monitor
        ;;

    all)
        echo -e "${YELLOW}Building, flashing, and monitoring...${NC}"
        idf.py -p "${PORT:-/dev/ttyUSB0}" build flash monitor
        ;;

    erase)
        echo -e "${YELLOW}Erasing flash...${NC}"
        idf.py -p "${PORT:-/dev/ttyUSB0}" erase-flash
        echo -e "${GREEN}Erase complete!${NC}"
        ;;

    size)
        echo -e "${YELLOW}Showing size info...${NC}"
        idf.py size
        idf.py size-components
        ;;

    *)
        echo "Usage: $0 {build|clean|fullclean|menuconfig|flash|monitor|all|erase|size}"
        echo ""
        echo "Commands:"
        echo "  build       - Build the project (default)"
        echo "  clean       - Clean build directory"
        echo "  fullclean   - Clean build and managed_components"
        echo "  menuconfig  - Open SDK configuration menu"
        echo "  flash       - Flash firmware to device"
        echo "  monitor     - Open serial monitor"
        echo "  all         - Build, flash, and monitor"
        echo "  erase       - Erase entire flash"
        echo "  size        - Show firmware size information"
        echo ""
        echo "Environment variables:"
        echo "  PORT        - Serial port (default: /dev/ttyUSB0)"
        echo "  IDF_PATH    - Path to ESP-IDF"
        echo "  ESP_MATTER_PATH - Path to ESP-Matter SDK"
        exit 1
        ;;
esac
