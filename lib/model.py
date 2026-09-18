"""a model [id] — a c model switch (--model on sessions.txt c+claude lines); page :1111/#model"""
import os,re,sys
F=sys.path[0]+'/../adata/git/workspace/sessions.txt';s=open(F).read();a=sys.argv[2:];h='htmlbody'in a;a=a[h:]
if a:s=re.sub(r'(?m)^c(laude)?\|.*',lambda l:re.sub(r' --model \S+','',l[0])+' --model '+a[0],s);open(F+'~','w').write(s);os.replace(F+'~',F)
L=re.search(r'(?m)^c\|.*',s)[0];c=(re.search(r'--model (\S+)',L)or[0,'default'])[1]
print(f'<p>{"✓ "*bool(a)}a c model: {c}</p>'+''.join(f'<div class=pg onclick=mdl(this){" style=background:#fff;color:#000"*(m==c)}>{m}</div>'for m in'claude-fable-5 claude-opus-5[1m] claude-opus-5 claude-sonnet-5 claude-haiku-4-5-20251001'.split())if h else L)
