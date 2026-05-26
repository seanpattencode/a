#!/usr/bin/env bash
# a say [-v r|s] [-p semitones] text…   Default: Ryan, -0.5  (r=ryan, s=sonia)
set -eu
V=en-GB-RyanNeural P=-0.5
while getopts v:p: o; do case $o in
    v) [ "$OPTARG" = s ] && V=en-GB-SoniaNeural ;;
    p) P=$OPTARG ;;
esac; done
shift $((OPTIND-1))
T="${*:?text required}"
D=$(mktemp -d); trap "rm -rf $D" EXIT
uvx --quiet --from edge-tts edge-tts --voice "$V" --text "$T" --write-media "$D/o.mp3" 2>/dev/null
F=$D/o.mp3
if [ "$P" != 0 ]; then
    r=$(python3 -c "print(2**($P/12))")
    SR=$(ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate -of csv=p=0 "$F")
    ffmpeg -y -loglevel error -i "$F" -af "asetrate=$SR*$r,aresample=$SR,atempo=1/$r" "$D/s.wav"
    F=$D/s.wav
fi
ffplay -nodisp -autoexit -loglevel error "$F"
