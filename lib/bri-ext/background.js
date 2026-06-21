// a-bridge background — owns the SINGLE poll connection to the bridge. Previously every
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
const post = (d) => fetch(RESP, {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(d)}).catch(()=>{});

// open+focus a tab — deduped by URL so a broadcast opens ONE tab (or focuses an existing one).
const _opening = new Map();
function openTab(url) {
  const key = url.split('#')[0];
  if (!_opening.has(key)) _opening.set(key, (async () => {
    const hit = (await browser.tabs.query({})).find(t => t.url && t.url.split('#')[0] === key);
    const tab = hit ? await browser.tabs.update(hit.id, {active:true})
                    : await browser.tabs.create({url, active:true});
    setTimeout(() => _opening.delete(key), 3000);
    return {id: tab.id, focused: !!hit};
  })());
  return _opening.get(key);
}

// execute one command: open/screenshot run here; everything else fans out to all frames.
async function run(cmd) {
  const id = cmd.id;
  if (cmd.action === 'open') {
    try { return post({id, src:'background', ok:true, value: await openTab(cmd.url)}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'screenshot') {
    try { return post({id, src:'background', ok:true, value: await browser.tabs.captureVisibleTab(null, {format: cmd.format||'png'})}); }
    catch (e) { return post({id, src:'background', error:String(e)}); }
  }
  if (cmd.action === 'close') {   // close the tab matching cmd.url (deck flip) or the active tab; privileged → must live here
    try { const ts = await browser.tabs.query(cmd.url ? {} : {active:true, currentWindow:true});
      const t = cmd.url ? ts.find(x => x.url && x.url.split('#')[0] === cmd.url.split('#')[0]) : ts[0];
      if (t) await browser.tabs.remove(t.id);
      return post({id, src:'background', ok:true, value:{closed: t ? t.id : null}}); }
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
  try { r = await fetch(POLL); } catch (e) { setTimeout(loop, 1500); return; }
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

// internal messages from the other content scripts (instant-preload.js etc.). open/screenshot
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
  if (msg.type === 'preload-debug') {
    const { debugNotifications } = await browser.storage.sync.get({debugNotifications: false});
    if (!debugNotifications) return;
    const d = msg.data, u = d.url.length > 60 ? d.url.slice(0,57)+'...' : d.url;
    const nid = `preload-${Date.now()}`;
    browser.notifications.create(nid, {type:'basic', iconUrl:'icon48.png', title:'Page Preloaded',
      message:u, contextMessage:`${d.triggerType} | ${d.duration}ms${d.method?' | '+d.method:''}${d.attemptedPrerender?' (prerender)':''}`});
    setTimeout(() => browser.notifications.clear(nid), 3000);
  }
});
