#!/bin/bash

# Script di flash per ESP32-C6 con parametri corretti (senza warning deprecati)

PORT=${1:-/dev/tty.usbmodem21101}

echo "======================================"
echo "  ESP32-C6 Flash Script"
echo "======================================"
echo "Port: $PORT"
echo ""

cd build

if [ "$2" == "erase" ]; then
    echo "Performing full erase and flash..."
    esptool --chip esp32c6 -p $PORT \
        --baud 460800 \
        --before default_reset --after hard_reset \
        erase-flash \
        write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB \
        0x0 bootloader/bootloader.bin \
        0x8000 partition_table/partition-table.bin \
        0xf000 ota_data_initial.bin \
        0x20000 matter_light_switch.bin
else
    echo "Flashing app only (no erase)..."
    esptool --chip esp32c6 -p $PORT \
        --baud 460800 \
        --before default_reset --after hard_reset \
        write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB \
        0x20000 matter_light_switch.bin
fi

echo ""
echo "Flash complete!"
echo ""
echo "Usage:"
echo "  ./flash.sh [port] [erase]"
echo ""
echo "Examples:"
echo "  ./flash.sh                              # Flash app only to default port"
echo "  ./flash.sh /dev/ttyUSB0                 # Flash app only to custom port"
echo "  ./flash.sh /dev/tty.usbmodem21101 erase # Full erase and flash"
