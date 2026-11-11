import base64,json,sys,os
from pathlib import Path
if len(sys.argv)<2:sys.exit('/webrtc')
H=Path.home()
p=H/{'github':'.config/gh/hosts.yml','gdrive':'.config/rclone/rclone.conf','vault':'a/adata/vault/enc'}[sys.argv[1]]
if len(sys.argv)==2:
 print(json.dumps({str(f.relative_to(H)):base64.b64encode(f.read_bytes()).decode() for f in ([p] if p.is_file() else p.rglob('*.enc'))}))
else:
 os.umask(0o077)
 for k,v in json.load(sys.stdin).items():
  f=(H/k).resolve()
  assert f==p or sys.argv[1]=='vault' and f.is_relative_to(p) and f.suffix=='.enc'
  b=base64.b64decode(v,validate=True)
  f.parent.mkdir(parents=True,exist_ok=True)
  t=f.with_suffix('.new');t.write_bytes(b)
  if f.exists() and f.read_bytes()!=b:f.replace(f.with_suffix('.before-adopt'))
  t.replace(f)
 print('Saved '+sys.argv[1])
