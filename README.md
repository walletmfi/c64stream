# C64 Ultimate OBS Plugin

[OBS Studio](https://obsproject.com/) source plugin for streaming video and audio from Commodore 64 Ultimate devices over a network connection.

## Overview

This plugin implements a native OBS source that receives video and audio streams from C64 Ultimate devices (Commodore 64 Ultimate, Ultimate 64, Ultimate II+, etc.) via the Ultimate's built-in data streaming capability. 

It eliminates the need for capture cards or composite video conversion by connecting directly to the Ultimate's network interface.

**Key Features:**
- Native OBS integration as a standard video source
- Real-time video streaming (PAL 384x272, NTSC 384x240)
- Synchronized audio streaming (16-bit stereo, ~48kHz)
- Network-based connection (UDP/TCP)
- Automatic VIC-II color space conversion
- Zero additional hardware requirements

**Use Cases:**
- Live streaming of C64 gaming sessions
- Recording programming tutorials (6502 assembly, BASIC)
- Documenting hardware modifications and repairs  
- Competitive gaming and speedrunning
- Educational content about 8-bit computing
- Demo scene presentations and releases

## Installation

### Prerequisites
- **OBS Studio** 31.1.1 or later
- **C64 Ultimate device** with network streaming support
- **Network connection** between OBS host and Ultimate device

### Binary Installation

Download platform-specific packages from the [Releases page](../../releases):

**Windows:**
```
1. Close OBS Studio
2. Extract c64u-obs-plugin-windows.zip
3. Copy c64u-plugin-for-obs.dll to C:\Program Files\obs-studio\obs-plugins\64bit\
4. Restart OBS Studio
```

**macOS:**
```
1. Close OBS Studio  
2. Install c64u-obs-plugin-macos.pkg
3. Restart OBS Studio
```

**Linux (Ubuntu/Debian):**
```bash
sudo dpkg -i c64u-obs-plugin-linux.deb
```

### Configuration

1. **Add Source**: In OBS, add a new source of type "C64 Ultimate Stream"
2. **Set IP Address** (optional): 
   - Enter your Ultimate device's specific IP address to enable remote control (start/stop streaming from OBS)
   - Leave as default `0.0.0.0` to receive streams from any C64 Ultimate on the network (manual control via device)
3. **Configure Ports**: Default video port 11000, audio port 11001 (usually no change needed)
4. **Enable Streaming**: On the Ultimate device, enable data streaming via the menu system

### Ultimate Device Setup

Access the Ultimate's menu (typically F2) and navigate to:
```
Network → Data Streaming → Enable
Video Port: 11000
Audio Port: 11001  
```

The plugin will automatically send start/stop commands to the Ultimate device when the OBS source is activated.

## Troubleshooting

**No video stream:**
- If using specific IP: Verify Ultimate device IP address is correct
- If using default (0.0.0.0): Ensure C64 Ultimate is streaming and both devices are on same network
- Ensure both devices are on the same network segment
- Check Ultimate device has data streaming enabled
- Confirm firewall allows UDP traffic on configured ports

**Audio sync issues:**
- Ultimate device audio streaming must be enabled separately
- Check audio port configuration (default 11001)
- Verify OBS audio monitoring settings

**Plugin not available in OBS:**
- Confirm OBS Studio version 31.1.1+
- Verify plugin installed to correct directory
- Check OBS logs for plugin loading errors
- Restart OBS completely after installation

**Connection timeouts:**
- Network latency should be <100ms for optimal performance
- Check for network congestion or WiFi interference
- Consider wired Ethernet connection for stability

---

# Developer Documentation

## Technical Implementation

This plugin implements the [C64 Ultimate Data Streams specification](https://1541u-documentation.readthedocs.io/en/latest/data_streams.html#data_streams) to receive video and audio streams from Ultimate devices via UDP/TCP network protocols.

## Technical Specifications

**Supported Platforms:**
- Windows 10/11 (x64)
- macOS 10.15+ (Intel/Apple Silicon)  
- Linux (Ubuntu 20.04+, other distributions via manual build)

**Video Formats:**
- PAL: 384x272 @ 50Hz
- NTSC: 384x240 @ 60Hz  
- Color space: VIC-II palette with automatic RGB conversion

**Audio Format:**
- 16-bit stereo PCM
- Sample rate: ~48kHz (device dependent)
- Low-latency streaming

**Network Requirements:**
- UDP/TCP connectivity to Ultimate device
- Bandwidth: ~2-5 Mbps (uncompressed video stream)
- Latency: <100ms on local network

---

# Developer Information

## Technical Overview

This plugin implements the [C64 Ultimate Data Streams specification](https://1541u-documentation.readthedocs.io/en/latest/data_streams.html#data_streams) to receive video and audio streams over UDP/TCP from Commodore 64 Ultimate devices.

**Key Features:**
- **Live C64 Video Streaming**: Captures real-time video output using C64U video stream protocol
- **Audio Support**: Streams C64 audio alongside video for complete experience  
- **Network-based**: No additional hardware required - connects over local network
- **OBS Integration**: Native OBS Studio source plugin with proper lifecycle management

## Building the Plugin

### Prerequisites

#### Linux (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
                        libobs-dev libobs0t64 zsh clang-format gersemi
```

#### macOS
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install dependencies via Homebrew  
brew install cmake ninja obs
```

#### Windows
- Visual Studio 2022 (Community, Professional, or Enterprise)
- CMake 3.28+
- PowerShell 7+

### Build Types

This project supports three distinct build configurations optimized for different use cases:

#### 1. **Local Development Build**
**Purpose**: Complete development environment with testing and debugging tools.

**Components built:**
- Plugin binary (`c64u-plugin-for-obs.so/.dll/.dylib`)
- Unit tests (`test_vic_colors`) for VIC-II color conversion
- Mock C64U server (`c64u_mock_server`) for protocol testing
- Integration tests (`test_integration`) using OBS libraries

**Ubuntu/Linux:**
```bash
# Configure with all local development features
cmake --preset ubuntu-x86_64

# Build everything (plugin + all tests)
cmake --build build_x86_64

# Run all tests including integration tests
cd build_x86_64 && ctest -V

# Test with mock server
./c64u_mock_server &
./test_integration
```

**macOS:**
```bash
cmake --preset macos
cmake --build build_macos
cd build_macos && ctest -V
```

**Windows:**
```powershell
cmake --preset windows-x64  
cmake --build build_x64 --config RelWithDebInfo
cd build_x64 && ctest -C RelWithDebInfo -V
```

#### 2. **CI Simulation Build**
**Purpose**: Test production build configuration locally without development overhead.

**Components built:**
- Plugin binary only (4/4 targets)
- No tests (faster build times)
- No mock server  
- No debugging tools

**How to run**:
```bash
# Simulate CI environment locally
CI=1 GITHUB_ACTIONS=1 cmake --preset ubuntu-x86_64
CI=1 GITHUB_ACTIONS=1 cmake --build build_x86_64

# Verify no tests were compiled
find build_x86_64 -name "*test*" -executable -type f
# Should return nothing
```

**Verification**: Build output should show:
```
-- Test configuration:
--   CI Build: ON
--   Mock Server: OFF  
--   Integration Tests: OFF
[4/4] Linking C shared module c64u-plugin-for-obs.so
```

#### 3. **Production CI Build**
**Purpose**: Automated release pipeline with code signing and packaging.

**Process:**
- Multi-platform compilation (Ubuntu, macOS, Windows)
- Code signing and notarization (macOS)
- Package generation (.deb, .pkg, .zip distributions)
- Binary artifact creation
- Code format validation
- No test compilation

**Triggers:** Repository pushes, pull requests, tagged releases

### Build Configuration Details

**Environment Detection Logic**:
```cmake
# In tests/CMakeLists.txt
set(IS_CI_BUILD FALSE)
if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS})
    set(IS_CI_BUILD TRUE)
    message(STATUS "CI Build: ON")
else()
    message(STATUS "CI Build: OFF - Local development mode")  
endif()
```

**Platform Scripts Behavior**:
- **Local**: `.github/scripts/build-*` run ctest after building
- **CI**: Same scripts detect CI environment and skip ctest entirely
- **Error Prevention**: Avoids "CMake Error: Unknown argument" when no tests exist

### Development Workflow

#### Quick Development Cycle
```bash
# 1. Clean slate
rm -rf build_x86_64

# 2. Configure for development  
cmake --preset ubuntu-x86_64

# 3. Build and test
cmake --build build_x86_64
cd build_x86_64 && ctest

# 4. Install to local OBS (optional)
cp c64u-plugin-for-obs.so ~/.config/obs-studio/plugins/c64u-plugin-for-obs/bin/64bit/
```

#### Code Formatting (Required)
```bash  
# Check formatting
./build-aux/run-clang-format --check    # C/C++ code
./build-aux/run-gersemi --check         # CMake files

# Auto-fix formatting  
./build-aux/run-clang-format            # Fix C/C++ code
./build-aux/run-gersemi                 # Fix CMake files
```

#### Testing Strategies

**Unit Testing**:
```bash
# Test VIC color conversion algorithms
cd build_x86_64 && ./test_vic_colors
```

**Integration Testing** (requires OBS):
```bash
# Test with mock C64U server
cd build_x86_64
./c64u_mock_server --port 1234 &
./test_integration --server-port 1234
```

**Local CI Validation**:
```bash
# Test what will run in CI (using act)
act -j ubuntu-build -P ubuntu-24.04=catthehacker/ubuntu:act-24.04 -s GITHUB_TOKEN="$(gh auth token)"
```

### Project Structure

```
c64u-obs/
├── src/
│   ├── plugin-main.c          # Main OBS plugin implementation
│   ├── plugin-support.h       # Plugin utilities and logging
│   └── plugin-support.c.in    # Template for plugin support
├── tests/
│   ├── CMakeLists.txt          # Test build configuration with CI detection
│   ├── test_vic_colors.c       # Unit tests for VIC color conversion  
│   ├── c64u_mock_server.c      # Mock C64U device for testing
│   └── test_integration.c      # Integration tests with real OBS
├── .github/
│   ├── workflows/              # CI/CD automation
│   ├── scripts/                # Platform-specific build scripts
│   └── actions/                # Reusable workflow components
├── cmake/                      # Build system configuration
├── data/locale/                # Localization files
└── doc/                        # Technical documentation
```

### Common Issues & Solutions

**Build Issues**:
- **"LibObs not found"**: Dependencies auto-download; check internet connection
- **"clang-format version too old"**: Need 19.1.1+; CI has correct version
- **"zsh not found"**: `sudo apt-get install zsh` (Linux only)

**CI Issues**:
- **Windows "Unknown argument" error**: Fixed by skipping ctest in CI builds
- **macOS "executable not found"**: Fixed by disabling test compilation in CI
- **Format check failures**: Run `./build-aux/run-clang-format` locally first

**Development Issues**:  
- **Plugin not loading**: Check OBS logs, verify plugin path
- **No C64U connection**: Verify network configuration; check IP address if using specific device, firewall settings
- **Build performance**: Use `ccache` for faster rebuilds

### Contributing

1. **Fork** the repository
2. **Create** a feature branch
3. **Format** code: `./build-aux/run-clang-format && ./build-aux/run-gersemi`  
4. **Test** locally: Full build + ctest + act validation
5. **Submit** pull request

### Resources

- **C64U Stream Spec**: [`doc/c64u-stream-spec.md`](doc/c64u-stream-spec.md)
- **Official Documentation**: [C64 Ultimate Data Streams](https://1541u-documentation.readthedocs.io/en/latest/data_streams.html#data_streams)
- **OBS Plugin Development**: [OBS Studio Plugin Guide](https://obsproject.com/wiki/Plugin-Development)
- **Build System**: Based on [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate)

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.