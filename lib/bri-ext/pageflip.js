// pageflip — discrete viewport paging, folded into a-bridge. Toggle: options "Pageflip" (chrome.storage.sync.pageflip, default on).
// Down=next page, Up=prev (one tap, exact viewport, no overlap). ▲▼ buttons bottom-right. Skips text fields. Live toggle, no reload.
(()=>{
  if(window.__pf)return; window.__pf=1;
  document.documentElement.style.scrollBehavior='auto';
  const max=()=>Math.max(0,Math.ceil(document.documentElement.scrollHeight/innerHeight)-1);
  let p=0,zones=[],active=false;
  const go=d=>{p=Math.min(max(),Math.max(0,p+d));scrollTo(0,p*innerHeight);};
  const onkey=e=>{const t=e.target;if(t&&(/^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName)||t.isContentEditable))return;
    if(e.key==='ArrowDown'){go(1);e.preventDefault();}else if(e.key==='ArrowUp'){go(-1);e.preventDefault();}};
  const mk=(l,d,b)=>{const e=document.createElement('div');e.textContent=l;e.className='__pf_btn';
    e.style.cssText=`position:fixed;right:12px;bottom:${b}px;width:52px;height:52px;z-index:2147483647;display:flex;align-items:center;justify-content:center;font:26px sans-serif;background:#000a;color:#fff;border-radius:11px;user-select:none;cursor:pointer`;
    e.addEventListener('pointerdown',ev=>{go(d);ev.preventDefault();});document.body.appendChild(e);return e;};
  const apply=on=>{
    if(on&&!active){active=true;p=Math.round(scrollY/innerHeight);addEventListener('keydown',onkey,true);zones=[mk('▲',-1,74),mk('▼',1,14)];}
    else if(!on&&active){active=false;removeEventListener('keydown',onkey,true);zones.forEach(z=>z.remove());zones=[];}};
  chrome.storage.sync.get({pageflip:true},d=>apply(d.pageflip));
  chrome.storage.onChanged.addListener(c=>c.pageflip&&apply(c.pageflip.newValue));
})();
