# /// script
# requires-python = ">=3.10"
# dependencies = ["aiohttp>=3.9"]
# ///
import sys, asyncio, os, pty, subprocess as S, struct, fcntl, termios, json, sqlite3; from html import escape as E; from aiohttp import web
# UI calls CLI (a binary) — no business logic here. prerendered SPA, CSS toggle, tmux sessions.
# curl-testable: all logic lives in CLI, HTML just calls it. curl POST/GET covers functionality.
# visual bugs are faster for the user to spot — agent tests logic via curl, user eyeballs layout.
#
# Index prepopulates the command box on load — the empty "focus" state tested pure but went unused.
_D = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
_A, _G = f'{_D}/adata/local/a', f'{_D}/adata/git'
def _kv(p):
    r = {}
    for l in open(p):
        if ':' in l: k, v = l.split(':', 1); r[k.strip()] = v.strip()
    return r
HTML = '''<!doctype html>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css">
<script src="https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/xterm-addon-webgl@0.16.0/lib/xterm-addon-webgl.min.js"></script>
<style>*{font-family:system-ui}[data-go]{touch-action:manipulation;cursor:pointer}.b{padding:16px 24px;font-size:24px;background:#000;color:#4af;border:2px solid #4af;border-radius:8px;cursor:pointer}.n{font-size:28px;color:#4af;cursor:pointer;padding:20px 40px;border:2px solid #4af;border-radius:12px}.f{background:#000;color:#fff;border:1px solid #333;border-radius:8px}.ni{padding:6px 0;color:#aaa;border-bottom:1px solid #222;display:flex;align-items:center}.nx{background:none;border:1px solid #555;color:#888;padding:12px 20px;margin-right:10px;border-radius:4px;cursor:pointer;font-size:16px}#w,#wm{position:fixed;right:10px;z-index:9}#w{top:10px;width:24px;height:24px;cursor:pointer;background:radial-gradient(#aaa 28%,transparent 32%) 0 0/8px 8px}#wm{top:44px;background:#111;border:1px solid #333;border-radius:8px;padding:8px;display:none;grid-template-columns:repeat(3,64px);gap:6px}#wm.x{display:grid}#wm a{color:#4af;border:1px solid #333;border-radius:6px;text-decoration:none;font-size:13px;aspect-ratio:1;display:grid;place-items:center}.v{position:absolute;inset:0;height:100vh;display:flex;flex-direction:column;visibility:hidden}.v.on{visibility:visible}</style>
<body style="margin:0;height:100vh;background:#000;overflow:hidden">
<div id=w onclick="wm.classList.toggle('x')"></div>
<div id=wm><a data-go="/">home</a><a href="/op">op</a><a href="/dash">dash</a><a data-go="/jobs">job</a><a data-go="/note">note</a><a data-go="/tasks">task</a><a data-go="/term">term</a></div>
<div id=v_index class=v style="align-items:center">
  <form id=omni style="margin-top:45vh"><input id=qi autofocus placeholder="" style="width:80vw;max-width:600px;font-size:24px;padding:16px;text-align:center;background:#000;color:#fff;border:1px solid #333;border-radius:8px;outline:none;caret-color:transparent"></form>
  <div id=qo style="width:90vw;max-width:800px;margin-top:20px;max-height:45vh;overflow-y:auto"></div>
</div>
__MY__
<div id=v_tasks class=v style="padding:20px;align-items:center;overflow-y:auto">
  <div id=tl style="width:95vw;max-width:900px;color:#fff;font-family:monospace;font-size:14px">__TO__</div>
</div>
<div id=v_term class=v style="display:block">
  <div id=t style="height:100vh"></div>
</div>
<div id=v_jobs class=v style="align-items:center;justify-content:center;gap:20px;color:#fff">
  <select id=jp class=f style="width:95vw;font-size:20px;padding:12px">__PO__</select>
  <select id=jd class=f style="width:95vw;font-size:20px;padding:12px">__DO__</select>
  <div style="display:flex;gap:10px;width:95vw;align-items:center">
    <input id=jc placeholder="prompt" onkeydown="if(event.key==='Enter')runjob()" class=f style="flex:1;font-size:24px;padding:16px">
    <select id=jn class=f style="font-size:20px;padding:12px"><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option></select>
    <label style="color:#4af;font-size:18px;display:flex;align-items:center;gap:4px"><input type=checkbox id=jpr>PR</label>
    <button class=b onclick="runjob()">run</button>
  </div>
  <div id=jl style="width:95vw;overflow-y:auto;flex:1;margin-top:10px;font-family:monospace;font-size:14px;white-space:pre;color:#aaa">__JO__</div>
</div>
<div id=v_note class=v style="padding-top:20px;align-items:center">
  <form id=nf style="display:flex;gap:10px;width:95vw;align-items:center">
    <input id=nc autofocus placeholder="note" class=f style="flex:1;font-size:24px;padding:16px">
    <button class=b type=submit>save</button>
    <button class=b type=button onclick="fetch('/api/sync',{method:'POST'}).then(()=>show('/note'))">sync</button>
    <button class=b type=button data-go="/term">term</button>
  </form>
  <div id=nl style="width:95vw;overflow-y:auto;flex:1;margin-top:10px">__NO__</div>
</div>
<div id=v_dc style="position:fixed;inset:0;background:#000;color:red;text-align:center;padding-top:45vh;display:none">not connected</div>
<script>
var views={'/':'v_index','/jobs':'v_jobs','/term':'v_term','/note':'v_note','/tasks':'v_tasks'__MV__}, T, F, W;
// perf: go() must stay <1ms. show() is DOM toggle only, no network. keep this instrumentation.
function go(p){var t=performance.now();history.pushState(null,'',p);show(p);console.log('go('+p+') '+(performance.now()-t).toFixed(2)+'ms');}
function show(p){wm.classList.remove('x');for(var k in views)window[views[k]].classList.toggle('on',k===p);if(p==='/term'&&F){connect();setTimeout(function(){F.fit();T.focus()},0);}if(p==='/note')fetch('/note-list').then(r=>r.text()).then(h=>nl.innerHTML=h);if(p==='/')qi.oninput()}
function arcn(f,el){fetch('/api/note/archive',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({f:f})});el.parentElement.remove();}
function arct(d,el){fetch('/api/task/archive',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({d:d})});el.parentElement.remove();}
function loadjobs(){Promise.all([fetch('/api/jobs').then(function(r){return r.text()}),fetch('/api/job-status').then(function(r){return r.json()})]).then(function(d){
  var h=d[0];if(d[1].length){h+='\\n\\n--- Job PRs ---\\n';d[1].forEach(function(j){
    h+=j.status+(j.step?' ['+j.step+']':'')+' '+j.name;
    if(j.session)h+=' ('+j.session+')';h+='\\n';});}
  jl.textContent=h;});}
function ws(d){if(W&&W.readyState===1)W.send(d);}
function runjob(){var v=jc.value.trim(),p=jp.value,n=parseInt(jn.value),d=jd.value,pr=jpr.checked;if(!v)return;fetch('/api/jobs',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({prompt:v,project:p,count:n,device:d,pr:pr})}).then(function(r){return r.json()}).then(function(r){jc.value='';jc.placeholder=r.command||r.error;loadjobs();});}
window.onpopstate=function(){show(location.pathname);};
try{
  T=new Terminal();F=new(FitAddon.FitAddon||FitAddon)();
  T.loadAddon(F);T.open(t);try{T.loadAddon(new WebglAddon.WebglAddon())}catch(e){}
  var sz=()=>ws(JSON.stringify({cols:T.cols,rows:T.rows}));
  function connect(){if(W&&W.readyState<2)return;
    W=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');
    W.onopen=()=>{v_dc.style.display='none';F.fit();sz()};
    W.onmessage=e=>T.write(e.data);
    W.onerror=W.onclose=()=>{v_dc.style.display='';setTimeout(connect,1000)};
  }
  T.onData(ws);
  new ResizeObserver(()=>{F.fit();sz()}).observe(t);
}catch(e){document.body.innerHTML='<pre style="color:red;padding:20px">'+e+'</pre>';}
nf.onsubmit=function(e){e.preventDefault();var c=nc.value.trim();if(c){nc.value='';fetch('/note',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'c='+encodeURIComponent(c)}).then(function(r){if(!r.ok)throw 0;return r.text()}).then(function(p){nl.insertAdjacentHTML('afterbegin','<div class=ni>'+c+'</div>');nc.placeholder=p;}).catch(function(){nc.placeholder='FAIL';nc.style.borderColor='red';setTimeout(function(){nc.style.borderColor='#333'},2000);});}};
var _tg=0;document.addEventListener('touchstart',function(e){var g=e.target.closest('[data-go]');if(g){e.preventDefault();_tg=1;go(g.dataset.go);}},{passive:false});
document.addEventListener('click',function(e){if(_tg){_tg=0;return;}if(!e.target.closest('#w,#wm'))wm.classList.remove('x');var g=e.target.closest('[data-go]');if(g)go(g.dataset.go);});
qi.onblur=function(){setTimeout(function(){qi.focus()},0)};
window.onfocus=function(){qi.style.borderColor='#555'};window.onblur=function(){qi.style.borderColor='#333'};
var _cmds=__CMDS__,_cs=0;
// html pages + nav views favored to the top; rest stays by freq
_cmds.sort(function(a,b){function h(c){return c[1]==='page'||views['/'+c[0]]?1:0}return h(b)-h(a)});
qi.oninput=function(){var v=qi.value.toLowerCase();_cs=0;var m=_cmds.filter(function(c){return c[0].indexOf(v)>=0}).slice(0,12);qo.innerHTML=m.map(function(c,i){return '<div data-c="'+c[0]+'" style="padding:8px 16px;color:'+(i?'#555':'#4af')+';cursor:pointer;font-size:18px" onclick="qi.value=this.dataset.c;omni.requestSubmit()">a '+c[0]+(c[1]?' <span style=color:#333>'+c[1]+'</span>':'')+'</div>'}).join('')};show(views[location.pathname]?location.pathname:'/');
qi.onkeydown=function(e){var it=qo.querySelectorAll('[data-c]');if(!it.length)return;if(e.key==='ArrowDown'){e.preventDefault();_cs=Math.min(_cs+1,it.length-1)}else if(e.key==='ArrowUp'){e.preventDefault();_cs=Math.max(_cs-1,0)}else if(e.key==='Tab'&&it[_cs]){e.preventDefault();qi.value=it[_cs].dataset.c;qi.oninput();return}else return;it.forEach(function(el,i){el.style.color=i===_cs?'#4af':'#555'})};
omni.onsubmit=function(e){e.preventDefault();var t=performance.now(),raw=qi.value.trim(),v=raw;if(!v)return;if(views['/'+v]){go('/'+v);qi.value='';console.log('omni total '+(performance.now()-t).toFixed(2)+'ms');return}var t2=performance.now();fetch('/api/omni',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'q='+encodeURIComponent(v)}).then(function(r){return r.text()}).then(function(h){qi.value='';if(h)qo.innerHTML=h;else qi.oninput();qi.focus();console.log('omni fetch '+(performance.now()-t2).toFixed(2)+'ms')})};
var _ek='';document.addEventListener('keydown',function(e){if(e.key.length<2)_ek=(_ek+e.key).slice(-5);if(_ek==='crush'){_ek='';(function(){var c=document.createElement('canvas'),x=c.getContext('2d'),Z=16,W=10,H=20,g=Array(W*H).fill(0),P=[[[0,0]],[[0,0],[1,0]],[[0,0],[1,0],[0,1]],[[0,0],[1,0],[2,0]],[[0,0],[1,0],[2,0],[0,1]]],K=['#0ff','#f0f','#0f0','#f80','#f00'],p,px,py,pi,iv,fc=0,cx=4,cy=H-1,jc=0,ky={};c.width=W*Z;c.height=H*Z;c.style.cssText='position:fixed;inset:0;margin:auto;z-index:9999;border:2px solid #4af';document.body.appendChild(c);function np(){pi=Math.random()*5|0;p=P[pi].map(function(a){return a.slice()});px=Math.random()*(W-2)|0;py=0;if(ht(0,0))ov()}function ht(dx,dy){for(var i=0;i<p.length;i++){var a=p[i][0]+px+dx,b=p[i][1]+py+dy;if(a<0||a>=W||b>=H||(b>=0&&g[b*W+a]))return 1}return 0}function lk(){for(var i=0;i<p.length;i++){var a=p[i][0]+px,b=p[i][1]+py;if(b>=0)g[b*W+a]=pi+1}if(g[cy*W+cx]){ov();return}for(var y=H-1;y>=0;y--){var f=1;for(var j=0;j<W;j++)if(!g[y*W+j])f=0;if(f){g.splice(y*W,W);for(var j=0;j<W;j++)g.unshift(0);if(cy<y)cy++;y++}}}function dr(){x.fillStyle='#000';x.fillRect(0,0,c.width,c.height);for(var i=0;i<W*H;i++)if(g[i]){x.fillStyle=K[g[i]-1];x.fillRect((i%W)*Z,(i/W|0)*Z,Z-1,Z-1)}for(var i=0;i<p.length;i++){x.fillStyle=K[pi];x.fillRect((p[i][0]+px)*Z,(p[i][1]+py)*Z,Z-1,Z-1)}x.fillStyle='#fff';x.fillRect(cx*Z+2,cy*Z+2,Z-5,Z-5)}function ov(){clearInterval(iv);c.remove();document.removeEventListener('keydown',kd);document.removeEventListener('keyup',ku)}function kd(e){var k=e.key;if(k==='ArrowLeft'&&!ht(-1,0))px--;if(k==='ArrowRight'&&!ht(1,0))px++;if(k==='ArrowDown'&&!ht(0,1))py++;if(k==='ArrowUp'){var n=p.map(function(a){return[-a[1],a[0]]});var o=p;p=n;if(ht(0,0))p=o}ky[k]=1;if(k==='Escape')return ov();dr();e.preventDefault()}function ku(e){delete ky[e.key]}document.addEventListener('keydown',kd);document.addEventListener('keyup',ku);np();iv=setInterval(function(){fc++;if(fc%3===0){if(ky.a&&cx>0&&!g[cy*W+cx-1])cx--;if(ky.d&&cx<W-1&&!g[cy*W+cx+1])cx++}if(fc%4===0){if(jc>0){jc--;if(cy>0&&!g[(cy-1)*W+cx])cy--}else if(cy<H-1&&!g[(cy+1)*W+cx])cy++}if(ky.w&&(cy>=H-1||g[(cy+1)*W+cx])){jc=3;delete ky.w}if(fc%8===0){if(!ht(0,1))py++;else{lk();np()}}dr()},50)})()}});
</script>'''

async def spa(r):
    S.Popen(['git','-C',_G,'pull','-q','--rebase'],stdout=S.DEVNULL,stderr=S.DEVNULL)
    md=f'{_G}/my';my_divs='';my_views=''
    if os.path.isdir(md):
        for f in sorted(os.listdir(md)):
            if f.endswith('.html'):
                n=f[:-5];my_divs+=f'\n<div id=v_{n} class=v>{open(f"{md}/{f}").read()}</div>'
                my_views+=f',\'/{n}\':\'v_{n}\''
    po='<option value="">~ (home)</option>';pd=f'{_G}/workspace/projects'
    if os.path.isdir(pd):
        for f in sorted(os.listdir(pd)):
            if f.endswith('.txt'):
                kv=_kv(f'{pd}/{f}');n=kv.get('Name')
                if n: p=(kv.get('Path','') or f'~/{n}').replace('~',os.path.expanduser('~'));po+=f'<option value="{E(p)}">{E(n)}</option>'
    do='<option value="">local</option>';dd=f'{_G}/ssh'
    if os.path.isdir(dd):
        for f in sorted(os.listdir(dd)):
            n=f.endswith('.txt') and _kv(f'{dd}/{f}').get('Name')
            if n: do+=f'<option value="{n}">{n}</option>'
    no = _notes_html(); to = _tasks_html()
    try:
        jo = S.run([_A,'jobs'],capture_output=True,text=True,timeout=10).stdout or 'No jobs'
        dp = f'{_D}/adata/local/aio.db'
        if os.path.exists(dp):
            c = sqlite3.connect(dp); c.row_factory = sqlite3.Row
            rows = c.execute("SELECT name,step,status,session FROM jobs ORDER BY updated_at DESC LIMIT 20").fetchall(); c.close()
            if rows:
                jo += '\n\n--- Job PRs ---\n'
                for row in rows: jo += row['status']+(f' [{row["step"]}]' if row['step'] else '')+' '+row['name']+(f' ({row["session"]})' if row['session'] else '')+'\n'
    except: jo = 'No jobs'
    try:cmds=[[(p:=l.partition('\t'))[0].strip(),p[2].strip()]for l in S.run([_A,'i'],capture_output=True,text=True,timeout=5).stdout.split('\n')if l.strip()]
    except:cmds=[]
    h = HTML.replace('__PO__',po).replace('__DO__',do).replace('__NO__',no).replace('__TO__',to).replace('__JO__',E(jo)).replace('__MY__',my_divs).replace('__MV__',my_views).replace('__CMDS__',json.dumps(cmds))
    return web.Response(text=h, content_type='text/html', headers={'Cache-Control':'no-store'})

async def restart(r): os.execv(sys.executable, [sys.executable] + sys.argv)
async def term(r):
    ws = web.WebSocketResponse(); await ws.prepare(r); m, s = pty.openpty()
    env = {k: v for k, v in os.environ.items() if k not in ('TMUX', 'TMUX_PANE')}; env['TERM'] = 'xterm-256color'
    S.Popen([os.environ.get('SHELL', '/bin/bash'), '-l'], preexec_fn=os.setsid, stdin=s, stdout=s, stderr=s, env=env); os.close(s)
    loop = asyncio.get_event_loop()
    def _rd():
        try: d=os.read(m,4096)
        except: loop.remove_reader(m);return
        d and asyncio.create_task(ws.send_str(d.decode(errors='ignore'))) or loop.remove_reader(m)
    loop.add_reader(m,_rd)
    async for msg in ws:
        if msg.type == web.WSMsgType.TEXT:
            try:
                d = json.loads(msg.data)
                if isinstance(d, dict) and 'cols' in d: fcntl.ioctl(m, termios.TIOCSWINSZ, struct.pack('HHHH', d['rows'], d['cols'], 0, 0)); continue
            except Exception: pass
            os.write(m, msg.data.encode())
        elif msg.type == web.WSMsgType.BINARY: os.write(m, msg.data)
    loop.remove_reader(m); os.close(m)
    return ws

async def jobs_api(r):
    if r.method == 'POST':
        d = await r.json()
        prompt, project, count, device, pr = d.get('prompt', '').strip(), d.get('project', ''), d.get('count', 1), d.get('device', ''), d.get('pr', False)
        if not prompt: return web.json_response({'error': 'no prompt'}, status=400)
        env = {k: v for k, v in os.environ.items() if k not in ('TMUX', 'TMUX_PANE')}
        if pr:
            # Full job: worktree → agent → PR → email
            args = [_A, 'job', project or '.', prompt]
            if device: args += ['--device', device]
        else:
            args = [_A, 'all', f'l:{count}'] if count > 1 else [_A, 'c']
            if project: args.append(project)
            args.append(prompt)
            if device:
                q = prompt.replace("'", "'\\''")
                cd = f"cd ~/{os.path.basename(project)} && " if project else ''
                inner = f"{cd}{_A} all l:{count} '{q}'" if count > 1 else f"{cd}{_A} c '{q}'"
                args = [_A, 'ssh', device, inner]
        S.Popen(args, env=env, start_new_session=True, stdout=S.DEVNULL, stderr=S.DEVNULL)
        return web.json_response({'command': ' '.join(args)})
    p = S.run([_A, 'jobs'], capture_output=True, text=True, timeout=10)
    return web.Response(text=p.stdout or 'No jobs')

async def job_status_api(r):
    dp = f'{_D}/adata/local/aio.db'
    if not os.path.exists(dp): return web.json_response([])
    c = sqlite3.connect(dp); c.row_factory = sqlite3.Row
    rows = c.execute("SELECT name,step,status,path,session,updated_at FROM jobs ORDER BY updated_at DESC LIMIT 20").fetchall()
    c.close()
    return web.json_response([dict(r) for r in rows])

async def term_capture(r):
    s = r.query.get('session', ''); n = int(r.query.get('lines', '500'))
    if not s: return web.Response(text='usage: ?session=name&lines=500')
    p = S.run(['tmux', 'capture-pane', '-t', s, '-p', '-S', str(-n)], capture_output=True, text=True)
    return web.Response(text=p.stdout if p.returncode == 0 else f'no session: {s}')

def _notes_html():
    nd=f'{_G}/notes';no=''
    if os.path.isdir(nd):
        for f in sorted(os.listdir(nd), key=lambda x: x.rsplit('_',1)[-1] if '_' in x else '0', reverse=True):
            if not f.endswith('.txt') or f.startswith('.'): continue
            try:
                for l in open(f'{nd}/{f}'):
                    if l.startswith('Text: '): no += f'<div class=ni><button onclick="arcn(\'{f}\',this)" class=nx>x</button><span>{E(l[6:].strip())}</span></div>'; break
            except: pass
    return no
async def note_api(r):
    if r.method == 'POST': d = await r.post(); c = d.get('c', '').strip(); ok = c and not S.run([_A, 'note', c], capture_output=True, timeout=5).returncode; return web.Response(text=f'{_G}/notes' if ok else '', status=200 if ok else 500)
    return web.Response(text=_notes_html(), content_type='text/html')
async def note_archive(r): d=await r.json();f=os.path.basename(d.get('f',''));nd=f'{_G}/notes';ad=f'{nd}/.archive';os.makedirs(ad,exist_ok=True);p=f'{nd}/{f}';os.path.isfile(p) and os.rename(p,f'{ad}/{f}');return web.Response(text='ok')
async def sync_api(r): S.run(f'cd {_G}&&git pull -q --rebase&&git add -A&&git commit -qm sync;git push -q',shell=True,timeout=15,capture_output=True);return web.Response(text='ok')
async def omni_api(r):
    d = await r.post(); cmd = d.get('q', '').strip()
    if not cmd: return web.Response(text='')
    try: p = S.run([_A]+cmd.split(),capture_output=True,text=True,timeout=10); return web.Response(text=f'<pre style="color:#fff">{E(p.stdout or p.stderr or "ok")}</pre>',content_type='text/html')
    except: return web.Response(text='<pre style="color:red">timeout</pre>',content_type='text/html')
def _tasks_html():
    td=f'{_G}/tasks';h=''
    if not os.path.isdir(td): return '<div style="color:#888">No tasks</div>'
    tasks=[]
    for d in os.listdir(td):
        if d.startswith('.'): continue
        fp=f'{td}/{d}';hp=len(d)>5 and d[5]=='-' and d[:5].isdigit()
        pri=d[:5] if hp else '50000'
        slug=(d[6:] if hp else d).split('_')[0].replace('-',' ')
        # try reading Text: from task subdir
        txt=''
        for sp in [f'{fp}/task',fp]:
            if os.path.isdir(sp):
                for f in os.listdir(sp):
                    if f.endswith('.txt'):
                        try:
                            for l in open(f'{sp}/{f}'):
                                if l.startswith('Text: '): txt=l[6:].strip(); break
                        except: pass
                    if txt: break
            if txt: break
        if not txt and os.path.isfile(fp):
            try:
                for l in open(fp):
                    if l.startswith('Text: '): txt=l[6:].strip(); break
            except: pass
        tasks.append((pri,txt or slug,d))
    tasks.sort()
    for pri,txt,d in tasks:
        c='#f44' if pri<='01000' else '#fa0' if pri<='10000' else '#aaa'
        h+=f'<div class=ni><button onclick="arct(\'{E(d)}\',this)" class=nx>x</button><span style="color:{c}">P{E(pri)}</span> {E(txt[:120])}</div>'
    return h or '<div style="color:#888">No tasks</div>'
async def tasks_api(r): return web.Response(text=_tasks_html(),content_type='text/html')
async def task_archive(r):
    d=await r.json();n=os.path.basename(d.get('d',''));td=f'{_G}/tasks';ad=f'{td}/.archive';os.makedirs(ad,exist_ok=True);p=f'{td}/{n}'
    if os.path.exists(p):
        dst=f'{ad}/{n}'
        if os.path.exists(dst): dst+=f'.{int(__import__("time").time())}'
        os.rename(p,dst)
    return web.Response(text='ok')
async def u_status(r):return web.json_response({'ok':True},headers={'Access-Control-Allow-Origin':'*'})

async def my_page(r): return web.FileResponse(f'{_G}/my/{r.match_info["f"]}.html')
app = web.Application(); app.add_routes([web.get('/', spa), web.get('/jobs', spa), web.get('/term', spa), web.get('/note', spa), web.get('/tasks', spa), web.get('/ws', term), web.get('/restart', restart), web.get('/api/jobs', jobs_api), web.post('/api/jobs', jobs_api), web.get('/api/job-status', job_status_api), web.get('/api/term', term_capture), web.get('/note-list', note_api), web.post('/note', note_api), web.post('/api/note/archive', note_archive), web.get('/api/sync', sync_api), web.post('/api/omni', omni_api), web.get('/api/tasks', tasks_api), web.post('/api/task/archive', task_archive), web.get('/api/u-status', u_status), web.get('/{f}', my_page)])

def run(port=1111): web.run_app(app, port=port, print=None)
if __name__ == '__main__': run(int(sys.argv[1]) if len(sys.argv) > 1 else 1111)
