# a tokreal [dir]: repo tokens as Claude (claude -p usage minus baseline, chunked) and Codex (tiktoken o200k_base) count them
import json,sys,subprocess as S,tiktoken
d=sys.argv[-1]if sys.argv[2:]else'.';K=dict(capture_output=1,text=1)
F=S.run(['git','-C',d,'grep','-lI',''],**K).stdout.split();t=''.join(open(d+'/'+f,errors='replace').read()for f in F)
def n(x):
 open('/tmp/tr','w').write(x);r=S.run(['claude','-p','--model','haiku','--output-format','json','ok']+['--append-system-prompt-file','/tmp/tr']*(x>''),stdin=S.DEVNULL,**K).stdout
 return sum(v for k,v in json.loads(r)[-1]['usage'].items()if'input'in k)
b=n('');print(f'{d}: {len(F)}f b/4 {len(t.encode())//4} claude {sum(n(t[i:i+300000])-b for i in range(0,len(t),300000))} codex {len(tiktoken.get_encoding("o200k_base").encode(t))}')
