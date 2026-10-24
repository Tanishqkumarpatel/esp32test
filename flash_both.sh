#!/usr/bin/env bash
set -euo pipefail

PYTHON="/home/mutant/.espressif/tools/python/v6.1/venv/bin/python3"
WORKSPACE="/home/mutant/Desktop/opbots/esp32test"

echo "========================================="
echo " Flashing uart_echo → /dev/ttyUSB0"
echo "========================================="
"$PYTHON" -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-size 2MB --flash-freq 40m \
    0x1000  "$WORKSPACE/uart_echo/build/bootloader/bootloader.bin" \
    0x8000  "$WORKSPACE/uart_echo/build/partition_table/partition-table.bin" \
    0x10000 "$WORKSPACE/uart_echo/build/uart_echo.bin"

echo ""
echo "========================================="
echo " Flashing uart_async_rxtxtasks → /dev/ttyUSB1"
echo "========================================="
"$PYTHON" -m esptool --chip esp32 -p /dev/ttyUSB1 -b 460800 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-size 2MB --flash-freq 40m \
    0x1000  "$WORKSPACE/uart_async_rxtxtasks/build/bootloader/bootloader.bin" \
    0x8000  "$WORKSPACE/uart_async_rxtxtasks/build/partition_table/partition-table.bin" \
    0x10000 "$WORKSPACE/uart_async_rxtxtasks/build/uart_async_rxtxtasks.bin"

echo ""
echo "✅ Both binaries flashed successfully."
