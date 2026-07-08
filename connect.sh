#!/usr/bin/env bash
set -Eeuo pipefail

# Fast Linux/Kubuntu Bash launcher equivalent of the Windows CMD/PowerShell script.
# Generates snapshots from OpenGothic game/server/docs/llm/tools and exports MySQL schema.
#
# Usage examples:
#   ./connect_opengothic_split_tools_updated_linux_fast.sh
#   ./connect_opengothic_split_tools_updated_linux_fast.sh --code-server-output wynik_code_server.txt --code-client-output wynik_code_client.txt --llm-output wynik_llm.txt
#   MAX_BYTES=2000000 MYSQL_HOST=127.0.0.1 MYSQL_USER=gothic MYSQL_PWD=secret ./connect_opengothic_split_tools_updated_linux_fast.sh

CODE_OUTPUT_LEGACY=""
CODE_SERVER_OUTPUT="wynik_code_server.txt"
CODE_CLIENT_OUTPUT="wynik_code_client.txt"
LLM_OUTPUT="wynik_llm.txt"
TOOLS_OUTPUT="wynik_tools.txt"
SCHEMA_OUTPUT="${MYSQL_SCHEMA_OUTPUT:-wynik_gothic_mmo_ch1_clean_schema.txt}"

usage() {
    cat <<'USAGE'
Usage:
  connect_opengothic_split_tools_updated_linux_fast.sh [options]

Options:
  --code-server-output PATH
                           Output file for server C/C++ source snapshot. Default: wynik_code_server.txt
  --code-client-output PATH
                           Output file for client C/C++ source snapshot. Default: wynik_code_client.txt
  --code-output PATH       Legacy option. Splits PATH into PATH_server/PATH_client style outputs.
  --llm-output PATH        Output file for docs/llm snapshot. Default: wynik_llm.txt
  --tools-output PATH      Output file for tools snapshot. Default: wynik_tools.txt
  --schema-output PATH     Output file for MySQL schema. Default: MYSQL_SCHEMA_OUTPUT or wynik_gothic_mmo_ch1_clean_schema.txt
  -h, --help               Show this help.

Environment variables:
  MAX_BYTES                         Max bytes per input file. Default: 1500000
  INCLUDE_PRIVATE_DOCS              1/0. Default: 1
  INCLUDE_PRIVATE_TOOLS             1/0. Default: 1
  MYSQL_HOST                        Default: 192.168.195.94
  MYSQL_PORT                        Default: 3306
  MYSQL_USER                        Default: gothic
  MYSQL_PWD                         Default: gothic_dev_password
  MYSQL_DATABASE                    Default: gothic_mmo_ch1_clean
  MYSQL_SCHEMA_INCLUDE_VIEWS        1/0. Default: 1
  MYSQL_PROTOCOL                    auto/tcp/socket. Default: auto
  MYSQL_SOCKET                      Optional Unix socket path, e.g. /var/run/mysqld/mysqld.sock
  MYSQL_AUTO_START                  1/0. Try to start local mysql/mariadb service if connection fails. Default: 1
  MYSQL_SERVICE_NAMES               Space-separated service names to try. Default: mysql mariadb
  MYSQL_EXE or MYSQL_BIN            Explicit mysql client path.
  MYSQLDUMP_EXE or MYSQLDUMP_BIN    Explicit mysqldump path.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --code-server-output)
            [[ $# -ge 2 ]] || { echo "Missing value for --code-server-output" >&2; exit 2; }
            CODE_SERVER_OUTPUT="$2"
            shift 2
            ;;
        --code-client-output)
            [[ $# -ge 2 ]] || { echo "Missing value for --code-client-output" >&2; exit 2; }
            CODE_CLIENT_OUTPUT="$2"
            shift 2
            ;;
        --code-output)
            [[ $# -ge 2 ]] || { echo "Missing value for --code-output" >&2; exit 2; }
            CODE_OUTPUT_LEGACY="$2"
            shift 2
            ;;
        --llm-output)
            [[ $# -ge 2 ]] || { echo "Missing value for --llm-output" >&2; exit 2; }
            LLM_OUTPUT="$2"
            shift 2
            ;;
        --tools-output)
            [[ $# -ge 2 ]] || { echo "Missing value for --tools-output" >&2; exit 2; }
            TOOLS_OUTPUT="$2"
            shift 2
            ;;
        --schema-output)
            [[ $# -ge 2 ]] || { echo "Missing value for --schema-output" >&2; exit 2; }
            SCHEMA_OUTPUT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

get_env_or_default() {
    local name="$1"
    local default="$2"
    local value="${!name:-}"
    if [[ -z "${value//[[:space:]]/}" ]]; then
        printf '%s\n' "$default"
    else
        printf '%s\n' "$value"
    fi
}

abs_path() {
    # GNU realpath is available on Kubuntu/coreutils. -m also works for paths that do not exist yet.
    realpath -m -- "$1"
}

find_project_root() {
    local dir base parent
    dir="$(pwd -P)"

    while true; do
        if [[ -d "$dir/game" && -d "$dir/docs/llm" ]]; then
            printf '%s\n' "$dir"
            return 0
        fi

        base="$(basename -- "$dir")"
        parent="$(dirname -- "$dir")"

        shopt -s nocasematch
        if [[ "$base" == "game" && -d "$parent/docs/llm" ]]; then
            shopt -u nocasematch
            printf '%s\n' "$parent"
            return 0
        fi

        if [[ "$base" == "server" && -d "$parent/game" && -d "$parent/docs/llm" ]]; then
            shopt -u nocasematch
            printf '%s\n' "$parent"
            return 0
        fi

        if [[ "$base" == "docs" && -d "$dir/llm" && -d "$parent/game" ]]; then
            shopt -u nocasematch
            printf '%s\n' "$parent"
            return 0
        fi

        if [[ "$base" == "tools" && -d "$parent/game" && -d "$parent/docs/llm" ]]; then
            shopt -u nocasematch
            printf '%s\n' "$parent"
            return 0
        fi
        shopt -u nocasematch

        if [[ -z "$parent" || "$parent" == "$dir" ]]; then
            echo "Cannot detect OpenGothic project root. Run from OpenGothic/, OpenGothic/game/, OpenGothic/server/, OpenGothic/docs/ or OpenGothic/tools/." >&2
            return 1
        fi

        dir="$parent"
    done
}

lower() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

is_in_list() {
    local needle="$1"
    shift
    local item
    for item in "$@"; do
        [[ "$needle" == "$item" ]] && return 0
    done
    return 1
}

file_ext_lower() {
    local name ext
    name="$(basename -- "$1")"
    if [[ "$name" == *.* && "$name" != .* ]]; then
        ext=".${name##*.}"
        lower "$ext"
    else
        printf '\n'
    fi
}

relative_path() {
    local root path
    root="$(abs_path "$1")"
    path="$(abs_path "$2")"
    if [[ "$path" == "$root/"* ]]; then
        printf '%s\n' "${path#"$root/"}"
    else
        printf '%s\n' "$path"
    fi
}

PROJECT_ROOT="$(find_project_root)"
GAME_ROOT="$PROJECT_ROOT/game"
SERVER_ROOT="$PROJECT_ROOT/server"
LLM_ROOT="$PROJECT_ROOT/docs/llm"
TOOLS_ROOT="$PROJECT_ROOT/tools"

if [[ -n "${CODE_OUTPUT_LEGACY//[[:space:]]/}" ]]; then
    legacy_code_output_abs="$(abs_path "$CODE_OUTPUT_LEGACY")"
    legacy_code_output_dir="$(dirname -- "$legacy_code_output_abs")"
    legacy_code_output_file="$(basename -- "$legacy_code_output_abs")"
    legacy_code_output_base="$legacy_code_output_file"
    legacy_code_output_ext=""
    if [[ "$legacy_code_output_file" == *.* && "$legacy_code_output_file" != .* ]]; then
        legacy_code_output_base="${legacy_code_output_file%.*}"
        legacy_code_output_ext=".${legacy_code_output_file##*.}"
    fi
    CODE_SERVER_OUTPUT="$legacy_code_output_dir/${legacy_code_output_base}_server${legacy_code_output_ext}"
    CODE_CLIENT_OUTPUT="$legacy_code_output_dir/${legacy_code_output_base}_client${legacy_code_output_ext}"
fi

CODE_SERVER_OUTPUT_ABS="$(abs_path "$CODE_SERVER_OUTPUT")"
CODE_CLIENT_OUTPUT_ABS="$(abs_path "$CODE_CLIENT_OUTPUT")"
LLM_OUTPUT_ABS="$(abs_path "$LLM_OUTPUT")"
TOOLS_OUTPUT_ABS="$(abs_path "$TOOLS_OUTPUT")"
SCHEMA_OUTPUT_ABS="$(abs_path "$SCHEMA_OUTPUT")"

MAX_BYTES="$(get_env_or_default MAX_BYTES 1500000)"
INCLUDE_PRIVATE_DOCS="$(get_env_or_default INCLUDE_PRIVATE_DOCS 1)"
INCLUDE_PRIVATE_TOOLS="$(get_env_or_default INCLUDE_PRIVATE_TOOLS 1)"

if ! [[ "$MAX_BYTES" =~ ^[0-9]+$ ]]; then
    echo "MAX_BYTES must be a non-negative integer, got: $MAX_BYTES" >&2
    exit 2
fi

OUTPUT_NAMES=(
    "$(basename -- "$CODE_SERVER_OUTPUT_ABS")"
    "$(basename -- "$CODE_CLIENT_OUTPUT_ABS")"
    "$(basename -- "$LLM_OUTPUT_ABS")"
    "$(basename -- "$TOOLS_OUTPUT_ABS")"
    "$(basename -- "$SCHEMA_OUTPUT_ABS")"
)

OUTPUT_PATHS_LOWER=(
    "$(lower "$CODE_SERVER_OUTPUT_ABS")"
    "$(lower "$CODE_CLIENT_OUTPUT_ABS")"
    "$(lower "$LLM_OUTPUT_ABS")"
    "$(lower "$TOOLS_OUTPUT_ABS")"
    "$(lower "$SCHEMA_OUTPUT_ABS")"
)

BINARY_OR_ARCHIVE_EXTS=(
    ".spv" ".o" ".obj" ".a" ".so" ".dll" ".lib" ".exe" ".pdb" ".ilk" ".exp"
    ".png" ".jpg" ".jpeg" ".tga" ".bmp" ".hdr" ".ktx" ".ktx2" ".dds" ".webp"
    ".fbx" ".gltf" ".glb" ".dae" ".blend" ".wav" ".mp3" ".ogg" ".flac"
    ".zip" ".7z" ".tar" ".gz" ".rar"
)

should_skip_common_path() {
    local file full lower_full name lower_name ext part
    file="$1"
    full="$(abs_path "$file")"
    lower_full="$(lower "$full")"
    name="$(basename -- "$file")"
    lower_name="$(lower "$name")"
    ext="$(file_ext_lower "$file")"

    is_in_list "$lower_full" "${OUTPUT_PATHS_LOWER[@]}" && return 0

    local output_name
    for output_name in "${OUTPUT_NAMES[@]}"; do
        [[ "$(lower "$output_name")" == "$lower_name" ]] && return 0
    done

    [[ "$lower_name" == "wynik.txt" || "$lower_name" == wynik_*.txt ]] && return 0
    [[ "$name" == "Gomol.log" || "$name" == "rvk_trace.json" || "$name" == "compile_commands.json" ]] && return 0

    for part in "/build/" "/out/" "/obj/" "/bin/" "/external/" "/third_party/" "/vendor/" "/.git/" "/.vscode/" "/.idea/" "/.cache/" "/Testing/" "/test-results/"; do
        [[ "$lower_full" == *"$(lower "$part")"* ]] && return 0
    done

    [[ "$lower_full" == *"/cmake-build-"* ]] && return 0
    is_in_list "$ext" "${BINARY_OR_ARCHIVE_EXTS[@]}" && return 0

    return 1
}

is_private_path() {
    local file lower_full name lower_name
    file="$1"
    lower_full="$(lower "$(abs_path "$file")")"
    name="$(basename -- "$file")"
    lower_name="$(lower "$name")"

    [[ "$lower_full" == *"/private/"* || "$lower_full" == *"/secret/"* || "$lower_full" == *"/secrets/"* ]] && return 0
    [[ "$lower_name" == ".env" || "$lower_name" == .env.* ]] && return 0
    return 1
}

has_allowed_extension() {
    local file ext allowed
    file="$1"
    shift
    ext="$(file_ext_lower "$file")"
    for allowed in "$@"; do
        [[ "$ext" == "$(lower "$allowed")" ]] && return 0
    done
    return 1
}

new_snapshot() {
    local output_path title include_private root file rel size
    output_path="$1"
    title="$2"
    include_private="$3"
    shift 3

    local -a roots=()
    while [[ $# -gt 0 && "$1" != "--" ]]; do
        roots+=("$1")
        shift
    done
    [[ $# -gt 0 && "$1" == "--" ]] && shift
    local -a extensions=("$@")

    mkdir -p -- "$(dirname -- "$output_path")"
    : > "$output_path"
    {
        printf '# %s\n' "$title"
        printf '# Generated: %s\n' "$(date '+%Y-%m-%dT%H:%M:%S')"
        printf '# Project root: %s\n' "$PROJECT_ROOT"
        printf '# MAX_BYTES per file: %s\n' "$MAX_BYTES"
        printf '\n'
    } >> "$output_path"

    for root in "${roots[@]}"; do
        if [[ ! -d "$root" ]]; then
            if [[ "$root" == "$SERVER_ROOT" ]]; then
                echo "Warning: missing server directory, skipping: $root" >&2
                continue
            fi
            echo "Missing directory: $root" >&2
            return 1
        fi

        while IFS= read -r -d '' file; do
            has_allowed_extension "$file" "${extensions[@]}" || continue
            should_skip_common_path "$file" && continue
            if [[ "$include_private" == "0" ]] && is_private_path "$file"; then
                continue
            fi

            rel="$(relative_path "$PROJECT_ROOT" "$file")"
            printf '===== %s =====\n' "$rel" >> "$output_path"

            size="$(stat -c '%s' -- "$file")"
            if (( size > MAX_BYTES )); then
                printf '[SKIPPED: file is %s bytes, MAX_BYTES=%s]\n\n' "$size" "$MAX_BYTES" >> "$output_path"
            else
                cat -- "$file" >> "$output_path"
            fi

            printf '\n\n' >> "$output_path"
        done < <(find "$root" -type f -print0 | sort -z)
    done
}

quote_arg() {
    printf '%q' "$1"
}

resolve_mysql_tool() {
    local tool_name="$1"
    local primary_env="$2"
    local secondary_env="${3:-}"
    local explicit="${!primary_env:-}"

    if [[ -z "${explicit//[[:space:]]/}" && -n "$secondary_env" ]]; then
        explicit="${!secondary_env:-}"
    fi

    if [[ -n "${explicit//[[:space:]]/}" ]]; then
        if [[ -f "$explicit" && -x "$explicit" ]]; then
            abs_path "$explicit"
            return 0
        fi
        echo "$primary_env is set, but file does not exist or is not executable: $explicit" >&2
        return 1
    fi

    if command -v "$tool_name" >/dev/null 2>&1; then
        command -v "$tool_name"
        return 0
    fi

    local candidate
    for candidate in \
        "/usr/bin/$tool_name" \
        "/usr/local/bin/$tool_name" \
        "/snap/bin/$tool_name" \
        "/opt/mysql/bin/$tool_name" \
        "/opt/mariadb/bin/$tool_name"; do
        if [[ -f "$candidate" && -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    echo "$tool_name not found. Install it, e.g. on Kubuntu: sudo apt install mysql-client, or set $primary_env to the full path." >&2
    return 1
}

run_native_capture() {
    local stdout_path stderr_path exit_code
    stdout_path="$(mktemp)"
    stderr_path="$(mktemp)"

    set +e
    "$@" >"$stdout_path" 2>"$stderr_path"
    exit_code=$?
    set -e

    RUN_STDOUT="$(cat -- "$stdout_path")"
    RUN_STDERR="$(cat -- "$stderr_path")"
    RUN_EXIT_CODE="$exit_code"
    rm -f -- "$stdout_path" "$stderr_path"
    return 0
}

mysql_query_escape_single_quotes() {
    printf '%s' "$1" | sed "s/'/''/g"
}

is_local_mysql_host() {
    case "${1,,}" in
        localhost|127.0.0.1|::1)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

try_start_local_mysql_service() {
    local mysql_host auto_start services service
    mysql_host="$1"
    auto_start="$(get_env_or_default MYSQL_AUTO_START 1)"

    [[ "$auto_start" == "1" ]] || return 1
    is_local_mysql_host "$mysql_host" || return 1
    command -v systemctl >/dev/null 2>&1 || return 1

    services="${MYSQL_SERVICE_NAMES:-mysql mariadb}"
    for service in $services; do
        if ! systemctl list-unit-files "${service}.service" >/dev/null 2>&1 && \
           ! systemctl status "${service}.service" >/dev/null 2>&1; then
            continue
        fi

        if systemctl is-active --quiet "${service}.service"; then
            echo "MySQL service is already active: ${service}.service"
            return 0
        fi

        echo "Starting local MySQL service: ${service}.service"
        if sudo systemctl start "${service}.service"; then
            return 0
        fi
    done

    return 1
}

mysql_client_connection_args() {
    local protocol mysql_host mysql_port mysql_user socket_path
    protocol="$1"
    mysql_host="$2"
    mysql_port="$3"
    mysql_user="$4"
    socket_path="${5:-}"

    CONN_ARGS=(
        --no-defaults
        "--user=$mysql_user"
    )

    case "$protocol" in
        tcp)
            CONN_ARGS+=(
                --protocol=TCP
                "--host=$mysql_host"
                "--port=$mysql_port"
            )
            ;;
        socket)
            CONN_ARGS+=(--protocol=SOCKET)
            if [[ -n "${socket_path//[[:space:]]/}" ]]; then
                CONN_ARGS+=("--socket=$socket_path")
            fi
            ;;
        *)
            echo "Internal error: unsupported MySQL protocol: $protocol" >&2
            return 2
            ;;
    esac
}

find_mysql_socket_path() {
    local configured candidate config_file config_socket
    configured="${MYSQL_SOCKET:-}"

    if [[ -n "${configured//[[:space:]]/}" ]]; then
        printf '%s\n' "$configured"
        return 0
    fi

    for candidate in \
        /var/run/mysqld/mysqld.sock \
        /run/mysqld/mysqld.sock \
        /var/lib/mysql/mysql.sock \
        /tmp/mysql.sock; do
        if [[ -S "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    while IFS= read -r -d '' config_file; do
        config_socket="$(awk -F= '
            /^[[:space:]]*socket[[:space:]]*=/ {
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2);
                print $2;
                exit;
            }
        ' "$config_file" 2>/dev/null || true)"
        if [[ -n "${config_socket//[[:space:]]/}" ]]; then
            printf '%s\n' "$config_socket"
            return 0
        fi
    done < <(find /etc/mysql -type f -name '*.cnf' -print0 2>/dev/null || true)

    # Let the client use its compiled-in default socket. This often works on Ubuntu/Kubuntu.
    printf '\n'
}

mysql_protocol_plan() {
    local requested mysql_host
    requested="$(lower "$1")"
    mysql_host="$2"

    case "$requested" in
        tcp)
            printf '%s\n' tcp
            ;;
        socket)
            printf '%s\n' socket
            ;;
        auto|'')
            if is_local_mysql_host "$mysql_host"; then
                printf '%s\n' tcp socket
            else
                printf '%s\n' tcp
            fi
            ;;
        *)
            echo "MYSQL_PROTOCOL must be auto, tcp or socket; got: $requested" >&2
            return 2
            ;;
    esac
}

build_mysql_test_command() {
    local mysql_exe protocol mysql_host mysql_port mysql_user socket_path
    mysql_exe="$1"
    protocol="$2"
    mysql_host="$3"
    mysql_port="$4"
    mysql_user="$5"
    socket_path="${6:-}"

    mysql_client_connection_args "$protocol" "$mysql_host" "$mysql_port" "$mysql_user" "$socket_path"
    test_command=(
        "$mysql_exe"
        "${CONN_ARGS[@]}"
        --connect-timeout=8
        --batch
        --raw
        --skip-column-names
        -e "SELECT 1;"
    )
}

try_mysql_connection_once() {
    local mysql_exe protocol mysql_host mysql_port mysql_user mysql_password socket_path
    mysql_exe="$1"
    protocol="$2"
    mysql_host="$3"
    mysql_port="$4"
    mysql_user="$5"
    mysql_password="$6"
    socket_path="${7:-}"

    build_mysql_test_command "$mysql_exe" "$protocol" "$mysql_host" "$mysql_port" "$mysql_user" "$socket_path"
    echo "Running: ${test_command[*]}"
    MYSQL_PWD="$mysql_password" run_native_capture "${test_command[@]}"

    if (( RUN_EXIT_CODE == 0 )); then
        MYSQL_ACTIVE_PROTOCOL="$protocol"
        MYSQL_ACTIVE_SOCKET="$socket_path"
        return 0
    fi

    MYSQL_LAST_STDERR="$RUN_STDERR"
    MYSQL_LAST_EXIT_CODE="$RUN_EXIT_CODE"
    return 1
}

try_mysql_connection_plan() {
    local mysql_exe mysql_host mysql_port mysql_user mysql_password requested_protocol socket_path protocol
    mysql_exe="$1"
    mysql_host="$2"
    mysql_port="$3"
    mysql_user="$4"
    mysql_password="$5"
    requested_protocol="$6"
    socket_path="${7:-}"

    MYSQL_LAST_STDERR=""
    MYSQL_LAST_EXIT_CODE=0

    while IFS= read -r protocol; do
        [[ -n "$protocol" ]] || continue
        if try_mysql_connection_once "$mysql_exe" "$protocol" "$mysql_host" "$mysql_port" "$mysql_user" "$mysql_password" "$socket_path"; then
            return 0
        fi
        echo -e "MySQL connection test failed for protocol=$protocol.\n$MYSQL_LAST_STDERR" >&2
    done < <(mysql_protocol_plan "$requested_protocol" "$mysql_host")

    return 1
}

wait_for_mysql_connection() {
    local mysql_exe mysql_host mysql_port mysql_user mysql_password requested_protocol socket_path max_attempts delay attempt
    mysql_exe="$1"
    mysql_host="$2"
    mysql_port="$3"
    mysql_user="$4"
    mysql_password="$5"
    requested_protocol="$6"
    socket_path="${7:-}"
    max_attempts="${8:-30}"
    delay="${9:-1}"

    for (( attempt=1; attempt<=max_attempts; attempt++ )); do
        if try_mysql_connection_plan "$mysql_exe" "$mysql_host" "$mysql_port" "$mysql_user" "$mysql_password" "$requested_protocol" "$socket_path"; then
            return 0
        fi
        sleep "$delay"
    done

    return 1
}

print_mysql_diagnostics() {
    local mysql_host mysql_port socket_path
    mysql_host="$1"
    mysql_port="$2"
    socket_path="${3:-}"

    echo "MySQL diagnostics:" >&2
    if command -v systemctl >/dev/null 2>&1; then
        systemctl is-active mysql.service >/dev/null 2>&1 && echo "  mysql.service: active" >&2 || true
        systemctl is-active mariadb.service >/dev/null 2>&1 && echo "  mariadb.service: active" >&2 || true
    fi

    if command -v ss >/dev/null 2>&1; then
        echo "  TCP listeners matching port $mysql_port:" >&2
        ss -ltn 2>/dev/null | awk -v p=":$mysql_port" '$4 ~ p { print "    " $0 }' >&2 || true
    fi

    if [[ -n "${socket_path//[[:space:]]/}" ]]; then
        if [[ -S "$socket_path" ]]; then
            echo "  Unix socket exists: $socket_path" >&2
        else
            echo "  Unix socket path configured/detected but not found as socket: $socket_path" >&2
        fi
    else
        echo "  Unix socket: not explicitly configured; client default will be tried in socket mode" >&2
    fi

    echo "Hints:" >&2
    echo "  - If there is no TCP listener on $mysql_host:$mysql_port, use MYSQL_PROTOCOL=socket." >&2
    echo "  - If TCP listens on 127.0.0.1 but not localhost, use MYSQL_HOST=127.0.0.1." >&2
    echo "  - If the next error is Access denied, set MYSQL_PWD to the gothic user password or fix MySQL grants." >&2
}

export_mysql_schema() {
    local mysqldump_exe mysql_exe mysql_host mysql_port mysql_user mysql_password mysql_database include_views requested_protocol socket_path
    local full_exit view_exit dump_exit view_query view_result view_name header
    local -a base_args ignore_view_args view_names full_command dump_command view_command test_command CONN_ARGS

    mysqldump_exe="$(resolve_mysql_tool "mysqldump" "MYSQLDUMP_EXE" "MYSQLDUMP_BIN")"
    mysql_exe="$(resolve_mysql_tool "mysql" "MYSQL_EXE" "MYSQL_BIN")"

    mysql_host="$(get_env_or_default MYSQL_HOST localhost)"
    mysql_port="$(get_env_or_default MYSQL_PORT 3306)"
    mysql_user="$(get_env_or_default MYSQL_USER gothic)"
    mysql_password="$(get_env_or_default MYSQL_PWD gothic_dev_password)"
    mysql_database="$(get_env_or_default MYSQL_DATABASE gothic_mmo_ch1_clean)"
    include_views="$(get_env_or_default MYSQL_SCHEMA_INCLUDE_VIEWS 1)"
    requested_protocol="$(get_env_or_default MYSQL_PROTOCOL auto)"
    socket_path="$(find_mysql_socket_path)"

    echo "MySQL schema export: host=$mysql_host port=$mysql_port user=$mysql_user database=$mysql_database include_views=$include_views protocol=$requested_protocol"
    if [[ -n "${socket_path//[[:space:]]/}" ]]; then
        echo "MySQL socket candidate: $socket_path"
    fi
    echo "mysql: $mysql_exe"
    echo "mysqldump: $mysqldump_exe"

    if ! try_mysql_connection_plan "$mysql_exe" "$mysql_host" "$mysql_port" "$mysql_user" "$mysql_password" "$requested_protocol" "$socket_path"; then
        if try_start_local_mysql_service "$mysql_host"; then
            echo "Waiting for MySQL to accept connections..."
            if wait_for_mysql_connection "$mysql_exe" "$mysql_host" "$mysql_port" "$mysql_user" "$mysql_password" "$requested_protocol" "$socket_path" 30 1; then
                echo "MySQL connection test passed after service start/retry. protocol=$MYSQL_ACTIVE_PROTOCOL"
            else
                echo -e "MySQL still does not accept connections after retry.\n$MYSQL_LAST_STDERR" >&2
                print_mysql_diagnostics "$mysql_host" "$mysql_port" "$socket_path"
                return 1
            fi
        else
            echo -e "MySQL connection test failed.\n$MYSQL_LAST_STDERR" >&2
            print_mysql_diagnostics "$mysql_host" "$mysql_port" "$socket_path"
            return 1
        fi
    else
        echo "MySQL connection test passed. protocol=$MYSQL_ACTIVE_PROTOCOL"
    fi

    mysql_client_connection_args "$MYSQL_ACTIVE_PROTOCOL" "$mysql_host" "$mysql_port" "$mysql_user" "$MYSQL_ACTIVE_SOCKET"
    base_args=(
        "${CONN_ARGS[@]}"
        --databases "$mysql_database"
        --no-data
        --routines
        --events
        --triggers
        --single-transaction
        --set-gtid-purged=OFF
        --column-statistics=0
        --no-tablespaces
    )

    if [[ "$include_views" == "1" ]]; then
        full_command=("$mysqldump_exe" "${base_args[@]}")
        echo "Running: ${full_command[*]}"
        MYSQL_PWD="$mysql_password" run_native_capture "${full_command[@]}"
        full_exit="$RUN_EXIT_CODE"
        if (( full_exit == 0 )); then
            mkdir -p -- "$(dirname -- "$SCHEMA_OUTPUT_ABS")"
            printf '%s' "$RUN_STDOUT" > "$SCHEMA_OUTPUT_ABS"
            return 0
        fi

        echo "Warning: full mysqldump failed. Retrying schema export with MySQL views skipped." >&2
        echo "$RUN_STDERR" >&2
    fi

    view_query="SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA = '$(mysql_query_escape_single_quotes "$mysql_database")' AND TABLE_TYPE = 'VIEW' ORDER BY TABLE_NAME;"
    mysql_client_connection_args "$MYSQL_ACTIVE_PROTOCOL" "$mysql_host" "$mysql_port" "$mysql_user" "$MYSQL_ACTIVE_SOCKET"
    view_command=(
        "$mysql_exe"
        "${CONN_ARGS[@]}"
        --connect-timeout=8
        --batch
        --raw
        --skip-column-names
        -e "$view_query"
    )

    echo "Running: ${view_command[*]}"
    MYSQL_PWD="$mysql_password" run_native_capture "${view_command[@]}"
    view_exit="$RUN_EXIT_CODE"
    if (( view_exit != 0 )); then
        echo -e "Could not read MySQL view list.\n$RUN_STDERR" >&2
        return 1
    fi

    mapfile -t view_names < <(printf '%s\n' "$RUN_STDOUT" | sed '/^[[:space:]]*$/d')

    ignore_view_args=()
    for view_name in "${view_names[@]}"; do
        ignore_view_args+=("--ignore-table=$mysql_database.$view_name")
    done

    header=""
    if (( ${#view_names[@]} > 0 )); then
        header+=$'-- NOTE: MySQL views were skipped in this schema export.\n'
        header+=$'--       This avoids mysqldump failures when a view is invalid or has a bad definer.\n'
        header+=$'--       Set MYSQL_SCHEMA_INCLUDE_VIEWS=1 to try exporting views.\n'
        header+=$'-- Skipped views:\n'
        for view_name in "${view_names[@]}"; do
            header+="--   $view_name"$'\n'
        done
        header+=$'\n'
    fi

    dump_command=("$mysqldump_exe" "${base_args[@]}" "${ignore_view_args[@]}")
    echo "Running: ${dump_command[*]}"
    MYSQL_PWD="$mysql_password" run_native_capture "${dump_command[@]}"
    dump_exit="$RUN_EXIT_CODE"
    if (( dump_exit != 0 )); then
        echo -e "mysqldump failed.\n$RUN_STDERR" >&2
        return 1
    fi

    mkdir -p -- "$(dirname -- "$SCHEMA_OUTPUT_ABS")"
    printf '%s%s' "$header" "$RUN_STDOUT" > "$SCHEMA_OUTPUT_ABS"
}

echo "Project root: $PROJECT_ROOT"
new_snapshot "$CODE_SERVER_OUTPUT_ABS" "OpenGothic server C/C++ source snapshot" 1 "$SERVER_ROOT" -- \
    ".c" ".cc" ".cpp" ".cxx" ".h" ".hh" ".hpp" ".hxx" ".inl" ".ipp" ".tpp" ".ixx" ".cppm" ".mpp"
new_snapshot "$CODE_CLIENT_OUTPUT_ABS" "OpenGothic client C/C++ source snapshot" 1 "$GAME_ROOT" -- \
    ".c" ".cc" ".cpp" ".cxx" ".h" ".hh" ".hpp" ".hxx" ".inl" ".ipp" ".tpp" ".ixx" ".cppm" ".mpp"
new_snapshot "$LLM_OUTPUT_ABS" "OpenGothic docs/llm snapshot" "$([[ "$INCLUDE_PRIVATE_DOCS" != "0" ]] && printf '1' || printf '0')" "$LLM_ROOT" -- \
    ".md" ".txt" ".rst"
new_snapshot "$TOOLS_OUTPUT_ABS" "OpenGothic tools snapshot" "$([[ "$INCLUDE_PRIVATE_TOOLS" != "0" ]] && printf '1' || printf '0')" "$TOOLS_ROOT" -- \
    ".py" ".sh" ".bash" ".md" ".txt" ".rst" ".json" ".jsonl" ".yml" ".yaml" ".toml" ".ini" ".cfg" ".sql"
export_mysql_schema

echo "Generated server code snapshot: $CODE_SERVER_OUTPUT_ABS"
echo "Generated client code snapshot: $CODE_CLIENT_OUTPUT_ABS"
echo "Generated llm snapshot:         $LLM_OUTPUT_ABS"
echo "Generated tools snapshot:       $TOOLS_OUTPUT_ABS"
echo "Generated MySQL schema:         $SCHEMA_OUTPUT_ABS"
