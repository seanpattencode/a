"""a man <topic> [term] — jump into any manual"""
import re,sys,subprocess as S
t,*a=sys.argv[2:]or sys.exit(__doc__)
q=' '.join(a)or sys.exit(S.call(['man',t]))
s=S.run(['man',t],stdout=-1,stderr=-3,text=1).stdout or sys.exit(f'x no man {t}')
M=[(len(x[1]),x.start())for x in re.finditer(rf'(?m)^( *){re.escape(q)}',s)]or sys.exit(f'x {q} not in man {t}')
d,i=min(M);e=re.compile(rf'(?m)^ {{0,{d}}}\S').search(s,i+1)
print(s[i:e.start()if e else len(s)][:3000])
