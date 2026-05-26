#!/usr/bin/env bash
# Perplexity Deep Research — DRAFT (selectors partially discovered; needs
# verification of Deep Research menu location on Sean's account).
# Usage: lib/bri/perplexity-dr.sh "your prompt here"
# Prereq: `a bri serve` running, Firefox signed into perplexity.ai (Pro tier).
#
# Discovered so far (perplexity.ai UI, May 2026):
#   + button → button[aria-label="Add files or tools"]
#   visible buttons in input row: Scheduled, Use incognito, Add files or tools,
#     Search, Computer, Model, Dictation, Submit
#   submit → button[aria-label="Submit"]
#   chat URL → https://www.perplexity.ai/search/<slug> after submit
#
# NOT YET VERIFIED: location of Deep Research toggle. Likely in the Model
# dropdown (click Model button, pick "Sonar Reasoning Pro" / "Deep Research")
# OR in the + menu items. Content script kept suspending mid-probe.
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
sleep 1

# 1. Open Model picker, attempt to select Deep Research / Pro mode.
#    NOTE: this is a best-guess; if it doesn't work, replace the selector with
#    the actual Deep Research menu item from your Perplexity Pro UI.
bri "{\"id\":1,\"action\":\"eval\",\"code\":\"(async()=>{const b=[...document.querySelectorAll('button')].find(x=>x.textContent.trim()==='Model'||(x.getAttribute('aria-label')||'')==='Model');if(!b)return'no model btn';const pr=b.getBoundingClientRect();const po={bubbles:true,cancelable:true,clientX:pr.x+pr.width/2,clientY:pr.y+pr.height/2,button:0,buttons:1,pointerType:'mouse',isPrimary:true};['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const E=t.startsWith('pointer')?PointerEvent:MouseEvent;b.dispatchEvent(new E(t,po))});await new Promise(r=>setTimeout(r,800));const dr=[...document.querySelectorAll('button,div,li')].find(x=>{const t=(x.textContent||'').trim().toLowerCase();return t==='deep research'||t.includes('deep research')||t.includes('sonar reasoning pro')});if(!dr)return'no dr option';const r=dr.getBoundingClientRect();['pointerdown','mousedown','pointerup','mouseup','click'].forEach(s=>{const E=s.startsWith('pointer')?PointerEvent:MouseEvent;dr.dispatchEvent(new E(s,{bubbles:true,cancelable:true,clientX:r.x+r.width/2,clientY:r.y+r.height/2,button:0,buttons:1,pointerType:'mouse',isPrimary:true}))});return'ok'})()\"}" >/dev/null
sleep 2

# 2. type prompt — Perplexity input is a textarea or contenteditable; try both
ESC=$(printf '%s' "$PROMPT" | python3 -c 'import sys,json;print(json.dumps(sys.stdin.read())[1:-1])')
bri "{\"id\":2,\"action\":\"eval\",\"code\":\"(()=>{const ta=document.querySelector('textarea');const ce=document.querySelector('[contenteditable=true]');const e=ta||ce;if(!e)return'no input';e.focus();if(e.tagName==='TEXTAREA'){const s=Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype,'value').set;s.call(e,'${ESC}');e.dispatchEvent(new Event('input',{bubbles:true}))}else{document.execCommand('insertText',false,'${ESC}');e.dispatchEvent(new InputEvent('input',{bubbles:true,inputType:'insertText',data:'${ESC}'}))}return(e.value||e.textContent).slice(0,40)})()\"}" >/dev/null
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
