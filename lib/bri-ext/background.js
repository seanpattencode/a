// bri-ext background — content scripts can't call browser.tabs.*, so screenshot
// (captureVisibleTab) lives here. Content forwards via runtime.sendMessage.
browser.runtime.onMessage.addListener(async (msg) => {
  if (msg.action === 'screenshot')
    return browser.tabs.captureVisibleTab(null, {format: msg.format || 'png'});
});
