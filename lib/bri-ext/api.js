// bri-ext apiScript — exposes bridge_fetch to each registered userscript.
// Routes HTTP through background (CSP-exempt) so the userscript can talk to
// 127.0.0.1:1234 even when page CSP's connect-src would block window.fetch.
// This is the GM_xmlhttpRequest equivalent, simplified to one call.
browser.userScripts.onBeforeScript.addListener((script) => {
  script.defineGlobals({
    bridge_fetch: async (url, opts) =>
      browser.runtime.sendMessage({a: 'fetch', url, opts: opts || {}}),
  });
});
