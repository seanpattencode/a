# a dictate — live streaming voice dictation (Sean 2026-08-14 / 2026-10-20).
# Two models: streaming fast-conformer types words ~0.5s behind speech; Parakeet re-decodes phrase at word-gaps (>=3s)
# or endpoint pauses and rewrites in place. Voice commands ("new agent", "next agent", "close agent", "backspace", "clear", "enter", "stop")
# fire immediately on streaming partials. In #mic (nt mode), streams to #mt2 on top AND types/acts in active tmux pane on bottom.
S=/tmp/a_dictate R=$S.ctl srv=/tmp/a_dictate_srv.py
case $(uname) in Darwin) # macOS: Apple Speech.framework = the OS dictation engine (sherpa/pactl/ydotool below are linux-only). Finals type
# into the most-recent tmux client's pane (A_DICT_WIN overrides); TCC dialogs only render in the GUI session (serve lives there) — not over ssh.
H=$HOME/.cache/a_dictate/macdict
case "$1" in
bind) echo 'macOS: open localhost:1111/mic (or: a dictate)'; exit;;
mic) [ "$2" ] || echo 'default	macOS system input — swap in System Settings > Sound	*'; exit;;
status) { pgrep -qf 'dictate.sh _loop' && echo '# listening · macOS' || echo '# off'; } | tee -a $S.log; exit;;
_loop)
mkdir -p ${H%/*}; cat >$H.sw.tmp <<'SW'
// Apple Speech.framework — stdout: ~partial =final #status !error. On-device never self-finals (verified 10-20): the timer endAudio()s a hypothesis quiet >1.2s.
import Speech
import AVFoundation
setbuf(stdout, nil)
let sem = DispatchSemaphore(value: 0)
if AVCaptureDevice.authorizationStatus(for: .audio) == .notDetermined { AVCaptureDevice.requestAccess(for: .audio) { _ in sem.signal() }; sem.wait() }
if SFSpeechRecognizer.authorizationStatus() == .notDetermined { SFSpeechRecognizer.requestAuthorization { _ in sem.signal() }; sem.wait() }
guard AVCaptureDevice.authorizationStatus(for: .audio) == .authorized else { print("! mic denied: System Settings > Privacy & Security > Microphone"); exit(1) }
guard SFSpeechRecognizer.authorizationStatus() == .authorized, let r = SFSpeechRecognizer(), r.isAvailable else { print("! speech denied/unavailable: System Settings > Privacy & Security > Speech Recognition"); exit(1) }
let eng = AVAudioEngine(), inp = eng.inputNode
var req: SFSpeechAudioBufferRecognitionRequest?, task: SFSpeechRecognitionTask?, txt = "", stamp = Date()
func go() {
    let q = SFSpeechAudioBufferRecognitionRequest()
    q.shouldReportPartialResults = true; q.taskHint = .dictation; q.requiresOnDeviceRecognition = r.supportsOnDeviceRecognition
    req = q
    task = r.recognitionTask(with: q) { res, err in
        guard req === q else { return }
        if let res = res { txt = res.bestTranscription.formattedString; stamp = Date(); print((res.isFinal ? "=" : "~") + txt); if res.isFinal { txt = ""; go() } }
        else if err != nil { txt = ""; usleep(250000); go() }
    }
}
inp.installTap(onBus: 0, bufferSize: 2048, format: inp.outputFormat(forBus: 0)) { b, _ in req?.append(b) }
do { try eng.start() } catch { print("! audio engine: \(error.localizedDescription)"); exit(1) }
go()
print("# listening · \(r.supportsOnDeviceRecognition ? "on-device" : "server") dictation · \(Int(inp.outputFormat(forBus: 0).sampleRate))Hz")
Timer.scheduledTimer(withTimeInterval: 0.4, repeats: true) { _ in if !txt.isEmpty, Date().timeIntervalSince(stamp) > 1.2 { req?.endAudio() } }
RunLoop.main.run()
SW
cmp -s $H.sw.tmp $H.swift 2>/dev/null && rm $H.sw.tmp || { mv $H.sw.tmp $H.swift; rm -f $H; }
[ -x $H ] || { echo '# compiling Apple Speech helper (one-time)'
printf '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>com.a.dictate</string><key>NSMicrophoneUsageDescription</key><string>a dictate listens while dictation is on.</string><key>NSSpeechRecognitionUsageDescription</key><string>a dictate transcribes your speech on this Mac.</string></dict></plist>\n' >$H.plist
swiftc -swift-version 5 -O -suppress-warnings -o $H $H.swift -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker $H.plist && codesign -fs - $H 2>/dev/null || { echo '! swiftc failed'; rm -f $S.on; exit 1; }; }
[ -f $S.on ] || exit 0
t(){ [ "$A_DICT_WIN" ] && { echo "$A_DICT_WIN"; return; }; v=$(tmux lsc -F '#{client_activity} #{client_session}' 2>/dev/null|sort -n|tail -1|cut -d' ' -f2); echo "${v:-a}"; }
rm -f $S.sess
"$H" | while IFS= read -r l; do p=${l#?}
case $l in
'~'*) printf '%s\n' "$p";;
'='*) w=$(printf %s "$p"|tr A-Z a-z|tr -cd 'a-z ')
case $w in
stop|*' stop') pkill -f "$H";;
enter) tmux send-keys -t "$(t)" Enter;;
clear) tmux send-keys -t "$(t)" C-u;;
backspace) tmux send-keys -t "$(t)" BSpace;;
'new agent') d=$(tmux display -t "$(t)" -p '#{pane_current_path}');w=$(cd "${d:-$HOME}"&&PATH=$HOME/.local/bin:$PATH a a 2>>$S.log|awk '/^→ win/{print $3}');[ "$w" ]&&[ "$w" != "?" ]&&for c in $(tmux lsc -F '#{client_session}'|grep '^a');do tmux select-window -t "$c:$w";done;;
'next agent'|'switch agent') tmux next-window -t "$(t)";;
'close agent') tmux kill-window -t "$(t)";;
*) [ "$p" ] && { tmux send-keys -t "$(t)" -l -- "$p "; printf '%s\n' "$p" >>$S.sess; };;
esac; printf '%s\n' "$p";;
*) printf '%s\n' "$l";;
esac
done
[ -s $S.sess ] && { mkdir -p "$HOME/a/adata/transcripts"; cat $S.sess >>"$HOME/a/adata/transcripts/$(date '+%Y-%m-%d %I-%M %p') dictate.txt"; rm -f $S.sess; }
rm -f $S.on; echo '# off'; exit;;
esac
:>>$S.log
if [ -f $S.on ] && pgrep -qf 'dictate.sh _loop'; then
if [ "$2" = on ]; then echo '# listening · macOS' >>$S.log; else rm -f $S.on; pkill -f "$H"; fi
elif [ "$2" != off ]; then :>$S.on; nohup sh "$0" _loop </dev/null >>$S.log 2>&1 &
else echo '# off' >>$S.log
fi
exit;;
esac
if [ "$1" = bind ]; then c=$HOME/.config/sway/config
  grep -q 'F5 exec a dictate' "$c" || echo 'bindsym F5 exec a dictate' >> "$c"
  swaymsg bindsym F5 exec a dictate && echo 'F5 -> a dictate (tap to start/stop)'; exit; fi
if [ "$1" = mic ]; then [ "$2" ] || { pactl list sources | awk -v d="$(pactl get-default-source)" '/^\tName: /{n=$2}/^\tDescription: /&&n!~/\.monitor$/{print n"\t"substr($0,15)(n==d?"\t*":"")}'; exit; }
  pactl set-default-source "$2"; pactl list short source-outputs | cut -f1 | xargs -rI% pactl move-source-output % "$2"
  pgrep -f a_dictate_srv.py >/dev/null && echo mic > $R; exit; fi
if [ "$1" = status ]; then pgrep -f a_dictate_srv.py >/dev/null && timeout 2 sh -c "echo mic > '$R'" || echo '# off' >> $S.log; exit; fi
cat > $srv.tmp <<'PY'
import json,os,subprocess,threading,time
T0=time.time()  # before the seconds-long model imports: the startup ON_F cleanup must only eat flags OLDER than this launch — the sh sets the fresh flag ms after spawning us, and a blind rm here raced it on every deploy, stranding mic-on with no flag (10-20)
import numpy as np,sherpa_onnx
CA=os.path.expanduser("~/.cache/a_dictate/")
D=CA+"sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8"
SM=CA+"sherpa-onnx-nemo-streaming-fast-conformer-transducer-en-80ms"
try:
    for pid in subprocess.run(["pgrep","-x","sway"],capture_output=True,text=True).stdout.split():
        d=[x for x in open(f"/proc/{pid}/environ").read().split("\0")if x.startswith("DBUS_SESSION_BUS_ADDRESS=")]
        if d:os.environ["DBUS_SESSION_BUS_ADDRESS"]=d[0].split("=",1)[1];break
except OSError:pass
OFF=sherpa_onnx.OfflineRecognizer.from_transducer(encoder=D+"/encoder.int8.onnx",decoder=D+"/decoder.int8.onnx",joiner=D+"/joiner.int8.onnx",tokens=D+"/tokens.txt",num_threads=6,model_type="nemo_transducer")
ON=sherpa_onnx.OnlineRecognizer.from_transducer(tokens=SM+"/tokens.txt",encoder=SM+"/encoder.onnx",decoder=SM+"/decoder.onnx",joiner=SM+"/joiner.onnx",num_threads=4,enable_endpoint_detection=True,rule1_min_trailing_silence=2.0,rule2_min_trailing_silence=0.7)
R="/tmp/a_dictate.ctl"; ON_F="/tmp/a_dictate.on"; T=os.path.expanduser("~/a/adata/transcripts")
S=subprocess.run;norm=lambda w:w.strip(".,!?;:").lower()
CMDS={("new","agent"):"new",("close","agent"):"close",("next","agent"):"next",("switch","agent"):"next",("clear",):"clear",("backspace",):"bs",("enter",):"enter",("stop",):"stop"}
def fcmd(ws):
    nw=[norm(w)for w in ws]
    return next(((i,c)for i in range(len(nw))for p,c in CMDS.items()if tuple(nw[i:i+len(p)])==p),(len(ws),None))
def yd(*a):S(["ydotool",*a])
def tm(*a):S(["tmux",*a])
def nfy(m,t):subprocess.Popen(["notify-send","-t",str(t),"-h","string:x-canonical-private-synchronous:adictate",m])
J=lambda w:json.loads(S(["pactl","-fjson","list",w],capture_output=True,text=True).stdout)
def mic():i={o["source"]for o in J("source-outputs")if o["properties"].get("media.name")=="parec"};return next((s["description"]for s in J("sources")if s["index"]in i),"no mic")
def tms():
    r=S(["tmux","lsc","-F","#{client_activity} #{client_session}"],capture_output=True,text=True).stdout.splitlines()
    return max(r,key=lambda x:int(x.split()[0])).split()[1]if r else"a"
def tma(c,s):
    q={"next":f'w=$(tmux selectw -t "{s}" -n\\; display -t "{s}" -p "#{{window_id}}")',
       "close":f'o=$(tmux display -t "{s}" -p "#{{window_id}}");w=$(tmux selectw -t "{s}" -n\\; display -t "{s}" -p "#{{window_id}}");[ "$o" != "$w" ]&&tmux killw -t "$o"',
       "new":f'cd "$(tmux display -t "{s}" -p "#{{pane_current_path}}")";w=$(PATH="$HOME/.local/bin:$PATH" a a 2>>/tmp/a_dictate.log|awk "/^→ win/{{print \\$3}}")'}[c]  # parse the spawned window from a a itself: new windows fill INDEX GAPS, so lsw|tail -1 switched everyone to the highest OLD window while the agent sat unseen (10-20)
    S(["sh","-c",q+';[ -n "$w" ]&&[ "$w" != "?" ]&&for c in $(tmux lsc -F "#{client_session}"|grep "^a");do tmux selectw -t "$c:$w";done'])
def pk(ph):
    s=OFF.create_stream();s.accept_waveform(16000,np.concatenate(ph));OFF.decode_stream(s)
    ws=s.result.text.split();return" ".join(ws[:fcmd(ws)[0]]).rstrip(".,!?;:")
stop=threading.Event();W=[None]
def worker(nt=0):
    p=subprocess.Popen(["parec","--rate=16000","--channels=1","--format=s16le","--latency-msec=30"],stdout=subprocess.PIPE)
    st=ON.create_stream();scr="";com="";Rf="";nR=0;nw=0;ph=[];last=lastw=0.0;SESS=[];t0=time.localtime();m=""
    def sync(new):
        nonlocal scr
        if new==scr:return
        i=0
        while i<min(len(scr),len(new))and scr[i]==new[i]:i+=1
        bs=len(scr)-i;add=new[i:];scr=new
        if nt:
            print(new.replace("\n"," ⏎ ")or" ",flush=True);s=tms()
            if bs:tm("send-keys","-t",s,*["BSpace"]*bs)
            if add:tm("send-keys","-t",s,"-l","--",add)
            return
        if bs:yd("key","-d","1",*["14:1","14:0"]*bs)
        if add:yd("type","-d","4","-H","4","--",add)
    while not stop.is_set():
        c=p.stdout.read(3200)
        if not c:break
        if not m:m=mic();nfy("🎤 dictation ON · "+m+" — speak close to the mic",0);nt and print("# listening · "+m,flush=True)
        a=np.frombuffer(c,np.int16).astype(np.float32)/32768.
        ph.append(a);st.accept_waveform(16000,a)
        while ON.is_ready(st):ON.decode_stream(st)
        hypw=ON.get_result(st).split();ci,cmd=fcmd(hypw);hypw=hypw[:ci]
        tail=hypw[nR:]
        if Rf and tail and norm(tail[0])==norm(Rf.split()[-1]):tail=tail[1:]
        sync((com+Rf+" "if Rf else com)+" ".join(tail))
        ac=sum(len(x)for x in ph)/16000
        if len(hypw)>nw:nw=len(hypw);lastw=ac
        if cmd is None and len(hypw)>nR and ac-last>3 and(ac-lastw>.35 or ac-last>5):
            Rf=pk(ph);nR=len(hypw);last=ac
            sync(com+Rf)
        if(ON.is_endpoint(st)and ac>1.0)or ac>15 or cmd:
            fin=(pk(ph)or" ".join(hypw))if ph and cmd!="stop"else""
            if fin:sync(com+fin);com=com+fin+" ";SESS.append(fin)
            Rf="";nR=0;nw=0;ph=[];last=lastw=0.0;ON.reset(st)
            if cmd=="stop":p.stdout.read(6400);break
            if cmd:
                s=tms()
                if cmd=="bs":
                    w=com.split()[:-1];com=" ".join(w)+(" "if w else"")
                    sync(com)if scr else(tm("send-keys","-t",s,"BSpace")if nt else yd("key","-d","1","14:1","14:0"))
                else:
                    if cmd=="clear":sync("")if scr else(nt and tm("send-keys","-t",s,"C-u"))
                    elif cmd=="enter":tm("send-keys","-t",s,"Enter")if nt else yd("key","-d","4","28:1","28:0");SESS.append("\n")
                    else:tma(cmd,s)
                    scr=com=""
                    if nt:print({"enter":"[⏎ sent]","clear":"[cleared]"}.get(cmd,f"[{cmd} agent]"),flush=True)
                p.stdout.read(6400)
    p.terminate()
    if ph:
        fin=pk(ph)
        if fin:sync(com+fin);SESS.append(fin)
    txt=" ".join(SESS).replace(" \n ","\n").strip()
    if txt:
        os.makedirs(T,exist_ok=True)
        open(os.path.join(T,time.strftime("%Y-%m-%d %-I-%M %p",t0)+" dictate.txt"),"a").write(txt+"\n")
    nt and print("# off",flush=True);nfy("⏹ dictation OFF",1500)
    try: os.remove(ON_F)
    except OSError: pass
try:
    if os.path.getmtime(ON_F)<T0-10:os.remove(ON_F)  # -10: the sh writes the fresh flag before python even reaches T0; truly stale flags are minutes old
except OSError: pass
while 1:
    try: cmd=open(R).readline().strip()
    except OSError: time.sleep(.2); continue
    if not cmd: continue
    if cmd.startswith("start"):
        stop.clear()
        W[0]=threading.Thread(target=worker,args=("nt" in cmd,),daemon=True);W[0].start()
    elif cmd=="stop": stop.set()
    elif cmd=="mic": print(("# listening · "+mic())if W[0]and W[0].is_alive()else"# off",flush=True)  # truthful either way: ON_F drifts when a deploy pkills mid-listen; worker liveness never does (10-20 "should always be accurate")
PY
cmp -s $srv.tmp $srv && rm -f $srv.tmp || { mv $srv.tmp $srv; pkill -f a_dictate_srv.py; sleep .4; }
[ -p $R ] || { rm -f $R; mkfifo $R; }
CA=$HOME/.cache/a_dictate
for M in sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8 sherpa-onnx-nemo-streaming-fast-conformer-transducer-en-80ms; do
  [ -d "$CA/$M" ] || { python3 -c "import sherpa_onnx" 2>/dev/null || pip install -q sherpa-onnx; mkdir -p $CA
    ( cd $CA && wget -q https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/$M.tar.bz2 && tar xf $M.tar.bz2 && rm -f $M.tar.bz2 ); }
done
pgrep -f a_dictate_srv.py >/dev/null || setsid -f python3 $srv >$S.log 2>&1 </dev/null
if [ -f $S.on ]; then if [ "$2" = on ]; then echo mic > $R; else rm -f $S.on; echo stop > $R; fi
elif [ "$2" != off ]; then : > $S.on; echo "start $1" > $R; fi
