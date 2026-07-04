#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")/.."
. ./linux/mmo_env.sh

SQLITE_DB="${SQLITE_DB:-runtime/g2notr_ch1_pre_xardas.sqlite}"

if [ ! -f "$SQLITE_DB" ]; then
  echo "Missing \"$SQLITE_DB\"." >&2
  echo "Create it with the client capture first:" >&2
  echo "./build/opengothic/Gothic2Notr -g /path/to/Gothic\\ II -g2 -mmo-sqlite \"$SQLITE_DB\" -mmo-sqlite-capture-pre-start-exit" >&2
  exit 1
fi

python3 tools/run_mmo_step55_clean_mysql_from_pre_xardas.py \
  --sqlite "$SQLITE_DB" \
  --mysql-url "$MYSQL_URL" \
  --i-understand-this-drops-database
