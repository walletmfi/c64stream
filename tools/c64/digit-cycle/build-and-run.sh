#!/bin/bash

# C64 Assembly Build and Run Script
# Builds digit-cycle.asm and runs it in VICE emulator

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASM_FILE="$SCRIPT_DIR/digit-cycle.asm"
PRG_FILE="$SCRIPT_DIR/digit-cycle.prg"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}C64 Assembly Build and Run Script${NC}"
echo "=================================="

# Check if 64tass is installed
if ! command -v 64tass >/dev/null 2>&1; then
    echo -e "${RED}Error: 64tass assembler not found${NC}"
    echo "Please install it with: sudo apt install 64tass"
    exit 1
fi

# Check if assembly file exists
if [ ! -f "$ASM_FILE" ]; then
    echo -e "${RED}Error: Assembly file not found: $ASM_FILE${NC}"
    exit 1
fi

echo -e "${YELLOW}Building C64 program...${NC}"
echo "Source: $ASM_FILE"
echo "Output: $PRG_FILE"

# Build the program using 64tass
if 64tass -a "$ASM_FILE" -o "$PRG_FILE"; then
    echo -e "${GREEN}Build successful!${NC}"
    echo "Created: $PRG_FILE"
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

# Check if VICE is installed
VICE_CMD=""
for cmd in x64sc x64 xvice; do
    if command -v "$cmd" >/dev/null 2>&1; then
        VICE_CMD="$cmd"
        break
    fi
done

if [ -z "$VICE_CMD" ]; then
    echo -e "${YELLOW}VICE emulator not found.${NC}"
    echo "Would you like to install it? (y/n)"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        echo -e "${YELLOW}Installing VICE...${NC}"
        sudo apt update
        sudo apt install -y vice

        # Check again for VICE commands
        for cmd in x64sc x64 xvice; do
            if command -v "$cmd" >/dev/null 2>&1; then
                VICE_CMD="$cmd"
                break
            fi
        done

        if [ -z "$VICE_CMD" ]; then
            echo -e "${RED}VICE installation failed or commands not found${NC}"
            echo "You can manually run the PRG file: $PRG_FILE"
            exit 1
        fi
    else
        echo -e "${YELLOW}Skipping VICE installation.${NC}"
        echo "You can manually run the PRG file: $PRG_FILE"
        echo "Or install VICE later with: sudo apt install vice"
        exit 0
    fi
fi

echo -e "${GREEN}Running program in VICE emulator...${NC}"
echo "Using: $VICE_CMD"
echo "Loading: $PRG_FILE"

# Run the program in VICE
# -autostart loads and runs the program automatically
# -autostartprgmode 1 sets autostart mode to load and run
"$VICE_CMD" -autostart "$PRG_FILE" -autostartprgmode 1

echo -e "${GREEN}Done!${NC}"
