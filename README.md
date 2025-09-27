# Commodore 64 Ultimate Plugin for OBS

This is an [OBS](https://obsproject.com/) source plugin to stream audio and video from a [Commodore 64 Ultimate](https://www.commodore.net/) or an [Ultimate 64](https://ultimate64.com/Ultimate64) to OBS.

For more details, see [C64 Ultimate Data Streams](https://1541u-documentation.readthedocs.io/en/latest/data_streams.html#data-streams).

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
- Git

### Local Build Instructions

#### Ubuntu/Linux
```bash
# Configure
cmake --preset ubuntu-x86_64

# Build
cmake --build build_x86_64

# Optional: Package
cmake --build build_x86_64 --target package
```

#### macOS
```bash
# Configure
cmake --preset macos

# Build 
cmake --build build_macos

# Optional: Package
cmake --build build_macos --target package
```

#### Windows
```powershell
# Configure
cmake --preset windows-x64

# Build
cmake --build build_x64 --config RelWithDebInfo

# Optional: Package  
cmake --build build_x64 --config RelWithDebInfo --target package
```

### Docker Build Script

For convenience, a Docker-based build script is provided that replicates the GitHub Actions workflow:

```bash
# Build Ubuntu version and install to OBS
./build-docker.sh

# Build specific platform
./build-docker.sh ubuntu
./build-docker.sh macos  
./build-docker.sh windows

# Build and auto-install to OBS plugin directory
./build-docker.sh ubuntu --install

# Build and start OBS for testing
./build-docker.sh ubuntu --install --start-obs
```

The Docker script will:
- Use the same build environment as GitHub Actions
- Build the plugin for the specified platform
- Optionally copy the plugin to your OBS plugin directory
- Optionally start OBS for immediate testing

### Manual Installation

#### Linux
Copy the built plugin to your OBS plugin directory:
```bash
mkdir -p ~/.config/obs-studio/plugins/c64u-plugin-for-obs/bin/64bit/
cp build_x86_64/c64u-plugin-for-obs.so ~/.config/obs-studio/plugins/c64u-plugin-for-obs/bin/64bit/
```

#### macOS
```bash
mkdir -p ~/Library/Application\ Support/obs-studio/plugins/
cp -r build_macos/c64u-plugin-for-obs.plugin ~/Library/Application\ Support/obs-studio/plugins/
```

#### Windows
Copy `build_x64/RelWithDebInfo/c64u-plugin-for-obs.dll` to:
- `%ProgramFiles%\obs-studio\obs-plugins\64bit\` (system-wide)
- `%APPDATA%\obs-studio\plugins\c64u-plugin-for-obs\bin\64bit\` (user-specific)

## Usage

1. Add a "C64U Display" source in OBS
2. Configure the C64 Ultimate device IP address 
3. Set video (default: 11000) and audio (default: 11001) ports if needed
4. Click "Start Streaming" to begin receiving video and audio from your C64

The plugin will automatically:
- Send control commands to start/stop streaming on the C64 Ultimate
- Receive and decode video frames (PAL 384x272 or NTSC 384x240)
- Process audio streams (16-bit stereo at ~48kHz)
- Handle both VIC color conversion and audio output to OBS

## Development

### Code Formatting
```bash
# Format C/C++ code
clang-format -i src/*.c tests/*.c

# Format CMake files  
gersemi -i CMakeLists.txt tests/CMakeLists.txt

# Check formatting
./build-aux/run-clang-format --check
./build-aux/run-gersemi --check
```

### Testing
```bash
# Build with tests enabled
cmake --preset ubuntu-x86_64 -DENABLE_TESTS=ON
cmake --build build_x86_64

# Run unit tests
cd build_x86_64 && ctest

# Run mock C64U server for testing
./build_x86_64/tests/c64u_mock_server
```

### Debugging
Enable debug logging in the plugin properties dialog, or check OBS logs for messages prefixed with `[C64U]`.

## License

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.