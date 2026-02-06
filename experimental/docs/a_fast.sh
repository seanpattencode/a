#!/bin/bash
# Proof of concept: all-builtin bash 'a' function — 0ms fast paths
# Source this: . experimental/a_fast.sh
# Then run: a_fast, a_fast 0, a_fast 1, etc.

a_fast() {
    local cache=~/.local/share/a/help_cache.txt
    local projects=~/.local/share/a/projects.txt

    # No args: print help from cache (builtin read, no cat)
    if [[ -z "$1" ]]; then
        [[ -f "$cache" ]] && printf '%s\n' "$(<"$cache")" || command python3 ~/.local/bin/a
        return
    fi

    # Number: cd to project (builtin mapfile, no sed)
    if [[ "$1" =~ ^[0-9]+$ ]]; then
        local -a lines
        mapfile -t lines < "$projects"
        local dir="${lines[$1]}"
        [[ -n "$dir" && -d "$dir" ]] && { printf '📂 %s\n' "$dir"; cd "$dir"; return; }
    fi

    # Directory: cd into it (builtin only)
    local d="${1/#\~/$HOME}"
    [[ "$1" == /projects/* ]] && d="$HOME$1"
    [[ -d "$d" ]] && { printf '📂 %s\n' "$d"; cd "$d"; return; }

    # Everything else: fall back to C binary or python
    command python3 ~/.local/bin/a "$@"
}
