"""a models: {agent:[[ids],[efforts]]}"""
import os,re,json,mmap,shutil
B=shutil.which('claude');b=B and mmap.mmap(os.open(B,0),0,access=mmap.ACCESS_READ)or b'';i=b.find(b'advisor_rank:')
R=sorted((int(x[2]),x[1].decode())for x in re.finditer(rb'id:"(claude-[^"]+)"(?:(?!id:").)*?advisor_rank:(\d+)',b[i-30000:i+30000])if b'bedrock:"'in x[0])[::-1]
O={'claude':[[i for _,i in R]or['claude-fable-5'],'max xhigh high medium low'.split()],'agy':[['gemini-3.8-flash-high','gemini-3.1-pro-high'],['low','medium','high']]}
C=os.getenv('HOME')+'/.codex/models_cache.json'
M=os.path.isfile(C)and[x for x in json.load(open(C))['models']if x['visibility']=='list']
if M:O['codex']=[[x['slug']for x in M],[e['effort']for e in M[0]['supported_reasoning_levels'][::-1]]]
print(json.dumps(O))
