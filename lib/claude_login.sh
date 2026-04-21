#!/bin/sh
# lib/claude_login.sh — install claude-code + apply oauth creds. Idempotent.
# Usage: sh claude_login.sh <creds.json>  OR  cat creds.json | sh claude_login.sh
set -e
S="$HOME/.claude/.credentials.json"
mkdir -p "$HOME/.claude" && chmod 700 "$HOME/.claude"
if [ $# -gt 0 ] && [ -f "$1" ]; then cp "$1" "$S"; else cat > "$S"; fi
chmod 600 "$S"
[ -x "$HOME/.local/bin/claude" ] || command -v claude >/dev/null 2>&1 || curl -fsSL https://claude.ai/install.sh | bash
[ -f "$HOME/.bash_profile" ] || echo '. ~/.bashrc' > "$HOME/.bash_profile"
grep -q '.local/bin' "$HOME/.bashrc" 2>/dev/null || sed -i '1iexport PATH=$HOME/.local/bin:$PATH' "$HOME/.bashrc" 2>/dev/null || echo 'export PATH=$HOME/.local/bin:$PATH' > "$HOME/.bashrc"
export PATH="$HOME/.local/bin:$PATH"
unset ANTHROPIC_API_KEY
claude -p 'Reply with exactly the word PONG' 2>&1 | tail -1
