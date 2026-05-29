const $ = id => document.getElementById(id);
// Firefox has no Speculation Rules API (libxul.so: 0 'speculation_rules' strings
// — not in Gecko at all, not just behind a pref). Always falls back to
// <link rel=prefetch>; for true prerender use a Chromium browser.
const sup = HTMLScriptElement.supports?.('speculationrules');
const isFF = navigator.userAgent.includes('Firefox');
$('cap').textContent = sup
  ? '✓ Speculation Rules supported — full prerender available'
  : isFF
    ? '⚠ Firefox: no Speculation Rules API (not implemented in Gecko yet — no about:config flag). Using <link rel=prefetch> fallback, which is the best Firefox offers.'
    : '⚠ No Speculation Rules — prefetch fallback only';
$('cap').className = sup ? 'ok' : 'warn';

const defs = {enablePrerender:true, debugNotifications:false, debugMode:false, pageflip:true};
chrome.storage.sync.get(defs, items => {
  for (const k in defs) {
    $(k).checked = items[k];
    $(k).onchange = e => chrome.storage.sync.set({[k]: e.target.checked}, () => {
      $('stat').textContent = `${k} = ${e.target.checked}`;
    });
  }
});

const probe = `({host:location.host, ready:document.readyState,
  inst: typeof _delayOnHover === 'number',
  preloaded: typeof _preloadedList !== 'undefined' ? _preloadedList.size : null,
  prefetch: document.querySelectorAll('link[rel=prefetch]').length,
  specrules: document.querySelectorAll('script[type=speculationrules]').length})`;
const refresh = () => chrome.tabs.query({active:true, currentWindow:true}, ([t]) => {
  if (!t) return;
  chrome.tabs.executeScript(t.id, {code: probe}, ([r]) => {
    if (chrome.runtime.lastError || !r) {
      $('live').textContent = `${t.url||'<no url>'}\n(extension can't access this URL — chrome://, about:, file://, or AMO)`;
      return;
    }
    $('live').textContent = `${r.host} [${r.ready}]
ext loaded:  ${r.inst ? '✓' : '✗ (script not active here)'}
preloaded:   ${r.preloaded ?? '—'} urls this session
link[rel=prefetch]:     ${r.prefetch}
script[type=specrules]: ${r.specrules}`;
  });
});
refresh();
$('refresh').onclick = refresh;
