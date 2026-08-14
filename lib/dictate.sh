# a dictate — live streaming dictation, macOS-style aggressive rewrite (Sean 2026-08-14: "rewrite aggressively, more than macos").
# Two models: streaming fast-conformer types words ~0.5s behind speech; parakeet re-decodes the current phrase ~1s
# cadence + at pauses, and the diff is REWRITTEN in place (backspace+retype) -> caps/punctuation/fixes sweep in behind you.
# Finalized phrases are never touched again; phrase = endpoint pause or 15s cap, so cost stays constant (old full-buffer
# re-decode compounded: 4s/pass at 2min, text trailed 10s). Voice commands ride the streaming partials ("stop" acts ~0.5s).
# Tap F5 or pad DICTATE: start/stop. a dictate bind: binds F5 (sway), synced. Models auto-download to ~/.cache/a_dictate.
# Sessions append to adata/transcripts/"YYYY-MM-DD H-MM AM dictate.txt".
S=/tmp/a_dictate R=$S.ctl srv=/tmp/a_dictate_srv.py
if [ "$1" = bind ]; then c=$HOME/.config/sway/config
  grep -q 'F5 exec a dictate' "$c" || echo 'bindsym F5 exec a dictate' >> "$c"
  swaymsg bindsym F5 exec a dictate && echo 'F5 -> a dictate (tap to start/stop)'; exit; fi
cat > $srv.tmp <<'PY'
import os,subprocess,threading,time
import numpy as np
import sherpa_onnx
CA=os.path.expanduser("~/.cache/a_dictate/")
D=CA+"sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8"
SM=CA+"sherpa-onnx-nemo-streaming-fast-conformer-transducer-en-80ms"
try:  # notifications live on sway's dbus-launch bus, not the systemd /run bus (NoReply there) — graft it whoever launched us
    for pid in subprocess.run(["pgrep","-x","sway"],capture_output=True,text=True).stdout.split():
        d=[x for x in open(f"/proc/{pid}/environ").read().split("\0")if x.startswith("DBUS_SESSION_BUS_ADDRESS=")]
        if d:os.environ["DBUS_SESSION_BUS_ADDRESS"]=d[0].split("=",1)[1];break
except OSError:pass
OFF=sherpa_onnx.OfflineRecognizer.from_transducer(encoder=D+"/encoder.int8.onnx",decoder=D+"/decoder.int8.onnx",joiner=D+"/joiner.int8.onnx",tokens=D+"/tokens.txt",num_threads=6,model_type="nemo_transducer")
ON=sherpa_onnx.OnlineRecognizer.from_transducer(tokens=SM+"/tokens.txt",encoder=SM+"/encoder.onnx",decoder=SM+"/decoder.onnx",joiner=SM+"/joiner.onnx",num_threads=4,enable_endpoint_detection=True,rule1_min_trailing_silence=2.0,rule2_min_trailing_silence=0.7)
R="/tmp/a_dictate.ctl"; ON_F="/tmp/a_dictate.on"; T=os.path.expanduser("~/a/adata/transcripts")
norm=lambda w:w.strip(".,!?;:").lower(); CMD=("enter","stop")
def yd(*a): subprocess.run(["ydotool",*a])
def nfy(m,t): subprocess.Popen(["notify-send","-t",str(t),"-h","string:x-canonical-private-synchronous:adictate",m])  # fire-and-forget: a broken bus must never stall the audio loop
def pk(ph):  # parakeet: punctuated truth for the phrase, command words stripped
    s=OFF.create_stream();s.accept_waveform(16000,np.concatenate(ph));OFF.decode_stream(s)
    return " ".join(w for w in s.result.text.split() if norm(w) not in CMD)
stop=threading.Event()
def worker():
    p=subprocess.Popen(["parec","--rate=16000","--channels=1","--format=s16le","--latency-msec=30"],stdout=subprocess.PIPE)
    nfy("🎤 dictation ON",0)
    st=ON.create_stream();scr="";com="";Rf="";nR=0;ph=[];last=0.0;SESS=[];t0=time.localtime()
    def sync(new):  # make screen match: backspace the differing tail, retype — never crosses into finalized text (com is always a shared prefix)
        nonlocal scr
        if new==scr:return
        i=0
        while i<min(len(scr),len(new))and scr[i]==new[i]:i+=1
        if len(scr)-i:yd("key","-d","1",*["14:1","14:0"]*(len(scr)-i))
        if new[i:]:yd("type","-d","4","-H","4","--",new[i:])
        scr=new
    while not stop.is_set():
        c=p.stdout.read(3200)
        if not c:break
        a=np.frombuffer(c,np.int16).astype(np.float32)/32768.
        ph.append(a);st.accept_waveform(16000,a)
        while ON.is_ready(st):ON.decode_stream(st)
        hypw=ON.get_result(st).split()
        ci=next((k for k,w in enumerate(hypw)if norm(w)in CMD),None)
        cmd=norm(hypw[ci])if ci is not None else None
        if ci is not None:hypw=hypw[:ci]
        tail=hypw[nR:]
        if Rf and tail and norm(tail[0])==norm(Rf.split()[-1]):tail=tail[1:]  # seam dedupe: models segment words differently
        sync((com+Rf+" "if Rf else com)+" ".join(tail))
        ac=sum(len(x)for x in ph)/16000
        if cmd is None and ac-last>max(1.0,ac/8)and ac>1.2:  # adaptive cadence: long unpaused phrases refine less often
            Rf=pk(ph);nR=len(hypw);last=ac
            sync(com+Rf)
        if(ON.is_endpoint(st)and ac>1.0)or ac>15 or cmd:
            fin=pk(ph)if ph else""
            if fin:sync(com+fin);com=com+fin+" ";SESS.append(fin)
            Rf="";nR=0;ph=[];last=0.0;ON.reset(st)
            if cmd=="enter":yd("key","-d","4","28:1","28:0");scr=com=com.rstrip()+"\n";SESS.append("\n")
            if cmd=="stop":break
    p.terminate()
    if ph:
        fin=pk(ph)
        if fin:sync(com+fin);SESS.append(fin)
    txt=" ".join(SESS).replace(" \n ","\n").strip()
    if txt:
        os.makedirs(T,exist_ok=True)
        open(os.path.join(T,time.strftime("%Y-%m-%d %-I-%M %p",t0)+" dictate.txt"),"a").write(txt+"\n")
    nfy("⏹ dictation OFF",1500)
    try: os.remove(ON_F)
    except OSError: pass
while 1:
    try: cmd=open(R).readline().strip()
    except OSError: time.sleep(.2); continue
    if not cmd: continue
    if cmd.startswith("start"):
        stop.clear()
        threading.Thread(target=worker,daemon=True).start()
    elif cmd=="stop": stop.set()
PY
cmp -s $srv.tmp $srv && rm -f $srv.tmp || { mv $srv.tmp $srv; pkill -f a_dictate_srv.py; sleep .4; }
[ -p $R ] || { rm -f $R; mkfifo $R; }
CA=$HOME/.cache/a_dictate
for M in sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8 sherpa-onnx-nemo-streaming-fast-conformer-transducer-en-80ms; do
  [ -d "$CA/$M" ] || { python3 -c "import sherpa_onnx" 2>/dev/null || pip install -q sherpa-onnx; mkdir -p $CA
    ( cd $CA && wget -q https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/$M.tar.bz2 && tar xf $M.tar.bz2 && rm -f $M.tar.bz2 ); }
done
pgrep -f a_dictate_srv.py >/dev/null || setsid -f python3 $srv >$S.log 2>&1 </dev/null
if [ -f $S.on ]; then rm -f $S.on; echo stop > $R
else : > $S.on; echo start > $R; fi
