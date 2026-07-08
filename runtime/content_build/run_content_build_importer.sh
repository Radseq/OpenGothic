#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

mkdir -p runtime/content_build

./build/mmo_cpp_server/mmo_content_build_importer \
  --content-revision-key \
  gothic2-notr-steam-local \
  --game-code \
  gothic2-notr \
  --source-root-label \
  local-gothic2-server-content \
  --output \
  runtime/content_build/parser_snapshot.json \
  --world-name \
  newworld \
  --world-zen \
  /home/radseq/Desktop/OpenGothic/runtime/content_build/vfs_extracted/newworld.zen \
  --world-zen-logical-path \
  newworld.zen \
  --world-zen-sha256 \
  95fe75f8cdf2e9e2678f3dcf5d7c2f0fbb8402a845d4d1aa448f1b85c96f9224 \
  --scripts-dat \
  '/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/_work/Data/Scripts/_compiled/GOTHIC.DAT' \
  --scripts-dat-logical-path \
  _work/data/scripts/_compiled/gothic.dat \
  --scripts-dat-sha256 \
  daefe0f9041c701cd3c484edfbdcf7eec6013d228ef3b2dd24db9aafeafcd6df \
  --dialog-ou \
  '/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/_work/Data/Scripts/Content/Cutscene/Ou.bin' \
  --dialog-ou-logical-path \
  _work/data/scripts/content/cutscene/ou.bin \
  --dialog-ou-sha256 \
  01d63fbb105a9dc25b387917de71f8866bbc87194022aed5eda9d90b69b7a5b6

# Default: generate SQL/report only. Applying a large DAT/OU snapshot to MySQL can take a while.
case "${MMO_CONTENT_BUILD_DB_MODE:-generate}" in
  generate)
tools/import_content_build_snapshot_database.py \
  --snapshot \
  runtime/content_build/parser_snapshot.json \
  --output \
  runtime/content_build/content_build_database_report.json \
  --sql-output \
  runtime/content_build/import_content_build_database.sql
    ;;
  apply)
tools/import_content_build_snapshot_database.py \
  --snapshot \
  runtime/content_build/parser_snapshot.json \
  --output \
  runtime/content_build/content_build_database_report.json \
  --sql-output \
  runtime/content_build/import_content_build_database.sql \
  --url \
  ${MYSQL_URL:?MYSQL_URL is not set}
    ;;
  skip)
    echo "Skipping content-build DB import because MMO_CONTENT_BUILD_DB_MODE=skip"
    ;;
  *)
    echo "Invalid MMO_CONTENT_BUILD_DB_MODE=${MMO_CONTENT_BUILD_DB_MODE}; expected generate, apply or skip" >&2
    exit 2
    ;;
esac
