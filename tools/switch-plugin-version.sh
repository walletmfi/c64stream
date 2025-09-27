#!/bin/bash

# Script to switch between development and published plugin versions
# Usage: ./tools/switch-plugin-version.sh [dev|published]

set -e

PLUGIN_DIR="$HOME/.config/obs-studio/plugins/c64u-plugin-for-obs"
PUBLISHED_DIR="$HOME/.config/obs-studio/plugins/c64u-plugin-for-obs-published"
BACKUP_DIR="$HOME/.config/obs-studio/plugins/c64u-plugin-for-obs-backup"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

usage() {
    echo "Usage: $0 [dev|published|status]"
    echo ""
    echo "  dev        - Switch to development version (F5 builds)"
    echo "  published  - Switch to published GitHub release version"
    echo "  status     - Show which version is currently active"
    echo ""
    echo "Examples:"
    echo "  $0 dev        # Use development version"
    echo "  $0 published  # Use published version"
    echo "  $0 status     # Check current version"
}

get_plugin_version() {
    if [ -f "$PLUGIN_DIR/bin/64bit/c64u-plugin-for-obs.so" ]; then
        # Try to extract version info from the binary (this is a simple approach)
        local version_info=$(strings "$PLUGIN_DIR/bin/64bit/c64u-plugin-for-obs.so" | grep -E "version|Version" | head -1 || echo "unknown")
        echo "$version_info"
    else
        echo "none"
    fi
}

show_status() {
    echo "🔍 ${BLUE}Plugin Status:${NC}"
    echo ""
    
    if [ -f "$PLUGIN_DIR/bin/64bit/c64u-plugin-for-obs.so" ]; then
        local build_time=$(stat -c %Y "$PLUGIN_DIR/bin/64bit/c64u-plugin-for-obs.so")
        local formatted_time=$(date -d @$build_time '+%Y-%m-%d %H:%M:%S')
        echo "  ${GREEN}✓${NC} Active plugin: $PLUGIN_DIR/bin/64bit/c64u-plugin-for-obs.so"
        echo "  📅 Last modified: $formatted_time"
        
        # Check if it matches development or published version
        local dev_file="../c64u-obs-private/build_x86_64/c64u-plugin-for-obs.so"
        local pub_file="$PUBLISHED_DIR/bin/64bit/c64u-plugin-for-obs.so"
        
        if [ -f "$dev_file" ] && cmp -s "$PLUGIN_DIR/bin/64bit/c64u-plugin-for-obs.so" "$dev_file"; then
            echo "  🔧 ${YELLOW}Type: Development version${NC} (matches build_x86_64/)"
        elif [ -f "$pub_file" ] && cmp -s "$PLUGIN_DIR/bin/64bit/c64u-plugin-for-obs.so" "$pub_file"; then
            echo "  📦 ${BLUE}Type: Published version${NC} (GitHub release)"
        else
            echo "  ❓ ${YELLOW}Type: Unknown version${NC}"
        fi
    else
        echo "  ${RED}❌ No plugin installed${NC}"
    fi
    
    echo ""
    echo "📂 Available versions:"
    if [ -f "../c64u-obs-private/build_x86_64/c64u-plugin-for-obs.so" ]; then
        local dev_time=$(stat -c %Y "../c64u-obs-private/build_x86_64/c64u-plugin-for-obs.so")
        local dev_formatted=$(date -d @$dev_time '+%Y-%m-%d %H:%M:%S')
        echo "  🔧 Development: ../c64u-obs-private/build_x86_64/c64u-plugin-for-obs.so ($dev_formatted)"
    else
        echo "  🔧 Development: ${RED}Not built${NC} (run F5 or cmake --build build_x86_64)"
    fi
    
    if [ -f "$PUBLISHED_DIR/bin/64bit/c64u-plugin-for-obs.so" ]; then
        local pub_time=$(stat -c %Y "$PUBLISHED_DIR/bin/64bit/c64u-plugin-for-obs.so")
        local pub_formatted=$(date -d @$pub_time '+%Y-%m-%d %H:%M:%S')
        echo "  📦 Published: $PUBLISHED_DIR/bin/64bit/c64u-plugin-for-obs.so ($pub_formatted)"
    else
        echo "  📦 Published: ${RED}Not downloaded${NC}"
    fi
}

switch_to_dev() {
    echo "🔧 ${YELLOW}Switching to development version...${NC}"
    
    # Check if development version exists
    if [ ! -f "../c64u-obs-private/build_x86_64/c64u-plugin-for-obs.so" ]; then
        echo "  ${RED}❌ Development version not found!${NC}"
        echo "  Build it first with: cmake --build build_x86_64"
        echo "  Or press F5 in VS Code"
        exit 1
    fi
    
    # Create plugin directory if it doesn't exist
    mkdir -p "$PLUGIN_DIR/bin/64bit"
    
    # Copy development version
    cp "../c64u-obs-private/build_x86_64/c64u-plugin-for-obs.so" "$PLUGIN_DIR/bin/64bit/"
    
    echo "  ${GREEN}✓${NC} Switched to development version"
    echo "  ${BLUE}ℹ${NC}  Your F5 debug workflow will now update this version"
}

switch_to_published() {
    echo "📦 ${YELLOW}Switching to published version...${NC}"
    
    # Check if published version exists
    if [ ! -f "$PUBLISHED_DIR/bin/64bit/c64u-plugin-for-obs.so" ]; then
        echo "  ${RED}❌ Published version not found!${NC}"
        echo "  Download it first from: https://github.com/chrisgleissner/c64u-obs/releases"
        exit 1
    fi
    
    # Create plugin directory if it doesn't exist
    mkdir -p "$PLUGIN_DIR/bin/64bit"
    mkdir -p "$PLUGIN_DIR/data/locale"
    
    # Copy published version
    cp "$PUBLISHED_DIR/bin/64bit/c64u-plugin-for-obs.so" "$PLUGIN_DIR/bin/64bit/"
    
    # Copy locale if it exists
    if [ -f "$PUBLISHED_DIR/data/locale/en-US.ini" ]; then
        cp "$PUBLISHED_DIR/data/locale/en-US.ini" "$PLUGIN_DIR/data/locale/"
    fi
    
    echo "  ${GREEN}✓${NC} Switched to published version"
    echo "  ${BLUE}ℹ${NC}  To switch back to dev, run: $0 dev"
}

# Main script
case "${1:-status}" in
    "dev"|"development")
        switch_to_dev
        ;;
    "pub"|"published"|"release")
        switch_to_published
        ;;
    "status"|"show"|"")
        show_status
        ;;
    "help"|"-h"|"--help")
        usage
        ;;
    *)
        echo "${RED}❌ Unknown option: $1${NC}"
        echo ""
        usage
        exit 1
        ;;
esac