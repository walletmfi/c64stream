# Build Instructions for CI/Copilot Sessions

This document describes how to build the c64stream plugin in a CI environment (GitHub Actions runner or Copilot session).

## Prerequisites

The following tools must be installed before building:

```bash
# Install zsh (required for build scripts)
sudo apt-get update -qq && sudo apt-get install -y zsh

# Fix any interrupted package installations
sudo dpkg --configure -a
```

## Quick Build

Use the GitHub build script which handles all dependencies automatically:

```bash
cd /home/runner/work/c64stream/c64stream
.github/scripts/build-ubuntu --target ubuntu-x86_64 --config RelWithDebInfo
```

This script will:
1. Check and install all required dependencies (ccache, OBS Studio SDK, Qt6, etc.)
2. Configure CMake with the correct preset
3. Build the plugin using Ninja
4. Generate the `c64stream.so` plugin file in `build_x86_64/`

## Incremental Build

After making code changes, you can do an incremental build:

```bash
cd /home/runner/work/c64stream/c64stream/build_x86_64
cmake --build .
```

## Verify Build Success

Check that the plugin was built successfully:

```bash
if [ -f "/home/runner/work/c64stream/c64stream/build_x86_64/c64stream.so" ]; then
    echo "✅ Build successful"
    ls -lh /home/runner/work/c64stream/c64stream/build_x86_64/c64stream.so
else
    echo "❌ Build failed"
    exit 1
fi
```

## Build Workflow for Development

When making code changes in a Copilot session, follow this loop:

1. **Make changes** to source files
2. **Format code**: `clang-format -style=file -i <changed-files>`
3. **Build**: 
   - First time: `.github/scripts/build-ubuntu --target ubuntu-x86_64 --config RelWithDebInfo`
   - Incremental: `cd build_x86_64 && cmake --build .`
4. **Check logs** for any compilation errors
5. **Fix issues** and repeat from step 2
6. **Verify success** before completing work

## Common Issues

### Package Manager Locks

If you encounter dpkg/apt-get lock errors:

```bash
# Kill any stuck processes
sudo killall -9 apt apt-get dpkg 2>/dev/null

# Remove lock files
sudo rm -f /var/lib/dpkg/lock* /var/lib/apt/lists/lock /var/cache/apt/archives/lock 2>/dev/null

# Reconfigure packages
sudo dpkg --configure -a
```

### Missing Dependencies

The build script automatically downloads:
- OBS Studio SDK (31.1.1)
- obs-deps (required for OBS development)
- Qt6 libraries (if needed)
- ccache (for faster rebuilds)

Dependencies are cached in the workspace and reused across builds.

### Build Directories

- `build_x86_64/` - Main build output directory
- `.ccache/` - ccache cache directory (if used)
- `.deps/` - Downloaded dependencies cache (created by some build systems)

## Performance Tips

- **Use ccache**: The build script automatically enables ccache for faster rebuilds
- **Incremental builds**: After the first full build, use `cmake --build build_x86_64` for faster rebuilds
- **Parallel builds**: Ninja automatically uses all available CPU cores

## Build Time Estimates

- **First build** (with dependency downloads): ~5-10 minutes
- **Clean build** (dependencies cached): ~2-3 minutes  
- **Incremental build** (few files changed): ~10-30 seconds

## Debugging Build Issues

If the build fails:

1. Check the last ~50 lines of output for the actual error
2. Look for:
   - Compilation errors (syntax, type mismatches, undefined symbols)
   - Linker errors (missing libraries, undefined references)
   - CMake configuration errors (missing dependencies)
3. Fix the identified issues
4. Rebuild incrementally to verify the fix
5. Repeat until all errors are resolved

## Integration with Copilot Workflow

When working on changes:

1. Complete your code changes
2. Run the build following the "Build Workflow for Development" section above
3. **Do not announce completion until the build passes without errors**
4. Only commit and push after verifying a successful build
5. Include build verification in your progress reports

Remember: A successful build is mandatory before completing any Copilot session.
