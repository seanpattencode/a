// bri-ext background — content scripts can't call browser.tabs.*, so screenshot
// (captureVisibleTab) lives here. Content forwards via runtime.sendMessage.
// Also handles preload-debug notifications from instant-preload.js.
//
// Also registers a userscript-world script with bridge_fetch privilege (the
// TM userscript equivalent, cloned from Violentmonkey's pattern). This bypasses
// page-CSP connect-src restrictions on claude.ai/perplexity.ai without
// requiring TM install. The userscript polls the bridge and dispatches DOM
// actions just like content.js does.
const USERSCRIPT = `
(() => {
  const POLL = 'http://127.0.0.1:1234/poll', RESP = 'http://127.0.0.1:1234/resp';
  const $ = s => document.querySelector(s);
  const dispatch = async (m) => {
    try {
      if (m.action === 'navigate') { top.location = m.url; return {ok:true}; }
      if (m.action === 'click')    { $(m.sel).click(); return {ok:true}; }
      if (m.action === 'type')     { let e=$(m.sel);
        if (!e.isContentEditable && e.tagName==='DIV') e = e.querySelector('[contenteditable]') || e;
        e.focus();
        if (e.isContentEditable) document.execCommand('insertText',false,m.text);
        else e.value = m.text;
        e.dispatchEvent(new InputEvent('input',{bubbles:true,data:m.text,inputType:'insertText'}));
        return {ok:true}; }
      if (m.action === 'keys')     { const e=$(m.sel)||document.activeElement; e.focus();
        (Array.isArray(m.keys)?m.keys:[m.keys]).forEach(k=>['keydown','keyup'].forEach(t=>
          e.dispatchEvent(new KeyboardEvent(t,{key:k,bubbles:true,cancelable:true}))));
        return {ok:true}; }
      if (m.action === 'text')     return {ok:true, value: $(m.sel).innerText};
      if (m.action === 'html')     return {ok:true, value: document.documentElement.outerHTML.slice(0,200000)};
      if (m.action === 'find')     { const need=(m.text||'').toLowerCase().trim();
        const sel=m.sel||'button,[role="button"],a,[tabindex]:not([tabindex="-1"])';
        const hits=[]; const walk=root=>{for(const el of root.querySelectorAll(sel)){
          const t=((el.innerText||el.textContent||'')+' '+(el.getAttribute('aria-label')||'')).toLowerCase();
          if(!need||t.includes(need)) hits.push(el);}
          for(const el of root.querySelectorAll('*')) if(el.shadowRoot) walk(el.shadowRoot);};
        walk(document); if(m.click&&hits.length) hits[0].click();
        return {ok:true, value:{n:hits.length, first:hits[0]?(hits[0].innerText||hits[0].getAttribute('aria-label')||'').trim().slice(0,80):null}}; }
      if (m.action === 'eval')     { let c=m.code;
        try { if (window.trustedTypes && trustedTypes.createPolicy) {
          const tt = window._abp || (window._abp = trustedTypes.createPolicy('abridge', {createScript: s=>s}));
          c = tt.createScript(m.code);
        }} catch(e) {}
        return {ok:true, value: await (async()=>eval(c))()}; }
      if (m.action === 'url')      return {ok:true, value: location.href};
      if (m.action === 'wait')     { await new Promise(r=>setTimeout(r,m.ms||500)); return {ok:true}; }
      return {error: 'unknown action: '+m.action};
    } catch(e) { return {error: String(e)}; }
  };
  const post = (d) => bridge_fetch(RESP, {method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({src: location.href, ...d})}).catch(()=>{});
  post({hello: location.href, title: document.title, source:'userscript-bri', v:'0.7'});
  const loop = async () => {
    try {
      const r = await bridge_fetch(POLL);
      const c = r && r.status === 200 ? r.body : null;
      if (c) { const out = await dispatch(c); await post({id:c.id, ...out}); }
      loop();
    } catch(e) { setTimeout(loop, 2000); }
  };
  loop();
})();
`;
if (browser.userScripts && browser.userScripts.register) {
  browser.userScripts.register({
    js: [{code: USERSCRIPT}],
    matches: ['<all_urls>'],
    runAt: 'document_end'
  }).catch(e => console.error('bri-ext userScripts.register failed:', e));
}

browser.runtime.onMessage.addListener(async (msg) => {
  if (msg.a === 'fetch') {
    const r = await fetch(msg.url, msg.opts || {});
    return {status: r.status, body: r.status === 200 ? await r.json() : null};
  }
  if (msg.action === 'screenshot')
    return browser.tabs.captureVisibleTab(null, {format: msg.format || 'png'});
  if (msg.type === 'preload-debug') {
    const { debugNotifications } = await browser.storage.sync.get({debugNotifications: false});
    if (!debugNotifications) return;
    const d = msg.data, u = d.url.length > 60 ? d.url.slice(0,57)+'...' : d.url;
    const id = `preload-${Date.now()}`;
    browser.notifications.create(id, {type:'basic', iconUrl:'icon48.png', title:'Page Preloaded',
      message:u, contextMessage:`${d.triggerType} | ${d.duration}ms${d.method?' | '+d.method:''}${d.attemptedPrerender?' (prerender)':''}`});
    setTimeout(() => browser.notifications.clear(id), 3000);
  }
});
