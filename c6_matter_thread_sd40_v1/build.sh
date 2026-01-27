#!/bin/bash

# Build and Flash script for ESP32C6 XIAO Matter Device

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default serial port
SERIAL_PORT="/dev/ttyACM0"

# Check for custom serial port
if [ ! -z "$1" ]; then
    SERIAL_PORT=$1
fi

echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}   ESP32C6 XIAO Matter Device Build Script     ${NC}"
echo -e "${GREEN}================================================${NC}"
echo ""

# Check if ESP-IDF is sourced
if [ -z "$IDF_PATH" ]; then
    echo -e "${RED}Error: ESP-IDF not sourced!${NC}"
    echo "Please run: . ~/esp/esp-idf/export.sh"
    exit 1
fi

# Check if ESP-Matter is sourced
if [ -z "$ESP_MATTER_PATH" ]; then
    echo -e "${YELLOW}Warning: ESP-Matter might not be sourced${NC}"
    echo "Trying to source it..."
    if [ -f "../esp-matter/export.sh" ]; then
        . ../esp-matter/export.sh
    else
        echo -e "${RED}Error: ESP-Matter not found!${NC}"
        exit 1
    fi
fi

# Function to show menu
show_menu() {
    echo -e "${YELLOW}Select an option:${NC}"
    echo "1) Clean build"
    echo "2) Build only"
    echo "3) Flash only"
    echo "4) Build and Flash"
    echo "5) Monitor serial"
    echo "6) Build, Flash and Monitor"
    echo "7) Menuconfig"
    echo "8) Erase flash (Factory Reset)"
    echo "9) Exit"
    echo ""
    read -p "Enter choice [1-9]: " choice
}

# Main loop
while true; do
    show_menu
    case $choice in
        1)
            echo -e "${GREEN}Cleaning build...${NC}"
            idf.py fullclean
            echo -e "${GREEN}Clean complete!${NC}"
            ;;
        2)
            echo -e "${GREEN}Building project...${NC}"
            idf.py build
            echo -e "${GREEN}Build complete!${NC}"
            ;;
        3)
            echo -e "${GREEN}Flashing to $SERIAL_PORT...${NC}"
            idf.py -p $SERIAL_PORT flash
            echo -e "${GREEN}Flash complete!${NC}"
            ;;
        4)
            echo -e "${GREEN}Building and flashing to $SERIAL_PORT...${NC}"
            idf.py build
            idf.py -p $SERIAL_PORT flash
            echo -e "${GREEN}Build and flash complete!${NC}"
            ;;
        5)
            echo -e "${GREEN}Starting serial monitor on $SERIAL_PORT...${NC}"
            echo -e "${YELLOW}Press Ctrl+] to exit monitor${NC}"
            idf.py -p $SERIAL_PORT monitor
            ;;
        6)
            echo -e "${GREEN}Building, flashing and monitoring on $SERIAL_PORT...${NC}"
            idf.py build
            idf.py -p $SERIAL_PORT flash monitor
            ;;
        7)
            echo -e "${GREEN}Opening menuconfig...${NC}"
            idf.py menuconfig
            ;;
        8)
            echo -e "${RED}WARNING: This will erase all flash including commissioning data!${NC}"
            read -p "Are you sure? (y/n): " confirm
            if [ "$confirm" == "y" ]; then
                echo -e "${GREEN}Erasing flash...${NC}"
                idf.py -p $SERIAL_PORT erase-flash
                echo -e "${GREEN}Flash erased!${NC}"
            else
                echo "Cancelled"
            fi
            ;;
        9)
            echo -e "${GREEN}Exiting...${NC}"
            exit 0
            ;;
        *)
            echo -e "${RED}Invalid option${NC}"
            ;;
    esac
    echo ""
    read -p "Press Enter to continue..."
    clear
done
