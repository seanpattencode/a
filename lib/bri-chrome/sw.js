// bri-chrome service worker: open options on toolbar click + show debug-preload notifications (gated by the debugNotifications toggle).
// dev hot-reload: hold a WS to the a-server; "reload" -> reload focused tab + the extension (picks up edited files on disk). Trigger: a extload reload.
(function r(){let ws;try{ws=new WebSocket('ws://localhost:1111/extreload')}catch(e){return setTimeout(r,3000)}
  ws.onmessage=e=>{if(e.data!=='reload')return;chrome.tabs.query({active:true,currentWindow:true},t=>{if(t[0])chrome.tabs.reload(t[0].id);chrome.runtime.reload()})};
  ws.onclose=ws.onerror=()=>setTimeout(r,3000);})();
chrome.action.onClicked.addListener(()=>chrome.runtime.openOptionsPage());
chrome.runtime.onMessage.addListener(msg=>{
  if(msg.type!=='preload-debug')return;
  chrome.storage.sync.get({debugNotifications:false},({debugNotifications})=>{
    if(!debugNotifications)return;
    const d=msg.data, u=d.url.length>60?d.url.slice(0,57)+'...':d.url, id='preload-'+Date.now();
    chrome.notifications.create(id,{type:'basic',iconUrl:'icon48.png',title:'Page Preloaded',
      message:u,contextMessage:`${d.triggerType} | ${d.duration}ms${d.method?' | '+d.method:''}${d.attemptedPrerender?' (prerender)':''}`});
    setTimeout(()=>chrome.notifications.clear(id),3000);
  });
});
// a-bridge: register a USER_SCRIPT-world poller (bridge.js) with an eval-capable CSP — MV3 analog of bri-ext's
// browser.userScripts path. Isolated world bypasses page CSP so eval works on chatgpt/claude. Needs Developer Mode (on for unpacked).
async function bridgeSetup(){
  if(!chrome.userScripts)return; // user-scripts toggle off
  // Frozen-shell: --load-extension is dead in branded Chrome (2026, Canary incl.) and repack+re-drag
  // to change logic is the scream. So this package is a stable SHELL — at each SW start it pulls the
  // LIVE bridge logic from the bri server (:1234 /bridge.js) and registers it as a userScripts CODE
  // string (userScripts is MV3's one sanctioned dynamic-code path). Edit lib/bri-chrome/bridge.js →
  // `a extload reload` (restarts SW → re-fetch) = new logic live, no repack, no re-drag. Same path
  // works once the shell is frozen (packed / policy force-install), since logic isn't in the crx.
  // Falls back to the packaged bridge.js when the server is down (last-known-good).
  let js=[{file:'bridge.js'}];
  try{const r=await fetch('http://127.0.0.1:1234/bridge.js');if(r.ok){const code=await r.text();if(code.trim())js=[{code}];}}catch(e){}
  try{
    await chrome.userScripts.configureWorld({csp:"script-src 'self' 'unsafe-eval'",messaging:true});
    await chrome.userScripts.unregister().catch(()=>{});
    await chrome.userScripts.register([{id:'bri-bridge',matches:['<all_urls>'],js,runAt:'document_end',world:'USER_SCRIPT',allFrames:true}]);
  }catch(e){console.error('bri bridge register failed',e);}
}
bridgeSetup();chrome.runtime.onStartup.addListener(bridgeSetup);
// newtab → :1111 is handled entirely by newtab.html (iframe-embeds the a-server). Deliberately NOT in
// the SW: a webNavigation/tabs redirect needs those permissions, and adding permissions to an unpacked
// extension needs a HARD reload (chrome.runtime.reload / `a extload reload` won't grant them) — which
// breaks the zero-touch reload workflow. Iframe in newtab.html needs no perms and no worker.
// bridge.js networking + screenshot proxy (CSP-exempt SW context). USER_SCRIPT-world messages arrive on onUserScriptMessage.
chrome.runtime.onUserScriptMessage?.addListener((msg,_s,reply)=>{
  if(msg&&msg.a==='fetch'){fetch(msg.url,msg.opts||{}).then(async r=>reply({status:r.status,body:r.status===200?await r.json():null})).catch(e=>reply({status:0,error:String(e)}));return true;}
  if(msg&&msg.action==='screenshot'){chrome.tabs.captureVisibleTab({format:msg.format||'png'}).then(reply).catch(()=>reply(null));return true;}
});
