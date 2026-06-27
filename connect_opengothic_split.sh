#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./connect_opengothic_split.sh
#   ./connect_opengothic_split.sh wynik_code.txt wynik_llm.txt
#
# Goal:
#   Produce two LLM-friendly snapshots:
#
#   1) wynik_code.txt
#      - combines C/C++ source/header files from OpenGothic/game
#      - default extensions: .c .cc .cpp .cxx .h .hh .hpp .hxx .inl .ipp .tpp .ixx .cppm .mpp
#
#   2) wynik_llm.txt
#      - combines documentation from OpenGothic/docs/llm
#      - default extensions: .md .txt .rst
#
# Run from:
#   - OpenGothic/
#   - OpenGothic/game/
#   - OpenGothic/docs/
#
# Optional env:
#   MAX_BYTES=5000000 ./connect_opengothic_split.sh
#   INCLUDE_PRIVATE_DOCS=0 ./connect_opengothic_split.sh

CODE_OUTPUT="${1:-wynik_code.txt}"
LLM_OUTPUT="${2:-wynik_llm.txt}"
MAX_BYTES="${MAX_BYTES:-1500000}"
INCLUDE_PRIVATE_DOCS="${INCLUDE_PRIVATE_DOCS:-1}"

export LC_ALL=C

realpath_m() {
    if command -v realpath >/dev/null 2>&1; then
        realpath -m "$1"
    else
        python3 - "$1" <<'PY'
import os, sys
print(os.path.abspath(sys.argv[1]))
PY
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

        if [ "$(basename "$dir")" = "docs" ] && [ -d "$dir/llm" ] && [ -d "$dir/../game" ]; then
            realpath_m "$dir/.."
            return 0
        fi

        dir="$(dirname "$dir")"
    done

    printf 'ERROR: Cannot detect OpenGothic project root. Run from OpenGothic/, OpenGothic/game/ or OpenGothic/docs/.\n' >&2
    return 1
}

PROJECT_ROOT="$(detect_project_root)"
GAME_ROOT="$PROJECT_ROOT/game"
LLM_ROOT="$PROJECT_ROOT/docs/llm"

CODE_OUTPUT_ABS="$(realpath_m "$CODE_OUTPUT")"
LLM_OUTPUT_ABS="$(realpath_m "$LLM_OUTPUT")"

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
        "$(basename "$CODE_OUTPUT_ABS")"|"$(basename "$LLM_OUTPUT_ABS")"| \
        wynik.txt|wynik_*.txt|Gomol.log|rvk_trace.json|compile_commands.json)
            return 0
            ;;
    esac

    case "$file" in
        "$CODE_OUTPUT_ABS"|"$LLM_OUTPUT_ABS")
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

combine_cpp_sources() {
    write_header "$CODE_OUTPUT_ABS" "OpenGothic C/C++ source snapshot"

    if [ ! -d "$GAME_ROOT" ]; then
        printf 'ERROR: Missing game directory: %s\n' "$GAME_ROOT" >&2
        return 1
    fi

    find "$GAME_ROOT" -type f -print0 |
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

        if [ "$INCLUDE_PRIVATE_DOCS" = "0" ]; then
            case "$file" in
                */private/*|*/secret/*|*/secrets/*)
                    continue
                    ;;
            esac
        fi

        append_file "$LLM_OUTPUT_ABS" "$PROJECT_ROOT" "$file"
    done
}

combine_cpp_sources
combine_llm_docs

printf 'Generated code snapshot: %s\n' "$CODE_OUTPUT_ABS"
printf 'Generated llm snapshot:  %s\n' "$LLM_OUTPUT_ABS"
