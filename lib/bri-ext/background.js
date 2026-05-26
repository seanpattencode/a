// bri-ext background — content scripts can't call browser.tabs.*, so screenshot
// (captureVisibleTab) lives here. Content forwards via runtime.sendMessage.
// Also handles preload-debug notifications from instant-preload.js.
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
