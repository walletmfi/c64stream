# PowerShell script to install C64U plugin to OBS Studio on Windows
# This script creates the necessary directory structure and copies the plugin files

$PluginPath = "$env:APPDATA\obs-studio\plugins\c64u-plugin-for-obs"
$BinPath = "$PluginPath\bin\64bit"
$DataPath = "$PluginPath\data"

# Create directories
Write-Host "Creating plugin directories..."
New-Item -ItemType Directory -Force -Path $BinPath | Out-Null
New-Item -ItemType Directory -Force -Path $DataPath | Out-Null

# Copy plugin DLL
$SourceDll = "build_x64\Debug\c64u-plugin-for-obs.dll"
$TargetDll = "$BinPath\c64u-plugin-for-obs.dll"

if (Test-Path $SourceDll) {
    Write-Host "Copying plugin DLL: $SourceDll -> $TargetDll"
    Copy-Item $SourceDll $TargetDll -Force
} else {
    Write-Error "Plugin DLL not found: $SourceDll"
    Write-Error "Make sure to build the plugin first with: cmake --build build_x64 --config Debug"
    exit 1
}

# Copy data files (locale and images)
if (Test-Path "data") {
    Write-Host "Copying plugin data files..."
    Copy-Item "data\*" $DataPath -Recurse -Force
} else {
    Write-Warning "Data directory not found, plugin may not work correctly"
}

Write-Host "Plugin installed successfully to: $PluginPath"
Write-Host "You can now launch OBS Studio to test the plugin"
