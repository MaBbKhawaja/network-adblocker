#!/usr/bin/env bash
# Bring up (or update) the Invidious stack.   usage: ./setup.sh [--new-keys] [--update] [--daily-restart]
set -euo pipefail
cd "$(dirname "$0")"
rand() { LC_ALL=C tr -dc 'A-Za-z0-9' </dev/urandom | head -c "$1"; }

# Invidious wants a restart at least daily. macOS: a launchd job at 04:30 (see the .plist). Linux: cron.
if [[ "${1:-}" == "--daily-restart" ]]; then
  if [[ "$(uname)" == "Darwin" ]]; then
    dst="$HOME/Library/LaunchAgents/com.network-adblocker.invidious-restart.plist"
    mkdir -p "$(dirname "$dst")"
    sed "s#/Users/mabb/Projects/network-adblocker/invidious#$(pwd)#" com.network-adblocker.invidious-restart.plist > "$dst"
    launchctl bootout "gui/$(id -u)/com.network-adblocker.invidious-restart" 2>/dev/null || true
    launchctl bootstrap "gui/$(id -u)" "$dst" && echo "installed: Invidious restarts daily at 04:30 ($dst)"
  else
    echo "add to crontab:  30 4 * * * docker compose -f $(pwd)/docker-compose.yml restart invidious"
  fi
  exit 0
fi

if [[ ! -f .env || "${1:-}" == "--new-keys" ]]; then
  printf 'HMAC_KEY=%s\nCOMPANION_KEY=%s\nINVIDIOUS_TAG=%s\nCOMPANION_TAG=latest\nPORT=80\n' "$(rand 20)" "$(rand 16)" latest > .env
  echo "wrote new .env"
fi
# The Postgres container needs two files from the upstream repo (schema + init script).
if [[ ! -d upstream/config/sql ]]; then
  rm -rf upstream
  git clone -q --depth 1 --filter=blob:none --sparse https://github.com/iv-org/invidious.git upstream
  git -C upstream sparse-checkout set config/sql docker >/dev/null
fi
[[ "${1:-}" == "--update" ]] && { git -C upstream pull -q --ff-only || true; docker compose pull; }
docker compose up -d
docker compose ps
echo "Invidious: http://$(hostname -s).local:$(grep -E '^PORT=' .env | cut -d= -f2)/"
