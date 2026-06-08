// pageflip — discrete viewport paging for the bri-chrome (Chrome) extension. Down=next, Up=prev (one tap).
// Universal step: detects the scroll container (window, or a large inner scrollable div) and the obscured
// top/bottom band (fixed/sticky bars) via pixel probes re-run each flip — so sticky / hide-on-scroll headers
// never skip content, on any site. ▲▼ buttons; skips text fields; toggle chrome.storage.sync.pageflip (default on).
// Firefox has its own separate extension at lib/bri-ext (no shared source).
(()=>{
  if(window.__pf)return; window.__pf=1;
  document.documentElement.style.scrollBehavior='auto';
  let zones=[],active=false;
  const cx=()=>Math.round(innerWidth/2);
  // px obscured by pinned fixed/sticky bars from the top edge (fromTop=1) or bottom (0): first pixel showing flowing content.
  const obsc=fromTop=>{const lim=Math.floor(innerHeight*0.5);for(let i=0;i<lim;i+=4){const y=fromTop?i:innerHeight-1-i;
    const st=document.elementsFromPoint(cx(),y);if(!st.length)return i;
    const t=st.find(e=>!(e.classList&&e.classList.contains('__pf_btn')));if(!t)return i;
    const p=getComputedStyle(t).position;if(p!=='fixed'&&p!=='sticky')return i; // flowing content → edge is clear
    // fixed/sticky obscures only if scrollable content sits behind it (else it's an in-flow sticky block, not a bar)
    if(!st.slice(st.indexOf(t)+1).some(e=>{const q=getComputedStyle(e).position;return q!=='fixed'&&q!=='sticky';}))return i;}return 0;};
  // scroll container: window (null) unless the document itself doesn't scroll and a large inner element does.
  const scr=()=>{const se=document.scrollingElement||document.documentElement;if(se&&se.scrollHeight>se.clientHeight+2)return null;
    let best=null,ba=0;for(const el of document.querySelectorAll('div,main,section,article,ul')){if(el.scrollHeight<=el.clientHeight+8)continue;
      if(!/(auto|scroll)/.test(getComputedStyle(el).overflowY))continue;const r=el.getBoundingClientRect(),a=r.width*r.height;if(a>ba){ba=a;best=el;}}return best;};
  const go=d=>{const s=scr();if(s){s.scrollBy(0,d*Math.max(1,s.clientHeight-8));return;}scrollBy(0,d*Math.max(1,innerHeight-obsc(1)-obsc(0)));};
  const onkey=e=>{const t=e.target;if(t&&(/^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName)||t.isContentEditable))return;
    if(e.key==='ArrowDown'||(e.key===' '&&!e.shiftKey)){go(1);e.preventDefault();}else if(e.key==='ArrowUp'||(e.key===' '&&e.shiftKey)){go(-1);e.preventDefault();}};
  const mk=(l,d,b)=>{const e=document.createElement('div');e.textContent=l;e.className='__pf_btn';
    e.style.cssText=`position:fixed;right:12px;bottom:${b}px;width:52px;height:52px;z-index:2147483647;display:flex;align-items:center;justify-content:center;font:26px sans-serif;background:#000a;color:#fff;border-radius:11px;user-select:none;cursor:pointer`;
    e.addEventListener('pointerdown',ev=>{go(d);ev.preventDefault();});document.body.appendChild(e);return e;};
  const apply=on=>{
    if(on&&!active){active=true;addEventListener('keydown',onkey,true);zones=[mk('▲',-1,74),mk('▼',1,14)];}
    else if(!on&&active){active=false;removeEventListener('keydown',onkey,true);zones.forEach(z=>z.remove());zones=[];}};
  chrome.storage.sync.get({pageflip:true},d=>apply(d.pageflip));
  chrome.storage.onChanged.addListener(c=>c.pageflip&&apply(c.pageflip.newValue));
})();
