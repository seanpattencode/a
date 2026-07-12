#if 0
#!/bin/bash
# a keys on|off — key remaps: linux keyd, mac CGEventTap (caps→summon)
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
    darwin*) launchctl unload ~/Library/LaunchAgents/a-keys.plist 2>/dev/null||true; hidutil property --set '{"UserKeyMapping":[]}' >/dev/null 2>&1||true; rm -f ~/Library/LaunchAgents/a-keys.plist "$ABIN/a-keys" "$ABIN/a-keys.swift" "$ABIN/a-summon.command"; ok "a-keys removed, capslock restored" ;;
    *) warn "unsupported OS" ;;
    esac
    exit 0
fi

case "$OSTYPE" in
linux*)
    if grep -qi microsoft /proc/version 2>/dev/null; then
        WU=$(powershell.exe -NoProfile -Command 'echo $env:USERNAME'|tr -d '\r\n ')
        SU="/mnt/c/Users/$WU/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Startup"
        mkdir -p "$SU"; cat > "$SU/a-rshift.ahk" << 'AHK'
#Requires AutoHotkey v2.0
~RShift::{
KeyWait "RShift"
if A_PriorKey="RShift"
SendInput "^{PgDn}"
}
AHK
        powershell.exe -NoProfile -Command "if(!(Get-Command AutoHotkey64.exe -EA 0)){winget install -e --silent --accept-package-agreements --accept-source-agreements AutoHotkey.AutoHotkey|Out-Null}" 2>/dev/null || :
        cmd.exe /c start "" "$(wslpath -w "$SU/a-rshift.ahk")" 2>/dev/null &
        ok "WSL: right shift = Ctrl+PageDown"
        exit 0
    fi
    { command -v keyd >/dev/null || sudo apt install -y keyd 2>/dev/null || sudo dnf install -y keyd 2>/dev/null || sudo pacman -S --noconfirm keyd 2>/dev/null; } && printf '[ids]\n*\n\n[main]\nleftshift = overloadt2(shift, A-left, 200)\nrightshift = overloadt2(shift, A-right, 200)\n\n[control]\ntab = A-right\n\n[control+shift]\ntab = A-left\n' | sudo tee /etc/keyd/default.conf >/dev/null && sudo systemctl enable --now keyd 2>/dev/null && sudo systemctl restart keyd && ok "keyd → Ctrl+Tab/Ctrl+Shift+Tab + L/R-shift tap = next/prev window" || warn "keyd skipped" ;;
darwin*)
    pgrep -x Hammerspoon >/dev/null && { killall Hammerspoon 2>/dev/null; rm -f ~/.hammerspoon/init.lua; info "stopped Hammerspoon"; }
    command -v swiftc >/dev/null || { warn "need Command Line Tools: xcode-select --install"; exit 1; }
    printf '#!/bin/sh\nexec "%s/a" i\n' "$ABIN" > "$ABIN/a-summon.command"; chmod +x "$ABIN/a-summon.command"
    cat > "$ABIN/a-keys.swift" << 'SWIFT'
import ApplicationServices
import Foundation
func run(_ a:[String]){let p=Process();p.executableURL=URL(fileURLWithPath:a[0]);p.arguments=Array(a.dropFirst());try? p.run()}
run(["/usr/bin/hidutil","property","--set","{\"UserKeyMapping\":[{\"HIDKeyboardModifierMappingSrc\":0x700000039,\"HIDKeyboardModifierMappingDst\":0x70000006D}]}"])
_ = AXIsProcessTrustedWithOptions([kAXTrustedCheckOptionPrompt.takeUnretainedValue():true] as CFDictionary)
let a=CommandLine.arguments, tmux=a.count>1 ? a[1]:"tmux", summon=a.count>2 ? a[2]:""
var t=0.0, cand=false
let cb:CGEventTapCallBack={_,type,e,_ in
  let kc=e.getIntegerValueField(.keyboardEventKeycode)
  if type == .keyDown { cand=false; if kc==79 && e.getIntegerValueField(.keyboardEventAutorepeat)==0 {run(["/usr/bin/open",summon])} }
  else if kc==60 {
    if e.flags.contains(.maskShift) {t=Date().timeIntervalSince1970; cand=true}
    else if cand && Date().timeIntervalSince1970-t<0.3 {run([tmux,"if-shell","ps -o comm= -t #{pane_tty} 2>/dev/null|grep -qE ^ssh","if-shell \"a fl n #{pane_id}\" next-window","next-window"]); cand=false} }
  return Unmanaged.passUnretained(e) }
let m=CGEventMask((1<<CGEventType.flagsChanged.rawValue)|(1<<CGEventType.keyDown.rawValue))
guard let tap=CGEvent.tapCreate(tap:.cgSessionEventTap,place:.headInsertEventTap,options:.listenOnly,eventsOfInterest:m,callback:cb,userInfo:nil) else {exit(1)}
CFRunLoopAddSource(CFRunLoopGetCurrent(),CFMachPortCreateRunLoopSource(nil,tap,0),.commonModes)
CGEvent.tapEnable(tap:tap,enable:true); CFRunLoopRun()
SWIFT
    swiftc "$ABIN/a-keys.swift" -o "$ABIN/a-keys" || { warn "swiftc failed"; exit 1; }
    PL=~/Library/LaunchAgents/a-keys.plist
    printf '<plist version="1.0"><dict><key>Label</key><string>a-keys</string><key>ProgramArguments</key><array><string>%s/a-keys</string><string>%s</string><string>%s/a-summon.command</string></array><key>RunAtLoad</key><true/></dict></plist>\n' "$ABIN" "$(command -v tmux)" "$ABIN" > "$PL"
    launchctl unload "$PL" 2>/dev/null||:; launchctl load "$PL"
    ok "a-keys → caps→summon · right-shift→next-window"
    info "GRANT: System Settings → Accessibility → remove old a-keys, then 'a keys' to re-prompt" ;;
*)  warn "unsupported OS" ;;
esac

exit 0
#endif
int main(void){return 0;}
