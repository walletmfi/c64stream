#!/bin/bash
#
# Simple C64 Assembly Build Script
# Builds av-sync.asm into av-sync.prg without running

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASM_FILE="$SCRIPT_DIR/av-sync.asm"
PRG_FILE="$SCRIPT_DIR/av-sync.prg"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "Building C64 A/V sync test program..."
echo "Source: $ASM_FILE"
echo "Output: $PRG_FILE"

# Check if 64tass is installed
if ! command -v 64tass >/dev/null 2>&1; then
    echo -e "${RED}Error: 64tass assembler not found${NC}"
    echo "Install it with: sudo apt install 64tass"
    exit 1
fi

# Build with 64tass
if 64tass \
    --cbm-prg \
    --labels \
    --quiet \
    -o "$PRG_FILE" \
    "$ASM_FILE"; then

    echo -e "${GREEN}Build successful!${NC}"
    echo "Created: $PRG_FILE"

    size=$(stat -c%s "$PRG_FILE")
    echo "File size: $size bytes"

    # Optional: show directory for convenience
    if command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$SCRIPT_DIR" >/dev/null 2>&1 &
    fi
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
