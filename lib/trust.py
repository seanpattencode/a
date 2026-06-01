"""Pre-accept claude's per-directory trust prompt for the cwd, so a-launched claude never asks.
Claude has no global trust off-switch and --dangerously-skip-permissions doesn't bypass it;
trust lives per-dir in ~/.claude.json. We mark the launch dir trusted (atomic, race-safe)."""
import json,os,tempfile
p=os.path.expanduser("~/.claude.json")
try:d=json.load(open(p)) if os.path.exists(p) else {}
except Exception:raise SystemExit(0)
pr=d.setdefault("projects",{}).setdefault(os.getcwd(),{})
if pr.get("hasTrustDialogAccepted") is True:raise SystemExit(0)
pr["hasTrustDialogAccepted"]=True
fd,tmp=tempfile.mkstemp(dir=os.path.dirname(p) or ".")
with os.fdopen(fd,"w") as f:json.dump(d,f)
os.replace(tmp,p)
