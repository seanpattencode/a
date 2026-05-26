#!/usr/bin/env bash
# a say [-v r|s] [-p semitones] text…   Default: Ryan, -0.5  (r=ryan, s=sonia)
set -eu
V=en-GB-RyanNeural P=-0.5
while getopts v:p: o; do case $o in
    v) [ "$OPTARG" = s ] && V=en-GB-SoniaNeural ;;
    p) P=$OPTARG ;;
esac; done
shift $((OPTIND-1))
[ $# -eq 0 ] && { cat <<EOF
a say [-v r|s] [-p semitones] text…
  -v   r=ryan (default)  s=sonia      British neural voices via edge-tts
  -p   semitones, negative=lower      default -0.5
e.g.  a say hello              # ryan, -0.5
      a say -v s -p -1 hello   # sonia, lower
EOF
exit 0; }
T="$*"; D=$(mktemp -d); trap "rm -rf $D" EXIT
T0=$(date +%s)
log(){ printf '\033[%sm[+%ss] %s\033[0m\n' "$1" "$(($(date +%s)-T0))" "$2" >&2; }
gen(){ local f=$D/c_$1.mp3 r SR
    uvx --quiet --from edge-tts edge-tts --voice "$V" --text "$2" --write-media "$f.raw" 2>/dev/null || return 1
    if [ "$P" != 0 ]; then
        r=$(python3 -c "print(2**($P/12))")
        SR=$(ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate -of csv=p=0 "$f.raw")
        ffmpeg -y -loglevel error -i "$f.raw" -af "asetrate=$SR*$r,aresample=$SR,atempo=1/$r" "$f"
    else mv "$f.raw" "$f"; fi
}
# split into numbered chunk files; returns count
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
log 2 "split into $N pieces"
[ "$N" -eq 0 ] && exit 0
# pipeline: gen N+1 in bg while playing N → no gap between chunks
gen 1 "$(cat $D/t_1)"
i=2
while [ $i -le $N ]; do
    gen $i "$(cat $D/t_$i)" &
    log 32 "▶ $((i-1))/$N $(head -c 70 $D/t_$((i-1)))"
    ffplay -nodisp -autoexit -loglevel error $D/c_$((i-1)).mp3 2>/dev/null
    wait
    i=$((i+1))
done
log 32 "▶ $N/$N $(head -c 70 $D/t_$N)"
ffplay -nodisp -autoexit -loglevel error $D/c_$N.mp3 2>/dev/null
log 2 "done"
