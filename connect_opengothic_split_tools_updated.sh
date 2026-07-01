#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./connect_opengothic_split.sh
#   ./connect_opengothic_split.sh wynik_code.txt wynik_llm.txt wynik_tools.txt gothic_mmo_ch1_clean_schema.sql
#
# Goal:
#   Produce three LLM-friendly snapshots and one MySQL schema dump:
#
#   1) wynik_code.txt
#      - combines C/C++ source/header files from OpenGothic/game and OpenGothic/server
#      - default extensions: .c .cc .cpp .cxx .h .hh .hpp .hxx .inl .ipp .tpp .ixx .cppm .mpp
#
#   2) wynik_llm.txt
#      - combines documentation from OpenGothic/docs/llm
#      - default extensions: .md .txt .rst
#
#   3) wynik_tools.txt
#      - combines utility/tooling files from OpenGothic/tools
#      - default extensions: .py .sh .bash .md .txt .rst .json .jsonl .yml .yaml .toml .ini .cfg .sql
#
#   4) gothic_mmo_ch1_clean_schema.sql
#      - exports MySQL schema only, without table data
#
# Run from:
#   - OpenGothic/
#   - OpenGothic/game/
#   - OpenGothic/server/
#   - OpenGothic/docs/
#   - OpenGothic/tools/
#
# Optional env:
#   MAX_BYTES=5000000 ./connect_opengothic_split.sh
#   INCLUDE_PRIVATE_DOCS=0 ./connect_opengothic_split.sh
#   INCLUDE_PRIVATE_TOOLS=0 ./connect_opengothic_split.sh
#   MYSQL_SCHEMA_OUTPUT=gothic_mmo_ch1_clean_schema.sql ./connect_opengothic_split.sh
#   MYSQL_SCHEMA_INCLUDE_VIEWS=1 ./connect_opengothic_split.sh

CODE_OUTPUT="${1:-wynik_code.txt}"
LLM_OUTPUT="${2:-wynik_llm.txt}"
TOOLS_OUTPUT="${3:-wynik_tools.txt}"
SCHEMA_OUTPUT="${4:-${MYSQL_SCHEMA_OUTPUT:-wynik_gothic_mmo_ch1_clean_schema.txt}}"
MAX_BYTES="${MAX_BYTES:-1500000}"
INCLUDE_PRIVATE_DOCS="${INCLUDE_PRIVATE_DOCS:-1}"
INCLUDE_PRIVATE_TOOLS="${INCLUDE_PRIVATE_TOOLS:-1}"

export LC_ALL=C

realpath_m() {
    if command -v realpath >/dev/null 2>&1; then
        realpath -m "$1"
    else
        python3 - "$1" <<'PY_REALPATH'
import os, sys
print(os.path.abspath(sys.argv[1]))
PY_REALPATH
    fi
}

detect_project_root() {
    local dir
    dir="$(pwd)"

    while [ "$dir" != "/" ]; do
        if [ -d "$dir/game" ] && [ -d "$dir/docs/llm" ]; then
            printf '%s\n' "$dir"
            return 0
        fi

        if [ "$(basename "$dir")" = "game" ] && [ -d "$dir/../docs/llm" ]; then
            realpath_m "$dir/.."
            return 0
        fi

        if [ "$(basename "$dir")" = "server" ] && [ -d "$dir/../game" ] && [ -d "$dir/../docs/llm" ]; then
            realpath_m "$dir/.."
            return 0
        fi

        if [ "$(basename "$dir")" = "docs" ] && [ -d "$dir/llm" ] && [ -d "$dir/../game" ]; then
            realpath_m "$dir/.."
            return 0
        fi

        if [ "$(basename "$dir")" = "tools" ] && [ -d "$dir/../game" ] && [ -d "$dir/../docs/llm" ]; then
            realpath_m "$dir/.."
            return 0
        fi

        dir="$(dirname "$dir")"
    done

    printf 'ERROR: Cannot detect OpenGothic project root. Run from OpenGothic/, OpenGothic/game/, OpenGothic/server/, OpenGothic/docs/ or OpenGothic/tools/.\n' >&2
    return 1
}

PROJECT_ROOT="$(detect_project_root)"
GAME_ROOT="$PROJECT_ROOT/game"
SERVER_ROOT="$PROJECT_ROOT/server"
LLM_ROOT="$PROJECT_ROOT/docs/llm"
TOOLS_ROOT="$PROJECT_ROOT/tools"

CODE_OUTPUT_ABS="$(realpath_m "$CODE_OUTPUT")"
LLM_OUTPUT_ABS="$(realpath_m "$LLM_OUTPUT")"
TOOLS_OUTPUT_ABS="$(realpath_m "$TOOLS_OUTPUT")"
SCHEMA_OUTPUT_ABS="$(realpath_m "$SCHEMA_OUTPUT")"

file_size_bytes() {
    local file="$1"
    if size=$(stat -c '%s' "$file" 2>/dev/null); then
        printf '%s' "$size"
    else
        wc -c < "$file" | tr -d ' '
    fi
}

write_header() {
    local output="$1"
    local title="$2"
    {
        printf '# %s\n' "$title"
        printf '# Generated: %s\n' "$(date -Iseconds)"
        printf '# Project root: %s\n' "$PROJECT_ROOT"
        printf '# MAX_BYTES per file: %s\n' "$MAX_BYTES"
        printf '\n'
    } > "$output"
}

should_skip_common_path() {
    local file="$1"
    local base="${file##*/}"

    case "$base" in
        "$(basename "$CODE_OUTPUT_ABS")"|"$(basename "$LLM_OUTPUT_ABS")"|"$(basename "$TOOLS_OUTPUT_ABS")"|"$(basename "$SCHEMA_OUTPUT_ABS")"| \
        wynik.txt|wynik_*.txt|Gomol.log|rvk_trace.json|compile_commands.json)
            return 0
            ;;
    esac

    case "$file" in
        "$CODE_OUTPUT_ABS"|"$LLM_OUTPUT_ABS"|"$TOOLS_OUTPUT_ABS"|"$SCHEMA_OUTPUT_ABS")
            return 0
            ;;
    esac

    case "$file" in
        */build/*|*/cmake-build-*/*|*/out/*|*/obj/*|*/bin/*| \
        */external/*|*/third_party/*|*/vendor/*| \
        */.git/*|*/.vscode/*|*/.idea/*|*/.cache/*| \
        */Testing/*|*/test-results/*)
            return 0
            ;;
    esac

    case "$file" in
        *.spv|*.o|*.obj|*.a|*.so|*.dll|*.lib|*.exe|*.pdb|*.ilk|*.exp| \
        *.png|*.jpg|*.jpeg|*.tga|*.bmp|*.hdr|*.ktx|*.ktx2|*.dds|*.webp| \
        *.fbx|*.gltf|*.glb|*.dae|*.blend|*.wav|*.mp3|*.ogg|*.flac| \
        *.zip|*.7z|*.tar|*.gz|*.rar)
            return 0
            ;;
    esac

    return 1
}

is_cpp_file() {
    local file="$1"
    case "$file" in
        *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*.inl|*.ipp|*.tpp|*.ixx|*.cppm|*.mpp)
            return 0
            ;;
    esac
    return 1
}

is_llm_doc_file() {
    local file="$1"
    case "$file" in
        *.md|*.txt|*.rst)
            return 0
            ;;
    esac
    return 1
}

is_tools_file() {
    local file="$1"
    case "$file" in
        *.py|*.sh|*.bash|*.md|*.txt|*.rst|*.json|*.jsonl|*.yml|*.yaml|*.toml|*.ini|*.cfg|*.sql)
            return 0
            ;;
    esac
    return 1
}

is_private_path() {
    local file="$1"
    case "$file" in
        */private/*|*/secret/*|*/secrets/*|*/.env|*/.env.*)
            return 0
            ;;
    esac
    return 1
}

append_file() {
    local output="$1"
    local root="$2"
    local file="$3"

    local rel
    rel="${file#"$root"/}"

    local size
    size="$(file_size_bytes "$file")"

    {
        printf '===== %s =====\n' "$rel"

        if [ "$size" -gt "$MAX_BYTES" ]; then
            printf '[SKIPPED: file is %s bytes, MAX_BYTES=%s]\n\n' "$size" "$MAX_BYTES"
            return 0
        fi

        cat "$file"
        printf '\n\n'
    } >> "$output"
}

append_cpp_sources_from_dir() {
    local root="$1"

    find "$root" -type f -print0 |
    sort -z |
    while IFS= read -r -d '' file; do
        if should_skip_common_path "$file"; then
            continue
        fi

        if ! is_cpp_file "$file"; then
            continue
        fi

        append_file "$CODE_OUTPUT_ABS" "$PROJECT_ROOT" "$file"
    done
}

combine_cpp_sources() {
    write_header "$CODE_OUTPUT_ABS" "OpenGothic C/C++ source snapshot"

    if [ ! -d "$GAME_ROOT" ]; then
        printf 'ERROR: Missing game directory: %s\n' "$GAME_ROOT" >&2
        return 1
    fi

    append_cpp_sources_from_dir "$GAME_ROOT"

    if [ -d "$SERVER_ROOT" ]; then
        append_cpp_sources_from_dir "$SERVER_ROOT"
    else
        printf 'WARN: Missing server directory, skipping: %s\n' "$SERVER_ROOT" >&2
    fi
}

combine_llm_docs() {
    write_header "$LLM_OUTPUT_ABS" "OpenGothic docs/llm snapshot"

    if [ ! -d "$LLM_ROOT" ]; then
        printf 'ERROR: Missing docs/llm directory: %s\n' "$LLM_ROOT" >&2
        return 1
    fi

    find "$LLM_ROOT" -type f -print0 |
    sort -z |
    while IFS= read -r -d '' file; do
        if should_skip_common_path "$file"; then
            continue
        fi

        if ! is_llm_doc_file "$file"; then
            continue
        fi

        if [ "$INCLUDE_PRIVATE_DOCS" = "0" ] && is_private_path "$file"; then
            continue
        fi

        append_file "$LLM_OUTPUT_ABS" "$PROJECT_ROOT" "$file"
    done
}

combine_tools() {
    write_header "$TOOLS_OUTPUT_ABS" "OpenGothic tools snapshot"

    if [ ! -d "$TOOLS_ROOT" ]; then
        printf 'ERROR: Missing tools directory: %s\n' "$TOOLS_ROOT" >&2
        return 1
    fi

    find "$TOOLS_ROOT" -type f -print0 |
    sort -z |
    while IFS= read -r -d '' file; do
        if should_skip_common_path "$file"; then
            continue
        fi

        if ! is_tools_file "$file"; then
            continue
        fi

        if [ "$INCLUDE_PRIVATE_TOOLS" = "0" ] && is_private_path "$file"; then
            continue
        fi

        append_file "$TOOLS_OUTPUT_ABS" "$PROJECT_ROOT" "$file"
    done
}

export_mysql_schema() {
    if ! command -v mysqldump >/dev/null 2>&1; then
        printf 'ERROR: mysqldump not found in PATH.\n' >&2
        return 1
    fi

    if ! command -v mysql >/dev/null 2>&1; then
        printf 'ERROR: mysql client not found in PATH.\n' >&2
        return 1
    fi

    local mysql_host="${MYSQL_HOST:-127.0.0.1}"
    local mysql_port="${MYSQL_PORT:-3306}"
    local mysql_user="${MYSQL_USER:-gothic}"
    local mysql_password="${MYSQL_PWD:-gothic_dev_password}"
    local mysql_database="${MYSQL_DATABASE:-gothic_mmo_ch1_clean}"
    local mysql_include_views="${MYSQL_SCHEMA_INCLUDE_VIEWS:-0}"
    local tmp_output="${SCHEMA_OUTPUT_ABS}.tmp.$$"

    local dump_args=(
        -h "$mysql_host"
        -P "$mysql_port"
        -u "$mysql_user"
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

    rm -f "$tmp_output"

    local escaped_database="${mysql_database//\\/\\\\}"
    escaped_database="${escaped_database//\'/\\\'}"

    local view_names=()
    local view_name
    while IFS= read -r view_name; do
        [ -n "$view_name" ] && view_names+=("$view_name")
    done < <(
        MYSQL_PWD="$mysql_password" mysql \
            -h "$mysql_host" \
            -P "$mysql_port" \
            -u "$mysql_user" \
            --batch \
            --raw \
            --skip-column-names \
            -e "SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA = '${escaped_database}' AND TABLE_TYPE = 'VIEW' ORDER BY TABLE_NAME;"
    )

    if [ "$mysql_include_views" = "1" ]; then
        if MYSQL_PWD="$mysql_password" mysqldump "${dump_args[@]}" > "$tmp_output"; then
            mv "$tmp_output" "$SCHEMA_OUTPUT_ABS"
            return 0
        fi

        rm -f "$tmp_output"
        printf 'WARN: Full mysqldump failed. Retrying schema export with MySQL views skipped.\n' >&2
    fi

    local ignore_view_args=()
    for view_name in "${view_names[@]}"; do
        ignore_view_args+=("--ignore-table=${mysql_database}.${view_name}")
    done

    {
        if [ "${#view_names[@]}" -gt 0 ]; then
            printf '%s\n' '-- NOTE: MySQL views were skipped in this schema export.'
            printf '%s\n' '--       This avoids mysqldump failures when a view is invalid or has a bad definer.'
            printf '%s\n' '--       Set MYSQL_SCHEMA_INCLUDE_VIEWS=1 to try exporting views.'
            printf '%s\n' '-- Skipped views:'
            for view_name in "${view_names[@]}"; do
                printf '%s\n' "--   $view_name"
            done
            printf '\n'
        fi
    } > "$tmp_output"

    if MYSQL_PWD="$mysql_password" mysqldump "${dump_args[@]}" "${ignore_view_args[@]}" >> "$tmp_output"; then
        mv "$tmp_output" "$SCHEMA_OUTPUT_ABS"
        return 0
    fi

    rm -f "$tmp_output"
    return 1
}

combine_cpp_sources
combine_llm_docs
combine_tools
export_mysql_schema

printf 'Generated code snapshot:  %s\n' "$CODE_OUTPUT_ABS"
printf 'Generated llm snapshot:   %s\n' "$LLM_OUTPUT_ABS"
printf 'Generated tools snapshot: %s\n' "$TOOLS_OUTPUT_ABS"
printf 'Generated MySQL schema:   %s\n' "$SCHEMA_OUTPUT_ABS"
