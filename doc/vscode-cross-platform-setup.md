# Cross-Platform VS Code F5 Debugging Setup - Summary

## What was Fixed

The issue you encountered was caused by VS Code task conflicts between workspace-level tasks and `.vscode/tasks.json` tasks. When pressing F5 on Linux, VS Code was trying to run both Windows and Linux tasks simultaneously, causing PowerShell commands to fail on Linux.

## Changes Made

### 1. Updated `.vscode/tasks.json`
- **Renamed Linux tasks** to avoid conflicts:
  - `"Build Debug Plugin (Linux)"` → `"VSCode: Build Debug Plugin (Linux)"`
  - `"Install Plugin to OBS (Linux)"` → `"VSCode: Install Plugin to OBS (Linux)"`
- **Added proper platform filtering** and error handling
- **Improved task dependencies** with `dependsOrder: "sequence"`
- **Added instanceLimit** to prevent multiple concurrent builds

### 2. Updated `.vscode/launch.json`
- **Updated Linux debug configuration** to use renamed task: `"VSCode: Build Debug Plugin (Linux)"`
- **Added better documentation** explaining F5 behavior
- **Maintained Windows compatibility** with `cppvsdbg` debugger

### 3. Enhanced `.vscode/settings.json`
- **Removed hardcoded Linux build directory** to make CMake more platform-agnostic
- **Kept existing cross-platform formatting settings**

## How F5 Works Now

### On Linux (Kubuntu 24.04):
1. **F5** automatically selects "Debug OBS with C64U Plugin (Linux)"
2. **Runs preLaunchTask**: `"VSCode: Build Debug Plugin (Linux)"`
3. **Task dependency chain**:
   - `VSCode: Install Plugin to OBS (Linux)` (copies plugin to `~/.config/obs-studio/plugins/`)
   - `cmake --build build_x86_64` (builds the plugin)
4. **Launches OBS** with GDB debugging: `/usr/bin/obs --verbose`

### On Windows 11:
1. **F5** automatically selects "Debug OBS with C64U Plugin (Windows)"
2. **Runs preLaunchTask**: `"Build Debug Plugin (Windows)"`
3. **Task dependency chain**:
   - `Install Plugin to OBS (Windows)` (runs PowerShell install script)
   - `cmake --build build_x64 --config Debug` (builds the plugin)
4. **Launches OBS** with Visual Studio debugging: `C:/Program Files/obs-studio/bin/64bit/obs64.exe --verbose`

## Verification

✅ **Plugin loads successfully**: `info: [C64U] Loading C64U plugin (version 1.0.0)`
✅ **Plugin connects to C64U device**: Video @ 50fps, Audio @ 48kHz
✅ **Cross-platform task isolation**: Linux tasks no longer trigger Windows PowerShell commands
✅ **OBS starts correctly**: No plugin loading errors

## Usage Instructions

### Linux Development:
```bash
# Press F5 in VS Code - it will automatically:
# 1. Build the plugin (cmake --build build_x86_64)
# 2. Install to OBS user directory
# 3. Launch OBS with GDB debugging

# Manual testing:
obs --verbose --collection "Test" --profile "Test"
```

### Windows Development:
```powershell
# Press F5 in VS Code - it will automatically:
# 1. Run build-aux/install-plugin-windows.ps1
# 2. Build the plugin (cmake --build build_x64 --config Debug)
# 3. Launch OBS with Visual Studio debugging
```

## What to Do If Issues Persist

If you still experience task conflicts:

1. **Reload VS Code window**: `Ctrl+Shift+P` → "Developer: Reload Window"
2. **Clear task cache**: Close VS Code completely and restart
3. **Check task selection**: `Ctrl+Shift+P` → "Tasks: Run Task" to verify correct tasks are available
4. **Verify plugin installation**: Check that the .so file timestamp matches recent build times

The configuration is now properly isolated between platforms and should work seamlessly on both Windows 11 and Kubuntu 24.04.
