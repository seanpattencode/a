// a-bridge poller for Chrome — runs in the userScripts USER_SCRIPT world: DOM access + eval allowed
// (sw.js configures that world's CSP with 'unsafe-eval', so eval works on chatgpt/claude despite page CSP).
// Networking is routed through the service worker (chrome.runtime.sendMessage) to bypass page connect-src.
// MV3 analog of lib/bri-ext/content.js; same /poll + /resp protocol, same dispatch actions.
(()=>{
  const POLL='http://127.0.0.1:1234/poll',RESP='http://127.0.0.1:1234/resp',$=s=>document.querySelector(s);
  const xfer=(url,opts)=>chrome.runtime.sendMessage({a:'fetch',url,opts:opts||{}});
  const dispatch=async m=>{try{switch(m.action){
    case 'navigate':top.location=m.url;return{ok:true};
    case 'click':$(m.sel).click();return{ok:true};
    case 'type':{let e=$(m.sel);if(!e.isContentEditable&&e.tagName==='DIV')e=e.querySelector('[contenteditable]')||e;e.focus();
      if(e.isContentEditable)document.execCommand('insertText',false,m.text);else e.value=m.text;
      e.dispatchEvent(new InputEvent('input',{bubbles:true,data:m.text,inputType:'insertText'}));return{ok:true};}
    case 'keys':{const e=$(m.sel)||document.activeElement;e.focus();
      (Array.isArray(m.keys)?m.keys:[m.keys]).forEach(k=>['keydown','keyup'].forEach(t=>e.dispatchEvent(new KeyboardEvent(t,{key:k,bubbles:true,cancelable:true}))));return{ok:true};}
    case 'text':return{ok:true,value:$(m.sel).innerText};
    case 'html':return{ok:true,value:document.documentElement.outerHTML.slice(0,200000)};
    case 'find':{const need=(m.text||'').toLowerCase().trim(),sel=m.sel||'button,[role="button"],a,[tabindex]:not([tabindex="-1"])',hits=[];
      const walk=root=>{for(const el of root.querySelectorAll(sel)){const t=((el.innerText||el.textContent||'')+' '+(el.getAttribute('aria-label')||'')).toLowerCase();if(!need||t.includes(need))hits.push(el);}
        for(const el of root.querySelectorAll('*'))if(el.shadowRoot)walk(el.shadowRoot);};
      walk(document);if(m.click&&hits.length)hits[0].click();
      return{ok:true,value:{n:hits.length,first:hits[0]?(hits[0].innerText||hits[0].getAttribute('aria-label')||'').trim().slice(0,80):null}};}
    case 'eval':return{ok:true,value:await(async()=>eval(m.code))()};
    case 'wait':await new Promise(r=>setTimeout(r,m.ms||500));return{ok:true};
    case 'url':return{ok:true,value:location.href};
    case 'screenshot':return{ok:true,value:await chrome.runtime.sendMessage({action:'screenshot',format:m.format})};
    default:return{error:'unknown action: '+m.action};
  }}catch(e){return{error:String(e)};}};
  const post=d=>xfer(RESP,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({src:location.href,...d})});
  post({hello:location.href,title:document.title,source:'chrome-userscript',v:'0.8'});
  const loop=async()=>{try{const r=await xfer(POLL);const c=r&&r.status===200?r.body:null;
    if(c){const out=await dispatch(c);await post({id:c.id,...out});}loop();}catch(e){setTimeout(loop,2000);}};
  loop();
})();
