#!/bin/bash

# Ubuntu Dependency Installation Script for C64 Stream
# This script handles the SIMDe dependency issue and installs required packages

set -euo pipefail

log_info() {
    echo -e "\033[0;34m[INFO]\033[0m $1"
}

log_success() {
    echo -e "\033[0;32m[SUCCESS]\033[0m $1"
}

log_warning() {
    echo -e "\033[1;33m[WARNING]\033[0m $1"
}

log_error() {
    echo -e "\033[0;31m[ERROR]\033[0m $1"
}

install_build_essentials() {
    log_info "Installing build essentials..."
    
    sudo apt-get update
    
    # Core build tools
    sudo apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        git \
        clang-format \
        python3-pip
    
    log_success "Build essentials installed"
}

install_simde() {
    log_info "Installing SIMDe (SIMD Everywhere)..."
    
    # Check if libsimde-dev is available in repos
    if apt-cache show libsimde-dev >/dev/null 2>&1; then
        log_info "Installing SIMDe from package repository..."
        sudo apt-get install -y libsimde-dev
        log_success "SIMDe installed from packages"
        return 0
    fi
    
    # Install from source if not available
    log_warning "SIMDe not available in package repos, installing from source..."
    
    local simde_version="0.7.6"
    local simde_dir="/tmp/simde-$simde_version"
    local install_prefix="/usr/local"
    
    # Download and extract SIMDe
    if [[ ! -d "$simde_dir" ]]; then
        cd /tmp
        curl -L "https://github.com/simd-everywhere/simde/archive/v${simde_version}.tar.gz" -o "simde-${simde_version}.tar.gz"
        tar -xzf "simde-${simde_version}.tar.gz"
    fi
    
    cd "$simde_dir"
    
    # Install SIMDe headers (header-only library)
    sudo mkdir -p "${install_prefix}/include"
    sudo cp -r simde "${install_prefix}/include/"
    
    # Create a pkg-config file for SIMDe
    sudo mkdir -p "${install_prefix}/lib/pkgconfig"
    sudo tee "${install_prefix}/lib/pkgconfig/simde.pc" > /dev/null << EOF
prefix=${install_prefix}
includedir=\${prefix}/include

Name: SIMDe
Description: SIMD Everywhere - portable SIMD intrinsics
Version: ${simde_version}
Cflags: -I\${includedir}
EOF
    
    log_success "SIMDe installed from source to ${install_prefix}"
}

install_obs_dependencies() {
    log_info "Setting up OBS dependencies..."
    
    # The build system will automatically download OBS dependencies
    # But we can try to install system libobs if available and compatible
    
    if apt-cache show libobs-dev >/dev/null 2>&1; then
        log_info "System libobs-dev package is available"
        
        # Check if we can install it without SIMDe issues
        if sudo apt-get install -y libobs-dev 2>/dev/null; then
            log_success "System libobs-dev installed successfully"
        else
            log_warning "System libobs-dev installation failed, will use downloaded OBS SDK"
            # Remove any partially installed libobs packages
            sudo apt-get remove -y libobs-dev libobs0 2>/dev/null || true
        fi
    else
        log_info "System libobs-dev not available, will use downloaded OBS SDK"
    fi
}

install_formatting_tools() {
    log_info "Installing code formatting tools..."
    
    # Install gersemi for CMake formatting
    pip3 install --user gersemi
    
    # Ensure clang-format is available
    if ! command -v clang-format >/dev/null 2>&1; then
        # Try to install a specific version
        if apt-cache show clang-format-19 >/dev/null 2>&1; then
            sudo apt-get install -y clang-format-19
            sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-19 100
        else
            sudo apt-get install -y clang-format
        fi
    fi
    
    log_success "Formatting tools installed"
}

verify_installation() {
    log_info "Verifying installation..."
    
    local -a required_commands=(
        "cmake"
        "ninja" 
        "gcc"
        "pkg-config"
        "clang-format"
        "gersemi"
    )
    
    local all_good=true
    
    for cmd in "${required_commands[@]}"; do
        if command -v "$cmd" >/dev/null 2>&1; then
            log_success "✓ $cmd found"
        else
            log_error "✗ $cmd not found"
            all_good=false
        fi
    done
    
    # Check SIMDe
    if pkg-config --exists simde 2>/dev/null || [[ -d /usr/include/simde || -d /usr/local/include/simde ]]; then
        log_success "✓ SIMDe found"
    else
        log_warning "⚠ SIMDe not found (build system will download OBS SDK instead)"
    fi
    
    if [[ "$all_good" == "true" ]]; then
        log_success "All dependencies verified successfully!"
    else
        log_error "Some dependencies are missing"
        return 1
    fi
}

main() {
    log_info "C64 Stream - Ubuntu Dependencies Installation"
    
    # Check if we're on Ubuntu/Debian
    if ! command -v apt-get >/dev/null 2>&1; then
        log_error "This script is for Ubuntu/Debian systems only"
        exit 1
    fi
    
    # Install components
    install_build_essentials
    install_simde
    install_obs_dependencies  
    install_formatting_tools
    verify_installation
    
    log_success "Ubuntu dependencies installation completed!"
    log_info ""
    log_info "Next steps:"
    log_info "  1. Run local build: ./local-build.sh linux --install-deps"
    log_info "  2. Or test with act: ./local-act.sh --platform ubuntu"
}

main "$@"