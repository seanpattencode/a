# a usage — Claude Max limits across accounts, via the undocumented oauth endpoint /usage uses.
# Claude Code holds ONE active login at a time: /login rewrites ~/.claude/.credentials.json in place,
# so accounts can't be listed from the CLI alone. This tool snapshots whichever account is active into
# ~/.claude-<email-slug>/ on every run — keep swapping logins as usual and all accounts accumulate here,
# kept alive by refresh (api.anthropic.com/v1/oauth/token; rotates the refresh token, new pair MUST be
# written back). A snapshot can go stale if the CLI rotated after our last capture (x row); it heals the
# next time that account is active during a run. CLAUDE_CONFIG_DIR=~/.claude-<name> dirs also picked up.
import json,os,re,glob,time,urllib.request,datetime as dt
def J(u,d=None,h={}):
    return json.load(urllib.request.urlopen(urllib.request.Request(u,json.dumps(d).encode() if d else None,{"Content-Type":"application/json",**h})))
def em_of(d):
    try:return json.load(open(d+"/.claude.json"))["oauthAccount"]["emailAddress"]
    except Exception:return ""
now=dt.datetime.now(dt.timezone.utc);H=os.path.expanduser("~")
mf=H+"/.claude/.credentials.json";me=em_of(H)
rows=[(f"{me or 'main'} (active)",mf)]
for f in sorted(glob.glob(H+"/.claude-*/.credentials.json")):
    d=os.path.dirname(f);e=em_of(d)
    if e!=me:rows.append((e or d.split("/")[-1],f))
for n,f in rows:
    print(n)
    try:
        c=json.load(open(f));o=c["claudeAiOauth"]
        if o["expiresAt"]/1000<time.time()+60:
            t=J("https://api.anthropic.com/v1/oauth/token",{"grant_type":"refresh_token","refresh_token":o["refreshToken"],"client_id":"9d1c250a-e61b-44d9-88ed-5944d1962f5e"})
            o.update(accessToken=t["access_token"],refreshToken=t.get("refresh_token",o["refreshToken"]),expiresAt=int((time.time()+t["expires_in"])*1000))
            tm=f+".tmp";open(tm,"w").write(json.dumps(c));os.chmod(tm,0o600);os.rename(tm,f)
        u=J("https://api.anthropic.com/api/oauth/usage",h={"Authorization":"Bearer "+o["accessToken"]})
        for l in u["limits"]:
            m=((l.get("scope") or {}).get("model") or {}).get("display_name") or "all";t=""
            if l["resets_at"]:r=dt.datetime.fromisoformat(l["resets_at"]);t=f'  resets {r.astimezone():%a %H:%M} ({(r-now).total_seconds()/3600:.0f}h)'
            print(f'  {l["kind"]:14}{m:8}{l["percent"]:3}%{t}')
    except Exception as e:print(f"  x {e}"+("  — dead refresh token; heal by running claude logged into THIS account once" if "400" in str(e) else ""))
# Snapshot the ACTIVE account LAST. The loop above refreshes it IN PLACE and the endpoint rotates the
# refresh token, so a copy taken before it holds a token that is already dead — the snapshot then 400s
# forever once that account stops being active. That is self-inflicted, not "the CLI rotated behind us".
if me and os.path.exists(mf):
    sd=H+"/.claude-"+re.sub(r"\W","_",me);os.makedirs(sd,exist_ok=True)
    sc=sd+"/.credentials.json";open(sc,"w").write(open(mf).read());os.chmod(sc,0o600)
    json.dump({"oauthAccount":{"emailAddress":me}},open(sd+"/.claude.json","w"))
if len(rows)<2:print("(one account so far — swap logins as usual; every account a usage sees gets kept + auto-refreshed)")
