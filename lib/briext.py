#!/usr/bin/env python3
"""a briext — build both browser extensions, one script (Firefox MV2 + Chrome MV3)
into adata/local/ext/ on demand. Mirrors `a apk`: single source file, makes the folder tree.
Icons are defined ONCE here so the two cannot drift.
  a briext           generate -> adata/local/ext/{bri-ext,bri-chrome}, print load paths
Edit this file to change either extension; rerun to redeploy. Firefox xpi: a bri deploy."""
import os,sys,base64
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT=os.path.join(ROOT,"adata/local/ext")

ICONS={
"icon16.png": "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAIAAACQkWg2AAAAiklEQVR4nGP0WR/AQApgIkn1SNXAgsYX5BBI0Un++ffH9Xc3dj/cS9gGdwW3TXc3Tzo/1VzSjJGRkbAGGR7pJ1+eMjAwfPr5SYCdn7AGBgYGBob/DAwMDAyM//9jkUPX8OjzY1leWQYGBgF2/o+/PmJqQPf0zge70nRT3BVcjzw99h+bFYyDL/EBAI+3KYtvPAM2AAAAAElFTkSuQmCC",
"icon48.png": "iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAIAAADYYG7QAAACN0lEQVR4nO3Yv2sTYRgH8O97uUviXWiS0vYuDaJBbQcHDWmsSBUHUQSHglLcxE2cBUG6SsFJnKSI+AcUY13EyR9LnJpqhdKkifgjTY2hsWlyIbnrncN1qInwwhXfZni/08vz3PvwgRfuPY5cSU2ilyLsN6AzHEQLB9HCQbRwEC0cRAsH0cJBtHAQLRxEi7iXzQMHBqZGriXUeNgfbprNXDU/n3+ZKS/uDygWPDwzcV+R5K321spGNujrS6jxhBp/+vlZanWeNUggwt3kHUWSX315Pbv0xLRMAPGhk9Pj924ev/Hx11Jhs+BysrttpyPj0UC0WC8+/jTraABkyoup1ReEkKvHJt2NdQ86pSUBvP3x3rKt3fU3398BGNMSHuJhCjoaOgIgV8111Iv1om7qsigPByJMQRFFA1DWK92tSrMCQFM0diBR8Hg9XgC6oXd3G4YOICAp7ECS4HUWpm10dw3L2P0MC5BhtZ2FSKTuriRIANpWix3ItLZb2y0Ayr/OxTmservBDgRgrV4CoCpDHXVCyKA8CKBYX2MKylZzAEbDIx31WF/M7/Fvtmrr+jpTULr0AcD5g+dE4a8X4MVDFwCkS2nbtpmCFn5m8r8LqqzePnFLFHYuxInomcuxS6ZlzmVT7sYCIK5/6Q0HIg/OzgR9wVq79rX2rd8fjgailm09XHjkXCCsQQBCvtD10amkNtbvDzcMfXljeS77fKWadT1wr6D/kZ77hOUgWjiIFg6ihYNo6TnQH7GDr3b+0FoyAAAAAElFTkSuQmCC",
"icon128.png": "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",
}
FF={
"manifest.json": r'''{
  "manifest_version": 2,
  "name": "a-bridge",
  "version": "1.2",
  "description": "HTTP long-poll bridge for `a` automation. The SINGLE poll connection lives in background.js (one connection, not tab-throttled); content.js runs dispatched commands per-frame. Deps: Firefox Nightly + xpinstall.signatures.required=false in user.js.",
  "permissions": [
    "<all_urls>",
    "storage",
    "tabs",
    "activeTab",
    "webNavigation"
  ],
  "user_scripts": {"api_script": "api.js"},
  "icons": {"16":"icon16.png","48":"icon48.png","128":"icon128.png"},
  "browser_specific_settings": {
    "gecko": {
      "id": "a-bridge@seanpatten",
      "strict_min_version": "115.0"
    }
  },
  "background": {"scripts": ["background.js"]},
  "chrome_url_overrides": {"newtab": "newtab.html"},
  "content_scripts": [
    {
      "matches": [
        "<all_urls>"
      ],
      "js": [
        "content.js"
      ],
      "run_at": "document_end",
      "all_frames": true
    }
  ]
}''',
"background.js": r'''// a-bridge background — owns the SINGLE poll connection to the bridge. Previously every
// content-script frame polled independently; two failures forced this redesign:
//   1) Firefox caps persistent connections per server at 6. On Google sites every frame's
//      poll is CSP-routed through here as a held connection, so >6 frames (Gmail main +
//      its many subframes + other Google tabs) saturate the 6 slots — the target frame's
//      poll never registers, so it can POST a hello but never RECEIVE a command. ONE
//      background-owned poll = one connection, no saturation.
//   2) Firefox throttles timers in background/unfocused tabs, starving a content-script
//      poll loop. The persistent background page is NOT tab-throttled.
// Flow: background long-polls; each command is fanned out to every frame of every tab via
// tabs.sendMessage (message handlers fire even in throttled tabs); each frame's reply is
// POSTed to /resp with the command id. open/screenshot are handled HERE (no fan-out).
const POLL = 'http://127.0.0.1:1234/poll', RESP = 'http://127.0.0.1:1234/resp';
let BRI_CHAN = 'firefox';   // exact channel — UA is frozen ('Firefox/152.0') and hides Nightly; getBrowserInfo isn't
try { browser.runtime.getBrowserInfo().then(i => { let c = /a\d/.test(i.version)?'nightly':/b\d/.test(i.version)?'beta':(i.buildID||'').startsWith('2010')?'release':'build'; BRI_CHAN = 'firefox-'+c+'/'+i.version; }).catch(()=>{}); } catch(e) {}
const post = (d) => fetch(RESP, {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({chan:BRI_CHAN, ...d})}).catch(()=>{});

// open+focus a tab — deduped so a broadcast opens ONE tab (or focuses an existing one).
// Match by NORMALIZED url (origin+path, no trailing slash / query / hash): real pages mutate their
// URL (Yahoo /quote/ACN → /quote/ACN/, ?p=…), and exact-match would miss the prefetched tab → dupes.
const _opening = new Map();
const _norm = u => { try { const x = new URL(u); return x.origin + x.pathname.replace(/\/+$/,''); }
                     catch (e) { return u.split(/[?#]/)[0].replace(/\/+$/,''); } };
function openTab(url, bg, fresh) {     // dedup by origin+path; hit → navigate to exact url
  if (fresh) return browser.tabs.create({url, active:!bg}).then(t => ({id:t.id, focused:!bg}));  // fresh: new tab
  const key = _norm(url);
  if (!_opening.has(key)) _opening.set(key, (async () => {
    const hit = (await browser.tabs.query({})).find(t => t.url && _norm(t.url) === key);
    const tab = hit || await browser.tabs.create({url, active:false});
    setTimeout(() => _opening.delete(key), 3000);
    return tab.id;
  })());
  const p = _opening.get(key);
  return bg ? p.then(id => ({id, focused:false}))
            : p.then(async id => { const t = await browser.tabs.get(id); await browser.tabs.update(id, t.url === url ? {active:true} : {url, active:true}); await browser.windows.update(t.windowId, {focused:true}); return {id, focused:true}; });  // same url = FOCUS only, no reload (a streaming answer survives; Sean 2026-09-03); else land on the EXACT url (SERP re-search), per-call not cached
}

// user.js loadDivertedInBackground (wiki-feed appends) backgrounds even hand-clicked target=_blank links; a click on a
// localhost dashboard must focus like Chrome. Human click = opener tab active+localhost; automation opens have no/bg opener.
browser.tabs.onCreated.addListener(async t => {
  if (t.active || !t.openerTabId) return;
  try { const o = await browser.tabs.get(t.openerTabId);
    if (o.active && /^https?:\/\/(localhost|127\.0\.0\.1):/.test(o.url)) browser.tabs.update(t.id, {active:true}); } catch (e) {}
});

// execute one command: open/screenshot run here; everything else fans out to all frames.
async function run(cmd) {
  const id = cmd.id;
  if (cmd.action === 'open') {
    try { return post({id, src:'background', ok:true, value: await openTab(cmd.url, cmd.bg, cmd.fresh)}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'screenshot') {
    try { return post({id, src:'background', ok:true, value: await browser.tabs.captureVisibleTab(null, {format: cmd.format||'png'})}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'navigate') {   // ACTIVE tab only (or the cmd.match tab). Must live here: the content-script path runs in EVERY frame of EVERY tab, so a bare `a bri <url>` BROADCAST-navigated every open tab — one command converted 4 live tabs, and for a query-by-URL site each hijacked tab started its own search (2026-08-08 perplexity storm; it also ate sibling providers' tabs mid-answer). Chrome's SW already scopes to active tabs; Firefox did not.
    try { const ts = await browser.tabs.query(cmd.match ? {} : {active:true, currentWindow:true});
      const t = cmd.match ? ts.find(x => (x.url||'').includes(cmd.match)) : ts[0];
      if (t) await browser.tabs.update(t.id, {url: cmd.url});
      return post({id, src:'background', ok:true, value:{navigated: t ? t.id : null}}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'close') {   // close the tab matching cmd.url (deck flip) or the active tab; privileged → must live here
    try { const ts = await browser.tabs.query(cmd.url ? {} : {active:true, currentWindow:true});
      const t = cmd.url ? ts.find(x => x.url && _norm(x.url) === _norm(cmd.url)) : ts[0];
      if (t) await browser.tabs.remove(t.id);
      return post({id, src:'background', ok:true, value:{closed: t ? t.id : null}}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'tabs') {   // list ALL tabs incl. error/discarded ones content scripts can't see
    try { return post({id, src:'background', ok:true, value:(await browser.tabs.query({})).filter(t=>!cmd.match||(t.url||'').includes(cmd.match)).map(t=>[t.id, t.windowId, t.discarded?'discarded':t.status, (t.url||'').slice(0,200), (t.title||'').slice(0,60)])}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'closeall') {   // close EVERY tab whose url contains cmd.match (batch job cleanup; match required)
    try { const hs = cmd.match ? (await browser.tabs.query({})).filter(t=>(t.url||'').includes(cmd.match)) : [];
      await browser.tabs.remove(hs.map(t=>t.id));
      return post({id, src:'background', ok:true, value:hs.length}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  const tabs = await browser.tabs.query({});
  await Promise.all(tabs.map(async (tab) => {
    let frames = null;
    try { frames = await browser.webNavigation.getAllFrames({tabId: tab.id}); } catch (e) {}
    const fids = (frames && frames.length) ? frames.map(f => f.frameId) : [0];
    await Promise.all(fids.map(async (fid) => {
      try {
        const out = await browser.tabs.sendMessage(tab.id, {__bri_cmd: cmd}, {frameId: fid});
        if (out) await post({id, ...out});
      } catch (e) { /* frame has no content script (about:/pdf/discarded) — skip silently */ }
    }));
  }));
}

// the one poll loop — re-registers immediately, runs the command without blocking the next poll
async function loop() {
  let r;
  try { r = await fetch(POLL, {headers:{'X-Bri-Chan':BRI_CHAN}}); } catch (e) { setTimeout(loop, 1500); return; }
  if (r.status === 200) {
    let cmd = null; try { cmd = await r.json(); } catch (e) {}
    loop();                       // re-register the poll before dispatching
    if (cmd) run(cmd).catch(()=>{});
    return;
  }
  loop();                          // 204 (idle) → poll again
}
loop();
post({src:'background', hello:'bri-ext background poll', v:'0.9'});

// internal messages (open/screenshot)
// kept here too so any in-page caller still works, sharing the same dedup as the poll path.
browser.runtime.onMessage.addListener(async (msg) => {
  if (!msg) return;
  if (msg.a === 'fetch') {
    const r = await fetch(msg.url, msg.opts || {});
    return {status: r.status, body: r.status === 200 ? await r.json() : null};
  }
  if (msg.action === 'open') return openTab(msg.url);
  if (msg.action === 'screenshot')
    return browser.tabs.captureVisibleTab(null, {format: msg.format || 'png'});
});
''',
"content.js": r'''// a-bridge content script — runs in EVERY frame (all_frames). It no longer polls; the
// SINGLE poll connection lives in background.js (see why there). This script just executes
// one dispatched command in ITS OWN frame and returns the result to the background, which
// POSTs it. Receiving a runtime message fires even in throttled/background tabs, so this
// path works where a content-script poll loop would be starved.
(() => {
  const $ = s => document.querySelector(s);
  const dispatch = async (m) => {
    try {
      if (m.match && !(self === top && location.href.includes(m.match))) return {skip:1};   // m.match targets any action at tabs whose URL contains it (top frame only)
      switch (m.action) {
        case 'navigate': if (self === top) top.location = m.url; return {ok:true};
        case 'click':    $(m.sel).click(); return {ok:true};
        case 'type':     { let e=$(m.sel);
                           if (!e.isContentEditable && e.tagName==='DIV')
                             e = e.querySelector('[contenteditable]') || e;
                           e.focus();
                           if (e.isContentEditable) document.execCommand('insertText',false,m.text);
                           else e.value = m.text;
                           e.dispatchEvent(new InputEvent('input',{bubbles:true,data:m.text,inputType:'insertText'}));
                           return {ok:true}; }
        case 'keys':     { const e=$(m.sel)||document.activeElement; e.focus();
                           (Array.isArray(m.keys)?m.keys:[m.keys]).forEach(k=>{
                             ['keydown','keyup'].forEach(t=>e.dispatchEvent(new KeyboardEvent(t,{key:k,bubbles:true,cancelable:true}))); });
                           return {ok:true}; }
        case 'text':     return {ok:true, value:$(m.sel).innerText};
        case 'html':     return {ok:true, value:document.documentElement.outerHTML.slice(0,200000)};
        case 'find':     { const need=(m.text||'').toLowerCase().trim();
                           const sel=m.sel||'button, [role="button"], a, [tabindex]:not([tabindex="-1"])';
                           const hits=[];
                           const walk=root=>{
                             for(const el of root.querySelectorAll(sel)){
                               const t=((el.innerText||el.textContent||'')+' '+(el.getAttribute('aria-label')||'')).toLowerCase();
                               if(!need||t.includes(need)) hits.push(el);
                             }
                             for(const el of root.querySelectorAll('*')) if(el.shadowRoot) walk(el.shadowRoot);
                           };
                           walk(document);
                           if(m.click&&hits.length) hits[0].click();
                           return {ok:true, value:{n:hits.length, first:(hits[0]?(hits[0].innerText||hits[0].getAttribute('aria-label')||'').trim().slice(0,80):null)}}; }
        case 'eval':     { let c=m.code; try{if(window.trustedTypes&&trustedTypes.createPolicy){const tt=window._abp||(window._abp=trustedTypes.createPolicy('abridge',{createScript:s=>s}));c=tt.createScript(m.code);}}catch(e){}
                           return {ok:true, value:await (async()=>eval(c))()}; }
        case 'wait':     await new Promise(r=>setTimeout(r,m.ms||500)); return {ok:true};
        case 'url':      return {ok:true, value:location.href};
        case 'links':    return {ok:true, value:[...document.querySelectorAll('a[href^="http"]')].filter(a=>a.offsetParent&&(a.innerText||'').trim().length>2).map(a=>[a.href,(a.innerText||'').trim().replace(/\s+/g,' ').slice(0,80)]).slice(0,300)};
        default: return {error:'unknown action: '+m.action};
      }
    } catch (e) { return {error:String(e)}; }
  };
  // Background fans each command here as {__bri_cmd}. Return the tagged result; background POSTs it.
  browser.runtime.onMessage.addListener((msg) => {
    if (msg && msg.__bri_cmd) return dispatch(msg.__bri_cmd).then(out => ({src: location.href, ...out}));
    // not ours → return undefined so other listeners (preload-debug etc.) still see it
  });
})();
''',
"api.js": r'''// bri-ext apiScript — exposes bridge_fetch to each registered userscript.
// Routes HTTP through background (CSP-exempt) so the userscript can talk to
// 127.0.0.1:1234 even when page CSP's connect-src would block window.fetch.
// This is the GM_xmlhttpRequest equivalent, simplified to one call.
browser.userScripts.onBeforeScript.addListener((script) => {
  script.defineGlobals({
    bridge_fetch: async (url, opts) =>
      browser.runtime.sendMessage({a: 'fetch', url, opts: opts || {}}),
  });
});
''',
"newtab.html": r'''<!doctype html><style>html,body{margin:0;height:100vh;background:#000}</style><script src="newtab.js"></script>''',
"newtab.js": r'''// Ctrl+T pins keyboard focus to the urlbar no matter what the page does (bugzilla 1411465);
// a tabs.CREATEd tab focuses content. NTO's trick: spawn the real tab, remove this shell.
browser.tabs.getCurrent().then(t => {
  browser.tabs.create({url: 'http://localhost:1111/', index: t.index + 1});
  browser.tabs.remove(t.id);
});
''',
}
CH={
"manifest.json": r'''{
  "manifest_version": 3,
  "name": "bri-chrome",
  "version": "1.8",
  "description": "Chrome extension: a-bridge automation (offscreen-doc long-poll :1234, focus-immune; commands run via chrome.scripting, no toggle).",
  "permissions": ["storage", "scripting", "alarms", "offscreen"],
  "host_permissions": ["<all_urls>"],
  "background": { "service_worker": "sw.js" },
  "chrome_url_overrides": { "newtab": "newtab.html" },
  "content_scripts": [
    { "matches": ["<all_urls>"], "js": ["wake.js"], "run_at": "document_start", "all_frames": false }
  ]
}
''',
"sw.js": r'''// bri-chrome service worker — privileged half of the bridge, works while Chrome is UNFOCUSED.
// MV3 SWs are killed (~30s) and CANNOT hold a long-poll, so the persistent poll lives in an OFFSCREEN
// DOCUMENT (offscreen.js — a real page, not tab-throttled, not SW-lifetime-capped). The offscreen relays
// each command here; the SW runs it in the target tab via chrome.scripting.executeScript (ISOLATED world,
// no "Allow user scripts" toggle) and POSTs the result to /resp. wake.js (content script) + onStartup/
// onInstalled/alarms re-create the offscreen doc if Chrome ever closes it — self-healing.
const RESP='http://127.0.0.1:1234/resp';
// keepalive:true lets the POST finish even if the SW is torn down the instant after (fire-and-forget from a
// dying worker otherwise aborts — this is why every earlier diagnostic vanished). Learned from claude-in-chrome.
const post=d=>fetch(RESP,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({chan:'chrome',...d}),keepalive:true}).catch(()=>{});
post({sw:'top',off:typeof chrome.offscreen});  // DIAG: SW ran + can reach :1234; reports if chrome.offscreen exists

// DISPATCH runs IN the target tab (ISOLATED world) via executeScript(func,args): pure fn of the command,
// returns the /resp payload. Must be self-contained (serialized standalone) — no outer refs. Edit = repack.
function DISPATCH(m){
  const $=s=>document.querySelector(s);
  if(m.vis&&document.hidden)return {skip:'hidden'};   // only the visible tab acts (Flutter ignores input when hidden)
  const PE=(t,x,y,b)=>new PointerEvent(t,{bubbles:true,cancelable:true,composed:true,clientX:x,clientY:y,view:window,pointerId:1,pointerType:'mouse',isPrimary:true,button:0,buttons:b});
  const run=async()=>{ switch(m.action){
    case 'navigate': top.location=m.url; return {ok:true};
    case 'click': $(m.sel).click(); return {ok:true};
    case 'tap': { const el=document.elementFromPoint(m.x,m.y)||document.body;   // synthetic tap at client coords (canvas/Flutter)
      ['pointerdown','mousedown','pointerup','mouseup','click'].forEach(t=>{const up=t.endsWith('up')||t==='click';
        el.dispatchEvent(t[0]==='p'?PE(t,m.x,m.y,up?0:1):new MouseEvent(t,{bubbles:true,cancelable:true,clientX:m.x,clientY:m.y,view:window,buttons:up?0:1}));});
      return {ok:true,value:el.tagName}; }
    case 'drag': { const el=$('flt-glass-pane')||document.elementFromPoint(m.x1,m.y1)||document.body,N=16;   // spin/pan a canvas
      el.dispatchEvent(PE('pointerdown',m.x1,m.y1,1));
      for(let i=1;i<=N;i++){const x=m.x1+(m.x2-m.x1)*i/N,y=m.y1+(m.y2-m.y1)*i/N;el.dispatchEvent(PE('pointermove',x,y,1));await new Promise(r=>setTimeout(r,16));}
      el.dispatchEvent(PE('pointerup',m.x2,m.y2,0)); return {ok:true}; }
    case 'type': { let e=$(m.sel); if(!e.isContentEditable&&e.tagName==='DIV')e=e.querySelector('[contenteditable]')||e; e.focus();
      if(e.isContentEditable)document.execCommand('insertText',false,m.text); else e.value=m.text;
      e.dispatchEvent(new InputEvent('input',{bubbles:true,data:m.text,inputType:'insertText'})); return {ok:true}; }
    case 'keys': { const e=$(m.sel)||document.activeElement; e.focus();
      (Array.isArray(m.keys)?m.keys:[m.keys]).forEach(k=>['keydown','keypress','keyup'].forEach(t=>e.dispatchEvent(new KeyboardEvent(t,{key:k,code:k,keyCode:k==='Enter'?13:0,bubbles:true,cancelable:true})))); return {ok:true}; }
    case 'text': return {ok:true,value:$(m.sel).innerText};
    case 'html': return {ok:true,value:document.documentElement.outerHTML.slice(0,200000)};
    case 'url': return {ok:true,value:location.href};
    case 'size': return {ok:true,value:[innerWidth,innerHeight]};
    case 'sem': { const p=document.querySelector('flt-semantics-placeholder');   // enable Flutter a11y tree, then enumerate labelled nodes
      if(p&&!document.querySelector('flt-semantics-host [role]')){p.click(); await new Promise(r=>setTimeout(r,900));}
      const seen=new Set(),out=[];
      for(const e of document.querySelectorAll('flt-semantics-host [aria-label],flt-semantics-host [role],flt-semantics-host input')){
        const r=e.getBoundingClientRect(); if(r.width<1&&r.height<1)continue;
        const lab=(e.getAttribute('aria-label')||e.getAttribute('role')||e.tagName), k=lab+'@'+(r.x+r.width/2|0)+','+(r.y+r.height/2|0);
        if(seen.has(k))continue; seen.add(k); out.push([e.getAttribute('role')||'',lab.slice(0,36),[r.x+r.width/2|0,r.y+r.height/2|0]]); }
      return {ok:true,value:out.slice(0,60)}; }
    case 'wait': await new Promise(r=>setTimeout(r,m.ms||500)); return {ok:true};
    case 'set': await chrome.storage.sync.set(m.kv||{}); return {ok:true,value:await chrome.storage.sync.get(null)};  // MV3 bans eval; flag flips
    default: return {error:'unknown action: '+m.action};
  } };
  return run().catch(e=>({error:String(e)}));
}

// open+focus; dedup by origin+path; hit → navigate to exact url (SW)
async function openTab(url,bg){const norm=u=>{try{const x=new URL(u);return x.origin+x.pathname.replace(/\/+$/,'')}catch(e){return (u||'').split(/[?#]/)[0]}};
  const key=norm(url);const hit=(await chrome.tabs.query({})).find(t=>t.url&&norm(t.url)===key);
  if(hit&&hit.url!==url)await chrome.tabs.update(hit.id,{url});
  const tab=hit||await chrome.tabs.create({url,active:!bg});if(hit&&!bg)await chrome.tabs.update(hit.id,{active:true});return{id:tab.id,focused:!bg};}

async function execCmd(cmd){
  const id=cmd.id;
  try{
    if(cmd.action==='screenshot')return post({id,src:'sw',ok:true,value:await chrome.tabs.captureVisibleTab({format:cmd.format||'png'})});
    if(cmd.action==='open')return post({id,src:'sw',ok:true,value:await openTab(cmd.url,cmd.bg)});
    if(cmd.action==='tabs')return post({id,src:'sw',ok:true,value:(await chrome.tabs.query({})).filter(t=>!cmd.match||(t.url||'').includes(cmd.match)).map(t=>[t.id,t.windowId,t.discarded?'discarded':t.status,(t.url||'').slice(0,200),(t.title||'').slice(0,60)])});
  }catch(e){return post({id,src:'sw',error:String(e)});}
  // target: tabs whose url contains cmd.host, else the active tab of each window (never a hidden background tab)
  let tabs=(await chrome.tabs.query({})).filter(t=>t.url&&/^https?:/.test(t.url));
  if(cmd.host)tabs=tabs.filter(t=>t.url.includes(cmd.host));
  else{const a=tabs.filter(t=>t.active);if(a.length)tabs=a;}
  await Promise.all(tabs.map(async t=>{try{
    const res=await chrome.scripting.executeScript({target:{tabId:t.id},world:'ISOLATED',func:DISPATCH,args:[cmd]});
    for(const r of (res||[])){const v=r&&r.result;if(v!=null)await post({id,src:t.url,...v});}
  }catch(e){}}));
}

// create the offscreen poller if absent. Called from every SW wake path so a closed doc self-heals.
let offP=null;   // single-flight: createDocument throws if called twice concurrently or if a doc already exists (claude-in-chrome pattern)
function ensureOffscreen(){
  if(offP)return offP;
  offP=(async()=>{
    try{
      const c=await chrome.runtime.getContexts({contextTypes:['OFFSCREEN_DOCUMENT']});   // getContexts is the race-free existence check
      if(c&&c.length){post({sw:'offscreen-exists'});return;}
      await chrome.offscreen.createDocument({url:'offscreen.html',reasons:['BLOBS'],justification:'hold the a-bridge localhost long-poll'});
      post({sw:'offscreen-created'});
    }catch(e){post({sw:'offscreen-err',e:String(e)});}
  })().finally(()=>{offP=null});
  return offP;
}
chrome.runtime.onMessage.addListener((msg,_s,reply)=>{
  if(msg&&msg.bri==='cmd'){execCmd(msg.cmd).then(()=>{try{reply({ok:1})}catch(e){}});return true;}  // await keeps the SW alive through exec
  // wake.js page-load ping → (re)create the offscreen poller. MUST return true + reply after awaiting, else
  // the SW dies before createDocument finishes (async work started from a listener needs the channel held open).
  (async()=>{try{await ensureOffscreen();post({sw:'offscreen-ok'});}catch(e){post({sw:'offscreen-err',e:String(e)});}try{reply({ok:1})}catch(e){}})();
  return true;
});
chrome.runtime.onStartup.addListener(ensureOffscreen);
chrome.runtime.onInstalled.addListener(ensureOffscreen);
chrome.alarms.create('bri',{periodInMinutes:0.4});   // ~24s heartbeat: re-create the offscreen doc if it was closed
chrome.alarms.onAlarm.addListener(ensureOffscreen);
ensureOffscreen();
''',
"offscreen.html": r'''<!doctype html><meta charset=utf-8><title>bri poller</title><script src="offscreen.js"></script>''',
"offscreen.js": r'''// bri-chrome persistent poller — runs in an offscreen document (NOT killed like the SW, NOT tab-throttled),
// so it holds the :1234 long-poll while Chrome is unfocused. Each command is relayed to the SW, which has the
// privileged chrome.scripting/tabs APIs to run it in the target tab and POST the result. This is the piece
// that made background driving work: the SW alone can't stay alive to poll.
const POLL='http://127.0.0.1:1234/poll', RESP='http://127.0.0.1:1234/resp';
fetch(RESP,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({hello:'offscreen-boot'}),keepalive:true}).catch(()=>{});
// keepalive: a message from the offscreen doc every <30s resets the SW idle timer, so the SW stays warm to
// relay commands into tabs (Chrome 109+ documented pattern). The offscreen doc itself never dies.
setInterval(()=>chrome.runtime.sendMessage({bri:'ka'}).catch(()=>{}),20000);
async function loop(){
  for(;;){
    let r; try{r=await fetch(POLL,{headers:{'X-Bri-Chan':'chrome'}});}catch(e){await new Promise(s=>setTimeout(s,1500));continue;}
    if(r.status===200){let c=null;try{c=await r.json();}catch(e){} if(c)chrome.runtime.sendMessage({bri:'cmd',cmd:c}).catch(()=>{});}
  }
}
loop();
''',
"wake.js": r'''chrome.runtime.sendMessage({bri:'wake'}).catch(()=>{});  // page-load ping wakes the SW → it (re)creates the offscreen poller''',
"newtab.html": r'''<!doctype html><meta charset=utf-8>
<!-- New tab = the a-server :1111 page, embedded. No redirect, no service worker, no extra permissions
     — those three were what kept breaking (NTP self-redirect blocked; new perms need a hard reload).
     a-server sends no X-Frame-Options/CSP so it frames fine. FROZEN file: change the new tab by editing
     the :1111 page server-side, never here. -->
<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}iframe{display:block;border:0;width:100vw;height:100vh}</style>
<iframe src="http://localhost:1111/" allow="clipboard-read; clipboard-write"></iframe>
<script>addEventListener('load',()=>{var f=document.querySelector('iframe');f.focus();})</script>
''',
}

def build():
    tg={"bri-ext":FF,"bri-chrome":CH}
    for name,files in tg.items():
        d=os.path.join(OUT,name); os.makedirs(d,exist_ok=True)
        for fn,c in files.items(): open(os.path.join(d,fn),"w",encoding="utf-8").write(c)
        for fn,b in ICONS.items(): open(os.path.join(d,fn),"wb").write(base64.b64decode(b))
    return [os.path.join(OUT,n) for n in tg]

def chrome_install(chrome='google-chrome-unstable'):
    # zero-drag Chrome install: pack a signed crx and force-install it via enterprise policy off a local
    # file:// update manifest (Chrome blocks http extension downloads; file:// is trusted; --load-extension
    # is DEAD in branded builds \u2014 silently ignored). Reuses the key \u2192 stable ID.
    import subprocess,hashlib,json,time
    build()
    ext=os.path.join(OUT,'bri-chrome'); pem=os.path.join(OUT,'bri-chrome.pem'); crx=os.path.join(OUT,'bri-chrome.crx')
    upd=os.path.join(OUT,'bri-chrome-update.xml')
    mf=os.path.join(ext,'manifest.json');m=json.load(open(mf));t=int(time.time())  # monotonic auto-bump: same/lower version than last-seen never installs
    m['version']=ver='1.%d.%d'%(t>>16,t&0xffff);json.dump(m,open(mf,'w'))
    cmd=[chrome,'--pack-extension='+ext,'--user-data-dir=/tmp/_abrpack','--no-first-run']
    if os.path.exists(pem): cmd.append('--pack-extension-key='+pem)
    subprocess.run(cmd,timeout=90,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    der=subprocess.run(['openssl','rsa','-in',pem,'-pubout','-outform','DER'],capture_output=True).stdout
    ID=''.join(chr(97+int(c,16)) for c in hashlib.sha256(der).hexdigest()[:32])
    open(upd,'w').write(
      "<?xml version='1.0' encoding='UTF-8'?>\n<gupdate xmlns='http://www.google.com/update2/response' protocol='2.0'>\n"
      "<app appid='%s'><updatecheck codebase='file://%s' version='%s'/></app>\n</gupdate>\n"%(ID,crx,ver))
    pol=json.dumps({"ExtensionInstallForcelist":["%s;file://%s"%(ID,upd)],"ExtensionInstallSources":["file:///*"]})
    subprocess.run(['sudo','mkdir','-p','/etc/opt/chrome/policies/managed'],check=True)
    subprocess.run(['sudo','tee','/etc/opt/chrome/policies/managed/bri-chrome.json'],input=pol.encode(),stdout=subprocess.DEVNULL,check=True)
    print("\u2713 force-install policy set  id=%s v%s crx=%d bytes"%(ID,ver,os.path.getsize(crx)))
    print("  -> a briext restart; lands ~1-5 min AFTER boot (roll-call), not at boot.  remove: a briext uninstall")
    return ID

def chrome_uninstall():
    import subprocess
    subprocess.run(['sudo','rm','-f','/etc/opt/chrome/policies/managed/bri-chrome.json'])
    print("\u2713 removed force-install policy (restart Chrome to drop the extension)")

def chrome_restart(chrome='google-chrome-canary'):
    # Chrome can't be restarted from inside the extension (scripts can't open chrome://restart; chrome.runtime.restart is ChromeOS-only),
    # so do it from the terminal: SIGTERM the MAIN browser process (the one with no --type=) for a clean shutdown, then relaunch w/ session restore.
    # pgrep ^-anchored to argv0: vmtouch pins the binary path in ITS argv — unanchored never sees it exit.
    import subprocess,time
    for pid in subprocess.run(['pgrep','-f','^/opt/google/chrome-canary/chrome'],capture_output=True,text=True).stdout.split():
        try: cl=open('/proc/%s/cmdline'%pid,'rb').read().split(b'\0')
        except OSError: continue
        if b'--type=' not in b' '.join(cl): subprocess.run(['kill','-TERM',pid])
    for _ in range(80):
        if not subprocess.run(['pgrep','-f','^/opt/google/chrome-canary/chrome'],capture_output=True).stdout.strip(): break
        time.sleep(0.1)
    subprocess.Popen([chrome,'--restore-last-session'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
    print("\u2713 Canary restarted (session restore) \u2014 new crx lands in ~1-5 min")

def main(argv):
    if "install" in argv[1:]: return chrome_install()
    if "uninstall" in argv[1:]: return chrome_uninstall()
    if "restart" in argv[1:]: return chrome_restart()
    paths=build()
    for p in paths: print("\u2713 "+p)
    print("chrome: a briext install   |   firefox xpi: a bri deploy")

if __name__=='__main__': main(sys.argv)   # `import briext; briext.build()` regenerates without running main
