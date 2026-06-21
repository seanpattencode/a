# a dictate — live streaming dictation. Tap F5: words appear at the cursor AS you speak (warm faster-whisper GPU
# server, --latency-msec=30 mic, settles recognized words, only the tail moves). Tap F5 again: stop.
# Voice commands (say them): "enter" -> newline, "stop" -> stop dictation. a dictate bind: binds F5 (sway), synced.
S=/tmp/a_dictate R=$S.ctl srv=/tmp/a_dictate_srv.py
if [ "$1" = bind ]; then c=$HOME/.config/sway/config
  grep -q 'F5 exec a dictate' "$c" || echo 'bindsym F5 exec a dictate' >> "$c"
  swaymsg bindsym F5 exec a dictate && echo 'F5 -> a dictate (tap to start/stop)'; exit; fi
cat > $srv.tmp <<'PY'
import os,subprocess,threading,time
import numpy as np
from faster_whisper import WhisperModel
try: M=WhisperModel("base.en",device="cuda",compute_type="float16")
except Exception: M=WhisperModel("base.en",device="cpu",compute_type="int8")
R="/tmp/a_dictate.ctl"; ON="/tmp/a_dictate.on"; MK="[dictating... say 'enter'=newline, 'stop'=end, or F5 to turn off]"
typed=""
def yd(*a): subprocess.run(["ydotool",*a])
def emit(new):
    global typed; i=0
    while i<len(typed) and i<len(new) and typed[i]==new[i]: i+=1
    if len(typed)>i: yd("key","-d","4",*(["14:1","14:0"]*(len(typed)-i)))
    if new[i:]: yd("type","-d","4","-H","4",new[i:])
    typed=new
def tx(buf):
    a=np.frombuffer(buf,np.int16).astype(np.float32)/32768.0
    s,_=M.transcribe(a,language="en")
    return " ".join(x.text.strip() for x in s).strip()
def norm(w): return w.strip(".,!?;:").lower()
stop=threading.Event()
def worker():
    global typed
    p=subprocess.Popen(["parec","--rate=16000","--channels=1","--format=s16le","--latency-msec=30"],stdout=subprocess.PIPE)
    buf=b""; last=time.time(); prev=[]; committed=[]; voicestop=False
    while not stop.is_set():
        c=p.stdout.read(4000)
        if c: buf+=c
        if time.time()-last>0.5 and len(buf)>8000:
            cur=tx(buf).split(); n=0
            while n<len(prev) and n<len(cur) and prev[n]==cur[n]: n+=1
            if n>len(committed): committed=cur[:n]
            prev=cur
            ci=next((i for i,w in enumerate(committed) if norm(w) in ("enter","stop")),None)
            if ci is not None:
                emit(" ".join(committed[:ci]))
                if norm(committed[ci])=="stop": voicestop=True; break
                yd("key","-d","4","28:1","28:0"); typed=""
                buf=b""; prev=[]; committed=[]; last=time.time(); continue
            emit(" ".join(committed+cur[len(committed):])); last=time.time()
    p.terminate()
    if not voicestop: emit(tx(buf) if len(buf)>8000 else "")
    try: os.remove(ON)
    except OSError: pass
while 1:
    try: cmd=open(R).readline().strip()
    except OSError: time.sleep(.2); continue
    if not cmd: continue
    if cmd.startswith("start"):
        stop.clear(); typed=""; emit(MK)
        threading.Thread(target=worker,daemon=True).start()
    elif cmd=="stop": stop.set()
PY
cmp -s $srv.tmp $srv 2>/dev/null && rm -f $srv.tmp || { mv $srv.tmp $srv; pkill -f a_dictate_srv.py 2>/dev/null; sleep .4; }
[ -p $R ] || { rm -f $R; mkfifo $R; }
pgrep -f a_dictate_srv.py >/dev/null 2>&1 || setsid python3 $srv >$S.log 2>&1 </dev/null
if [ -f $S.on ]; then rm -f $S.on; echo stop > $R
else : > $S.on; echo start > $R; fi
