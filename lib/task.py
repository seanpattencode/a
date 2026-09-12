"""a-side task engine, CANONICAL (i task = stub). Data adata/git/tasks.txt + tasks-archive.txt; ordinal = block order; block = `== [date] title [a:window] ==` + context lines.
CLI: task.py [N|add <t>|ctx N <l>|down N|archive N (github-url receipt)|agent N [window|self]|date N [ts]|rank N K]; free text = add; add --by <model> = LLM task; sync = a sync."""
import html,os,re,subprocess,sys,threading,time
D=os.environ['HOME']+'/a/adata/git';F=D+'/tasks.txt';TAG=r' ?\[a:([^\]\s]*)\]';DT=r'^== (\d{4}-\d\d-\d\d(?: \d\d:\d\d:\d\d)?|\d\d-\d\d) ' # date prefix on the title, to the second; MM-DD (days.txt form) still read
_c=os.path.expanduser('~/a/adata/git/workspace/config.txt');_cfg=open(_c).read()if os.path.exists(_c)else''
g=lambda k,d='':(re.search(r'^'+k+r': *(\S+)',_cfg,re.M)or(0,d))[1] # config field
AG=g('m_agent','claude');MD=g('m_model');EF=g('m_effort');PM=g('m_perms','bypass') # a j spawn knobs
CF='--dangerously-skip-permissions'+(MD and' --model '+MD)+(EF and' --effort '+EF) # same flags a res resumes with
MO=dict(claude=('claude-fable-5 claude-fable-5-1'.split(),'max high medium low'.split()),codex=('gpt-5.5 gpt-6-astra gpt-5'.split(),'xhigh max high medium low'.split()),agy=('gemini-3.8-flash-high gemini-3.1-pro-high'.split(),'low medium high'.split())) # models+efforts per agent, default first
def sel(k,v,o):return'<select class=bb onchange="cf(\''+k+' \'+this.value)">'+''.join('<option'+(' selected'if x==v else'')+'>'+x+'</option>'for x in o)+'</select>' # dropdown -> cf
P0=g('prompt','default');TP=D+'/common/prompts/task-agent.txt' # a j appends common/prompts/<P0>.txt under every spawn (data.c dprompt); none = appends nothing
DEV=os.uname().nodename # this machine; a task's dev: line says where its agent lives
TS='tasks.txt tasks-done.txt common/prompts/task-agent.txt'
SYNC=f'flock {D}/.tasksync sh -c "cd {D}&&git add {TS} 2>/dev/null;git commit -qm task -- {TS}" >/dev/null 2>&1 &'  # LOCAL commit only: adata/git main has no push uplink; fleet sync distributes
tm=lambda f:subprocess.run(['tmux','list-panes','-s','-t','a','-F',f],capture_output=True,text=True).stdout.splitlines()
def blocks(): # → (lines, [header line idx…, end])
 ls=open(F).read().splitlines();return ls,[i for i,l in enumerate(ls)if l.startswith('== ')]+[len(ls)]
def me(): # (window, session-id) of the agent calling this: pid chain → `claude --resume <uuid>` → tmux pane
 P=dict(l.split(None,1)for l in tm('#{pane_pid} #{window_name}'));p=os.getpid();s=''
 while p>1:
  c=open(f'/proc/{p}/cmdline','rb').read().decode('utf8','replace').replace('\0',' ')
  if not s and'claude'in c and(m:=re.search(r'[0-9a-f]{8}-[0-9a-f-]{27}',c)):s=m[0]
  if str(p)in P:return P[str(p)],s
  p=int(open(f'/proc/{p}/stat').read().rsplit(')',1)[1].split()[1])
 return'',s
def live(): # {session-id: (window, session:index, command)} for every agent under a pane — identity that survives a reboot, unlike the window name (a res re-pairs names with sessions)
 PS={}
 for l in subprocess.run(['ps','-eo','pid=,ppid=,args='],capture_output=True,text=True).stdout.splitlines():
  f=l.split(None,2);PS[f[0]]=(f[1],f[2]if len(f)>2 else'')
 P={l.split('\t')[0]:tuple(l.split('\t')[1:])for l in tm('#{pane_pid}\t#{window_name}\t#{session_name}:#{window_index}\t#{pane_current_command}\t#{pane_current_path}')}
 o={};late=[]
 for q,(_,a)in PS.items():
  if not a.startswith('claude ')and'/claude 'not in a[:60]:continue
  m=re.search(r'[0-9a-f]{8}-[0-9a-f-]{27}',a);z=q
  while z in PS and z not in P:z=PS[z][0]
  if z not in P:continue
  if m:o[m[0]]=P[z][:3]
  else:late.append((q,P[z]))
 for q,w in late: # fresh agent (no id in argv): newest unclaimed transcript for its cwd, only if written since the process started — an older one is a different conversation (09-12 wrong-sid bug)
  try:t0=os.stat('/proc/'+q).st_ctime-5
  except OSError:t0=0
  d=os.path.expanduser('~/.claude/projects/'+w[3].replace('/','-'))
  c=sorted(((os.path.getmtime(d+'/'+f),f[:-6])for f in os.listdir(d)if f.endswith('.jsonl')and f[:-6]not in o and os.path.getmtime(d+'/'+f)>=t0),reverse=True)if os.path.isdir(d)else[]
  if c:o[c[0][1]]=w[:3]
 return o
def run(a,cli=False): # THE mutation path — CLI and the web bridge both call it; returns the reply text
 ls,hi=blocks();n=len(hi)-1;blk=lambda k:slice(hi[k],hi[k+1])
 if a[:1]==['add']and a[1:]:by=a[2]if a[1]=='--by'and a[3:]else'';ls[0:0]=['== '+' '.join(a[3:]if by else a[1:])+' ==']+([f'by: {by}']if by else[])+[''] # --by <model>: provenance line for an LLM-entered task (default.txt LLM-TAGGED TASKS)
 elif a[:1]==['ctx']and a[2:]:ls.insert(hi[int(a[1])],' '.join(a[2:]))
 elif a[:1]==['down']:k=int(a[1])-1;j=min(k+100,n-1);ls[hi[k]:hi[j+1]]=ls[hi[k+1]:hi[j+1]]+ls[blk(k)] # rank down 100 places (Sean 08-27: one is too close; 09-12: 10 still too close)
 elif a[:1]in(['archive'],['done']):k=int(a[1])-1;open(D+'/tasks-archive.txt','a').write(time.strftime('%F ')+'\n'.join(ls[blk(k)])+'\n');del ls[blk(k)];arch=1
 elif a[:1]==['rank']and a[2:]:k=int(a[1])-1;j=min(max(int(a[2])-1,0),n-1);b=ls[blk(k)];del ls[blk(k)];hi=[i for i,l in enumerate(ls)if l.startswith('== ')]+[len(ls)];ls[hi[j]:hi[j]]=b # move block N to position K
 elif a[:1]==['date']:k=hi[int(a[1])-1];v=' '.join(a[2:]).replace('T',' ');ls[k]=re.sub(DT,'== ',ls[k]);ls[k]=ls[k].replace('== ',f'== {v} ',1)if re.fullmatch(r'\d{4}-\d\d-\d\d(?: \d\d:\d\d(?::\d\d)?)?|\d\d-\d\d',v)else ls[k]
 elif a[:1]==['agent']:
  L=live()
  if'?'in a[1:]:return'\n'.join(f'{v[1]:<6} {v[0]:<26} {i[:8]}'for i,v in sorted(L.items(),key=lambda t:(len(t[1][1]),t[1][1])))or'x none running' # `agent ?` — the agents running right now — pick one by session:index, window, or sid prefix
  k=hi[int(a[1])-1];q=a[2]if a[2:]else''
  C=[(i,v)for i,v in L.items()if q and(v[0]==q or v[1]==q or i.startswith(q))] # resolve among the RUNNING: window name · session:index · sid prefix — no id to type by hand
  if len(C)>1:return f'x ambiguous ({len(C)}) — '+' · '.join(f'{v[1]}={i[:8]}'for i,v in C) # never bind the first match: window names repeat (3 are 'r-i'), and first-match silently tags a neighbour's agent
  w,sd=me()if q=='self'else(C[0][1][0],C[0][0])if C else(q if q in tm('#{window_name}')else'','') # a name matching no live agent must still be a real window, else 'x no such window'
  ls[k]=re.sub(TAG,'',ls[k])[:-2].rstrip()+(f' [a:{w}]'if w else'')+' =='
  ls[k+1:hi[int(a[1])]]=[l for l in ls[k+1:hi[int(a[1])]]if not l.startswith(('sid: ','dev: '))]
  if sd:ls.insert(k+1,f'sid: {sd}') # the session id IS the agent; the window name is only its current address
  if w:ls.insert(k+1,f'dev: {DEV}') # which machine it runs on. FUTURE (Sean 2026-08-28): spawn/resume on ANOTHER device over ssh (a ssh <name> + this same resume command) — not implemented; today every agent here is local
  wd=w and next((l.split('\t')[1]for l in tm('#{window_name}\t#{pane_current_path}')if l.split('\t')[0]==w),'')
  ls[k+1:hi[int(a[1])]]=[l for l in ls[k+1:hi[int(a[1])]]if not l.startswith('resume: ')]
  if wd:ls.insert(k+1,'resume: cd '+wd+' && '+(f'claude {CF} --resume {sd}'if sd else'claude {CF} --continue')) # full copy-pasteable command; --resume <sid> hits THAT session (--continue would take the cwd's newest)
  if not w and a[2:]:return'x no such window'
 else:return'x add|ctx|down|archive|agent'
 open(F,'w').write('\n'.join(ls)+'\n')
 os.system(SYNC)if cli else threading.Thread(target=os.system,args=(SYNC,),daemon=True).start() # git sync off the request path
 if locals().get('arch'): # archive must PROVE itself: push board+archive to origin, receipt = the github commit url (ui-principles 5/16)
  gc='cd '+D+'&&git fetch origin -q&&export GIT_INDEX_FILE=/tmp/_ti$$;git read-tree origin/main;git update-index --add --cacheinfo 100644,$(git hash-object -w tasks.txt),tasks.txt;git update-index --add --cacheinfo 100644,$(git hash-object -w tasks-archive.txt),tasks-archive.txt;n=$(git commit-tree $(git write-tree) -p origin/main -m "task archive");rm -f $GIT_INDEX_FILE;unset GIT_INDEX_FILE;git push origin -q $n:main&&u=$(git config remote.origin.url);u=${u#*github.com[:/]};echo "https://github.com/${u%.git}/commit/$(git rev-parse --short $n)"'
  r=subprocess.run(['sh','-c',gc],capture_output=True,text=True).stdout.strip()
  return '✓ archived · pushed '+r if r.startswith('http') else '✓ archived locally · ✗ push failed — retry or check origin'
 return'✓'
def setblock(n,t): # replace block n with the editor's text (first line = title; == == re-added if dropped); web /tasks/set
 ls,hi=blocks();L=[l.rstrip()for l in t.strip('\n').splitlines()]
 if not L:return'x empty'
 if not L[0].startswith('== '):L[0]=f'== {L[0].strip("= ").strip()} =='
 ls[hi[n-1]:hi[n]]=L+[''];open(F,'w').write('\n'.join(ls)+'\n');threading.Thread(target=os.system,args=(SYNC,),daemon=True).start();return'✓'
def pset(t): # /tasks/set n=0: save the prompt template (spawn reads TP, else TA)
 open(TP,'w').write(t.rstrip('\n')+'\n');threading.Thread(target=os.system,args=(SYNC,),daemon=True).start();return'✓ prompt saved → '+TP
TA="You are an agent spawned to help your user accomplish the task described above. You should do this step by step with the user rather than all at once and expect that this process will result in slight or major changes in the above task as preliminary steps change what the user realizes is valuable and they review the output at each step to be able to direct the next one. You should gather context as needed then propose the most straightforward simplified thing to do and then wait for the user to ok it or modify it before actually proceeding with actions or coding beyond simply reading information. It is strongly recommended that each step be kept to under 200 token equivalent of code or information so that the user can review and redirect the thing continuously throughout the process and minimize the amount of errors and assumptions that can be made that differ from the user's goals." # Sean's text 2026-09-12, typos cleaned as ordered · the task block is prepended by spawn(), never stored in the editable template · file override: adata/git/common/prompts/task-agent.txt
def spawn(n): # board task n -> a j with the task-agent prompt, label the window, tag [a:label] + resume line (i web.py _spawn ported a-side: no bridge, Sean 2026-09-05)
 ls,hi=blocks()
 if not 0<n<len(hi):return'x bad task'
 h=ls[hi[n-1]].strip('= ').strip()
 if'[a:'in h:return'x already owned: '+h[:60]
 lab=('t%d-'%n+re.sub(r'[^a-z0-9]+','-',h.lower())[:14].strip('-'))[:20].rstrip('-')
 pr=('== TASK __N__ (localhost:1111/tasks · tmux window __LABEL__) ==\n__ENTRY__\n\n'+(open(TP).read()if os.path.exists(TP)else TA)).replace('__LABEL__',lab).replace('__N__',str(n)).replace('__ENTRY__','\n'.join(ls[hi[n-1]:hi[n]]))
 if os.environ.get('TASK_DRY'):return'dry: label %s, prompt %d chars'%(lab,len(pr))
 r=subprocess.run(['a','j',pr],capture_output=True,text=True,timeout=60);m=re.search(r'\bj-[\w.-]+',r.stdout+r.stderr)
 if not m:return'x spawn: '+(r.stdout+r.stderr).strip()[-160:]
 subprocess.run(['sh',os.path.expanduser('~/a/lib/label.sh'),lab,m[0]],timeout=10)
 for _ in range(4): # transcript lands a beat after launch: wait for a real sid
  r=run(['agent',str(n),lab],True);ls,hi=blocks()
  if any(l.startswith('sid: ')for l in ls[hi[n-1]:hi[n]]):break
  time.sleep(1.2)
 return'spawned %s ← %s · '%(lab,m[0])+r
def resume(n): # RESUMABLE -> LIVE: run the block's resume: line in a window carrying its label
 ls,hi=blocks()
 if not 0<n<len(hi):return'x bad task'
 m=re.search(TAG,ls[hi[n-1]]);r=next((l[8:].strip()for l in ls[hi[n-1]+1:hi[n]]if l.startswith('resume: ')),'')
 if not(m and r):return'x no [a:label] + resume: line'
 subprocess.run(['tmux','new-window','-d','-n',m[1],'sh','-c',r+'; exec bash'],timeout=20);return'resumed → tmux window '+m[1]
def page(): # Local navigation; rank = file order, date = soonest first.
 W={};[W.setdefault(p[0],p[1:])for p in(l.split('\t')for l in tm('#{window_name}\t#{session_name}:#{window_index}\t#{pane_current_command}'))if len(p)==3];S=live()
 B=lambda v,t,x='',_K={'archive':'e','done':'e','down':'d','rank':'r','agent':'a','spawn':'g','resume':'g','go':'g'}:(lambda k=_K.get(v):f'<button class=bb'+(f' data-k={k}' if k else '')+f' onpointerdown="tq(\'{v}\',this,\'{x}\')">{t}'+(f' ({k})' if k else '')+'</button>')();e=html.escape;LM=lambda t:re.sub(r'(?m)^(\[LLM.*)',r'<i style=color:#666>\1</i>',e(t));ls,hi=blocks();R=lambda k,t:f'<b style="color:{k}">{t}</b> ';O=range(len(hi)-1);mo=MO.get(AG)or MO['claude'];apnd=P0!='none'and os.path.exists(D+'/common/prompts/'+P0+'.txt')
 def row(i,h,body):
  pv='\n'.join(l for l in body.splitlines() if not l.startswith(('resume: ','sid: ','dev: ')))
  m=re.search(TAG,h);w=m[1]if m else'';sd=next((l[5:].strip()for l in body.splitlines()if l.startswith('sid: ')),'');rs=next((l[8:].strip()for l in body.splitlines()if l.startswith('resume: ')),'');dvc=next((l[5:].strip()for l in body.splitlines()if l.startswith('dev: ')),'')
  inf=S.get(sd)or next((v for v in S.values()if w and v[0]==w),None)or(None if sd else(w,)+W[w]if w in W else None) # sid first; else ANY live agent holding the window — a stale sid must not hide a running agent (09-12); else the bare window
  w=inf[0]if inf else w;st='LIVE'if inf else'NO AGENT'if not w else'RESUMABLE'if rs else'GONE';c='#6a6'if inf else'#fc6'if st=='RESUMABLE'else'#f66'
  d=re.match(DT,h);dp=f'<span class=n style="background:#524">{d[1]}</span> 'if d else'';dv=(d[1]if d and len(d[1])>5 else'').replace(' ','T')
  D=[('rank',str(i),'position in the list'),('date',d[1]if d else'','deadline, to the second'),('state',st,'LIVE · RESUMABLE · GONE · NO AGENT'),('window',w,'tmux window name — the address it has today'),('tmux',inf[1]if inf else'','session:index'),('process',inf[2]if inf else'','foreground process in that pane'),('device',dvc,'the machine this agent runs on — cross-device spawn over ssh is future work'),('session id',sd,'THE agent — survives a reboot, unlike the window name'),('resume',rs,'full command — copy it, or press resume')]
  dt=''.join(f'<div><b title="{t}">{k}</b><span>{e(v)}</span>'+('<button class=bb style="font-size:12px;padding:0 7px" onpointerdown="navigator.clipboard.writeText(this.previousElementSibling.textContent);this.textContent=\'copied\'">copy</button>'if k=='resume'else'')+'</div>'for k,v,t in D if v)
  ix=inf[1].split(':')[-1]if inf else''
  act=B('spawn','spawn new agent')if st=='NO AGENT'else f'<a class=bb data-w="{ix}" target=_blank>open in a term</a>'+B('go','go → local terminal',ix)if inf else B('resume','resume')if st=='RESUMABLE'else''
  s=R(c,st)+(f'<span class=n>{e(w)}</span> 'if w else'')+act+'<button class=bb data-k=i onpointerdown="nf(this)">details ⋯ (i)</button>'
  return(f'<div class=tk data-i={i} data-d="{d and(d[1]if len(d[1])>5 else time.strftime("%Y-")+d[1])or""}" style="margin:6px 0;border:1px solid {c};border-radius:8px;padding:5px 8px"><div style="font-size:17px"><span class=n>rank {i}</span> {dp}{e(re.sub(DT,"== ",re.sub(TAG,"",h)).strip("= ").strip())}</div>'
   +(f'<div style="color:#999;font-size:14px;white-space:pre-wrap;margin:2px 0 0;flex:1 1 auto;min-height:0;overflow-y:auto">{LM(pv)}</div>' if pv else'')
   +f'<div class=nf hidden style="font-size:13px;margin-top:6px;border-top:1px solid #444;padding-top:6px">{dt}</div>'
   +'<div class=ft>'+(f'<div class=tlw data-w="{ix}"><span class=d style="font-size:10.5px">terminal output · {e(w)} · press to open full</span><pre class=tl data-x="{inf[1]}">connecting…</pre></div>'if inf else'')+f'<div class=bt>{s} {B("archive","archive")} {B("down","rank down 100")} <label class=bb style="font-size:12px">date <input type=datetime-local step=1 value="{dv}" onchange="tq(\'date\',this,this.value)" style="font:inherit;background:#000;color:#fff;border:0" title="empty the field to clear the date"></label> <details class=mm><summary class=bb>more ⋯</summary>{B("rank","set rank")} {B("agent","tag existing agent")}</details></div></div></div>')
 return('<meta charset=utf8><meta name=viewport content="width=device-width,initial-scale=1"><title>a tasks</title><style>body{margin:0;padding:12px;background:#000;color:#fff;font:16px ui-monospace,monospace;touch-action:manipulation}a{color:#fff}.d{color:#888;font-size:13px}.bb{font:14px ui-monospace,monospace;padding:6px 12px;background:#000;color:#fff;border:1px solid #666;border-radius:8px;cursor:pointer}'   # own dark shell: the page is served by a itself now (Sean 2026-09-05 'white but should be black bg'); i's frame used to supply body/.bb/.d
 '.ft{position:sticky;bottom:0;background:#000;margin-top:auto;padding-top:2px}.bt{display:flex;flex-wrap:wrap;gap:5px;align-items:center;font-size:12px;padding-top:4px}.bt>*{flex:none}.mm{display:flex;gap:5px;align-items:center;flex-wrap:wrap}.mm summary{list-style:none;cursor:pointer}.mm[open] summary{background:#333}.tlw{cursor:pointer;background:#0a0a0a;border:1px solid #666;border-radius:8px;padding:3px 8px}.tl{margin:0;font-size:12px;line-height:1.4;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#fff}.mm[open]{flex:1 1 100%}.bt .bb{font-size:12px;padding:1px 8px;text-decoration:none}.top .bb{font-size:12px;padding:2px 8px}@media(max-width:700px){.bt{flex-direction:column;align-items:stretch}}.n{background:#333;color:#ccc;border-radius:5px;padding:0 6px;font-size:11px;vertical-align:middle}.top{display:flex;flex-wrap:wrap;gap:7px;align-items:center;margin:0 0 6px}.grp{display:flex;gap:5px;align-items:center;border:1px solid #555;border-radius:7px;padding:2px 6px;background:#111;font-size:12px}#cnt{min-width:6.5em;text-align:center;font:12px system-ui;color:#ccc}#dgs{flex-wrap:wrap;max-width:100%}#dgs select{max-width:84vw}body.one .tk{display:none}body.one .tk.on{display:flex;flex-direction:column;height:56vh;overflow-y:auto}.nf div{display:flex;gap:9px;align-items:baseline;margin:4px 0}.nf b{background:#222;border:1px solid #555;border-radius:5px;padding:0 7px;min-width:7em;font:13px system-ui;color:#bbb;text-align:center}</style>' # thin: one button per line
+''.join(row(j+1,ls[hi[j]],'\n'.join(ls[hi[j]+1:hi[j+1]]).strip())for j in O)
 +f'<div class=top><span class=grp><button class=bb id=sR onpointerdown="tsort()">order: rank (s)</button></span>'
 '<span class=grp><button class=bb onpointerdown="step(-1)">‹ prev (k)</button><b id=cnt>list</b><button class=bb onpointerdown="step(1)">next › (j)</button></span>'
 +'<span class=grp id=dgs><button class=bb onpointerdown="sp()" title="tag+spawn the top N tasks of the current order (rank or date) with the knobs to the right">tag+spawn top</button><input id=sn class=bb value=3 inputmode=numeric style="width:3.5em"><label class=d>spawn</label><select class=bb onchange="ga(this.value)">'+''.join('<option'+(' selected'if AG==x else'')+'>'+x+'</option>'for x in MO)+'</select>'+sel('m_model',MD or mo[0][0],mo[0])+(sel('m_effort',EF or mo[1][0],mo[1])if mo[1]else'')+sel('m_perms',PM,['bypass','ask'])+'</span><span class=grp><button class=bb onpointerdown="pv()">prompt · view/edit (o)</button></span></div>'
 '<div id=tr0 class=d style="height:20px;line-height:20px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">&nbsp;</div><div id=tar class=d style="height:20px;line-height:20px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis"></div>'
 +'<div id=pp hidden style="border:1px solid #666;border-radius:8px;padding:8px;margin:6px 0"><div class=d>what every task agent is told. The task text is placed above it automatically. Saves to '+e(TP)+'</div><label class=d style="display:block;margin:4px 0"><input type=checkbox '+('checked 'if apnd else'')+'onchange="cf(this.checked?\'prompt default\':\'prompt none\')" style="width:18px;height:18px;vertical-align:middle"> '+('common/prompts/'+e(P0)+'.txt is also added under it on every spawn. Untick to send this prompt alone.'if apnd else'default.txt is NOT added. The agent gets this prompt alone. Tick to add it.')+'</label><textarea id=pt spellcheck=false style="width:96%;height:38vh;background:#000;color:#fff;font:14px/1.45 monospace;border:1px solid #fff;border-radius:8px;padding:8px">'+e(open(TP).read()if os.path.exists(TP)else TA)+'</textarea><div><button class=bb onpointerdown="ps()">save prompt</button></div></div>'
 
 +'<input id=tn class=bb placeholder="new task · c = focus" style="width:70%;font-size:20px" onkeydown="if(event.key==\'Enter\'&&tn.value)tq(\'add\',null,tn.value)"> <button class=bb onpointerdown="if(tn.value)tq(\'add\',null,tn.value)">add (⏎)</button>'
 '<script>const rows=()=>[...document.querySelectorAll(".tk")];var K=0;'
  'function step(d){show(d,performance.now())}'
 'function q(c){return fetch("/api/omni",{method:"POST",body:"q=config "+c})}function cf(c){q(c).then(()=>location.reload())}' # strip -> a config via omni
 'function ga(v){q("m_agent "+v).then(()=>q("m_model off")).then(()=>cf("m_effort off"))}' # agent switch clears model+effort -> defaults
 'function sp(){var n=+sn.value||3,R=rows(),L=[];for(var i=0;i<R.length&&L.length<n;i++){var r=R[ORD?ORD[i]:i];if(r)L.push(+r.dataset.i)}(function go(k){if(k>=L.length){tr0.textContent="tag+spawn done ("+k+") - reloading";return setTimeout(()=>location.reload(),900)}tr0.textContent="tag+spawn "+(k+1)+"/"+L.length+" (task "+L[k]+")...";fetch("/tasks/spawn?n="+L[k]).then(q=>q.text()).then(t=>{tar.textContent="task "+L[k]+": "+t.trim().slice(0,90);go(k+1)}).catch(()=>{tar.textContent="task "+L[k]+": unreachable";go(k+1)})})(0)}' # top-N of the CURRENT order (rank, or date when sorted); spawn() itself refuses already-owned tasks


 'function show(d,t0){var R=rows();K=Math.max(0,Math.min(R.length-1,K+d));try{sessionStorage.tk=K}catch(e){}var cu=ORD?ORD[K]:K;R.forEach((r,i)=>r.classList.toggle("on",i==cu));'
 'cnt.textContent=(K+1)+" of "+R.length;tr0.textContent="task "+(K+1)+" of "+R.length+" · "+(performance.now()-t0).toFixed(3)+"ms"}'
 'document.body.classList.add("one");try{K=+sessionStorage.tk||0}catch(e){}show(0,performance.now());if(location.search.indexOf("by=date")>0)tsort();'
 'document.querySelectorAll("a[data-w]").forEach(a=>a.href="http://"+location.hostname+":1111/op?w="+a.dataset.w);' # real link: window.open was popup-blocked; /op keeps ?w= (/term drops it); w = window INDEX (names repeat after a res)
 'function renum(){rows().forEach((r,k)=>{r.dataset.i=k+1;r.querySelector(".n").textContent="rank "+(k+1)})}function nf(b){var x=b.closest(".tk").querySelector(".nf");x.hidden=!x.hidden}var TB=0;function tl(){if(document.hidden||TB)return;var r=rows()[ORD?ORD[K]:K],p=r&&r.querySelector(".tl");if(!p)return;TB=1;fetch("/api/omni",{method:"POST",headers:{"content-type":"application/x-www-form-urlencoded"},body:"q=cmd tmux capture-pane -pJt "+p.dataset.x+" -S -25"}).then(q=>q.text()).then(t=>{TB=0;t=t.replace(/^<pre[^>]*>/,"").replace(/<\\/pre>\\s*$/,"");var k=-1;for(var m of["⏵⏵","bypass permissions on","plan mode on","accept edits on"])k=Math.max(k,t.lastIndexOf(m));if(k>0)t=t.slice(0,t.lastIndexOf("\\n",k));var L=t.split("\\n").filter(x=>!/^\\d+(\\.\\d+)?us$|^[\\s>❯╭╰│╮╯─⏵⏸·]*$|^\\s*[·✢✳✻✽✶✺✹✸✷*]\\s/.test(x));p.textContent=(L[L.length-1]||"").trim()||"(blank pane)"}).catch(()=>{TB=0})}setInterval(tl,800);addEventListener("load",tl);document.addEventListener("pointerdown",function(e){var v=e.target.closest(".tlw");if(v)location="/fw?w="+v.dataset.w});function pv(){pp.hidden=!pp.hidden}function ps(){var t0=performance.now();fetch("/tasks/set",{method:"POST",body:"n=0&b="+encodeURIComponent(pt.value),headers:{"content-type":"application/x-www-form-urlencoded"}}).then(q=>q.text()).then(t=>{tr0.textContent="prompt · "+t.trim()+" · "+(performance.now()-t0).toFixed(1)+"ms"})}'
 'var ORD=null;function mkord(){var R=rows();return R.map(function(_,i){return i}).sort(function(a,b){var x=R[a].dataset.d,y=R[b].dataset.d;return(x?0:1)-(y?0:1)||(x<y?-1:x>y?1:0)||a-b})}function tsort(){var t0=performance.now();ORD=ORD?null:mkord();K=0;sR.textContent=ORD?"order: date (s)":"order: rank (s)";history.replaceState("","",ORD?"/tasks?by=date":"/tasks");show(0,t0)}function tq(v,b,x){var t0=performance.now(),r=b&&b.closest(".tk"),R=rows(),i=r?+r.dataset.i:0,c=v+(i?" "+i:"")+(x&&/^(add|date)$/.test(v)?" "+x:""),tt=r?r.querySelector("div").textContent.replace(/^rank \\d+ ?/,"").trim():"";'
  'var P={agent:"tmux window name of the agent already on it (blank = clear)",rank:"new rank (1 = top)"};if(P[v]){var w=prompt(P[v]);if(w===null)return;c+=" "+w}'
 'if(v=="down"){R[Math.min(i+99,R.length-1)].after(r);renum();if(ORD)ORD=mkord();show(0,t0)}if(v=="archive"){tar.textContent="archiving \u201c"+tt+"\u201d \u2026";r.remove();renum();if(ORD)ORD=mkord();show(0,t0)}var op=performance.now()-t0,local=/^(down|archive)$/.test(v);'
 'var u="/tasks/run",bd="c="+encodeURIComponent(c);if(/^(spawn|resume)$/.test(v)){u="/tasks/"+v+"?n="+i;bd=""}if(v=="go"){u="/problems/go?w="+encodeURIComponent(x);bd="";local=1}'
 'requestAnimationFrame(()=>requestAnimationFrame(()=>{var pt=performance.now()-t0;fetch(u,bd?{method:"POST",body:bd,headers:{"content-type":"application/x-www-form-urlencoded"}}:{}).then(q=>q.text()).then(t=>{'
 'tr0.textContent=c+" · op "+op.toFixed(3)+"ms · painted "+pt.toFixed(2)+"ms · "+t.trim()+" · round trip "+(performance.now()-t0).toFixed(1)+"ms";if(v=="archive"){var uu=(t.match(/https\\S+/)||[])[0];if(uu){tar.textContent="\u2713 archived \u201c"+tt+"\u201d \u2014 saved to ";var A=document.createElement("a");A.href=uu;A.target="_blank";A.style.color="#8ac";A.textContent=uu;tar.appendChild(A)}else tar.textContent="\u26a0 "+t.trim()+" \u2014 \u201c"+tt+"\u201d"}if(t[0]=="x"||!local)setTimeout(()=>location.reload(),600)}).catch(function(){tr0.textContent="\u2717 "+c+" did not reach the server \u2014 RELOAD this page (stale tab or server was restarting)";if(v=="archive")tar.textContent=tr0.textContent})}))}document.addEventListener("keydown",function(ev){if(ev.ctrlKey||ev.metaKey||ev.altKey)return;if(/INPUT|TEXTAREA|SELECT/.test((document.activeElement||{}).tagName||""))return;var q=ev.key;if(q=="j")return step(1);if(q=="k")return step(-1);if(q=="s")return tsort();if(q=="c"){ev.preventDefault();tn.focus();return}if(q=="o")return pv();var R=rows(),r=R[ORD?ORD[K]:K],b=r&&r.querySelector("[data-k="+q+"]");if(b)b.dispatchEvent(new PointerEvent("pointerdown",{bubbles:true}))});</script>')
if __name__=='__main__':
 t=time.perf_counter_ns();a=sys.argv[1:]
 if a[:1]in(['task'],['t']):a=a[1:] # via `a task` / `a t`: the command name rides argv
 if a[:1]==['sync']:os.execvp('a',['a','sync']) # hub job `a task sync` = adata/git sync
 if a and not a[0].isdigit()and a[0]not in('add','ctx','down','archive','done','rank','date','agent','page','web','set','spawn','resume'):a=['add']+a # free text = add (a task <text>, phone Cap)
 ls,hi=blocks();n=len(hi)-1
 if a and a[0].isdigit():print('\n'.join(ls[hi[int(a[0])-1]:hi[int(a[0])]]))
 elif a==['page']:print(page())
 elif a==['web']:print(run(sys.stdin.readline().split(),True)) # :1111 POST /tasks/run, the command line on stdin
 elif a[:1]==['set']:n=int(a[1]);print(pset(sys.stdin.read())if n==0 else setblock(n,sys.stdin.read())) # :1111 POST /tasks/set, block text on stdin; n=0 = the task-agent prompt template
 elif a[:1]==['spawn']:print(spawn(int(a[1])))
 elif a[:1]==['resume']:print(resume(int(a[1])))
 elif a:print(run(a,True))
 else:print(*(f'{j+1:3} {re.sub(TAG,"",ls[hi[j]]).strip("= ").strip()}{" ["+m[1]+"]"if(m:=re.search(TAG,ls[hi[j]]))else""}'for j in range(n)),f'{n} · {(time.perf_counter_ns()-t)/1e6:.4f}ms',sep='\n')
