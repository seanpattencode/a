"""a verify — runs each `a done` entry's <test> in its dir; pass/fail table. arg=alt done.log."""
import sys,os,re,time,subprocess as S
L=sys.argv[2] if sys.argv[2:] else os.path.expanduser('~/a/adata/local/done.log')
t=time.perf_counter_ns()
try:R=[l.rstrip().split('\t') for l in open(L) if l.strip()]
except FileNotFoundError:sys.exit('no done queue: '+L)
def rc(c,d):
 try:return S.run(c,shell=1,cwd=d or'.',timeout=20,stdout=-3,stderr=-3).returncode
 except:return 124
for r in reversed(R):
 ts,w,nm,d,m=(r+['']*5)[:5];a=max(0,int(time.time())-int(ts or 0))
 a=f'{a//86400}d'if a>=86400 else f'{a//3600}h'if a>=3600 else f'{a//60}m'if a>=60 else f'{a}s'
 x=re.search(r'<test>(.*?)</test>',m);c=re.sub(r'\[<\w+>.*?</\w+>\]','',m).strip()[:52]
 v='\033[2m- none\033[0m'if not x else('\033[32m✓ PASS\033[0m'if rc(x[1],d)==0 else'\033[31m✗ FAIL\033[0m')
 print(f'{v} {a:>4} {(w+":"+nm)[:16]:16} {c}')
print(f'{len(R)} entries · {(time.perf_counter_ns()-t)/1e6:.1f}ms')
