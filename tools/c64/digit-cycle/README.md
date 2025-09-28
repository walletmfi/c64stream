# C64 Digit Cycle Demo

This is a simple C64 assembly program that cycles through digits 0-9 on the screen using raster interrupts.

## Files

- `digit-cycle.asm` - The main assembly source file
- `build.sh` - Simple build script (compile only)
- `build-and-run.sh` - Build and run script (compiles and launches in VICE)

## Building

To just build the program:
```bash
./build.sh
```

This will create `digit-cycle.prg` which you can load into any C64 emulator.

## Building and Running

To build and automatically run in VICE emulator:
```bash
./build-and-run.sh
```

This will:
1. Build the program using 64tass
2. Check if VICE is installed (and offer to install it if not)
3. Launch the program in the VICE C64 emulator

## Requirements

- `64tass` assembler (install with: `sudo apt install 64tass`)
- `vice` emulator (install with: `sudo apt install vice`) - optional, only needed for auto-run

## What the Program Does

The program waits for raster line 255. Then it:

1. Fills the entire screen with the current digit (0-9)
2. Increments to the next digit (wrapping from 9 back to 0)

This creates a visual effect where the screen cycles through digits continuously.

## Technical Details

- Uses BASIC stub starting at $0801 to auto-run with SYS 2064
- Machine code starts at $0810
- Converts digits to PETSCII for display
- Fills screen memory at $0400-$07E7
