// a-bridge content script — extension equivalent of the Tampermonkey userscript
// embedded in lib/bri.py USERSCRIPT. Either client can run (or both together);
// the bridge server (:1234 HTTP + :1235 cmd) is identical for both.
// Build: cd lib/bri-ext && zip a-bridge.xpi manifest.json content.js
// Deps: Firefox Nightly/Developer + user_pref("xpinstall.signatures.required",
//       false) in profile user.js; drop a-bridge.xpi into <profile>/extensions/.
// Wins over userscript: extension-context fetch (no GM_xhr, no page CSP for our
//       own network); eval works on chatgpt.com (TM hits unsafe-eval CSP there).
// Walls still standing on both: closed shadow DOM (Firefox lacks
//       chrome.dom.openOrClosedShadowRoot); Trusted Types on claude.ai/gemini.
(() => {
  const POLL = 'http://127.0.0.1:1234/poll', RESP = 'http://127.0.0.1:1234/resp';
  const $ = s => document.querySelector(s);
  const dispatch = async (m) => {
    try {
      switch (m.action) {
        case 'navigate': location.href = m.url; return {ok:true};
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
        default: return {error:'unknown action: '+m.action};
      }
    } catch (e) { return {error:String(e)}; }
  };
  const post = d => fetch(RESP, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({src:location.href, ...d})}).catch(()=>{});
  post({hello:location.href, title:document.title, source:'extension', v:'0.4'});
  const loop = async () => {
    try {
      const r = await fetch(POLL);
      if (r.status === 200) {
        const c = await r.json();
        const out = await dispatch(c);
        await post({id:c.id, ...out});
      }
      loop();
    } catch (e) { setTimeout(loop, 2000); }
  };
  loop();
})();
