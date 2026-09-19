#!/usr/bin/env python3
"""a briext — build BOTH browser extensions (FF MV2 + Chrome MV3) from this one file into adata/local/ext/;
icons defined once so they can't drift. Edit here, rerun to redeploy; FF xpi: a bri deploy; chrome: a briext install."""
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
"background.js": r'''// background owns the SINGLE poll: FF caps 6 held conns/server (Gmail's frames saturated it — a frame could POST but never RECEIVE)
// and throttles bg-tab timers; the persistent background page is neither. Commands fan out per-frame via tabs.sendMessage
// (fires even in throttled tabs), replies POST /resp with the cmd id; privileged actions (open/screenshot/navigate/tabs) run HERE.
const POLL = 'http://127.0.0.1:1234/poll', RESP = 'http://127.0.0.1:1234/resp';
let BRI_CHAN = 'firefox';   // exact channel — UA is frozen ('Firefox/152.0') and hides Nightly; getBrowserInfo isn't
try { browser.runtime.getBrowserInfo().then(i => { let c = /a\d/.test(i.version)?'nightly':/b\d/.test(i.version)?'beta':(i.buildID||'').startsWith('2010')?'release':'build'; BRI_CHAN = 'firefox-'+c+'/'+i.version; }).catch(()=>{}); } catch(e) {}
const post = (d) => fetch(RESP, {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({chan:BRI_CHAN, ...d})}).catch(()=>{});

// openTab deduped by NORMALIZED url (origin+path): pages mutate their URLs, exact-match dupes
const _opening = new Map();
const _tag = new Map(), _win = new Map();   // _win: cmd.win name -> window id   // cmd.tag -> tab id: redirects (gnews) rewrite the tab's url, the id survives — open/close by tag ride this (i q)
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

// loadDivertedInBackground backgrounds hand-clicked _blank links too: focus when opener = active localhost tab
browser.tabs.onCreated.addListener(async t => {
  if (t.active || !t.openerTabId) return;
  try { const o = await browser.tabs.get(t.openerTabId);
    if (o.active && /^https?:\/\/(localhost|127\.0\.0\.1):/.test(o.url)) browser.tabs.update(t.id, {active:true}); } catch (e) {}
});

async function run(cmd) {
  const id = cmd.id;
  if (cmd.action === 'open') {
    try { let v;
      if (cmd.tag && _tag.has(cmd.tag)) { try { const t = await browser.tabs.get(_tag.get(cmd.tag));
        if (!cmd.bg) { await browser.tabs.update(t.id, {active:true}); if (!cmd.nofocus) await browser.windows.update(t.windowId, {focused:true}); }   // nofocus: show the tab in its window without stealing keyboard focus (i web /scan dual j/k)
        v = {id:t.id, focused:!cmd.bg}; } catch (e) { _tag.delete(cmd.tag); } }
      if (!v && cmd.win) {   // win:<name> = a dedicated window (i web /scan dual mode): tabs of one job live there, never among Sean's
        let w = _win.get(cmd.win); try { if (w != null) await browser.windows.get(w); else throw 0; } catch (e) { w = (await browser.windows.create({url: cmd.url})).id; _win.set(cmd.win, w); v = (await browser.tabs.query({windowId: w}))[0]; }
        if (!v) { const t = await browser.tabs.create({url: cmd.url, windowId: w, active: !cmd.bg}); v = t; }
        v = {id: v.id, focused: !cmd.bg}; if (cmd.tag) _tag.set(cmd.tag, v.id); }
      if (!v) { v = await openTab(cmd.url, cmd.bg, cmd.fresh); if (cmd.tag) _tag.set(cmd.tag, v.id); }
      return post({id, src:'background', ok:true, value:v}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'screenshot') {
    try { return post({id, src:'background', ok:true, value: await browser.tabs.captureVisibleTab(null, {format: cmd.format||'png'})}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'navigate') {   // ACTIVE (or match) tab only, HERE: the per-frame path once broadcast-navigated every open tab (2026-08-08 storm)
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
"content.js": r'''// content script: NO polling (background owns it); executes one dispatched command in ITS frame, returns the result
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
  browser.runtime.onMessage.addListener((msg) => {
    if (msg && msg.__bri_cmd) return dispatch(msg.__bri_cmd).then(out => ({src: location.href, ...out}));
    // not ours → return undefined so other listeners (preload-debug etc.) still see it
  });
})();
''',
"api.js": r'''// apiScript: bridge_fetch for userscripts — HTTP via background, CSP-exempt (GM_xmlhttpRequest equivalent)
browser.userScripts.onBeforeScript.addListener((script) => {
  script.defineGlobals({
    bridge_fetch: async (url, opts) =>
      browser.runtime.sendMessage({a: 'fetch', url, opts: opts || {}}),
  });
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
"sw.js": r'''// MV3 SW dies (~30s), can't hold a long-poll: the OFFSCREEN DOCUMENT polls (not throttled, not capped), relays cmds here;
// SW runs them via chrome.scripting (ISOLATED world, no toggle) + POSTs /resp. wake.js + onStartup/onInstalled/alarms re-create the doc.
const RESP='http://127.0.0.1:1234/resp';
// keepalive:true: POSTs from a dying SW otherwise abort (ate every earlier diagnostic)
const post=d=>fetch(RESP,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({chan:'chrome',...d}),keepalive:true}).catch(()=>{});
post({sw:'top',off:typeof chrome.offscreen});  // DIAG: SW ran + can reach :1234; reports if chrome.offscreen exists

// DISPATCH runs IN the tab via executeScript: self-contained (serialized), no outer refs
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

let offP=null;   // single-flight: concurrent createDocument throws
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
  // MUST return true + reply after await: the SW dies before createDocument otherwise
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
"offscreen.js": r'''// offscreen doc = the persistent poller (not SW-killed, not tab-throttled); relays cmds to the SW
const POLL='http://127.0.0.1:1234/poll', RESP='http://127.0.0.1:1234/resp';
fetch(RESP,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({hello:'offscreen-boot'}),keepalive:true}).catch(()=>{});
// 20s ping resets the SW idle timer (documented pattern); the doc itself never dies
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
}
NT={  # new tab: Ctrl+T pins focus to the url bar (FF bug 1411465, Chrome by design); only a tabs.create'd tab focuses content (Chrome's iframe/redirect/SW versions never did)
"newtab.html": r'''<!doctype html><style>html,body{margin:0;height:100vh;background:#000}</style><script src="newtab.js"></script>''',
"newtab.js": r'''chrome.tabs.getCurrent(t=>chrome.tabs.create({url:'http://localhost:1111/',index:t.index+1},()=>chrome.tabs.remove(t.id)))''',
}

def build():
    tg={"bri-ext":FF,"bri-chrome":CH}
    for name,files in tg.items():
        d=os.path.join(OUT,name); os.makedirs(d,exist_ok=True)
        for fn,c in (files|NT).items(): open(os.path.join(d,fn),"w",encoding="utf-8").write(c)
        for fn,b in ICONS.items(): open(os.path.join(d,fn),"wb").write(base64.b64decode(b))
    return [os.path.join(OUT,n) for n in tg]

def chrome_install(chrome='google-chrome-canary'):  # pack with the channel that installs it
    # pack signed crx + force-install via enterprise policy off a file:// update manifest (http blocked; --load-extension DEAD in branded builds); key reuse = stable ID
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
    pol=json.dumps({"ExtensionInstallForcelist":["%s;file://%s"%(ID,upd)],"ExtensionInstallSources":["file:///*"],"NTPFooterExtensionAttributionEnabled":False,"NTPFooterManagementNoticeEnabled":False})   # Chrome 138+ NTP footer off
    subprocess.run(['sudo','mkdir','-p','/etc/opt/chrome/policies/managed'],check=True)
    subprocess.run(['sudo','tee','/etc/opt/chrome/policies/managed/bri-chrome.json'],input=pol.encode(),stdout=subprocess.DEVNULL,check=True)
    print("\u2713 force-install policy set  id=%s v%s crx=%d bytes"%(ID,ver,os.path.getsize(crx)))
    print("  -> updater ignores file:// exts: to refresh, close Chrome, rm Extensions/<id>/<ver>_0 + its extensions.settings entry, a briext restart.  remove: a briext uninstall")
    return ID

def chrome_uninstall():
    import subprocess
    subprocess.run(['sudo','rm','-f','/etc/opt/chrome/policies/managed/bri-chrome.json'])
    print("\u2713 removed force-install policy (restart Chrome to drop the extension)")

def chrome_restart(chrome='google-chrome-canary'):
    # kill the default-profile browser only (not renderers/crashpad/other --user-data-dir instances: the pad kiosk), relaunch with the session env a bare shell lacks — no WAYLAND_DISPLAY: ozone picks X11 and Chrome exits; no DBUS: a keyring prompt stalls every load
    import subprocess,select,ctypes
    pr=lambda *a:subprocess.run(['pgrep',*a],capture_output=True,text=True).stdout.split()
    ps=[int(p) for p in pr('-f','^/opt/google/chrome-canary/chrome( |$)') if not any(x in open('/proc/%s/cmdline'%p).read() for x in('--type=','--user-data-dir='))]
    fds=[ctypes.CDLL(None).syscall(434,p,0) for p in ps]  # pidfd_open (this python lacks os.pidfd_open)
    for p in ps: os.kill(p,15)
    for f in fds: select.select([f],[],[],8)
    env=dict(os.environ)
    for p in pr('-x','sway')[:1]: env.update(l.split('=',1) for l in open('/proc/%s/environ'%p).read().split('\0') if l[:5] in('DBUS_','XDG_R','XDG_S'))
    r=env.setdefault('XDG_RUNTIME_DIR','/run/user/%d'%os.getuid())
    if not env.get('WAYLAND_DISPLAY'): env['WAYLAND_DISPLAY']=next((f for f in sorted(os.listdir(r)) if f[:8]=='wayland-' and f[-5:]!='.lock'),'')  # sway never exports it
    subprocess.Popen([chrome,'--restore-last-session']+(['--ozone-platform=wayland'] if env.get('WAYLAND_DISPLAY') else []),env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
    print("\u2713 Canary restarted (session restore) \u2014 new crx lands in ~1-5 min")

def main(argv):
    if "install" in argv[1:]: return chrome_install()
    if "uninstall" in argv[1:]: return chrome_uninstall()
    if "restart" in argv[1:]: return chrome_restart()
    paths=build()
    for p in paths: print("\u2713 "+p)
    print("chrome: a briext install   |   firefox xpi: a bri deploy")

if __name__=='__main__': main(sys.argv)   # `import briext; briext.build()` regenerates without running main
