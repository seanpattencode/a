# a usage — Claude Max plan limits via the undocumented oauth endpoint /usage uses (no official API; gh issues #44328 #32796)
import json,os,urllib.request,datetime as dt
try:
    tok=json.load(open(os.path.expanduser("~/.claude/.credentials.json")))["claudeAiOauth"]["accessToken"]
    u=json.load(urllib.request.urlopen(urllib.request.Request("https://api.anthropic.com/api/oauth/usage",headers={"Authorization":"Bearer "+tok})))
except Exception as e:exit(f"x {e} — stale/missing token? any claude run refreshes ~/.claude/.credentials.json")
now=dt.datetime.now(dt.timezone.utc)
for l in u["limits"]:
    r=dt.datetime.fromisoformat(l["resets_at"])
    m=((l.get("scope") or {}).get("model") or {}).get("display_name") or "all"
    print(f'{l["kind"]:14}{m:8}{l["percent"]:3}%  resets {r.astimezone():%a %H:%M} ({(r-now).total_seconds()/3600:.0f}h)')
