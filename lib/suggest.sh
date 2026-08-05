#!/bin/sh
# suggest.sh [book|-] [lens.txt] — full-corpus suggestion generation → adata/git/suggestions/ + email.
#   book: dir under adata/books with transcriptions/*.txt ('-' = none). Swap weekly = decorrelation lever.
#   lens: prompt file; default common/prompts/suggest.txt, auto-seeded below on first run — edit it freely.
# Scheduled by hub job 'suggest' (default 21:30 daily — edit adata/git/hub/suggest.txt, then `a hub sync`).
# Review surface: `a flow` page SUGGESTIONS — [m] promotes by TASK:/PROMPT:/NOTE: tag, [e] dismisses.
# Recipe proven 2026-06-12: better context + better model + better question (note 20260611T231610).
# claude -p gates on ~chars/2.86 vs the 1M window → corpus budget ~2.6MB. Cuts that buy book room:
# exact-duplicate notes collapsed; me.txt newest 100KB. Direct API = upgrade path for more corpus.
# POSIX sh ONLY: `a suggest` runs lib/*.sh under sh (dash) — no ${v:o:l}, declare -A, $RANDOM, echo -e.
set -e
SR=$HOME/a/adata/git; BOOK="${1:--}"; LENS="${2:-$SR/common/prompts/suggest.txt}"
OUT=$(mktemp /tmp/suggest_corpus.XXXXXX); RES=$(mktemp /tmp/suggest_result.XXXXXX)
sec(){ echo; echo "=== $* ==="; }
docs(){ t=$1; shift; for f in "$@"; do sec "$t DOC: $(basename "$f")"; cat "$f"; done; }
[ -f "$LENS" ] || { mkdir -p "$(dirname "$LENS")"; cat > "$LENS" <<'DEF'
You are an idea-generator for Sean, an operator who directs AI agents (he directs+verifies, AI implements). Below: project documentation for his three systems (/a = agent-command system, /i = infra/health product, /u = quant/trading research), all his pending tasks, all his pending voice notes (raw speech-to-text, typos expected) in chronological order, possibly a full book to draw ideas, mechanisms and framings from, and a list of previous suggestions you must not repeat.
Your job: generate EXACTLY 11 suggestions of the highest possible quality — things Sean has not already written down, found by connecting distant parts of this corpus. Mix of concrete TASKs and PROMPTs (a prompt = full text he can hand a coding agent). At most 2 NOTEs (insights/risks) if genuinely important.
Quality bar: each must be (a) grounded in specific corpus items (and book sections where book-inspired), (b) non-obvious synthesis, not paraphrase, (c) concrete enough to act on this week, (d) not a restatement of any previous suggestion.
Output format - STRICT: exactly 11 lines. Each line begins with the literal characters TASK: or PROMPT: or NOTE: (tag = very first characters, no numbering, no parentheses, no markdown). End each line with [grounded in: ...] naming its sources. No preamble, no other text.
DEF
echo "seeded default lens: $LENS"; }
cat "$LENS" > "$OUT"
{ docs /a $SR/mem/index.txt $SR/mem/*.md $SR/mem/i.txt; docs /i $HOME/i/*.md; docs /u $HOME/i/u/*.md; } >> "$OUT"
if [ "$BOOK" != "-" ] && ls $HOME/a/adata/books/"$BOOK"/transcriptions/*.txt >/dev/null 2>&1; then
    sec "BOOK: $BOOK (full transcription)" >> "$OUT"
    for f in $(ls $HOME/a/adata/books/"$BOOK"/transcriptions/*.txt | sort); do cat "$f"; echo; done >> "$OUT"
fi
sec "ALL PENDING TASKS (lower P = higher priority)" >> "$OUT"
for d in $SR/tasks/*/; do n=$(basename "$d"); [ "$n" = ".archive" ] && continue
    printf "P%.5s " "$n"; find "$d" -name '*.txt' -type f -exec cat {} + 2>/dev/null | tr '\n' ' '; echo; done >> "$OUT"
sec "PREVIOUS SUGGESTIONS — DO NOT REPEAT OR RESTATE ANY OF THESE" >> "$OUT"
ls -t $SR/suggestions/*.txt $SR/suggestions/.archive/*.txt 2>/dev/null | head -200 | while IFS= read -r f; do head -1 "$f" | sed 's/^Text: //'; done >> "$OUT"
sec "ALL PENDING NOTES (deduplicated, oldest first, newest last)" >> "$OUT"
ls $SR/notes/*.txt | awk -F_ '{print $NF" "$0}' | sort | cut -d' ' -f2- | xargs md5sum | awk '!s[$1]++{print $2}' |
    while IFS= read -r f; do u=$(basename "$f"); printf "[%.13s] " "${u#?????????}"; tr '\n' ' ' < "$f"; echo; done >> "$OUT"
# me.txt is the ELASTIC cut: gets whatever budget remains under the gate (max 100KB), shrinking
# automatically as notes/suggestions grow. Last position = newest typed messages nearest the instruction.
GATE=2700000   # measured: 2,647,201 ran (963k real tokens), 2,747,201 bounced — true CLI limit ~2.72MB
ME=$((GATE - $(wc -c < "$OUT") - 1000)); [ "$ME" -gt 100000 ] && ME=100000
if [ "$ME" -gt 5000 ] && [ -f $HOME/i/idata/me.txt ]; then sec "/u DOC: me.txt (Sean's typed messages - NEWEST $ME BYTES)" >> "$OUT"; tail -c "$ME" $HOME/i/idata/me.txt >> "$OUT"
else echo "! me.txt skipped (missing, or corpus grew past its room) - run continues without it"; fi
sec "END OF CORPUS" >> "$OUT"
echo "Now produce the 11 suggestions exactly as specified at the top: 11 lines, literal TASK: / PROMPT: / NOTE: prefixes, each ending [grounded in: ...]. Do not repeat previous suggestions." >> "$OUT"
SZ=$(wc -c < "$OUT"); echo "corpus: $SZ bytes (gate $GATE)"
[ "$SZ" -gt "$GATE" ] && { echo "x corpus over the CLI gate even after me.txt cut - trim docs or book"; exit 1; }
env -u CLAUDECODE -u CLAUDE_CODE_ENTRYPOINT claude -p --dangerously-skip-permissions --model claude-fable-5 --effort max --strict-mcp-config \
    --system-prompt "You are a single-shot idea generator. Follow the instructions in the user message exactly. Do not use any tools. Output only the requested lines." \
    < "$OUT" > "$RES"
mkdir -p "$SR/suggestions"; n=0
while IFS= read -r ln; do case "$ln" in TASK:*|PROMPT:*|NOTE:*)
    ts=$(date +%Y%m%dT%H%M%S); printf 'Text: %s\nStatus: pending\nDevice: %s\nCreated: %s\n' "$ln" "$(hostname)" "$ts" \
        > "$SR/suggestions/$(od -An -N4 -tx4 /dev/urandom|tr -d ' ')_${ts}.$(date +%N).txt"
    n=$((n+1)); sleep 1;; esac; done < "$RES"   # sleep 1: filename timestamps are the sort key - unique seconds keep render order
echo "✓ $n suggestions saved -> a flow"
BODY=$(mktemp /tmp/suggest_mail.XXXXXX)
{ echo "$n new suggestions (book: $BOOK). Review + act on any device:"
  echo "  a flow      -> j to the SUGGESTIONS page (4/5)"
  echo "  [m] promotes one into your real queues, routed by its TASK:/PROMPT:/NOTE: tag"
  echo "  [e] dismisses, [o] edits  -- nothing enters tasks/notes/prompts without your keypress"
  echo; i=0; grep -E '^(TASK|PROMPT|NOTE):' "$RES" | while IFS= read -r ln; do i=$((i+1)); echo "$i. $ln"; echo; done
} > "$BODY"
# Send via `a email` (base.py) — ONE sender path, honors the active/roster account (a email use …).
# Recipient is whatever `a email` is set to (default: jaredtwo2two self-archive, readable via
# `a email read sent`). No duplicate SMTP here.
"$(command -v a || echo "$HOME/.local/bin/a")" email "a suggestions — $n new · $(date +%F)" < "$BODY" || echo "x email failed (suggestions still saved -> a flow)"
rm -f "$OUT" "$RES" "$BODY"
