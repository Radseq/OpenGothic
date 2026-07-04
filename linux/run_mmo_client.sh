#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")/.."

CLIENT_EXE="${CLIENT_EXE:-./build/opengothic/Gothic2Notr}"
GOTHIC2_DIR="${GOTHIC2_DIR:-/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/}"
SESSION_KEY="${SESSION_KEY:-local-dev-PC_HERO_TEST}"

exec "$CLIENT_EXE" \
  -g "$GOTHIC2_DIR" \
  -g2 \
  -mmo-client-server 127.0.0.1:29777 \
  -mmo-action-session-key "$SESSION_KEY"
