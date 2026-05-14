"""aio gdrive"""
import sys,os,subprocess as sp,json
from _common import cloud_login,cloud_logout,cloud_sync,cloud_status,_configured_remotes,RCLONE_BACKUP_PATH,DATA_DIR,SYNC_ROOT,DEVICE_ID,alog,get_rclone
from sync import cloud_sync as cloud_sync_tar
_AF=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),'adata','git','gdrive_accounts.txt')
def _accts():
    if not os.path.exists(_AF):return[]
    return[{'n':p[0],'r':p[1],'d':p[2]if len(p)>2 else''}for l in open(_AF)if len(p:=l.strip().split(None,2))>=2]
def _res(n):
    for a in _accts():
        if n in(a['n'],a['r']):return a['r']
    for a in _accts():
        if n.lower()in a['n'].lower():return a['r']
    return n if n in _configured_remotes()else None
def _tgt(s):return s.split(':',1)if':'in s else(s,'')
def _rp(n,p):
    r=_res(n)
    if not r:print(f"x Unknown '{n}'");return None
    return f'{r}:{p}'
def _rc(args,**kw):
    rc=get_rclone()
    if not rc:print("x no rclone");return None
    return sp.run([rc]+args,**kw)
def _ok(r):return"✓"if r and r.returncode==0 else"x Failed"
def _trp(a,u):
    if not a:print(u);return None
    n,p=_tgt(a[0]);return _rp(n,p)
def _xfer(a,op,u,flags=[]):
    if len(a)<2:print(u);return
    if op!='copy'and not os.path.exists(a[0]):print(f"x {a[0]} not found");return
    n,p=_tgt(a[1]if op!='copy'else a[0]);rp=_rp(n,p)
    if not rp:return
    if op=='dl':
        print(f"← {rp} → {a[1]}");print(_ok(_rc(['copy',rp,a[1],'-P'])))
    else:
        rcmd='copy'if os.path.isdir(a[0])else'copyto'
        if op=='move':rcmd='move'
        print(f"→ {a[0]} → {rp}");print(_ok(_rc([rcmd,a[0],rp,'-P']+flags)))
def cmd_cp(a):_xfer(a,'cp',"a gdrive cp <local> <acct:dest>")
def cmd_mv(a):_xfer(a,'move',"a gdrive mv <local> <acct:dest>",['--delete-empty-src-dirs'])
def cmd_get(a):
    if len(a)<2:print("a gdrive get <acct:path> <local>");return
    n,p=_tgt(a[0]);rp=_rp(n,p)
    if rp:print(f"← {rp} → {a[1]}");print(_ok(_rc(['copy',rp,a[1],'-P'])))
def cmd_ls(a):
    rp=_trp(a,"a gdrive ls <acct:path>")
    if not rp:return
    r1=_rc(['lsd',rp],capture_output=True,text=True)
    r2=_rc(['lsf',rp,'--files-only'],capture_output=True,text=True)
    ds=[l.split()[-1]for l in(r1.stdout if r1 else'').strip().splitlines()if l.strip()]
    fs=(r2.stdout if r2 else'').strip().splitlines()
    if not ds and not fs:print("  (empty)");return
    for d in ds:print(f"  {d}/")
    for f in fs:print(f"  {f}")
    print(f"\n  {len(ds)} folders, {len(fs)} files")
def cmd_tree(a):
    rp=_trp(a,"a gdrive tree <acct:path>")
    if rp:r=_rc(['lsf',rp,'-R'],capture_output=True,text=True);print(r.stdout if r and r.stdout else"  (empty)")
def cmd_find(a):
    if len(a)<2:print("a gdrive find <acct:path> <glob>");return
    n,p=_tgt(a[0]);rp=_rp(n,p)
    if not rp:return
    r=_rc(['lsf',rp,'-R','--include',a[1]],capture_output=True,text=True)
    ls=(r.stdout if r else'').strip().splitlines()
    for l in ls:print(f"  {l}")
    print(f"  {len(ls)} matches"if ls else"  No matches")
def cmd_size(a):
    rp=_trp(a,"a gdrive size <acct:path>")
    if rp:_rc(['size',rp])
def cmd_rm(a):
    rp=_trp(a,"a gdrive rm <acct:path>")
    if not rp:return
    print(f"Delete {rp}? [y/N] ",end='',flush=True)
    if input().strip().lower()!='y':print("Cancelled");return
    r=_rc(['deletefile',rp],capture_output=True,text=True)
    if not r or r.returncode!=0:r=_rc(['purge',rp])
    print(_ok(r))
def cmd_link(a):
    rp=_trp(a,"a gdrive link <acct:path>")
    if not rp:return
    r=_rc(['link',rp],capture_output=True,text=True)
    print(r.stdout.strip()if r and r.returncode==0 and r.stdout.strip()else"x No link")
def cmd_backup(a):
    import re
    rcf=os.path.expanduser('~/.config/rclone/rclone.conf')
    if not os.path.exists(rcf):print("x no rclone.conf");return
    rem=_configured_remotes();cur=rem[0] if rem else None
    if not a:
        print(f"backup target → {cur or '(none)'}\n")
        for x in rem:
            r=_rc(['about',f'{x}:','--json'],capture_output=True,text=True,timeout=10)
            tag=' ←' if x==cur else ''
            if r and r.returncode==0:
                d=json.loads(r.stdout);u,t=d.get('used',0)/2**30,d.get('total',0)/2**30
                print(f"  {x:15s} {u:6.1f}G /{t:5.0f}G{tag}")
            else:print(f"  {x:15s} (no quota){tag}")
        return
    new=_res(a[0])
    if not new:print(f"x unknown: {a[0]}");return
    txt=open(rcf).read();secs=re.findall(r'(\[[^\]]+\][^\[]*)',txt,re.S)
    pre=f'[{new}]';open(rcf,'w').write(''.join([s for s in secs if s.startswith(pre)]+[s for s in secs if not s.startswith(pre)]))
    print(f"✓ backup → {new}")
def cmd_migrate(a):
    if len(a)<2:print("a gdrive migrate <src> <dst> [path] [+rclone flags]  (read-only src)");return
    s,d=_res(a[0]),_res(a[1])
    if not(s and d):print("x unknown acct");return
    p,fl='adata/backup',[]
    for x in a[2:]:fl.append(x) if x.startswith('-') else (p:=x)
    print(f"→ {s}:{p} → {d}:{p}")
    print(_ok(_rc(['copy',f'{s}:{p}',f'{d}:{p}','-P','--transfers=64','--checkers=128','--fast-list','--drive-pacer-min-sleep=10ms']+fl)))
def _pull_auth():
    rem=_configured_remotes();rem or(print("Login first"),exit(1))
    for f,d in[('hosts.yml','~/.config/gh'),('rclone.conf','~/.config/rclone')]:
        os.makedirs(os.path.expanduser(d),exist_ok=True);sp.run(['rclone','copy',f'{rem[0]}:{RCLONE_BACKUP_PATH}/backup/auth/{f}',os.path.expanduser(d),'-q'])
    open(f"{DATA_DIR}/.auth_shared","w").close();os.path.exists(f"{DATA_DIR}/.auth_local")and os.remove(f"{DATA_DIR}/.auth_local");print("✓ Auth synced")
_C={'cp':cmd_cp,'mv':cmd_mv,'get':cmd_get,'ls':cmd_ls,'tree':cmd_tree,'find':cmd_find,'size':cmd_size,'rm':cmd_rm,'link':cmd_link,'migrate':cmd_migrate,'backup':cmd_backup}
def run():
    a=sys.argv[1:]
    if a and a[0]in('gdrive','dummy'):a=a[1:]
    c,r=(a[0],a[1:])if a else(None,[])
    if c in _C:_C[c](r)
    elif c=='login':cloud_login(custom=(r[0]if r else'')=='custom')
    elif c=='logout':cloud_logout()
    elif c=='sync':cloud_sync(wait=True);ok,msg=cloud_sync_tar(str(SYNC_ROOT),'git');print(f"✓ Synced ({msg})");ok and alog("gdrive sync")
    elif c=='init':_pull_auth()
    else:
        print("a gdrive — Google Drive\n")
        for x in _accts():
            r=_rc(['about',f'{x["r"]}:','--json'],capture_output=True,text=True)
            if r and r.returncode==0:d=json.loads(r.stdout);print(f"  {x['n']:20s} {d.get('used',0)/2**30:.1f}G / {d.get('total',0)/2**30:.0f}G  {x['d']}")
            else:print(f"  {x['n']:20s} (error)")
        print("  cp <local> <acct:dest>    Copy to gdrive\n  mv <local> <acct:dest>    Move to gdrive\n  get <acct:path> <local>   Download\n  ls <acct:path>            List\n  tree <acct:path>          Recursive list\n  find <acct:path> <glob>   Search\n  size <acct:path>          Size\n  rm <acct:path>            Delete\n  link <acct:path>          Share link\n  migrate <src> <dst> [p]   Copy across accts (read-only src)\n  backup [acct]             Show/flip primary backup target\n  sync                      Backup all\n  login|logout|init         Account mgmt\n  Paths: acct:path  Fuzzy: 'jared' → 'jaredtwo2two'")
run()
