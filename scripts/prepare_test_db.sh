#!/usr/bin/env bash
# Prepare or refresh the persistent Firebird database used by fbpp tests.
# The script relies on environment variables shared with CI (see ci-linux-gcc.yml):
#   FIREBIRD_HOST, FIREBIRD_PORT, FIREBIRD_USER, FIREBIRD_PASSWORD,
#   FIREBIRD_PERSISTENT_DB_PATH, FIREBIRD_CHARSET

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCHEMA_FILE="${ROOT_DIR}/tests/test_data/query_generator_schema.sql"
CONFIG_FILE="${CONFIG_FILE:-${ROOT_DIR}/config/test_config.json}"

if [[ ! -f "${SCHEMA_FILE}" ]]; then
    echo "✗ Schema file not found: ${SCHEMA_FILE}" >&2
    exit 1
fi

read_config_value() {
    local key_path="$1"
    if [[ ! -f "${CONFIG_FILE}" ]]; then
        return
    fi
    python3 - "$CONFIG_FILE" "$key_path" <<'PY'
import json
import sys

config_path = sys.argv[1]
path = sys.argv[2].split('.')

try:
    with open(config_path, 'r', encoding='utf-8') as fh:
        data = json.load(fh)
except Exception:
    sys.exit(0)

value = data
for key in path:
    if isinstance(value, dict) and key in value:
        value = value[key]
    else:
        value = ""
        break

if isinstance(value, (str, int, float)):
    print(value)
PY
}

CFG_HOST="$(read_config_value 'tests.persistent_db.server')"
CFG_PATH="$(read_config_value 'tests.persistent_db.path')"
CFG_USER="$(read_config_value 'tests.persistent_db.user')"
CFG_PASS="$(read_config_value 'tests.persistent_db.password')"
CFG_CHARSET="$(read_config_value 'tests.persistent_db.charset')"

HOST="${FIREBIRD_HOST:-${CFG_HOST:-localhost}}"
PORT="${FIREBIRD_PORT:-3050}"
USER="${FIREBIRD_USER:-${CFG_USER:-SYSDBA}}"
PASSWORD="${FIREBIRD_PASSWORD:-${CFG_PASS:-masterkey}}"
CHARSET="${FIREBIRD_CHARSET:-${CFG_CHARSET:-UTF8}}"
DB_PATH="${FIREBIRD_PERSISTENT_DB_PATH:-${CFG_PATH:-/tmp/testdb.fdb}}"

if [[ -z "${HOST}" || -z "${DB_PATH}" ]]; then
    echo "✗ FIREBIRD_HOST and FIREBIRD_PERSISTENT_DB_PATH must be set" >&2
    exit 1
fi

ISQL_BIN="${ISQL_BIN:-}"
if [[ -z "${ISQL_BIN}" ]]; then
    if command -v isql >/dev/null 2>&1; then
        ISQL_BIN="$(command -v isql)"
    elif [[ -x "/opt/firebird/bin/isql" ]]; then
        ISQL_BIN="/opt/firebird/bin/isql"
    else
        echo "✗ isql utility is required but not found (searched PATH and /opt/firebird/bin)" >&2
        exit 1
    fi
fi

if [[ -z "${FIREBIRD_MSG_PATH:-}" ]]; then
    if [[ -f "/opt/firebird/lib/firebird.msg" ]]; then
        export FIREBIRD_MSG_PATH="/opt/firebird/lib"
    elif [[ -f "/usr/lib/firebird.msg" ]]; then
        export FIREBIRD_MSG_PATH="/usr/lib"
    fi
fi

if [[ -n "${PORT}" ]]; then
    DSN="${HOST}/${PORT}:${DB_PATH}"
else
    DSN="${HOST}:${DB_PATH}"
fi

echo "→ Ensuring database exists at ${DSN} (user=${USER})"
if ! echo "quit;" | "${ISQL_BIN}" -q -user "${USER}" -password "${PASSWORD}" "${DSN}" >/dev/null 2>&1; then
    echo "   Database not reachable, attempting to create..."
    "${ISQL_BIN}" -q -user "${USER}" -password "${PASSWORD}" <<SQL
CREATE DATABASE '${DSN}'
  USER '${USER}' PASSWORD '${PASSWORD}'
  PAGE_SIZE 8192
  DEFAULT CHARACTER SET ${CHARSET};
SQL
fi

echo "→ Applying schema from ${SCHEMA_FILE}"
"${ISQL_BIN}" -q -user "${USER}" -password "${PASSWORD}" "${DSN}" -i "${SCHEMA_FILE}"

echo "✓ Test database is ready"
