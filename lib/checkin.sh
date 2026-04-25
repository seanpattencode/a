#!/bin/sh
# a checkin <session> [prompt] — wake an LLM tmux session with a prompt.
#
# How it works:
#   1. <session> is a tmux window name in the `a:` session running an LLM TUI
#      (claude code, codex, gemini, etc.) sitting idle at its input prompt.
#   2. We exec `a send <session> <prompt>`, which calls `tmux send-keys -l`
#      (literal text, no key translation) then `tmux send-keys Enter` to
#      submit. The LLM treats the injected text as if the user typed it.
#   3. Default prompt is intentionally terse — "send a email on status" —
#      so the woken LLM decides what to inspect (logs, jobs, ckpts) and
#      writes its own custom email body via `a email`. Pass a custom prompt
#      as args to override.
#   4. Schedule via hub: `a hub add checkin-1h "*:00" a checkin <session>`.
#      Hub installs a systemd --user timer that runs this every hour;
#      target session must still exist when the timer fires.
sn="$1"; [ -n "$sn" ] && shift || sn="$(tmux display-message -p -t "${TMUX_PANE:-}" '#{window_name}' 2>/dev/null)"
[ -n "$sn" ] || { echo "usage: a checkin [session] [prompt] (no tmux window detected)"; exit 1; }
p="${*:-send a email on status}"
dev="$(grep -lF "OS: Linux $(uname -r)" /home/sean/a/adata/git/ssh/*.txt 2>/dev/null | head -1 | xargs -r awk -F': ' '/^Name:/{print $2;exit}')"
[ -n "$dev" ] || dev="$(hostname -s 2>/dev/null || hostname)"
exec a send "$sn" "$p (append [dev=$dev win=$sn] to email subject)"
