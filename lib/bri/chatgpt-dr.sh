#!/usr/bin/env bash
# ChatGPT Deep Research — fire a prompt end-to-end, log the chat URL.
# Usage: lib/bri/chatgpt-dr.sh "your prompt here"
# Prereq: `a bri serve` running, Firefox signed into chatgpt.com (Pro tier).
#
# Discovered selectors (ChatGPT Pro UI, May 2026):
#   + button → button[aria-label="Add files and more"]
#   Deep research entry → [role=menuitemradio] with text "Deep research"
#   prompt input → textarea (id=prompt-textarea or placeholder*="Ask")
#   submit → KeyboardEvent Enter on the textarea works
#   active-DR confirmation → chip "Deep research, click to remove" present
#   chat URL → https://chatgpt.com/c/<uuid> (capture after submit)
#
# Differs from Gemini: textarea (not Quill, .value= works); standard ARIA roles
# instead of mat-list-item; ChatGPT auto-starts research without a separate
# "Start research" approval click (unlike Gemini). Both still need the full
# pointer-event chain to fire Angular/React click handlers reliably.
set -e
PROMPT="${1:?usage: $0 \"prompt\"}"

bri() { a bri "$@" 2>/dev/null; }

firefox-nightly --new-tab "https://chatgpt.com/" 2>/dev/null & disown
sleep 6

# 1. + menu + click Deep research (chained — menu auto-dismisses on next sync call)
bri "{\"id\":1,\"action\":\"eval\",\"code\":\"(async()=>{const p=document.querySelector('button[aria-label=\\\"Add files and more\\\"]');if(!p)return'no plus';const pr=p.getBoundingClientRect();const po={bubbles:true,cancelable:true,clientX:pr.x+pr.width/2,clientY:pr.y+pr.height/2,button:0,buttons:1,view:window,pointerType:'mouse',isPrimary:true};['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const E=t.startsWith('pointer')?PointerEvent:MouseEvent;p.dispatchEvent(new E(t,po))});await new Promise(r=>setTimeout(r,800));const d=[...document.querySelectorAll('[role=menuitemradio]')].find(e=>(e.textContent||'').trim()==='Deep research');if(!d)return'no dr';const dr=d.getBoundingClientRect();const _do={bubbles:true,cancelable:true,clientX:dr.x+dr.width/2,clientY:dr.y+dr.height/2,button:0,buttons:1,view:window,pointerType:'mouse',isPrimary:true};['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const E=t.startsWith('pointer')?PointerEvent:MouseEvent;d.dispatchEvent(new E(t,_do))});return'ok'})()\"}" >/dev/null
sleep 2

# 2. type prompt into textarea (set .value directly + dispatch input)
ESC=$(printf '%s' "$PROMPT" | python3 -c 'import sys,json;print(json.dumps(sys.stdin.read())[1:-1])')
bri "{\"id\":2,\"action\":\"eval\",\"code\":\"(()=>{const e=document.querySelector('#prompt-textarea, textarea[placeholder*=\\\"Ask\\\"]');if(!e)return'no';e.focus();const s=Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype,'value').set;s.call(e,'${ESC}');e.dispatchEvent(new Event('input',{bubbles:true}));return e.value.length})()\"}" >/dev/null
sleep 1

# 3. submit via Enter
bri "{\"id\":3,\"action\":\"eval\",\"code\":\"(()=>{const e=document.querySelector('#prompt-textarea, textarea[placeholder*=\\\"Ask\\\"]');if(!e)return'no';e.focus();['keydown','keypress','keyup'].forEach(t=>e.dispatchEvent(new KeyboardEvent(t,{key:'Enter',code:'Enter',keyCode:13,which:13,bubbles:true,cancelable:true})));return'sent'})()\"}" >/dev/null
sleep 6

# 4. log the chat URL (ChatGPT redirects to /c/<uuid> after submit)
URL=$(bri url 2>/dev/null | grep -oE 'https://chatgpt\.com/c/[a-f0-9-]+' | head -1)
echo "$(date -Iseconds) chatgpt-deep-research ${URL:-pending} $PROMPT" >> "$HOME/a/adata/git/urls.txt"
echo "+ logged → adata/git/urls.txt"
echo "+ chat: ${URL:-pending}"
