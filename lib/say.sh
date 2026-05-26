#!/usr/bin/env bash
# a say [-l] [-v voice] [-p semitones] text…
#   online (default): edge-tts en-GB-RyanNeural at -0.5    -v r=ryan, s=sonia, or full
#   local  (-l):      Kokoro   bm_lewis         at -2.5    -v bm_lewis|bm_george|... or am_*/af_*
set -eu
L=0; V=""; P=""
while getopts lv:p: o; do case $o in
    l) L=1 ;; v) V=$OPTARG ;; p) P=$OPTARG ;;
esac; done
shift $((OPTIND-1))
[ $# -eq 0 ] && { cat <<EOF
a say [-l] [-v voice] [-p semitones] text…
  online (default): edge-tts en-GB-RyanNeural at -0.5  (-v r=ryan|s=sonia|<full>)
  local  (-l):      Kokoro   bm_lewis         at -2.5  (-v bm_lewis|bm_george|...)
  -p semitones, negative = lower pitch
e.g.  a say hello                       # ryan online
      a say -l hello                    # bm_lewis local
      a say -l -v bm_george -p -3 hi    # different local voice + pitch
EOF
exit 0; }
if [ $L = 1 ]; then : "${V:=bm_lewis}"; : "${P:=-2.5}"
else case "$V" in r|"") V=en-GB-RyanNeural;; s) V=en-GB-SoniaNeural;; esac; : "${P:=-0.5}"
fi
T="$*"
# Termux: launch the TTS APK (adata/git/my/tts_apk) which calls setVoice() — needed because
# termux-tts-speak only exposes setLanguage(Locale) and can't pick by voice name.
# Defaults below were chosen by Sean using the slider UI in the APK:
#   voice = en-gb-x-gbd-network  (Google's online GBD British male — same voice Readera uses)
#   pitch = 0.48 multiplier (~-12.7 semitones)
# Why 0.48: the GBD voice itself reads slightly brighter than other en-GB Google voices, so
# going below the Pixel system pitch (0.54) is needed to land at the same warmth Sean prefers.
if [ -d /data/data/com.termux ]; then
    [ -n "$P" ] && PM=$(python3 -c "print(2**($P/12))") || PM=0.48
    VN="${V:-en-gb-x-gbd-network}"
    # primary: APK HeadlessActivity → TTSService (lets us setVoice() for exact gbd-network)
    if pm path com.spatten.ttsdumper >/dev/null 2>&1; then
        OUT=$(am start -n com.spatten.ttsdumper/.HeadlessActivity --es text "$T" --es voice "$VN" --ef pitch "$PM" 2>&1)
        # Android 12+ blocks background activity launches when termux is not foregrounded
        # (e.g. SSH'd in remotely). Only fall through to termux-tts-speak if that happened.
        echo "$OUT" | grep -qiE 'blocked|error:' || exit 0
    fi
    # fallback: built-in termux-tts-speak (loses exact-voice control — uses default en-GB)
    command -v termux-tts-speak >/dev/null || pkg install -y termux-api >/dev/null 2>&1
    printf '%s' "$T" | termux-tts-speak -l en -n GB -p "$PM"
    exit $?
fi
D=$(mktemp -d); trap "rm -rf $D" EXIT
T0=$(date +%s); log(){ printf '\033[%sm[+%ss] %s\033[0m\n' "$1" "$(($(date +%s)-T0))" "$2" >&2; }
SHIFT(){ local r=$(python3 -c "print(2**($P/12))") SR=$1; [ "$P" = 0 ] && echo "" || echo "-af asetrate=$SR*$r,aresample=$SR,atempo=1/$r"; }
gen(){ local f=$D/c_$1.wav
    if [ $L = 1 ]; then
        uvx --quiet --from kokoro-onnx python - "$V" "$2" >$D/r_$1 2>/dev/null <<'PY'
import sys,os
from kokoro_onnx import Kokoro
v,t=sys.argv[1],sys.argv[2]
K=os.path.expanduser('~/a/adata/local/kokoro')
k=Kokoro(f'{K}/kokoro-v1.0.onnx',f'{K}/voices-v1.0.bin')
s,sr=k.create(t,voice=v,lang='en-gb' if v.startswith('b') else 'en-us')
sys.stdout.buffer.write((s*32767).astype('int16').tobytes())
PY
        ffmpeg -y -loglevel error -f s16le -ar 24000 -ac 1 -i $D/r_$1 $(SHIFT 24000) "$f"
    else
        uvx --quiet --from edge-tts edge-tts --voice "$V" --text "$2" --write-media $D/r_$1 2>/dev/null || return 1
        local SR=$(ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate -of csv=p=0 $D/r_$1)
        ffmpeg -y -loglevel error -i $D/r_$1 $(SHIFT $SR) "$f"
    fi
}
N=$(printf '%s' "$T" | python3 -c "
import sys, re
D = sys.argv[1]; n = 0
for c in re.split(r'(?<=[.!?])\s+|\n\s*\n+', sys.stdin.read()):
    c = ' '.join(c.split())
    if c:
        n += 1
        open(f'{D}/t_{n}','w').write(c)
print(n)
" "$D")
log 2 "split: $N pieces  mode=$([ $L = 1 ] && echo local || echo edge)  voice=$V  pitch=${P}st"
[ "$N" -eq 0 ] && exit 0
gen 1 "$(cat $D/t_1)"
i=2
while [ $i -le $N ]; do
    gen $i "$(cat $D/t_$i)" &
    log 32 "▶ $((i-1))/$N $(head -c 70 $D/t_$((i-1)))"
    ffplay -nodisp -autoexit -loglevel error $D/c_$((i-1)).wav 2>/dev/null
    wait
    i=$((i+1))
done
log 32 "▶ $N/$N $(head -c 70 $D/t_$N)"
ffplay -nodisp -autoexit -loglevel error $D/c_$N.wav 2>/dev/null
log 2 "done"
