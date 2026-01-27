#!/bin/bash
#
# flash.sh - Flash and monitor script for Matter SCD40 OLED project
#
# This script provides a convenient way to flash the firmware
# and start the serial monitor.
#
# Usage:
#   ./flash.sh              # Flash and monitor (default port)
#   ./flash.sh /dev/ttyACM0 # Flash and monitor on specific port

set -e

# Color codes
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Default port - adjust based on your system
# Common ports:
#   Linux:   /dev/ttyUSB0, /dev/ttyACM0
#   macOS:   /dev/cu.usbserial-*, /dev/cu.usbmodem*
#   Windows: COM3, COM4, etc.
DEFAULT_PORT="/dev/ttyUSB0"

# Use provided port or default
PORT="${1:-$DEFAULT_PORT}"

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}  Flash and Monitor - Matter SCD40 OLED${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo "Using port: $PORT"
echo ""

# Check if port exists
if [ ! -e "$PORT" ]; then
    echo -e "${YELLOW}Warning: Port $PORT not found${NC}"
    echo ""
    echo "Available serial ports:"
    ls -la /dev/tty* 2>/dev/null | grep -E '(USB|ACM|usbserial|usbmodem)' || echo "  No USB serial ports found"
    echo ""
    echo "Please specify the correct port: ./flash.sh /dev/ttyXXX"
    exit 1
fi

# Check if IDF_PATH is set
if [ -z "$IDF_PATH" ]; then
    echo "Error: IDF_PATH not set. Please source ESP-IDF export.sh first."
    exit 1
fi

# Flash and monitor
echo -e "${YELLOW}Flashing firmware...${NC}"
idf.py -p "$PORT" flash monitor

echo -e "${GREEN}Done!${NC}"
