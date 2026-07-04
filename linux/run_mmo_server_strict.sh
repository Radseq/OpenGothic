#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")/.."
. ./linux/mmo_env.sh

SERVER_EXE="${SERVER_EXE:-./build/mmo_cpp_server/mmo_udp_server}"
SESSION_KEY="${SESSION_KEY:-local-dev-PC_HERO_TEST}"
CHARACTER_KEY="${CHARACTER_KEY:-PC_HERO}"

exec "$SERVER_EXE" \
  --bind 127.0.0.1:29777 \
  --mysql-url "$MYSQL_URL" \
  --session-key "$SESSION_KEY" \
  --character-key "$CHARACTER_KEY" \
  --require-db-save-checkpoint-restore
