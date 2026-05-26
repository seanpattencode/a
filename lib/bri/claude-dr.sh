#!/usr/bin/env bash
# Claude.ai Research — fire a prompt end-to-end, log the chat URL.
# Usage: lib/bri/claude-dr.sh "your prompt here"
# Prereq: `a bri serve` running, Firefox signed into claude.ai.
#
# Discovered selectors (Claude.ai UI, May 2026):
#   + button → button[aria-label="Add files, connectors, and more"]
#   Research entry → div[role=menuitemcheckbox] containing text "Research"
#   prompt input → div[contenteditable=true]  (proven by bri.py docstring)
#   submit → KeyboardEvent Enter on the contenteditable
#   chat URL → https://claude.ai/chat/<uuid> after submit
#
# Same pattern as Gemini/ChatGPT: + menu → Research → type → send. Per-char
# beforeinput+execCommand+input not needed here (per Sean's bri.py docstring
# Claude.ai's contenteditable accepts native execCommand insertText fine).
set -e
PROMPT="${1:?usage: $0 \"prompt\"}"

bri() { a bri "$@" 2>/dev/null; }

firefox-nightly --new-tab "https://claude.ai/new" 2>/dev/null & disown
sleep 6

# 1. + menu + click Research's wide ancestor (chained in one eval — menu auto-
#    dismisses between separate bri calls due to focus loss).
bri "{\"id\":1,\"action\":\"eval\",\"code\":\"(async()=>{const p=document.querySelector('button[aria-label=\\\"Add files, connectors, and more\\\"]');if(!p)return'no plus';const pr=p.getBoundingClientRect();['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const E=t.startsWith('pointer')?PointerEvent:MouseEvent;p.dispatchEvent(new E(t,{bubbles:true,cancelable:true,clientX:pr.x+pr.width/2,clientY:pr.y+pr.height/2,button:0,buttons:1,view:window,pointerType:'mouse',isPrimary:true}))});await new Promise(r=>setTimeout(r,800));const t=[...document.querySelectorAll('div[role=menuitemcheckbox]')].find(e=>(e.textContent||'').includes('Research'));if(!t)return'no research';const r=t.getBoundingClientRect();['pointerdown','mousedown','pointerup','mouseup','click'].forEach(s=>{const E=s.startsWith('pointer')?PointerEvent:MouseEvent;t.dispatchEvent(new E(s,{bubbles:true,cancelable:true,clientX:r.x+r.width/2,clientY:r.y+r.height/2,button:0,buttons:1,view:window,pointerType:'mouse',isPrimary:true}))});return'ok'})()\"}" >/dev/null
sleep 2

# 2. type prompt into contenteditable (proven path from bri.py docstring)
bri click 'div[contenteditable=true]' >/dev/null
bri type 'div[contenteditable=true]' "$PROMPT" >/dev/null
sleep 1

# 3. submit via Enter
bri keys 'div[contenteditable=true]' Enter >/dev/null
sleep 8

# 4. log the chat URL — must verify by content. bri url broadcasts to ALL tabs
# so a regex match returns whatever /chat/ comes first (often a STALE chat).
# Instead, eval on every tab and only accept a tab where the body contains a
# unique fragment from THIS prompt. Poll because claude.ai's content script is
# reliably slow to respond.
FRAG=$(printf '%s' "$PROMPT" | head -c 30 | sed 's/"/\\"/g')
URL=""
for i in 1 2 3 4 5 6; do
    truncate -s 0 /tmp/bri.log 2>/dev/null
    bri "{\"id\":99,\"action\":\"eval\",\"code\":\"location.pathname.startsWith('/chat/')&&(document.body.innerText||'').includes('${FRAG}')?location.href:null\"}" >/dev/null
    sleep 6
    URL=$(grep '"id":99' /tmp/bri.log | grep -oE 'https://claude\.ai/chat/[a-f0-9-]+' | head -1)
    [ -n "$URL" ] && break
done
echo "$(date -Iseconds) claude-research ${URL:-pending} $PROMPT" >> "$HOME/a/adata/git/urls.txt"
echo "+ logged → adata/git/urls.txt"
echo "+ chat: ${URL:-pending}"
