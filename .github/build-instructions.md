# Build Instructions for CI/Copilot Sessions

This document describes how to build the c64stream plugin in a CI environment (GitHub Actions runner or Copilot session).

## Automated Setup Workflow

For the fastest setup in a Copilot session, you can use the automated setup workflow that includes comprehensive caching:

```bash
# Trigger the setup workflow (if you have access to workflow dispatch)
gh workflow run copilot-setup-steps.yml
```

This workflow will:
- Install all prerequisites (zsh, etc.)
- Set up caching for APT packages, ccache, OBS SDK, and build artifacts
- Perform an initial build
- Cache the build directory for incremental builds

## Manual Setup

### Prerequisites

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

## Caching Strategy

The `.github/workflows/copilot-setup-steps.yml` workflow implements comprehensive caching to speed up builds:

### Cached Components

1. **APT Packages Cache**
   - Caches `/var/cache/apt/archives` and `/var/lib/apt/lists`
   - Saves ~2-3 minutes on package installations
   - Key: `${{ runner.os }}-apt-${{ hashFiles('.github/scripts/.Aptfile') }}`

2. **ccache (Compiler Cache)**
   - Caches compiled object files in `.ccache/`
   - Reduces compilation time by 80-90% on incremental builds
   - Key: `${{ runner.os }}-ubuntu-24.04-ccache-x86_64-RelWithDebInfo-${{ github.sha }}`
   - Restores from previous commits if exact match not found

3. **Build Dependencies Cache**
   - Caches downloaded OBS SDK and dependencies in `.deps/`
   - Saves ~5-10 minutes on dependency downloads
   - Key: `${{ runner.os }}-deps-${{ hashFiles('buildspec.json') }}`
   - Only invalidated when buildspec.json changes

4. **OBS Studio SDK Cache**
   - Caches system-installed OBS headers and libraries
   - Saves additional time on repeated builds
   - Key: `${{ runner.os }}-obs-31.1.1`

5. **Build Directory Cache**
   - Caches the entire `build_x86_64/` directory
   - Enables instant incremental builds
   - Key: `${{ runner.os }}-build-${{ github.sha }}`

### Cache Benefits

With all caches warm:
- **First build**: ~5-10 minutes (with dependency downloads)
- **Subsequent builds (same commit)**: ~10-30 seconds (incremental)
- **Builds after code changes**: ~30-60 seconds (recompile changed files only)

### Using Caches in Copilot Sessions

The caches are automatically restored when using the setup workflow. For manual builds:

1. Caches persist across Copilot sessions in the same PR
2. ccache automatically speeds up compilations
3. Dependencies are only re-downloaded when buildspec.json changes
4. Incremental builds reuse the existing build directory

This makes iterative development much faster, especially when fixing compilation errors or making small changes.
