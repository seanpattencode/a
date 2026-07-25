#!/usr/bin/env python3
"""a cal g — Google Calendar via service account (headless: no OAuth consent screens, no 7-day token expiry).
  a cal g [days]        agenda, default 7 days, merged from every calendar the SA can see
  a cal g add <text..>  quickAdd natural language ('lunch with Ilir Tue 2pm') into the first writable calendar
  a cal g setup         print the one-time Google Cloud recipe + this SA's email
  a cal g setup <email> subscribe the SA to that calendar (AFTER sharing it with the SA email) + smoke-test
Key: adata/local/gcal-sa.json, chmod 600 — per-device, outside all repos; copy to fleet machines that need it.
No deps: JWT RS256 signed via openssl subprocess. Output shape matches a cal lines (YYYY-MM-DD HH:MM text).
Future (Sean 2026-07-19, explicitly NOT now): mutual-scheduling service on seanpatten.com rides on this same SA.
"""
import base64,datetime,json,os,subprocess,sys,tempfile,time,urllib.error,urllib.parse,urllib.request
SA=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),'adata/local/gcal-sa.json')
def b64(b):return base64.urlsafe_b64encode(b).rstrip(b'=')
def token(j):
    now=int(time.time())
    seg=b64(json.dumps({'alg':'RS256','typ':'JWT'}).encode())+b'.'+b64(json.dumps({
        'iss':j['client_email'],'scope':'https://www.googleapis.com/auth/calendar','aud':j['token_uri'],
        'iat':now-30,'exp':now+3600}).encode())
    with tempfile.NamedTemporaryFile('w',suffix='.pem') as f:
        f.write(j['private_key']);f.flush()
        sig=subprocess.run(['openssl','dgst','-sha256','-sign',f.name],input=seg,capture_output=True,check=True).stdout
    body=urllib.parse.urlencode({'grant_type':'urn:ietf:params:oauth:grant-type:jwt-bearer',
        'assertion':(seg+b'.'+b64(sig)).decode()}).encode()
    return json.load(urllib.request.urlopen(urllib.request.Request(j['token_uri'],body)))['access_token']
def api(tok,path,method='GET',q=None,body=None):
    u='https://www.googleapis.com/calendar/v3/'+path+('?'+urllib.parse.urlencode(q) if q else '')
    r=urllib.request.Request(u,json.dumps(body).encode() if body is not None else None,
        {'Authorization':'Bearer '+tok,'Content-Type':'application/json'},method=method)
    try:return json.load(urllib.request.urlopen(r))
    except urllib.error.HTTPError as e:sys.exit(f'x gcal {e.code} {path}: {e.read().decode()[:300]}')
def main():
    a=sys.argv[1:]
    if not os.path.exists(SA) or a[:1]==['setup'] and len(a)==1:
        sae=json.load(open(SA))['client_email'] if os.path.exists(SA) else '(create the key first — steps 1-4)'
        sys.exit(f'''gcal setup ({"key ok" if os.path.exists(SA) else "NO KEY"} — {SA}):
1. console.cloud.google.com/projectcreate — create/pick a project
2. console.cloud.google.com/apis/library/calendar-json.googleapis.com — Enable
3. console.cloud.google.com/iam-admin/serviceaccounts/create — create SA, no roles needed
4. SA -> Keys -> Add key -> JSON -> save as {SA} (chmod 600)
5. calendar.google.com -> Settings and sharing -> Share with specific people -> add
   {sae} -> "Make changes to events"
6. a cal g setup <your-calendar-email>''')
    tok=token(json.load(open(SA)))
    if a[:1]==['setup']:
        api(tok,'users/me/calendarList','POST',body={'id':a[1]});print(f'+ SA subscribed to {a[1]}');a=[]
    if a[:1]==['add']:
        w=[c for c in api(tok,'users/me/calendarList').get('items',[]) if c.get('accessRole') in('writer','owner')]
        if not w:sys.exit('x no writable calendar — share with "Make changes to events", then a cal g setup <email>')
        e=api(tok,f"calendars/{urllib.parse.quote(w[0]['id'])}/events/quickAdd",'POST',q={'text':' '.join(a[1:])})
        s=e.get('start',{});sys.exit(f"+ {s.get('dateTime',s.get('date','?'))} {e.get('summary','')}\n  {e.get('htmlLink','')}")
    days=int(a[0]) if a and a[0].isdigit() else 7
    t0=datetime.datetime.now().astimezone()
    cl=api(tok,'users/me/calendarList').get('items',[])
    if not cl:sys.exit('x SA sees no calendars — a cal g setup   (prints the recipe)')
    ev=[]
    for c in cl:
        for e in api(tok,f"calendars/{urllib.parse.quote(c['id'])}/events",q={'timeMin':t0.isoformat(),
                'timeMax':(t0+datetime.timedelta(days=days)).isoformat(),'singleEvents':'true',
                'orderBy':'startTime','maxResults':'60'}).get('items',[]):
            s=e.get('start',{});d=s.get('dateTime',s.get('date',''))
            ev.append(f"{d[:10]} {d[11:16] or 'all-day'} {e.get('summary','(untitled)')}")
    print('\n'.join(sorted(ev)) or f'(no events next {days}d)')
main()
