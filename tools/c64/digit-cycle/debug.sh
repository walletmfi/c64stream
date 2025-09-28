#!/bin/bash

# C64 Assembly Debug Script - Launches VICE with monitor/debugger
# Usage: ./debug.sh

set -e

echo "C64 Assembly Debug Script"
echo "========================="

# Build the program first
echo "Building C64 program..."
SOURCE_FILE="digit-cycle.asm"
OUTPUT_FILE="digit-cycle.prg"

echo "Source: $(pwd)/$SOURCE_FILE"
echo "Output: $(pwd)/$OUTPUT_FILE"

# Compile with 64tass
64tass "$SOURCE_FILE" -o "$OUTPUT_FILE" -L digit-cycle.lst --verbose-list

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo "Created: $(pwd)/$OUTPUT_FILE"
    echo "Listing: $(pwd)/digit-cycle.lst"
    echo "File size: $(stat -c%s "$OUTPUT_FILE") bytes"
else
    echo "Build failed!"
    exit 1
fi

# Check if VICE is installed
if ! command -v x64sc &> /dev/null; then
    echo "Error: VICE emulator not found. Please install VICE first."
    echo "Run: sudo apt-get install vice"
    exit 1
fi

echo
echo "Launching VICE with debugger..."
echo "Using: x64sc"
echo "Loading: $(pwd)/$OUTPUT_FILE"
echo
echo "DEBUGGER COMMANDS:"
echo "==================="
echo "Basic commands:"
echo "  n           - Next instruction (step over)"
echo "  s           - Step into instruction"
echo "  c           - Continue execution"
echo "  r           - Show registers"
echo "  d [addr]    - Disassemble from address"
echo "  m [addr]    - Show memory dump"
echo "  break [addr]- Set breakpoint"
echo "  del [num]   - Delete breakpoint"
echo "  x           - Exit monitor"
echo
echo "C64 specific addresses:"
echo "  \$d012       - Raster line register"
echo "  \$0400-\$07e7 - Screen memory (1000 chars)"
echo "  \$d800-\$dbe7 - Color memory (1000 chars)"
echo
echo "Program addresses (from listing):"
echo "  See digit-cycle.lst for exact addresses"
echo
echo "Starting debugger in 3 seconds..."
sleep 3

# Launch VICE with monitor enabled
x64sc \
    -autostart "$OUTPUT_FILE" \
    -autostartprgmode 1 \
    -moncommands <(echo "
        ; Set up initial breakpoints
        break \$$(grep 'start:' digit-cycle.lst | cut -d' ' -f1 | head -1)
        break \$$(grep 'main_loop:' digit-cycle.lst | cut -d' ' -f1 | head -1)
        break \$$(grep 'wait_raster:' digit-cycle.lst | cut -d' ' -f1 | head -1)
        ; Show initial state
        r
        d \$$(grep 'start:' digit-cycle.lst | cut -d' ' -f1 | head -1) \$$(grep 'start:' digit-cycle.lst | cut -d' ' -f1 | head -1)+20
    ") \
    +confirmexit
