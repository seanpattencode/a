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
  try{
    await chrome.userScripts.configureWorld({csp:"script-src 'self' 'unsafe-eval'",messaging:true});
    await chrome.userScripts.unregister().catch(()=>{});
    await chrome.userScripts.register([{id:'bri-bridge',matches:['<all_urls>'],js:[{file:'bridge.js'}],runAt:'document_end',world:'USER_SCRIPT',allFrames:true}]);
  }catch(e){console.error('bri bridge register failed',e);}
}
bridgeSetup();chrome.runtime.onStartup.addListener(bridgeSetup);
// bridge.js networking + screenshot proxy (CSP-exempt SW context). USER_SCRIPT-world messages arrive on onUserScriptMessage.
chrome.runtime.onUserScriptMessage?.addListener((msg,_s,reply)=>{
  if(msg&&msg.a==='fetch'){fetch(msg.url,msg.opts||{}).then(async r=>reply({status:r.status,body:r.status===200?await r.json():null})).catch(e=>reply({status:0,error:String(e)}));return true;}
  if(msg&&msg.action==='screenshot'){chrome.tabs.captureVisibleTab({format:msg.format||'png'}).then(reply).catch(()=>reply(null));return true;}
});
