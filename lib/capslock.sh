#!/bin/bash
# a set capslock on|off — remap capslock+a to launch a i or a ui
set -e
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; ACTION="${1:-on}"; ABIN="${D%%/adata/worktrees/*}/adata/local"
G='\033[32m' Y='\033[33m' C='\033[36m' R='\033[0m'
ok() { echo -e "${G}✓${R} $1"; }; info() { echo -e "${C}>${R} $1"; }; warn() { echo -e "${Y}!${R} $1"; }
KB="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/a-launch/"

if [[ "$ACTION" == "off" ]]; then
    case "$OSTYPE" in
    linux*)
        # Remove caps:hyper from xkb options
        OPTS=$(gsettings get org.gnome.desktop.input-sources xkb-options 2>/dev/null)
        NEW=$(echo "$OPTS" | sed "s/, *'caps:hyper'//;s/'caps:hyper', *//;s/'caps:hyper'//" | sed "s/\[, /[/")
        [[ "$NEW" != "$OPTS" ]] && gsettings set org.gnome.desktop.input-sources xkb-options "$NEW" && ok "capslock restored"
        # Remove GNOME shortcut
        gsettings reset org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$KB name 2>/dev/null
        gsettings reset org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$KB command 2>/dev/null
        gsettings reset org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$KB binding 2>/dev/null
        EX=$(gsettings get org.gnome.settings-daemon.plugins.media-keys custom-keybindings 2>/dev/null)
        NEW=$(echo "$EX" | sed "s|, *'$KB'||;s|'$KB', *||;s|'$KB'||" | sed "s/\[, /[/")
        gsettings set org.gnome.settings-daemon.plugins.media-keys custom-keybindings "$NEW"
        # Restore GNOME Super+a
        gsettings set org.gnome.shell.keybindings toggle-application-view "['<Super>a']" 2>/dev/null
        ok "capslock remap off" ;;
    *) warn "unsupported OS" ;;
    esac
    exit 0
fi

# === ON ===
# Write launcher script
cat > "$ABIN/a-launch" << 'LAUNCH'
#!/bin/sh
ABIN="$(dirname "$0")"
[ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ] && exit 0
if curl -s -o /dev/null http://a.local:1111 2>/dev/null; then
    xdg-open "http://a.local:1111" >/dev/null 2>&1 &
else
    for T in ptyxis gnome-terminal alacritty foot xterm; do command -v "$T" >/dev/null 2>&1 && break; done
    case "$T" in
        ptyxis|gnome-terminal) "$T" -- "$ABIN/a" i ;;
        alacritty) "$T" -e "$ABIN/a" i ;;
        foot) "$T" "$ABIN/a" i ;;
        *) xterm -e "$ABIN/a" i ;;
    esac
fi
LAUNCH
chmod +x "$ABIN/a-launch"; ok "wrote $ABIN/a-launch"

case "$OSTYPE" in
linux*)
    if grep -qi microsoft /proc/version 2>/dev/null; then
        WU=$(powershell.exe -NoProfile -Command 'echo $env:USERNAME'|tr -d '\r\n ')
        SU="/mnt/c/Users/$WU/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Startup"
        mkdir -p "$SU"; cat > "$SU/a-rshift.ahk" << 'AHK'
#Requires AutoHotkey v2.0
~RShift::return
~RShift Up::{
    if (A_PriorKey = "RShift")
        SendInput "^b{n}"
}
AHK
        powershell.exe -NoProfile -Command "if(!(Get-Command AutoHotkey64.exe -EA 0)){winget install -e --silent --accept-package-agreements --accept-source-agreements AutoHotkey.AutoHotkey|Out-Null}" 2>/dev/null || :
        cmd.exe /c start "" "$(wslpath -w "$SU/a-rshift.ahk")" 2>/dev/null &
        ok "WSL: right shift tap = tmux next-window"
        exit 0
    fi
    # Capslock → Hyper
    OPTS=$(gsettings get org.gnome.desktop.input-sources xkb-options 2>/dev/null)
    if [[ "$OPTS" != *"caps:hyper"* ]]; then
        if [[ "$OPTS" == "@as []" || "$OPTS" == "[]" ]]; then OPTS="['caps:hyper']"
        else OPTS="${OPTS%]*}, 'caps:hyper']"; fi
        gsettings set org.gnome.desktop.input-sources xkb-options "$OPTS"
        ok "capslock → Hyper"
    else info "capslock already Hyper"; fi
    # Unbind GNOME Super+a (conflicts since Hyper=Super on mod4)
    gsettings set org.gnome.shell.keybindings toggle-application-view "[]" 2>/dev/null
    info "unbound Super+a (app view) to avoid conflict"
    # Hyper+a → launcher
    EX=$(gsettings get org.gnome.settings-daemon.plugins.media-keys custom-keybindings 2>/dev/null)
    if [[ "$EX" != *"a-launch"* ]]; then
        if [[ "$EX" == "@as []" || "$EX" == "[]" ]]; then EX="['$KB']"
        else EX="${EX%]*}, '$KB']"; fi
        gsettings set org.gnome.settings-daemon.plugins.media-keys custom-keybindings "$EX"
    fi
    gsettings set org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$KB name 'a launch'
    gsettings set org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$KB command "$ABIN/a-launch"
    gsettings set org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$KB binding '<Hyper>a'
    ok "CapsLock+a → a i (or a ui if running)"
    sudo apt install -y keyd >/dev/null 2>&1 && printf "[ids]\n*\n\n[main]\nrightshift = overloadt(shift, command(runuser -u %s -- %s next-window), 200)\n" "$USER" "$(command -v tmux)" | sudo tee /etc/keyd/default.conf >/dev/null && sudo systemctl enable --now keyd 2>/dev/null && sudo systemctl restart keyd && ok "keyd → right shift tap = tmux next-window" || warn "keyd skipped" ;;
darwin*)
    [[ -d /Applications/Hammerspoon.app ]] || { info "installing Hammerspoon..."; brew install --cask hammerspoon &>/dev/null || { warn "brew install failed"; exit 1; }; }
    TMUX_BIN=$(command -v tmux)
    mkdir -p ~/.hammerspoon
    cat > ~/.hammerspoon/init.lua << LUA
local t,c,et=0,false,hs.eventtap.event.types
hs.eventtap.new({et.flagsChanged},function(e)
  if e:getKeyCode()==60 then
    if e:getFlags().shift then t=hs.timer.secondsSinceEpoch();c=true
    elseif c and hs.timer.secondsSinceEpoch()-t<.3 then hs.task.new("$TMUX_BIN",nil,{"next-window"}):start();c=false end end end):start()
hs.eventtap.new({et.keyDown},function() c=false end):start()
LUA
    killall Hammerspoon 2>/dev/null; sleep 1; open -a Hammerspoon
    ok "Hammerspoon → right shift tap = tmux next-window"
    info "GRANT: System Settings → Privacy & Security → Accessibility → enable Hammerspoon" ;;
*)  warn "unsupported OS — run $ABIN/a-launch manually" ;;
esac
