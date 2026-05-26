#!/usr/bin/env bash
# Gemini Deep Research — fire a prompt end-to-end and emit the final report text.
# Usage: lib/bri/gemini-dr.sh "your prompt here"
# Prereq: `a bri serve` running, Firefox signed into gemini.google.com.
#
# Discovered selectors (Gemini Pro UI, May 2026):
#   + button → aria-label "Upload & tools"
#   Deep research entry → <mat-list-item> containing text "Deep research"
#   prompt input → rich-textarea [contenteditable]
#   submit → button[aria-label="Send message"]
#   start research → button with text "Start research"
#   final report → look for "Open in Docs" or "Export" button to know it's done
#
# All clicks dispatch the full pointer sequence with coordinates; Angular Material
# ignores bare .click() and dispatchEvent('click'). Text inserts char-by-char via
# beforeinput+execCommand+input because Quill (ql-editor) blocks bulk execCommand.
set -e
PROMPT="${1:?usage: $0 \"prompt\"}"

bri() { a bri "$@" 2>/dev/null; }

# 1. navigate (idempotent — focuses existing tab if open)
firefox-nightly --new-tab "https://gemini.google.com/app" 2>/dev/null & disown
sleep 5

# 2. open + menu
bri click 'button[aria-label*="Upload"]' >/dev/null
sleep 1

# 3. click Deep research (full pointer-event sequence, no aria-label so use text)
bri "{\"id\":1,\"action\":\"eval\",\"code\":\"(()=>{const i=[...document.querySelectorAll('mat-list-item')].find(e=>(e.textContent||'').includes('Deep research'));if(!i)return'no';const r=i.getBoundingClientRect();const x=r.x+r.width/2,y=r.y+r.height/2;const o={bubbles:true,cancelable:true,clientX:x,clientY:y,button:0,buttons:1,view:window,pointerType:'mouse',isPrimary:true};['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const E=t.startsWith('pointer')?PointerEvent:MouseEvent;i.dispatchEvent(new E(t,o))});return'ok'})()\"}" >/dev/null
sleep 2

# 4. type prompt char-by-char (Quill needs per-char beforeinput+execCommand+input)
ESC=$(printf '%s' "$PROMPT" | python3 -c 'import sys,json;print(json.dumps(sys.stdin.read())[1:-1])')
bri "{\"id\":2,\"action\":\"eval\",\"code\":\"(()=>{const e=document.querySelector('rich-textarea [contenteditable]');if(!e)return'no';e.focus();const t='${ESC}';for(const c of t){e.dispatchEvent(new InputEvent('beforeinput',{bubbles:true,cancelable:true,inputType:'insertText',data:c}));document.execCommand('insertText',false,c);e.dispatchEvent(new InputEvent('input',{bubbles:true,inputType:'insertText',data:c}))}return e.textContent.length})()\"}" >/dev/null
sleep 1

# 5. click Send message
bri "{\"id\":3,\"action\":\"eval\",\"code\":\"(()=>{const b=document.querySelector('button[aria-label=\\\"Send message\\\"]');if(!b)return'no';const r=b.getBoundingClientRect();const x=r.x+r.width/2,y=r.y+r.height/2;const o={bubbles:true,cancelable:true,clientX:x,clientY:y,button:0,buttons:1,view:window,pointerType:'mouse',isPrimary:true};['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const E=t.startsWith('pointer')?PointerEvent:MouseEvent;b.dispatchEvent(new E(t,o))});return'ok'})()\"}" >/dev/null

# 6. wait for the research plan + Start research button (event-driven)
echo "+ waiting for research plan..."
until bri "{\"id\":4,\"action\":\"find\",\"sel\":\"button\",\"text\":\"start research\"}" | grep -q '"n":1'; do sleep 2; done

# 7. click Start research
bri "{\"id\":5,\"action\":\"eval\",\"code\":\"(()=>{const b=[...document.querySelectorAll('button')].find(x=>(x.textContent||'').trim()==='Start research');if(!b)return'no';const r=b.getBoundingClientRect();const x=r.x+r.width/2,y=r.y+r.height/2;const o={bubbles:true,cancelable:true,clientX:x,clientY:y,button:0,buttons:1,view:window,pointerType:'mouse',isPrimary:true};['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const E=t.startsWith('pointer')?PointerEvent:MouseEvent;b.dispatchEvent(new E(t,o))});return'ok'})()\"}" >/dev/null

# 7b. log the chat URL + prompt for later recall (Gemini often hangs on "Analyzing
# results" forever — chat persists, can be revisited via this log). Format matches
# the per-line space-separated convention of adata/git/email.txt et al:
#   <iso-datetime> <source> <url> <prompt/note>
sleep 2
URL=$(bri url 2>/dev/null | grep -oE 'https://gemini[^"]*/app/[^" ]+' | head -1)
echo "$(date -Iseconds) gemini-deep-research ${URL:-pending} $PROMPT" >> "$HOME/a/adata/git/urls.txt"
echo "+ logged → adata/git/urls.txt"

# 8. wait for completion. Gemini has a known server-side bug where Deep Research
# gets stuck on "Analyzing results" forever (reproduced in both Firefox and
# Chrome Canary, 2026-05). Cap the wait at 20 min so we don't block on it.
# When that happens the chat URL persists (gemini.google.com/app/<id>) and the
# user can revisit later — the research often completes hours later or never.
echo "+ research running, polling for completion (max 20 min, then assume Gemini stuck)..."
END=$(($(date +%s) + 1200))
while [ $(date +%s) -lt $END ]; do
    bri "{\"id\":6,\"action\":\"find\",\"sel\":\"button\",\"text\":\"open in docs\"}" | grep -q '"n":1' && break
    sleep 30
done

# 9. emit the report (best effort — may be partial if Gemini stuck on Analyzing)
bri text 'message-content' | grep -oE '"value":"[^"]*"' | head -1
echo "+ chat URL: $(bri url | grep -oE 'https://gemini[^"]*' | head -1)"
