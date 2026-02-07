#!/usr/bin/env python3
"""Base module for agents — shared: save(), send(), ask_claude(), ask_gemini()"""
import subprocess,smtplib,sqlite3,os,shutil,socket
from datetime import datetime
from pathlib import Path
from email.message import EmailMessage

P=os.path.dirname(os.path.abspath(__file__))
DB=os.path.join(P,"email.db")
AIO=os.path.join(os.path.dirname(P),"lib","a.py")
GOALS=os.path.join(P,"goals.md")
FILTERS=os.path.join(P,"great_filters.md")

# Device ID (matches _common.py logic)
_dev_file=os.path.expanduser("~/.local/share/a/.device")
DEVICE_ID=open(_dev_file).read().strip() if os.path.exists(_dev_file) else socket.gethostname()[:8]

# Conversation save dir
AGENTS_DIR=Path(P).parent.parent/'adata'/'git'/'agents'

def save(name, output):
    """Save agent conversation to git-synced agents dir"""
    AGENTS_DIR.mkdir(parents=True, exist_ok=True)
    now=datetime.now()
    ts=now.strftime('%Y%m%dT%H%M%S')
    fn=f'{name}_{ts}_{DEVICE_ID}.txt'
    header=f'Agent: {name}\nDate: {now:%Y-%m-%d %H:%M}\nDevice: {DEVICE_ID}\n---\n'
    (AGENTS_DIR/fn).write_text(header+output+'\n')

def get_creds():
    db=sqlite3.connect(DB);db.execute("CREATE TABLE IF NOT EXISTS c(f,t,p)")
    r=db.execute("SELECT*FROM c").fetchone()
    if not r:
        f,t,p=input("from: "),input("to: "),input("pass: ")
        db.execute("INSERT INTO c VALUES(?,?,?)",(f,t,p));db.commit();r=(f,t,p)
    return r

def send(subj,body):
    f,t,p=get_creds()
    msg=EmailMessage();msg["From"],msg["To"],msg["Subject"]=f,t,subj;msg.set_content(body)
    s=smtplib.SMTP_SSL("smtp.gmail.com",465);s.login(f,p);s.sendmail(f,t,msg.as_string());s.quit()
    print(f"Sent '{subj}' to {t}")

def ask_gemini(prompt,timeout=120):
    gemini=shutil.which('gemini')or os.path.expanduser('~/.local/bin/gemini')
    r=subprocess.run([gemini,'-p',prompt],capture_output=True,text=True,timeout=timeout)
    return r.stdout.strip()

def ask_claude(prompt,tools="Read,Glob,Grep",timeout=120):
    try:
        claude=shutil.which('claude')or os.path.expanduser('~/.local/bin/claude')
        r=subprocess.run([claude,'-p','--allowedTools',tools],input=prompt,capture_output=True,text=True,timeout=timeout,cwd=os.path.dirname(AIO))
        return r.stdout.strip() if r.returncode==0 else f"failed: {r.stderr}"
    except Exception as e:
        return f"failed: {e}"
