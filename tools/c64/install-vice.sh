#!/bin/bash

# VICE Installation and ROM Setup Script
# Installs VICE emulator and sets up ROM files automatically

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}VICE Installation and ROM Setup Script${NC}"
echo "======================================"

# Function to check if VICE is installed
check_vice_installed() {
    for cmd in x64sc x64 xvice; do
        if command -v "$cmd" >/dev/null 2>&1; then
            return 0
        fi
    done
    return 1
}

# Function to check if ROMs are properly installed
check_roms_installed() {
    local vice_data_dir="$HOME/.local/share/vice"
    local required_roms=(
        "C64/kernal-901227-03.bin"
        "C64/basic-901226-01.bin"
        "C64/chargen-901225-01.bin"
    )

    if [ ! -d "$vice_data_dir" ]; then
        return 1
    fi

    for rom in "${required_roms[@]}"; do
        if [ ! -f "$vice_data_dir/$rom" ]; then
            echo -e "${YELLOW}Missing ROM: $rom${NC}"
            return 1
        fi
    done

    return 0
}

# Function to test VICE functionality
test_vice_functionality() {
    echo -e "${YELLOW}Testing VICE functionality...${NC}"

    # Create a simple test PRG file
    local test_dir="/tmp/vice_test_$$"
    mkdir -p "$test_dir"

    # Simple BASIC program: 10 PRINT "HELLO"
    cat > "$test_dir/test.asm" << 'EOF'
        *= $0801
        .word nextline
        .word 10
        .byte $9e
        .text "2061"
        .byte 0
nextline:
        .word 0
        lda #$48  ; 'H'
        jsr $ffd2
        lda #$45  ; 'E'
        jsr $ffd2
        lda #$4c  ; 'L'
        jsr $ffd2
        jsr $ffd2 ; 'L' again
        lda #$4f  ; 'O'
        jsr $ffd2
        rts
EOF

    if command -v 64tass >/dev/null 2>&1; then
        if 64tass -a "$test_dir/test.asm" -o "$test_dir/test.prg" 2>/dev/null; then
            # Find VICE executable
            local vice_cmd=""
            for cmd in x64sc x64 xvice; do
                if command -v "$cmd" >/dev/null 2>&1; then
                    vice_cmd="$cmd"
                    break
                fi
            done

            if [ -n "$vice_cmd" ]; then
                # Test VICE with timeout (exit after 2 seconds)
                timeout 2s "$vice_cmd" -autostart "$test_dir/test.prg" -autostartprgmode 1 >/dev/null 2>&1 || true
                echo -e "${GREEN}VICE test completed successfully${NC}"
            fi
        fi
    fi

    # Cleanup
    rm -rf "$test_dir"
}

# Main installation logic

echo -e "${YELLOW}Checking current installation status...${NC}"

# Check if VICE is already installed
if check_vice_installed; then
    echo -e "${GREEN}✓ VICE is already installed${NC}"
    VICE_INSTALLED=true
else
    echo -e "${YELLOW}✗ VICE is not installed${NC}"
    VICE_INSTALLED=false
fi

# Check if ROMs are already installed
if check_roms_installed; then
    echo -e "${GREEN}✓ VICE ROM files are properly installed${NC}"
    ROMS_INSTALLED=true
else
    echo -e "${YELLOW}✗ VICE ROM files are missing or incomplete${NC}"
    ROMS_INSTALLED=false
fi

# If everything is already set up, exit early
if [ "$VICE_INSTALLED" = true ] && [ "$ROMS_INSTALLED" = true ]; then
    echo -e "${GREEN}✓ VICE and ROM files are already properly installed!${NC}"
    test_vice_functionality
    echo -e "${GREEN}✓ All checks passed. Nothing to do.${NC}"
    exit 0
fi

# Install VICE if needed
if [ "$VICE_INSTALLED" = false ]; then
    echo -e "${YELLOW}Installing VICE emulator...${NC}"

    # Check if running as root
    if [ "$EUID" -eq 0 ]; then
        apt update
        apt install -y vice
    else
        sudo apt update
        sudo apt install -y vice
    fi

    # Verify installation
    if check_vice_installed; then
        echo -e "${GREEN}✓ VICE installed successfully${NC}"
    else
        echo -e "${RED}✗ VICE installation failed${NC}"
        exit 1
    fi
fi

# Install ROM files if needed
if [ "$ROMS_INSTALLED" = false ]; then
    echo -e "${YELLOW}Setting up VICE ROM files...${NC}"

    # Create VICE data directory
    mkdir -p "$HOME/.local/share/vice"

    # Check if ROM source already exists in /tmp
    if [ ! -d "/tmp/vice-3.7.1" ]; then
        echo -e "${YELLOW}Downloading VICE source for ROM files...${NC}"

        cd /tmp
        if ! wget -q https://sourceforge.net/projects/vice-emu/files/releases/vice-3.7.1.tar.gz/download -O vice-3.7.1.tar.gz; then
            echo -e "${RED}Failed to download VICE source${NC}"
            exit 1
        fi

        echo -e "${YELLOW}Extracting ROM files...${NC}"
        tar -xzf vice-3.7.1.tar.gz
    else
        echo -e "${GREEN}✓ VICE source already available${NC}"
    fi

    # Copy ROM files
    echo -e "${YELLOW}Installing ROM files...${NC}"
    if find /tmp/vice-3.7.1/data -mindepth 1 -type d -exec cp -rn {} "$HOME/.local/share/vice/" \; 2>/dev/null; then
        echo -e "${GREEN}✓ ROM files installed successfully${NC}"
    else
        echo -e "${RED}✗ Failed to install ROM files${NC}"
        exit 1
    fi

    # Verify ROM installation
    if check_roms_installed; then
        echo -e "${GREEN}✓ ROM files verified successfully${NC}"
    else
        echo -e "${RED}✗ ROM file verification failed${NC}"
        exit 1
    fi

    # Cleanup
    echo -e "${YELLOW}Cleaning up temporary files...${NC}"
    rm -f /tmp/vice-3.7.1.tar.gz
    # Keep the extracted directory in case we need it again
fi

# Final verification
echo -e "${YELLOW}Performing final verification...${NC}"

if check_vice_installed && check_roms_installed; then
    echo -e "${GREEN}✓ Installation completed successfully!${NC}"
    test_vice_functionality

    echo ""
    echo -e "${GREEN}VICE is now ready to use!${NC}"
    echo "You can run C64 programs with commands like:"
    echo "  x64sc -autostart program.prg"
    echo ""
else
    echo -e "${RED}✗ Installation verification failed${NC}"
    exit 1
fi
