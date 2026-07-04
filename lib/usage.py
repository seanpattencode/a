# a usage — Claude Max limits, all accounts (~/.claude + ~/.claude-*/); undocumented oauth endpoint /usage uses; auto-refreshes expired tokens
import json,os,glob,time,urllib.request,datetime as dt
def J(u,d=None,h={}):
    return json.load(urllib.request.urlopen(urllib.request.Request(u,json.dumps(d).encode() if d else None,{"Content-Type":"application/json",**h})))
now=dt.datetime.now(dt.timezone.utc)
for f in [os.path.expanduser("~/.claude/.credentials.json")]+sorted(glob.glob(os.path.expanduser("~/.claude-*/.credentials.json"))):
    n=f.split("/")[-2].replace(".claude","").lstrip("-") or "main"
    try:
        c=json.load(open(f));o=c["claudeAiOauth"]
        if o["expiresAt"]/1000<time.time()+60:
            t=J("https://api.anthropic.com/v1/oauth/token",{"grant_type":"refresh_token","refresh_token":o["refreshToken"],"client_id":"9d1c250a-e61b-44d9-88ed-5944d1962f5e"})
            o.update(accessToken=t["access_token"],refreshToken=t.get("refresh_token",o["refreshToken"]),expiresAt=int((time.time()+t["expires_in"])*1000))
            open(f+".tmp","w").write(json.dumps(c));os.rename(f+".tmp",f)
        u=J("https://api.anthropic.com/api/oauth/usage",h={"Authorization":"Bearer "+o["accessToken"]})
        for l in u["limits"]:
            r=dt.datetime.fromisoformat(l["resets_at"])
            m=((l.get("scope") or {}).get("model") or {}).get("display_name") or "all"
            print(f'{n:10}{l["kind"]:14}{m:8}{l["percent"]:3}%  resets {r.astimezone():%a %H:%M} ({(r-now).total_seconds()/3600:.0f}h)')
    except Exception as e:print(f"{n:10}x {e}")
