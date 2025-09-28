#!/bin/bash

# Simple C64 Assembly Build Script
# Just builds digit-cycle.asm without running

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASM_FILE="$SCRIPT_DIR/digit-cycle.asm"
PRG_FILE="$SCRIPT_DIR/digit-cycle.prg"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "Building C64 program..."
echo "Source: $ASM_FILE"
echo "Output: $PRG_FILE"

# Check if 64tass is installed
if ! command -v 64tass >/dev/null 2>&1; then
    echo -e "${RED}Error: 64tass assembler not found${NC}"
    echo "Please install it with: sudo apt install 64tass"
    exit 1
fi

# Build the program using 64tass
if 64tass -a "$ASM_FILE" -o "$PRG_FILE"; then
    echo -e "${GREEN}Build successful!${NC}"
    echo "Created: $PRG_FILE"

    # Show file size
    size=$(stat -c%s "$PRG_FILE")
    echo "File size: $size bytes"
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
