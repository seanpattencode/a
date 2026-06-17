#if 0
# lib/claude_login.c — install claude-code + apply oauth creds (idempotent). BOOTSTRAP script: run on a bare remote via
#   sh ~/a/lib/claude_login.c <creds.json>   OR   cat creds.json | sh ~/a/lib/claude_login.c
# Polyglot per IDEAS #130/#131: bootstrap shell lives in a .c via the if-trick (NOT a standalone .sh); stays sh-runnable before `a` exists on the box.
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
exit 0
#endif
int main(void) { return 0; }
