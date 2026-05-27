#!/usr/bin/env bash
# Perplexity Deep Research — DRAFT (selectors partially discovered; needs
# verification of Deep Research menu location on Sean's account).
# Usage: lib/bri/perplexity-dr.sh "your prompt here"
# Prereq: `a bri serve` running, Firefox signed into perplexity.ai (Pro tier).
#
# Discovered selectors (perplexity.ai UI, May 2026):
#   Deep Research opens via the "Search" button (NOT the + / "Add files or
#     tools" — that one is for file uploads only). Click Search → menu shows
#     "Deep Research" entry.
#   submit → button[aria-label="Submit"]
#   input → [contenteditable=true] (Perplexity has no <textarea>)
#   chat URL → https://www.perplexity.ai/search/<slug> after submit
#
# Quirks vs Gemini/Claude: PointerEvent constructor REJECTS view:window on
# perplexity.ai ("'view' member does not implement Window") — drop view from
# the pointer-event init dict here. Same CSP-restricted connect-src as
# Claude.ai (so the bri-ext background fallback is required — TM userscript
# helps reliability too).
set -e
PROMPT="${1:?usage: $0 \"prompt\"}"

bri() { a bri "$@" 2>/dev/null; }

firefox-nightly --new-tab "https://www.perplexity.ai/" 2>/dev/null & disown
sleep 7

wtype -M ctrl -k 0 -m ctrl 2>/dev/null  # reset zoom to 100% so coords are stable across sway tile sizes

. "$(dirname "$0")/_tm_check.sh"; tm_check "www.perplexity.ai" || exit 2
sleep 1

# 1. Click Search button → opens mode menu containing "Deep Research"
bri "{\"id\":1,\"action\":\"eval\",\"code\":\"(async()=>{const b=[...document.querySelectorAll('button')].find(x=>x.textContent.trim()==='Search'||(x.getAttribute('aria-label')||'')==='Search');if(!b)return'no search btn';const pr=b.getBoundingClientRect();const po={bubbles:true,cancelable:true,clientX:pr.x+pr.width/2,clientY:pr.y+pr.height/2,button:0,buttons:1,pointerType:'mouse',isPrimary:true};['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const E=t.startsWith('pointer')?PointerEvent:MouseEvent;b.dispatchEvent(new E(t,po))});await new Promise(r=>setTimeout(r,800));const dr=[...document.querySelectorAll('*')].find(x=>{const own=[...x.childNodes].filter(n=>n.nodeType===3).map(n=>n.textContent.trim()).join('');return own==='Deep Research'||own==='Deep research'});if(!dr)return'no dr option';let t=dr;while(t&&t.getBoundingClientRect().width<150)t=t.parentElement;const r=t.getBoundingClientRect();['pointerdown','mousedown','pointerup','mouseup','click'].forEach(s=>{const E=s.startsWith('pointer')?PointerEvent:MouseEvent;t.dispatchEvent(new E(s,{bubbles:true,cancelable:true,clientX:r.x+r.width/2,clientY:r.y+r.height/2,button:0,buttons:1,pointerType:'mouse',isPrimary:true}))});return'ok'})()\"}" >/dev/null
sleep 2

# 2. type prompt — Perplexity uses contenteditable (no textarea)
ESC=$(printf '%s' "$PROMPT" | python3 -c 'import sys,json;print(json.dumps(sys.stdin.read())[1:-1])')
bri "{\"id\":2,\"action\":\"eval\",\"code\":\"(()=>{const e=document.querySelector('[contenteditable=true]');if(!e)return'no input';e.focus();document.execCommand('insertText',false,'${ESC}');e.dispatchEvent(new InputEvent('input',{bubbles:true,inputType:'insertText',data:'${ESC}'}));return e.textContent.slice(0,40)})()\"}" >/dev/null
sleep 1

# 3. submit
bri click 'button[aria-label="Submit"]' >/dev/null
sleep 8

# 4. log chat URL — content-verified poll (~30s) like the claude recipe
FRAG=$(printf '%s' "$PROMPT" | head -c 30 | sed 's/"/\\"/g')
URL=""
for i in 1 2 3 4 5 6; do
    truncate -s 0 /tmp/bri.log 2>/dev/null
    bri "{\"id\":99,\"action\":\"eval\",\"code\":\"location.pathname.startsWith('/search/')&&(document.body.innerText||'').includes('${FRAG}')?location.href:null\"}" >/dev/null
    sleep 6
    URL=$(grep '"id":99' /tmp/bri.log 2>/dev/null | grep -oE 'https://www\.perplexity\.ai/search/[a-zA-Z0-9_-]+' | head -1)
    [ -n "$URL" ] && break
done

echo "$(date -Iseconds) perplexity-deep-research ${URL:-pending} $PROMPT" >> "$HOME/a/adata/git/urls.txt"
echo "+ logged → adata/git/urls.txt"
echo "+ chat: ${URL:-pending}"
