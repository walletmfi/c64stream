#!/bin/bash

# Simple VICE debugger launcher
# This starts VICE with the built-in monitor for debugging

echo "Starting VICE with debugger..."
echo "================================"
echo
echo "Key addresses from listing:"
echo "  \$080e = start (program entry)"
echo "  \$0831 = main_loop"
echo "  \$0831 = wait_raster"
echo "  \$083f = fill_screen"
echo "  \$0869 = current_digit variable"
echo
echo "VICE Monitor Commands:"
echo "  r          - Show registers"
echo "  d \$080e    - Disassemble from start"
echo "  m \$0869    - Show current_digit variable"
echo "  m \$d012    - Show raster line register"
echo "  break \$080e - Set breakpoint at start"
echo "  break \$0831 - Set breakpoint at main loop"
echo "  n          - Next instruction"
echo "  s          - Step into"
echo "  c          - Continue"
echo "  x          - Exit monitor"
echo
echo "Starting in 3 seconds..."
sleep 3

# Start VICE with monitor
x64sc -autostart digit-cycle.prg -autostartprgmode 1 -remotemonitor +confirmexit
