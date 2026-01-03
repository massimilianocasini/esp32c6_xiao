#!/bin/bash

# ESP-Matter Installation Script for ESP32C6 XIAO Matter Project

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}     ESP-Matter Installation Script            ${NC}"
echo -e "${BLUE}================================================${NC}"
echo ""

# Check if ESP-IDF is installed
if [ -z "$IDF_PATH" ]; then
    echo -e "${RED}Error: ESP-IDF not found!${NC}"
    echo -e "Please install ESP-IDF first:"
    echo -e "${YELLOW}"
    echo "mkdir -p ~/esp"
    echo "cd ~/esp"
    echo "git clone -b v5.2.3 --recursive https://github.com/espressif/esp-idf.git"
    echo "cd esp-idf"
    echo "./install.sh esp32c6"
    echo ". ./export.sh"
    echo -e "${NC}"
    exit 1
fi

echo -e "${GREEN}✓ ESP-IDF found at: $IDF_PATH${NC}"
echo ""

# Check ESP-IDF version
IDF_VERSION=$(cd $IDF_PATH && git describe --tags 2>/dev/null || echo "unknown")
echo -e "${GREEN}ESP-IDF Version: $IDF_VERSION${NC}"

# Installation directory
ESP_DIR="$HOME/esp"
MATTER_DIR="$ESP_DIR/esp-matter"

# Check if ESP-Matter is already installed
if [ -d "$MATTER_DIR" ]; then
    echo -e "${YELLOW}ESP-Matter already exists at $MATTER_DIR${NC}"
    read -p "Do you want to reinstall? (y/n): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Using existing installation..."
        echo -e "${GREEN}"
        echo "To use ESP-Matter, run:"
        echo "cd $MATTER_DIR"
        echo ". ./export.sh"
        echo -e "${NC}"
        exit 0
    fi
    echo "Removing old installation..."
    rm -rf "$MATTER_DIR"
fi

# Create esp directory if it doesn't exist
mkdir -p "$ESP_DIR"
cd "$ESP_DIR"

echo -e "${BLUE}Installing ESP-Matter...${NC}"
echo "This may take several minutes..."

# Clone ESP-Matter repository
echo "Cloning ESP-Matter repository..."
git clone --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter

# Checkout stable version (optional - you can specify a specific tag)
# git checkout v1.2.0

echo ""
echo -e "${BLUE}Installing ESP-Matter dependencies...${NC}"

# Install ESP-Matter
./install.sh

echo ""
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}     ESP-Matter Installation Complete!         ${NC}"
echo -e "${GREEN}================================================${NC}"
echo ""
echo -e "${YELLOW}IMPORTANT: Before building the project, run:${NC}"
echo -e "${GREEN}"
echo "# 1. Source ESP-IDF"
echo ". $IDF_PATH/export.sh"
echo ""
echo "# 2. Source ESP-Matter"  
echo ". $MATTER_DIR/export.sh"
echo ""
echo "# 3. Build the project"
echo "cd $HOME/esp/project/matter_light_switch"
echo "idf.py set-target esp32c6"
echo "idf.py build"
echo -e "${NC}"

# Ask if user wants to source now
read -p "Do you want to source ESP-Matter now? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    . "$MATTER_DIR/export.sh"
    echo -e "${GREEN}✓ ESP-Matter environment loaded${NC}"
    echo -e "${GREEN}✓ ESP_MATTER_PATH=$ESP_MATTER_PATH${NC}"
fi

echo ""
echo -e "${BLUE}You can now build your Matter project!${NC}"
