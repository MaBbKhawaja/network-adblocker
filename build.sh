#!/usr/bin/env bash
# Build / flash / watch the netmon firmware.   usage: [NODE=2] ./build.sh [compile|upload|monitor|all|secrets|ota]
#   secrets  regenerate netmon/secrets.h from ../.env (WIFI_SSID, WIFI_PASSWORD, NETMON_IP/GATEWAY/NETMASK, NTFY_TOPIC)
set -euo pipefail
cd "$(dirname "$0")"
envget() { grep -E "^$1=" .env 2>/dev/null | head -1 | cut -d= -f2- | sed -e 's/^"//' -e 's/"$//'; }
# NODE=2 ./build.sh ...  builds for the second board: NETMON2_HOST / NETMON2_IP / NETMON2_SERIAL_PORT in .env
# (anything not set for node 2 falls back to the NETMON_* value).
NODE="${NODE:-}"
nodeget() { local v; [[ -n "$NODE" ]] && v="$(envget "NETMON${NODE}_$1")"; [[ -n "$v" ]] && echo "$v" || envget "NETMON_$1"; }
PORT="${PORT:-$(nodeget SERIAL_PORT)}"; PORT="${PORT:-/dev/cu.usbmodem2101}"
# ESP32-S3 N16R8: 16 MB flash, 8 MB octal PSRAM (holds the blocklist), built-in USB-Serial/JTAG.
# Partition: 3 MB app + 9.9 MB FFat filesystem (caches the blocklist across reboots).
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
case "${1:-all}" in
  compile) [[ -f .env ]] && "$0" secrets >/dev/null; python3 extension/pack.py && arduino-cli compile --fqbn "$FQBN" --warnings default netmon ;;
  upload)  arduino-cli upload  --fqbn "$FQBN" --port "$PORT" netmon ;;
  monitor) arduino-cli monitor --port "$PORT" --config baudrate=115200 ;;
  ota)     # update a running board over Wi-Fi: [NODE=2] ./build.sh ota   (needs OTA_PASSWORD in .env and a board already running OTA-capable firmware)
           [[ -f .env ]] && "$0" secrets >/dev/null; python3 extension/pack.py
           arduino-cli compile --fqbn "$FQBN" --output-dir build/netmon netmon
           IP="$(nodeget IP)"; ESPOTA="$(ls -d "$HOME"/Library/Arduino15/packages/esp32/hardware/esp32/*/tools/espota.py | tail -1)"
           echo "uploading to $IP over Wi-Fi"; python3 "$ESPOTA" -r -i "$IP" -p 3232 "--auth=$(envget OTA_PASSWORD)" -f build/netmon/netmon.ino.bin ;;
  secrets) [[ -f .env ]] || { echo "no .env — copy .env.example to .env first" >&2; exit 1; }
           printf '#pragma once\n// generated from ../.env by ./build.sh secrets%s — edit .env, not this file\n' "${NODE:+ (NODE=$NODE)}" > netmon/secrets.h
           printf '#define WIFI_SSID   "%s"\n#define WIFI_PASS   "%s"\n' "$(envget WIFI_SSID)" "$(envget WIFI_PASSWORD)" >> netmon/secrets.h
           printf '#define HOSTNAME_STR "%s"   // -> http://<this>.local\n' "$(nodeget HOST | sed 's/\.local$//')" >> netmon/secrets.h
           printf '#define STATIC_IP   "%s"     // "" = DHCP\n#define STATIC_GW   "%s"\n#define STATIC_MASK "%s"\n' "$(nodeget IP)" "$(envget NETMON_GATEWAY)" "$(envget NETMON_NETMASK)" >> netmon/secrets.h
           printf '#define NTFY_TOPIC  "%s"                 // optional ntfy.sh topic for phone alerts\n' "$(envget NTFY_TOPIC)" >> netmon/secrets.h
           printf '#define OTA_PASS    "%s"     // over-the-air update password ("" = OTA off)\n#define PEER_BOARDS "%s"   // other boards, comma-separated IPs\n' "$(envget OTA_PASSWORD)" "$(nodeget PEERS)" >> netmon/secrets.h
           echo "netmon/secrets.h written from .env${NODE:+ for node $NODE}: $(grep -oE "HOSTNAME_STR \"[^\"]*\"|STATIC_IP +\"[^\"]*\"" netmon/secrets.h | tr "\n" " ")" ;;
  all)     [[ -f .env ]] && "$0" secrets >/dev/null; python3 extension/pack.py && arduino-cli compile --fqbn "$FQBN" netmon && arduino-cli upload --fqbn "$FQBN" --port "$PORT" netmon ;;
  *) echo "usage: [NODE=2] $0 [compile|upload|monitor|all|secrets|ota]" >&2; exit 1 ;;
esac
