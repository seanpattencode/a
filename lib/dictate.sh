# a dictate — live streaming dictation. Tap F5: words appear at the cursor AS you speak. A warm faster-whisper GPU
# server captures mic PCM, re-transcribes the growing buffer ~1x/sec and types only the new text, correcting on
# revision via a prefix-diff. Tap F5 again: final flush + stop. a dictate bind: binds F5 (sway), synced. No sound.
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
R="/tmp/a_dictate.ctl"
typed=""
def yd(*a): subprocess.run(["ydotool",*a])
def emit(new):
    global typed; i=0
    while i<len(typed) and i<len(new) and typed[i]==new[i]: i+=1
    if len(typed)>i: yd("key","-d","0",*(["14:1","14:0"]*(len(typed)-i)))
    if new[i:]: yd("type","-d","0","-H","0",new[i:])
    typed=new
def tx(buf):
    a=np.frombuffer(buf,np.int16).astype(np.float32)/32768.0
    s,_=M.transcribe(a,language="en")
    return " ".join(x.text.strip() for x in s).strip()
stop=threading.Event()
def worker(marker):
    pre=marker+" "
    p=subprocess.Popen(["parec","--rate=16000","--channels=1","--format=s16le","--latency-msec=30"],stdout=subprocess.PIPE)
    buf=b""; last=time.time(); prev=[]; committed=[]
    while not stop.is_set():
        c=p.stdout.read(4000)
        if c: buf+=c
        if time.time()-last>0.5 and len(buf)>8000:
            cur=tx(buf).split(); n=0
            while n<len(prev) and n<len(cur) and prev[n]==cur[n]: n+=1
            if n>len(committed): committed=cur[:n]
            prev=cur; emit(pre+" ".join(committed+cur[len(committed):])); last=time.time()
    p.terminate()
    emit(tx(buf) if len(buf)>8000 else "")
while 1:
    try: cmd=open(R).readline().strip()
    except OSError: time.sleep(.2); continue
    if not cmd: continue
    if cmd.startswith("start"):
        stop.clear(); typed=""; mk=cmd[6:] or "[listening]"; emit(mk)
        threading.Thread(target=worker,args=(mk,),daemon=True).start()
    elif cmd=="stop": stop.set()
PY
cmp -s $srv.tmp $srv 2>/dev/null && rm -f $srv.tmp || { mv $srv.tmp $srv; pkill -f a_dictate_srv.py 2>/dev/null; sleep .4; }
[ -p $R ] || { rm -f $R; mkfifo $R; }
pgrep -f a_dictate_srv.py >/dev/null 2>&1 || setsid python3 $srv >$S.log 2>&1 </dev/null
m=$(pactl list sources | awk -v s="$(pactl get-default-source)" '$1=="Name:"&&$2==s{f=1} f&&/Description:/{sub(/[^:]*: /,"");print;exit}')
if [ -f $S.on ]; then rm -f $S.on; echo stop > $R
else : > $S.on; echo "start [${m:-mic} - F5 to turn off]" > $R; fi
