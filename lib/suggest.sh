#!/bin/bash
# suggest.sh [book|-] [lens.txt] — full-corpus suggestion generation → adata/git/suggestions/ + email.
#   book: dir under adata/books with transcriptions/*.txt ('-' = none). Swap weekly = decorrelation lever.
#   lens: prompt file; default common/prompts/suggest.txt, auto-seeded below on first run — edit it freely.
# Scheduled by hub job 'suggest' (default 21:30 daily — edit adata/git/hub/suggest.txt, then `a hub sync`).
# Review surface: `a flow` page SUGGESTIONS — [m] promotes by TASK:/PROMPT:/NOTE: tag, [e] dismisses.
# Recipe proven 2026-06-12: better context + better model + better question (note 20260611T231610).
# claude -p gates on ~chars/2.86 vs the 1M window → corpus budget ~2.6MB. Cuts that buy book room:
# exact-duplicate notes collapsed; me.txt newest 100KB. Direct API = upgrade path for more corpus.
set -e
SR=$HOME/a/adata/git; BOOK="${1:--}"; LENS="${2:-$SR/common/prompts/suggest.txt}"
OUT=$(mktemp /tmp/suggest_corpus.XXXXXX); RES=$(mktemp /tmp/suggest_result.XXXXXX)
[ -f "$LENS" ] || { mkdir -p "$(dirname "$LENS")"; cat > "$LENS" <<'DEF'
You are an idea-generator for Sean, an operator who directs AI agents (he directs+verifies, AI implements). Below: project documentation for his three systems (/a = agent-command system, /i = infra/health product, /u = quant/trading research), all his pending tasks, all his pending voice notes (raw speech-to-text, typos expected) in chronological order, possibly a full book to draw ideas, mechanisms and framings from, and a list of previous suggestions you must not repeat.
Your job: generate EXACTLY 11 suggestions of the highest possible quality — things Sean has not already written down, found by connecting distant parts of this corpus. Mix of concrete TASKs and PROMPTs (a prompt = full text he can hand a coding agent). At most 2 NOTEs (insights/risks) if genuinely important.
Quality bar: each must be (a) grounded in specific corpus items (and book sections where book-inspired), (b) non-obvious synthesis, not paraphrase, (c) concrete enough to act on this week, (d) not a restatement of any previous suggestion.
Output format - STRICT: exactly 11 lines. Each line begins with the literal characters TASK: or PROMPT: or NOTE: (tag = very first characters, no numbering, no parentheses, no markdown). End each line with [grounded in: ...] naming its sources. No preamble, no other text.
DEF
echo "seeded default lens: $LENS"; }
cat "$LENS" > "$OUT"
for f in $SR/mem/index.txt $SR/mem/*.md $SR/mem/i.txt; do echo -e "\n=== /a DOC: $(basename "$f") ==="; cat "$f"; done >> "$OUT"
for f in $HOME/i/*.md; do echo -e "\n=== /i DOC: $(basename "$f") ==="; cat "$f"; done >> "$OUT"
for f in $HOME/u/*.md; do echo -e "\n=== /u DOC: $(basename "$f") ==="; cat "$f"; done >> "$OUT"
echo -e "\n=== /u DOC: me.txt (Sean's typed messages - NEWEST 100KB ONLY) ===" >> "$OUT"; tail -c 100000 $HOME/u/me.txt >> "$OUT"
if [ "$BOOK" != "-" ] && ls $HOME/a/adata/books/"$BOOK"/transcriptions/*.txt >/dev/null 2>&1; then
    echo -e "\n=== BOOK: $BOOK (full transcription) ===" >> "$OUT"
    for f in $(ls $HOME/a/adata/books/"$BOOK"/transcriptions/*.txt | sort); do cat "$f"; echo; done >> "$OUT"
fi
echo -e "\n=== ALL PENDING TASKS (lower P = higher priority) ===" >> "$OUT"
for d in $SR/tasks/*/; do n=$(basename "$d"); [ "$n" = ".archive" ] && continue
    printf "P%s " "${n:0:5}"; find "$d" -name '*.txt' -type f -exec cat {} + 2>/dev/null | tr '\n' ' '; echo; done >> "$OUT"
echo -e "\n=== PREVIOUS SUGGESTIONS — DO NOT REPEAT OR RESTATE ANY OF THESE ===" >> "$OUT"
for f in $SR/suggestions/*.txt $SR/suggestions/.archive/*.txt; do [ -f "$f" ] && head -1 "$f" | sed 's/^Text: //'; done >> "$OUT" 2>/dev/null
echo -e "\n=== ALL PENDING NOTES (deduplicated, oldest first, newest last) ===" >> "$OUT"
declare -A SEEN
for f in $(ls $SR/notes/*.txt | awk -F_ '{print $NF" "$0}' | sort | cut -d' ' -f2-); do
    h=$(md5sum "$f" | cut -d' ' -f1); [ -n "${SEEN[$h]}" ] && continue; SEEN[$h]=1
    u=$(basename "$f"); printf "[%s] " "${u:9:13}"; tr '\n' ' ' < "$f"; echo; done >> "$OUT"
echo -e "\n=== END OF CORPUS ===\nNow produce the 11 suggestions exactly as specified at the top: 11 lines, literal TASK: / PROMPT: / NOTE: prefixes, each ending [grounded in: ...]. Do not repeat previous suggestions." >> "$OUT"
SZ=$(wc -c < "$OUT"); echo "corpus: $SZ bytes (gate ~$((SZ/2860))k of 1000k)"
[ "$SZ" -gt 2650000 ] && { echo "x corpus over the ~2.65MB CLI gate - cut docs or book"; exit 1; }
env -u CLAUDECODE -u CLAUDE_CODE_ENTRYPOINT claude -p --dangerously-skip-permissions --model claude-fable-5 --effort max --strict-mcp-config \
    --system-prompt "You are a single-shot idea generator. Follow the instructions in the user message exactly. Do not use any tools. Output only the requested lines." \
    < "$OUT" > "$RES"
mkdir -p "$SR/suggestions"; n=0
while IFS= read -r ln; do case "$ln" in TASK:*|PROMPT:*|NOTE:*)
    ts=$(date +%Y%m%dT%H%M%S); printf 'Text: %s\nStatus: pending\nDevice: %s\nCreated: %s\n' "$ln" "$(hostname)" "$ts" \
        > "$SR/suggestions/$(printf '%08x' $((RANDOM*32768+RANDOM)))_${ts}.$(date +%N).txt"
    n=$((n+1)); sleep 1;; esac; done < "$RES"   # sleep 1: filename timestamps are the sort key - unique seconds keep render order
echo "✓ $n suggestions saved -> a flow"
BODY=$(mktemp /tmp/suggest_mail.XXXXXX)
{ echo "$n new suggestions (book: $BOOK). Review + act on any device:"
  echo "  a flow      -> j to the SUGGESTIONS page (4/5)"
  echo "  [m] promotes one into your real queues, routed by its TASK:/PROMPT:/NOTE: tag"
  echo "  [e] dismisses, [o] edits  -- nothing enters tasks/notes/prompts without your keypress"
  echo; i=0; grep -E '^(TASK|PROMPT|NOTE):' "$RES" | while IFS= read -r ln; do i=$((i+1)); echo "$i. $ln"; echo; done
} > "$BODY"
python3 - "$BODY" "$n" <<'PY' || echo "x email failed (suggestions still saved)"
import smtplib,sys,datetime
from pathlib import Path
from email.message import EmailMessage
c={k.strip():v.strip() for k,v in (l.split(':',1) for l in (Path.home()/'a/adata/git/email.txt').read_text().splitlines() if ':' in l)}
m=EmailMessage(); m['From']=c['From']; m['To']=c['To']
m['Subject']=f"a suggestions — {sys.argv[2]} new · {datetime.date.today()}"
m.set_content(Path(sys.argv[1]).read_text())
s=smtplib.SMTP_SSL('smtp.gmail.com',465); s.login(c['From'],c['Pass']); s.sendmail(c['From'],c['To'],m.as_string()); s.quit()
print("✓ emailed",c['To'])
PY
rm -f "$OUT" "$RES" "$BODY"
