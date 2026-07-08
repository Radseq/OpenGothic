#!/usr/bin/env bash
# Shared Linux/Kubuntu MMO dev defaults.
#
# Usage from OpenGothic repo:
#   source ./mmo_env.sh
#   echo "$MYSQL_URL"
#
# Override before sourcing if needed, for example:
#   export GOTHIC_MMO_MYSQL_HOST=127.0.0.1
#   source ./mmo_env.sh

if [[ -z "${GOTHIC_MMO_MYSQL_HOST:-}" ]]; then
  export GOTHIC_MMO_MYSQL_HOST="192.168.195.94"
fi

if [[ -z "${GOTHIC_MMO_MYSQL_PORT:-}" ]]; then
  export GOTHIC_MMO_MYSQL_PORT="3306"
fi

if [[ -z "${GOTHIC_MMO_MYSQL_USER:-}" ]]; then
  export GOTHIC_MMO_MYSQL_USER="gothic"
fi

if [[ -z "${GOTHIC_MMO_MYSQL_PASSWORD:-}" ]]; then
  export GOTHIC_MMO_MYSQL_PASSWORD="gothic_dev_password"
fi

if [[ -z "${GOTHIC_MMO_MYSQL_DATABASE:-}" ]]; then
  export GOTHIC_MMO_MYSQL_DATABASE="gothic_mmo_ch1_clean"
fi

if [[ -z "${GOTHIC_MMO_MYSQL_URL:-}" ]]; then
  export GOTHIC_MMO_MYSQL_URL="mysql://${GOTHIC_MMO_MYSQL_USER}:${GOTHIC_MMO_MYSQL_PASSWORD}@${GOTHIC_MMO_MYSQL_HOST}:${GOTHIC_MMO_MYSQL_PORT}/${GOTHIC_MMO_MYSQL_DATABASE}"
fi

if [[ -z "${MYSQL_URL:-}" ]]; then
  export MYSQL_URL="${GOTHIC_MMO_MYSQL_URL}"
fi

if [[ -z "${MYSQL_EXE:-}" ]]; then
  if command -v mysql >/dev/null 2>&1; then
    export MYSQL_EXE="$(command -v mysql)"
  fi
fi

MYSQL_URL_REDACTED="mysql://${GOTHIC_MMO_MYSQL_USER}:***@${GOTHIC_MMO_MYSQL_HOST}:${GOTHIC_MMO_MYSQL_PORT}/${GOTHIC_MMO_MYSQL_DATABASE}"
echo "MYSQL_URL=${MYSQL_URL_REDACTED}"

if [[ -n "${MYSQL_EXE:-}" ]]; then
  echo "MYSQL_EXE=${MYSQL_EXE}"
else
  echo "MYSQL_EXE not found in PATH"
fi
