"""pre-accept claude+codex per-dir trust prompt (no off-switch, skip-permissions doesn't bypass)"""
import json,os,tempfile
c,h=os.path.expanduser("~/.codex/config.toml"),f'[projects."{os.getcwd()}"]'
if os.path.exists(c) and h not in open(c).read():open(c,"a").write(f'\n{h}\ntrust_level = "trusted"\n')
p=os.path.expanduser("~/.claude.json")
try:d=json.load(open(p)) if os.path.exists(p) else {}
except Exception:raise SystemExit(0)
pr=d.setdefault("projects",{}).setdefault(os.getcwd(),{})
if pr.get("hasTrustDialogAccepted") is True:raise SystemExit(0)
pr["hasTrustDialogAccepted"]=True
fd,tmp=tempfile.mkstemp(dir=os.path.dirname(p) or ".")
with os.fdopen(fd,"w") as f:json.dump(d,f)
os.replace(tmp,p)
