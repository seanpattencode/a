// bri-chrome options — instant-preload (prerender / debug notifications / debug mode) + pageflip. Persists in chrome.storage.sync.
const defs={enablePrerender:true, debugNotifications:false, debugMode:false, pageflip:true};
chrome.storage.sync.get(defs, items=>{
  for(const k in defs){const e=document.getElementById(k);e.checked=items[k];
    e.onchange=()=>chrome.storage.sync.set({[k]:e.checked});}
});
