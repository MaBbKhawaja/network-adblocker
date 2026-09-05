#!/usr/bin/env bash
# Build / flash / watch the netmon firmware.   usage: ./build.sh [compile|upload|monitor|all]
set -euo pipefail
cd "$(dirname "$0")"
PORT="${PORT:-/dev/cu.usbmodem2101}"
# ESP32-S3 N16R8: 16 MB flash, 8 MB octal PSRAM (holds the blocklist), built-in USB-Serial/JTAG.
# Partition: 3 MB app + 9.9 MB FFat filesystem (caches the blocklist across reboots).
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
case "${1:-all}" in
  compile) arduino-cli compile --fqbn "$FQBN" --warnings default netmon ;;
  upload)  arduino-cli upload  --fqbn "$FQBN" --port "$PORT" netmon ;;
  monitor) arduino-cli monitor --port "$PORT" --config baudrate=115200 ;;
  all)     arduino-cli compile --fqbn "$FQBN" netmon && arduino-cli upload --fqbn "$FQBN" --port "$PORT" netmon ;;
  *) echo "usage: $0 [compile|upload|monitor|all]" >&2; exit 1 ;;
esac
