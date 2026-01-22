#!/bin/bash
set -e

G='\033[32m' Y='\033[33m' C='\033[36m' R='\033[0m'
ok() { echo -e "${G}✓${R} $1"; }
info() { echo -e "${C}>${R} $1"; }
warn() { echo -e "${Y}!${R} $1"; }
die() { echo "✗ $1"; exit 1; }

BIN="$HOME/.local/bin"
mkdir -p "$BIN"
export PATH="$BIN:$PATH"

# Fast check for existing deps
if command -v tmux &>/dev/null && command -v node &>/dev/null && command -v npm &>/dev/null && command -v git &>/dev/null && command -v python3 &>/dev/null; then
    ok "system packages (found)"
else
    # Detect OS
    if [[ "$OSTYPE" == darwin* ]]; then OS=mac
    elif [[ -f /data/data/com.termux/files/usr/bin/bash ]]; then OS=termux
    elif [[ -f /etc/debian_version ]]; then OS=debian
    elif [[ -f /etc/arch-release ]]; then OS=arch
    elif [[ -f /etc/fedora-release ]]; then OS=fedora
    else OS=unknown; fi

    # Sudo helper
    get_sudo() {
        [[ $EUID -eq 0 ]] && return
        if sudo -n true 2>/dev/null; then SUDO="sudo"; return; fi
        if [[ -t 0 ]]; then info "sudo password needed for missing packages"; sudo -v && SUDO="sudo"; else SUDO=""; fi
    }

    case $OS in
        mac)
            command -v brew &>/dev/null || die "Install Homebrew first"
            brew install tmux node 2>/dev/null || true; ok "tmux + node"
            ;;
        debian)
            get_sudo
            export DEBIAN_FRONTEND=noninteractive
            $SUDO apt-get update -qq
            $SUDO apt-get install -y -qq tmux git curl nodejs npm python3-pip 2>/dev/null || true
            ok "system packages"
            ;;
        arch)
            get_sudo; $SUDO pacman -Sy --noconfirm tmux nodejs npm git python-pip 2>/dev/null && ok "system packages"
            ;;
        fedora)
            get_sudo; $SUDO dnf install -y tmux nodejs npm git python3-pip 2>/dev/null && ok "system packages"
            ;;
        termux) pkg update -y && pkg install -y tmux nodejs git python && ok "system packages" ;;
        *) warn "Unknown OS - install tmux/node manually" ;;
    esac
fi

# Node CLIs
install_cli() {
    command -v "$2" &>/dev/null && return
    info "Installing $2..."
    npm install -g "$1" --quiet 2>&1 >/dev/null && ok "$2" || warn "$2 failed"
}
install_cli "@anthropic-ai/claude-code" "claude"
install_cli "@openai/codex" "codex"
install_cli "@google/gemini-cli" "gemini"

# Python extras
if ! python3 -c "import pexpect, prompt_toolkit" 2>/dev/null; then
    pip3 install --user -q pexpect prompt_toolkit 2>/dev/null && ok "python extras" || true
fi

# aio itself
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "$SCRIPT_DIR/aio.py" ]]; then
    cp "$SCRIPT_DIR/aio.py" "$BIN/aio" && chmod +x "$BIN/aio"
else
    curl -fsSL "https://raw.githubusercontent.com/seanpattencode/aio/main/aio.py" -o "$BIN/aio" && chmod +x "$BIN/aio"
fi
ok "aio installed"

# PATH & Config
RC="$HOME/.bashrc"; [[ -f "$HOME/.zshrc" ]] && RC="$HOME/.zshrc"
grep -q '.local/bin' "$RC" 2>/dev/null || echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$RC"

FUNC='aio() { if [[ $# -eq 0 ]] && [[ -f "$HOME/.local/share/aios/help_cache_short.txt" ]] && [[ "$HOME/.local/bin/aio" -ot "$HOME/.local/share/aios/help_cache_short.txt" ]]; then cat "$HOME/.local/share/aios/help_cache_short.txt"; elif [[ "$1" == "help" ]] && [[ -f "$HOME/.local/share/aios/help_cache_full.txt" ]] && [[ "$HOME/.local/bin/aio" -ot "$HOME/.local/share/aios/help_cache_full.txt" ]]; then cat "$HOME/.local/share/aios/help_cache_full.txt"; else "$HOME/.local/bin/aio" "$@"; fi; }'
[[ -f "$RC" ]] && { sed -i '/^aio() {/d' "$RC" 2>/dev/null || sed -i '' '/^aio() {/d' "$RC"; }; echo "$FUNC" >> "$RC"

[[ ! -s "$HOME/.tmux.conf" ]] && "$BIN/aio" config tmux_conf y 2>/dev/null

if command -v aio >/dev/null; then
    echo -e "${G}✓ Updated${R}"
else
    echo -e "${G}Done!${R} Run: source $RC && aio"
fi
