/* a feed — fleet agent review TUI (C): cache-seeded paint, live ps/ssh fan-out, type-to-search. */
#include <poll.h>

/* per-box scanner (runs locally; single-quoted = ssh-safe): emits  live|sid|cwd|started|model|tail|desc */
/* EVERY BYTE HERE IS RATIONED: the whole scanner ships as base64 inside `a ssh <cmd>`, whose buffer is
   char cmd[B], B=4096 (a.c, ssh.c). Over that it is cut with no error — and the @ sentinel leads the
   stream, so the box still reports ANSWERED: no rows, no warning, on every remote box at once. f_sh
   now refuses rather than half-ship, so an overrun shows as "not accessible". Check the margin with
   A_SZDBG=1 a feed before growing this. */
#define FLIVE "LIVE=$(ps -eo args 2>/dev/null|grep -oE '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}')\nC=$(date +%s 2>/dev/null||echo 0);C=$((C>0?C-120:9999999999))\n"
#define FWTL(f) "w=$(tail -c 80000 " f " 2>/dev/null|grep -o '\"text\":\"[^\"]\\{1,300\\}'|tail -1|sed 's/^\"text\":\"//;s/\\\\[nt]/ /g;s/|/\\//g'|tail -c 60)\n"   /* end of the last model text = what the convo just said */
/* one stat, both clocks: t=birth (started; 0 where the fs lacks it -> mtime), n=mtime (feeds liveness) */
#define FSTAT(f) "set -- $(stat -c '%W %Y' " f " 2>/dev/null||stat -f '%B %m' " f " 2>/dev/null||echo 0 0);t=$1;n=$2;[ \"${t:-0}\" -gt 0 ]||t=$n\n"
/* live = sid in some argv, OR the transcript was written seconds ago: headless runs (claude -p, the shape every batch job uses) put no sid in argv and hold no fd, so argv alone calls a working agent "parked" and ↵ on it spawns a SECOND agent against the live session */
/* C is a real epoch or 9999999999 when date failed — never a small number, or n>C would call every row live */
#define FEMIT "printf '%s|%s|%s|%s|%s|%s|%s\\n' \"$(echo \"$LIVE\"|grep -q \"$s\"&&echo 1||{ [ \"${n:-0}\" -gt \"$C\" ]&&echo 1||echo 0;})\" \"$s\" \"${c:-?}\" \"$t\" \"${m:-?}\" \"$w\" \"${p:-?}\";done\n"
#define FRQG /* grok: session dir per sid, summary.json = meta; live = sid in argv (a grok launches --session-id) */ \
"e(){ grep -o \"\\\"$1\\\": \\\"[^\\\"]*\" \"$j\" 2>/dev/null|head -1|cut -d'\"' -f4;}\n" \
"for d in $(ls -td ~/.grok/sessions/*/*/ 2>/dev/null|head -15);do s=$(basename \"$d\");j=\"$d/summary.json\"\n" \
"c=$(e cwd);m=$(e current_model_id);p=$(e generated_title)\n" \
FWTL("\"$d/chat_history.jsonl\"") FSTAT("\"$d/chat_history.jsonl\"") FEMIT   /* the transcript, not the dir: a dir mtime does not move when a message is appended, so liveness would never fire */
#define FRQC /* codex: rollout-<ts>-<sid>.jsonl, line-1 source cli = interactive (exec = headless noise); live = argv sid (resume) or rollout held open by a codex pid — no launch sid flag */ \
"LIVE=\"$LIVE $(for q in $(pgrep -x codex 2>/dev/null);do ls -l /proc/$q/fd 2>/dev/null;done|grep -oE '[0-9a-f-]{36}\\.jsonl')\"\n" \
"for j in $(ls -t ~/.codex/sessions/*/*/*/rollout-*.jsonl 2>/dev/null|xargs awk 'FNR==1{if(/\"source\":\"cli\"/)print FILENAME;nextfile}' /dev/null 2>/dev/null|head -15);do s=$(basename \"$j\" .jsonl|tail -c 37)\n" \
"c=$(head -c 900 \"$j\" 2>/dev/null|grep -o '\"cwd\":\"[^\"]*'|cut -d'\"' -f4)\n" \
"m=$(grep -o '\"model\":\"[^\"]*' \"$j\" 2>/dev/null|tail -1|cut -d'\"' -f4)\n" \
"p=$(grep -o '\"user_message\",\"message\":\"[^\"]\\{1,46\\}' \"$j\" 2>/dev/null|head -1|sed 's/.*message\":\"//')\n" \
FWTL("\"$j\"") FSTAT("\"$j\"") FEMIT
static const char *FRQ =
FLIVE
"for j in $(ls -t ~/.claude/projects/*/*.jsonl 2>/dev/null|head -15);do s=$(basename \"$j\" .jsonl)\n"
"c=$(grep -o '\"cwd\":\"[^\"]*\"' \"$j\" 2>/dev/null|head -1|cut -d'\"' -f4)\n"
"m=$(grep -o '\"model\":\"[^\"]*\"' \"$j\" 2>/dev/null|tail -1|cut -d'\"' -f4)\n"
"p=$(grep -o '\"role\":\"user\",\"content\":\"[^\"]\\{1,46\\}' \"$j\" 2>/dev/null|grep -vE 'local-command|command-name'|head -1|sed 's/.*content\":\"//')\n"
FWTL("\"$j\"") FSTAT("\"$j\"") FEMIT
FRQG FRQC;
static const char *FRQL = FLIVE FRQG FRQC;                               /* statx local: claude via f_lemit, grok/codex via shell */

typedef struct { char host[40], cwd[256], sid[48], mdl[40], desc[160], tail[64], lbl[24]; int live, seen; long mt; } FI;
static FI FL[512]; static int FNN, f_live_scan = 1, f_top, f_tty;
typedef struct { int fd; char host[40], buf[8192]; int blen, got, ph; char via[28]; } FCh;   /* got: box answered (@ sentinel) — silent box = not accessible; ph+via: adb-attached phone (serial/addr) */
static FCh fch[64]; static int fnch;
static struct termios f_saved;
static volatile sig_atomic_t f_winch;
static void f_onwinch(int s) { (void)s; f_winch = 1; }
static char f_filt[64]; static int f_filt_n, f_idx[512], f_n, f_mode;
static char f_vb[131072]; static int f_vn; static double f_vms;
static char f_arc[65536]; static int f_arcn;                             /* archived sids (email 'e'): skipped on add, persisted in feed_arc.txt */

static int f_sane(char *dst, const char *src, int cap) {                 /* printable, collapse spaces, '|' can't break the wire/dat format */
    int o = 0; for (const char *p = src; *p && o < cap; p++) {
        unsigned char ch = (unsigned char)*p; if (ch < 32 || ch > 126) ch = ' '; if (ch == '|') ch = '/';
        if (ch == ' ' && (o == 0 || dst[o - 1] == ' ')) continue; dst[o++] = (char)ch; }
    while (o && dst[o - 1] == ' ') o--; dst[o] = 0; return o;
}
static char f_lbls[32768];                                               /* feed_lbl.txt, append-only, LAST wins, ''=cleared. Two line shapes:
    sid|label    — this one session
    ~match|label — sticky rule: any row whose host/dir/prompt matches. A batch job mints a NEW sid per call
                   (13 sids for one vn folder), so sid-keyed labels alone can never name the JOB — the rule does,
                   and it catches sessions that do not exist yet without restarting anything. */
static const char *f_lblof(const char *sid, const char *hay) { static char o[24]; o[0] = 0;
    for (const char *q = f_lbls; *q; ) { const char *e = strchr(q, '\n'); size_t L = e ? (size_t)(e - q) : strlen(q);
        const char *b = memchr(q, '|', L);
        if (b) { char k[128]; size_t kn = (size_t)(b - q);
            if (kn && kn < sizeof k) { memcpy(k, q, kn); k[kn] = 0;
                if (k[0] == '~' ? (hay && k[1] && strcasestr(hay, k + 1)) : !strcmp(k, sid)) {   /* exact sid, not substring: a bare strstr also fires inside another row's label text */
                    size_t n = L - kn - 1; if (n > 23) n = 23; memcpy(o, b + 1, n); o[n] = 0; } } }
        if (!e) break; q = e + 1; }
    return o; }
static void f_add(const char *host, int live, const char *cwd, const char *sid, long mt, const char *mdl, const char *desc, const char *tail) {
    if (f_arc[0] && strstr(f_arc, sid)) return;
    for (int i = 0; i < FNN; i++) if (!strcmp(FL[i].sid, sid)) {          /* dedup by session: patch in place (no rebuild → no reflash) */
        if (live && !FL[i].live) { FL[i].live = 1; snprintf(FL[i].host, 40, "%s", host); }
        if (f_live_scan && mt > 0) FL[i].mt = mt;                         /* adopt fresh start-time — cached rows carry the old mtime-era value */
        if (f_live_scan && tail && tail[0]) f_sane(FL[i].tail, tail, 63); /* convo moved on → fresh last-message tail */
        if (f_live_scan) FL[i].seen = 1; return; }
    if (FNN >= 512) return;
    FI *x = &FL[FNN++]; x->live = live; x->mt = mt; x->seen = f_live_scan;
    snprintf(x->host, 40, "%s", host); snprintf(x->cwd, 256, "%s", cwd); snprintf(x->sid, 48, "%s", sid);
    snprintf(x->mdl, 40, "%s", mdl);
    f_sane(x->desc, desc, 159); f_sane(x->tail, tail ? tail : "", 63);
    { char hay[600]; snprintf(hay, sizeof hay, "%s %s %s", host, cwd, x->desc);   /* label after desc: ~rules match on it */
      snprintf(x->lbl, 24, "%s", f_lblof(sid, hay)); }
}

static void f_line(const char *host, char *l) {                          /* wire: live|sid|cwd|started|model|tail|desc (tail before desc: desc may hold anything) */
    char *p[6], *s = l; for (int i = 0; i < 6; i++) { char *q = strchr(s, '|'); if (!q) return; *q = 0; p[i] = s; s = q + 1; }
    if (strlen(p[1]) >= 8) f_add(host, atoi(p[0]), p[2], p[1], atol(p[3]), p[4], s, p[5]);
}

#define FWIRE 4096                                                       /* a ssh's remote-command buffer (a.c: B) — the hard ceiling on a shipped scanner */
/* ship the scanner inline (no quotes) → any reachable box works, no deploy.
   cap is NOT optional: the wire is "echo @\n<scanner>", so a payload cut short still decodes its
   prefix, the @ sentinel lands, and the box counts as ANSWERED while the scanner it needed was
   silently truncated — zero rows AND no "not accessible" warning. Returns 0 rather than send that. */
#define FWIRE 4096                                                       /* a ssh's remote-command buffer (a.c: B) — the hard ceiling on a shipped scanner */
static int f_b64(const char *in, char *out, size_t cap) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int n = (int)strlen(in), o = 0;
    out[0] = 0; if ((size_t)(n + 2) / 3 * 4 + 1 > cap) return 0;
    for (int i = 0; i < n; i += 3) {
        unsigned v = (unsigned)(unsigned char)in[i] << 16;
        if (i + 1 < n) v |= (unsigned)(unsigned char)in[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned)(unsigned char)in[i + 2];
        out[o++] = T[(v >> 18) & 63]; out[o++] = T[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? T[(v >> 6) & 63] : '='; out[o++] = (i + 2 < n) ? T[v & 63] : '=';
    }
    out[o] = 0; return 1;
}

/* resolve THE session's pane: sid-bearing procs (grep '[a]bc' form never matches its own argv) + codex fd-holders (no argv sid) → climb ppids to a pane; every candidate tried (transient ghosts die mid-climb). $s = raw sid for scripts, $i = pane or empty */
#define FSID "s=$(printf %%s '%s'|tr -d '[]');L=$(tmux list-panes -a -F '#{pane_pid} #{pane_id}' 2>/dev/null);i=;" \
    "for p in $(grep -al '%s' /proc/[0-9]*/cmdline 2>/dev/null|cut -d/ -f3) $(for q in $(pgrep -x codex 2>/dev/null);do ls -l /proc/$q/fd 2>/dev/null|grep -q \"$s\"&&echo $q;done);do " \
    "while [ -n \"$p\" ]&&[ \"$p\" -gt 1 ] 2>/dev/null;do i=$(echo \"$L\"|awk -v x=\"$p\" '$1==x{print $2;exit}');[ -n \"$i\" ]&&break 2;p=$(awk '{print $4}' /proc/$p/stat 2>/dev/null);done;done;"
#define FPAT(b, x) char b[56]; snprintf(b, 56, "[%c]%s", (x)->sid[0], (x)->sid + 1)
#define FREAP while (waitpid(-1, 0, WNOHANG) > 0) {}
#define FBASH(sc) { execlp("bash", "bash", "-c", sc, (char *)0); _exit(127); }
static int f_sh(const char *host, const char *sc) {                     /* run sc on a box (local or ssh); returns stdout fd, caller reads to EOF then reaps. setsid: detach from tty — ssh else reads /dev/tty and steals keystrokes */
    int pf[2]; if (pipe(pf)) return -1; pid_t pid = fork(); if (pid < 0) { close(pf[0]); close(pf[1]); return -1; }
    if (!pid) { setsid(); dup2(pf[1], 1); close(pf[0]); close(pf[1]); int dn = open("/dev/null", O_RDWR); if (dn >= 0) { dup2(dn, 0); dup2(dn, 2); if (dn > 2) close(dn); }
        if (strcmp(host, DEV)) { static char b6[FWIRE], cm[FWIRE + 64];
            if (!f_b64(sc, b6, FWIRE - 64)) _exit(127);
            snprintf(cm, sizeof cm, "echo %s|base64 -d|bash", b6);
            if (strlen(cm) >= FWIRE) _exit(127);   /* die silent-but-visible: the box lands in "✗ not accessible" instead of answering with a truncated scanner */
            execlp("a", "a", "ssh", host, cm, (char *)0); }
        FBASH(sc) }
    close(pf[1]); return pf[0];
}
static void f_drain(int fd) { if (fd < 0) return; char t[64]; while (read(fd, t, 64) > 0) {} close(fd); FREAP }   /* fire a script, eat its output, reap */
static char f_cb[131072]; static int f_cbn, f_cfd = -1; static char f_csid[48]; static struct timespec f_ct0; static double f_tms;
static void f_cap_start(FI *x) {                                         /* 1ms mandate: spawn the fetch and return — keypress paints instantly, content lands via the poll loop */
    if (f_cfd >= 0) { close(f_cfd); f_cfd = -1; FREAP }                  /* flipped again mid-fetch: drop the stale one */
    clock_gettime(CLOCK_MONOTONIC, &f_ct0);
    #define FTAIL "printf '\\033[33m\xe2\x8f\xb8 not live \xe2\x80\x94 saved transcript tail (\xe2\x86\xb5 resume)\\033[0m\\n';j=$(ls -t ~/.claude/projects/*/%s.jsonl ~/.grok/sessions/*/%s/chat_history.jsonl ~/.codex/sessions/*/*/*/*%s.jsonl 2>/dev/null|head -1);" \
        "tail -c 80000 \"$j\" 2>/dev/null|grep -o '\"text\":\"[^\"]\\{1,400\\}'|sed 's/^\"text\":\"//;s/\\\\n/ /g'|tail -8"
    char sc[1400]; FPAT(pat, x);   /* parked: transcript only; live: THE session's pane by sid, transcript fallback */
    if (!x->live) snprintf(sc, sizeof sc, FTAIL, x->sid, x->sid, x->sid);
    else snprintf(sc, sizeof sc, FSID "if [ -n \"$i\" ];then tmux capture-pane -ep -t \"$i\" 2>/dev/null;else " FTAIL ";fi", pat, pat, "$s", "$s", "$s");
    #undef FTAIL
    f_cbn = 0; snprintf(f_csid, 48, "%s", x->sid); f_cfd = f_sh(x->host, sc);
}
static void f_stamp(struct timespec ka) {                                /* true keypress→painted time, patched into the fixed header cell after the frame is on screen */
    struct timespec kb; clock_gettime(CLOCK_MONOTONIC, &kb);
    f_tms = (double)(kb.tv_sec - ka.tv_sec) * 1e3 + (double)(kb.tv_nsec - ka.tv_nsec) / 1e6;
    dprintf(1, "\0337\033[1;17H\033[32m%8.4fms\033[0m\0338", f_tms);     /* col 17 = right after the fixed "a feed key→paint" prefix */
}
static void f_archive(int sel) {                                         /* email 'e': persist sid, drop row in place */
    if (!f_n) return; FI *x = &FL[f_idx[sel]];
    char ap[P]; snprintf(ap, P, "%s/feed_arc.txt", DDIR); FILE *af = fopen(ap, "a"); if (af) { fprintf(af, "%s\n", x->sid); fclose(af); }
    if (f_arcn + 40 < (int)sizeof f_arc) f_arcn += snprintf(f_arc + f_arcn, 40, "%s\n", x->sid);
    int i = (int)(x - FL); memmove(&FL[i], &FL[i + 1], (size_t)(FNN - 1 - i) * sizeof(FI)); FNN--;
}
static int f_dismiss(void) {                                             /* archive every parked row (sid → feed_arc.txt), drop in place; live rows stay */
    char ap[P]; snprintf(ap, P, "%s/feed_arc.txt", DDIR); FILE *af = fopen(ap, "a"); int nd = 0;
    for (int i = FNN - 1; i >= 0; i--) if (!FL[i].live) { if (af) fprintf(af, "%s\n", FL[i].sid);
        memmove(&FL[i], &FL[i + 1], (size_t)(FNN - 1 - i) * sizeof(FI)); FNN--; nd++; }
    if (af) fclose(af); return nd;
}
static int f_rows(void) { struct winsize w; ioctl(1, TIOCGWINSZ, &w); return w.ws_row ? w.ws_row : 24; }
static int f_rem(void) { int r = 0; for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) r++; return r; }   /* scan channels still open */
static void f_rst(void) { tcsetattr(f_tty, TCSANOW, &f_saved); printf("\033[?7h\033[H\033[2J"); fflush(stdout); }
#define FHDR "\033[H\033[1ma feed\033[0m \033[90mkey\xe2\x86\x92paint\033[0m\033[32m%8.4fms\033[0m \033[90m%d/%d%s\033[0m"
static void f_when(long t, char *o) {                                    /* started, clock-style: 702a today, Jul9 older (window-name convention) */
    o[0] = 0; if (t <= 0) return; time_t tt = (time_t)t, nw = time(0); struct tm lt, ln; localtime_r(&tt, &lt); localtime_r(&nw, &ln);
    if (lt.tm_yday == ln.tm_yday && lt.tm_year == ln.tm_year) { int h = lt.tm_hour % 12; if (!h) h = 12; snprintf(o, 8, "%d%02d%s", h, lt.tm_min, lt.tm_hour >= 12 ? "p" : "a"); }
    else { char m[6]; strftime(m, 6, "%b", &lt); snprintf(o, 8, "%s%d", m, lt.tm_mday); }
}
static void f_bar(void) {                                                /* bottom rows pinned (tui.md r3): omnibox = the place to type, menu = every live key (r4), key yellow + meaning gray */
    #define MI(k,l) "\033[1;93m" k "\033[0;90m" l
    char b[768]; int o = snprintf(b, 768, "\033[%d;1H\033[K\033[93m/%s\xe2\x96\x8f\033[0m\033[90m%s\033[0m\r\n\033[K%s\033[0m",
        f_rows() - 1, f_filt, f_filt[0] ? "" : (f_mode ? " type to filter" : " press / to filter"),
        f_mode ? MI(" \xe2\x86\xb5", " open  ") MI("Tab", " window  ") MI("\xe2\x86\x91\xe2\x86\x93", " move  ") MI("\xe2\x86\x90\xe2\x86\x92", " page  ") MI("\xe2\x8c\xab", " back  ") MI("esc", " quit")
               : MI(" \xe2\x86\xb5", " open  ") MI("j/k", " move  ") MI("p", " park  ") MI("D", " sweep \xe2\x8f\xb8  ") MI("m", " model  ") MI("e", " arc  ") MI("E", " arc all  ") MI("/", " find  ") MI("Tab", " list  ") MI("q", " quit"));
    (void)!write(1, b, (size_t)o);
    #undef MI
}
static int f_wrow(const char *q, size_t L, int W, int emit) {            /* rows a line occupies at visible width W; emit=1 writes wrapped chunks. ANSI seqs zero-width+atomic, utf8 continuations zero-width */
    int rows = 1, vis = 0; size_t cs = 0;
    for (size_t i = 0; i < L; i++) {
        if (q[i] == 27) { size_t j = i + 1; if (j < L && q[j] == '[') { j++; while (j < L && !isalpha((unsigned char)q[j])) j++; if (j < L) j++; } i = j - 1; continue; }
        if (((unsigned char)q[i] & 0xC0) == 0x80) continue;
        if (vis == W) { if (emit) { (void)!write(1, q + cs, i - cs); (void)!write(1, "\033[K\r\n", 5); } cs = i; rows++; vis = 0; }
        vis++; }
    if (emit) { (void)!write(1, q + cs, L - cs); (void)!write(1, "\033[0m\033[K\r\n", 9); }
    return rows;
}
static void f_vpaint(int sel) {                                          /* gmail split view: list strip on top, the actual window below. in-place (per-line K, final J): scan rows repaint it live, no 2J flash */
    struct winsize w; ioctl(1, TIOCGWINSZ, &w); int rows = w.ws_row ? w.ws_row : 24, rem = f_rem();
    char h[2048]; FI *x = f_n ? &FL[f_idx[sel]] : NULL; char ag[8]; f_when(x ? x->mt : 0, ag);
    int o = snprintf(h, 2048, FHDR " %s\033[97m%s\033[0m:%.8s \033[90m%s\033[0m \033[35m%.12s\033[0m\033[K\r\n",
        f_tms, f_n ? sel + 1 : 0, f_n, rem ? " \xe2\x80\xa6" : "", x && x->live ? "\033[32m\xe2\x97\x8f\033[0m " : "\xe2\x8f\xb8 ", x ? x->host : "?", x ? bname(x->cwd) : "", ag[0] ? ag : "?", x ? x->mdl : "");
    int ls = sel - 2; if (ls > f_n - 5) ls = f_n - 5; if (ls < 0) ls = 0; int shown = 0;
    for (int r = ls; r < ls + 5 && r < f_n; r++, shown++) { FI *y = &FL[f_idx[r]]; char ya[8], yl[36]; f_when(y->mt, ya);
        yl[0] = 0; if (y->lbl[0]) snprintf(yl, 36, "\033[36m%.10s\033[39m ", y->lbl);
        o += snprintf(h + o, (size_t)(2048 - o), "%s%s %-6.6s%-12.12s %-8.8s %s%.24s\033[90m%s%.30s\033[0m\033[K\r\n",
            r == sel ? "\033[7m" : "", y->live ? "\033[32m\xe2\x97\x8f\033[39m" : "\xe2\x8f\xb8", ya, y->host, bname(y->cwd), yl, y->desc, y->tail[0] ? " \xc2\xb7 " : "", y->tail); }
    if (!f_vn && f_cfd >= 0 && x) o += snprintf(h + o, (size_t)(2048 - o), "\033[90m---------- \xe2\x9f\xb3 fetching %s\xe2\x80\xa6 ----------\033[0m\033[K\r\n", x->host);
    else o += snprintf(h + o, (size_t)(2048 - o), "\033[90m---------- content fetch %.1fms ----------\033[0m\033[K\r\n", f_vms);   /* the number describes the pane below it */
    (void)!write(1, h, (size_t)o);
    int W = w.ws_col ? w.ws_col : 80, bud = rows - 4 - shown; if (bud < 1) bud = 1;
    struct { const char *q; size_t L; } ln[512]; int nl = 0;             /* wrap DOWN, never clip (Sean 7/9): budget by wrapped rows so content still ends flush above the bar */
    for (char *q = f_vb; q < f_vb + f_vn && nl < 512;) { char *e = memchr(q, '\n', (size_t)(f_vb + f_vn - q)); size_t L = e ? (size_t)(e - q) : (size_t)(f_vb + f_vn - q);
        ln[nl].q = q; ln[nl].L = L; nl++; q = e ? e + 1 : f_vb + f_vn; }
    int start = nl, acc = 0;
    while (start > 0) { int r = f_wrow(ln[start - 1].q, ln[start - 1].L, W, 0); if (acc + r > bud) break; acc += r; start--; }
    for (int i = start; i < nl; i++) f_wrow(ln[i].q, ln[i].L, W, 1);
    (void)!write(1, "\033[J", 3);                                        /* clear leftovers below (shrunk content / resize) — bar repaints right after */
    f_bar();
}
#if defined(__linux__) && defined(STATX_BTIME)
static void f_xtail(const char *hb, char *tl) {                          /* end of the LAST "text" value in buf = the convo's actual last words; ≤56 chars, cut at a word */
    tl[0] = 0; const char *q = hb, *last = 0;
    while ((q = strstr(q, "\"text\":\""))) { last = q + 8; q += 8; }
    if (!last) return;
    char t[321]; int n = 0;
    for (const char *p = last; *p && *p != '"' && n < 320; p++) {
        char c = *p;
        if (c == '\\' && p[1]) { p++; if (*p == 'u') { c = ' '; for (int k = 0; k < 4 && p[1]; k++) p++; } else c = (*p == 'n' || *p == 't') ? ' ' : *p; }
        t[n++] = c; }
    t[n] = 0;
    char s[321]; int sn = f_sane(s, t, 320), st = sn > 56 ? sn - 56 : 0;
    if (st) { char *sp = strchr(s + st, ' '); if (sp && sp - s - st < 14) st = (int)(sp - s) + 1; }
    memcpy(tl, s + st, (size_t)(sn - st) + 1);
}
static void f_jmeta(const char *jp, char *cwd, char *mdl, char *desc, char *tl) {  /* new sid: head 256K = cwd + first real user msg; tail 256K = last model + last words (FRQ grep parity, bounded — never the whole transcript) */
    static char hb[262144]; cwd[0] = mdl[0] = desc[0] = tl[0] = 0;
    int fd = open(jp, O_RDONLY); if (fd < 0) return;
    int hn = (int)read(fd, hb, sizeof hb - 1); if (hn < 0) hn = 0; hb[hn] = 0;
    char *q = strstr(hb, "\"cwd\":\""), *z;
    if (q && (z = strchr(q + 7, '"')) && z - q - 7 < 256) { memcpy(cwd, q + 7, (size_t)(z - q - 7)); cwd[z - q - 7] = 0; }
    for (q = hb; (q = strstr(q, "\"role\":\"user\",\"content\":\"")); q += 25) {
        char t[47]; int tn = 0; while (tn < 46 && q[25 + tn] && q[25 + tn] != '"') { t[tn] = q[25 + tn]; tn++; } t[tn] = 0;
        if (tn && !strstr(t, "local-command") && !strstr(t, "command-name")) { memcpy(desc, t, (size_t)tn + 1); break; } }
    off_t sz = lseek(fd, 0, SEEK_END);
    for (int t2 = 0; ; t2++) {                                           /* model: last match, head then tail (tail wins; tool-dump tails can lack it) */
        for (q = hb; (q = strstr(q, "\"model\":\"")); q += 9) if ((z = strchr(q + 9, '"')) && z - q - 9 < 40) { memcpy(mdl, q + 9, (size_t)(z - q - 9)); mdl[z - q - 9] = 0; }
        if (t2 || sz <= (off_t)sizeof hb - 1) break;
        lseek(fd, sz - ((off_t)sizeof hb - 1), SEEK_SET); hn = (int)read(fd, hb, sizeof hb - 1); if (hn < 0) hn = 0; hb[hn] = 0; }
    f_xtail(hb, tl);                                                     /* hb = final chunk read (tail when the file outgrew it) */
    close(fd);
}
static void f_lemit(void) {                                              /* child of f_spawn_local: same-box scan, FRQ-format rows on fd1 — parent painted at ~0.2ms, these patch in via the poll loop */
    struct { char p[P]; long mt, bt; } nj[15]; int nn = 0; struct dirent *de;
    char pr[P]; snprintf(pr, P, "%s/.claude/projects", HOME);
    DIR *rd = opendir(pr);
    while (rd && (de = readdir(rd))) { if (de->d_name[0] == '.') continue;
        char sp[P]; snprintf(sp, P, "%s/%s", pr, de->d_name); DIR *sd = opendir(sp); struct dirent *fe;
        while (sd && (fe = readdir(sd))) { size_t L = strlen(fe->d_name);
            if (L < 14 || L > 52 || strcmp(fe->d_name + L - 6, ".jsonl")) continue;   /* sid 8..46 chars (f_line parity) */
            struct statx sx;                                             /* one dirfd-relative call: mtime ranks (ls -t), birth = started, no path walk */
            if (statx(dirfd(sd), fe->d_name, 0, STATX_BTIME | STATX_MTIME, &sx)) continue;
            long m = (long)sx.stx_mtime.tv_sec;
            if (nn == 15 && m <= nj[14].mt) continue;
            int i = nn < 15 ? nn++ : 14;                                 /* newest 15 by mtime = FRQ ls -t|head -15 */
            while (i > 0 && nj[i - 1].mt < m) { nj[i] = nj[i - 1]; i--; }
            snprintf(nj[i].p, P, "%s/%s", sp, fe->d_name); nj[i].mt = m;
            nj[i].bt = (sx.stx_mask & STATX_BTIME) && sx.stx_btime.tv_sec > 0 ? (long)sx.stx_btime.tv_sec : m; }
        if (sd) closedir(sd); }
    if (rd) closedir(rd);
    char sid[15][48]; int lv[15];
    for (int i = 0; i < nn; i++) { const char *bn = strrchr(nj[i].p, '/') + 1;
        size_t L = strlen(bn) - 6; memcpy(sid[i], bn, L); sid[i][L] = 0; lv[i] = 0; }
    DIR *pd = opendir("/proc"); char cb[32768];                          /* live = sid in some argv (FRQ ps parity) */
    while (pd && (de = readdir(pd))) { if (de->d_name[0] < '1' || de->d_name[0] > '9') continue;
        char cp[288]; snprintf(cp, sizeof cp, "/proc/%s/cmdline", de->d_name);
        int fd = open(cp, O_RDONLY); if (fd < 0) continue;
        int r = (int)read(fd, cb, sizeof cb - 1); close(fd); if (r <= 0) continue;
        for (int i = 0; i < r; i++) if (!cb[i]) cb[i] = ' '; cb[r] = 0;
        for (int i = 0; i < nn; i++) if (!lv[i] && strstr(cb, sid[i])) lv[i] = 1; }
    if (pd) closedir(pd);
    { long now = (long)time(0);                                          /* FEMIT parity: a transcript written seconds ago is a live agent, whatever argv says */
      for (int i = 0; i < nn; i++) if (!lv[i] && now - nj[i].mt <= 120) lv[i] = 1; }
    for (int i = 0; i < nn; i++) {
        int knw = 0; for (int j = 0; j < FNN; j++) if (!strcmp(FL[j].sid, sid[i])) { knw = 1; break; }
        char cwd[256], mdl[40], desc[160], tl[64];
        if (knw) { cwd[0] = mdl[0] = desc[0] = tl[0] = 0;                 /* cached row: patch live/mt/seen + FRESH tail only — skip the head reads */
            int fd = open(nj[i].p, O_RDONLY); if (fd >= 0) { static char tb[80001]; off_t sz = lseek(fd, 0, SEEK_END);
                lseek(fd, sz > 80000 ? sz - 80000 : 0, SEEK_SET); int r = (int)read(fd, tb, 80000); if (r < 0) r = 0; tb[r] = 0; f_xtail(tb, tl); close(fd); } }
        else f_jmeta(nj[i].p, cwd, mdl, desc, tl);
        dprintf(1, "%d|%s|%s|%ld|%s|%s|%s\n", lv[i], sid[i], cwd[0] ? cwd : "?", nj[i].bt, mdl[0] ? mdl : "?", tl, desc[0] ? desc : "?"); }
}
static void f_spawn_local(void) {                                        /* local FRQ replacement: fork straight into a channel — no bash, no whole-file greps; scans while f_hosts popen-parses the registry */
    if (fnch >= 64) return; int pf[2]; if (pipe(pf)) return;
    pid_t pid = fork(); if (pid < 0) { close(pf[0]); close(pf[1]); return; }
    if (!pid) { dup2(pf[1], 1); close(pf[0]); close(pf[1]); f_lemit(); _exit(0); }
    close(pf[1]); FCh *ch = &fch[fnch++]; ch->fd = pf[0]; ch->blen = 0; ch->buf[0] = 0; ch->got = 1; ch->ph = 0; ch->via[0] = 0;
    snprintf(ch->host, 40, "%s", DEV);
}
#endif
static void f_spawn(const char *host, const char *sc) {                  /* host=NULL → local box; remote gets an @ sentinel so silence = not accessible */
    if (fnch >= 64) return; static char wr[24576]; if (host) { snprintf(wr, sizeof wr, "echo @\n%s", sc); sc = wr; }
    int fd = f_sh(host ? host : DEV, sc); if (fd < 0) return;
    FCh *ch = &fch[fnch++]; ch->fd = fd; ch->blen = 0; ch->buf[0] = 0; ch->got = !host; ch->ph = 0; ch->via[0] = 0;
    snprintf(ch->host, 40, "%s", host ? host : DEV);
}
static void f_phones(void) {                                             /* adb-attached phones (adata/git/adb/*.txt): match serial/wireless on THIS box, then scan termux over adb-forwarded ssh (sshd must be up; android sshd-over-wifi is the unreliable one — ssh.c) */
    char ad[P]; snprintf(ad, P, "%s/adb", SROOT); char pfs[16][P]; int na = listdir(ad, pfs, 16);
    for (int i = 0; i < na && fnch < 64; i++) { kvs_t kv = kvfile(pfs[i]);
        const char *nm = kvget(&kv, "Name"), *se = kvget(&kv, "Serial"), *wi = kvget(&kv, "Wireless");
        if (!nm || !se) continue;
        static char b6[24576]; if (!f_b64(FRQ, b6, sizeof b6)) continue; char sc[26000]; int pt = 18100 + i;
        snprintf(sc, sizeof sc, "d=$(adb devices 2>/dev/null|awk '/\\tdevice$/{print $1}'|grep -m1 -x -e '%s' -e '%s');[ -n \"$d\" ]||exit 0;echo @;echo \"@adb|$d\";"
            "u=$(adb -s \"$d\" shell cmd package list packages -U 2>/dev/null|awk '/com.termux /{sub(\".*uid:\",\"\");print;exit}');[ -n \"$u\" ]||exit 0;"
            "adb -s \"$d\" forward tcp:%d tcp:8022 >/dev/null 2>&1;ssh-keygen -R '[localhost]:%d' >/dev/null 2>&1;"
            "ssh -oConnectTimeout=4 -oStrictHostKeyChecking=accept-new -p %d u0_a$((u-10000))@localhost 'echo %s|base64 -d|bash' 2>/dev/null;"
            "adb -s \"$d\" forward --remove tcp:%d >/dev/null 2>&1",
            se, (wi && wi[0]) ? wi : se, pt, pt, pt, b6, pt);
        if (getenv("A_FDBG")) { puts(sc); continue; }
        int fd = f_sh(DEV, sc); if (fd < 0) continue;                    /* runs LOCALLY, rows labeled as the phone */
        FCh *ch = &fch[fnch++]; ch->fd = fd; ch->blen = 0; ch->buf[0] = 0; ch->got = 0; ch->ph = 0; ch->via[0] = 0;
        snprintf(ch->host, 40, "%s", nm); }
}

static void f_hosts(void) {                                              /* local + every registry box (lan/wan/usb variants deduped) */
#if defined(__linux__) && defined(STATX_BTIME)
    f_spawn_local(); f_spawn(NULL, FRQL);
#else
    f_spawn(NULL, FRQ);                                                  /* no /proc|statx → local via FRQ */
#endif
    char cmd[B]; snprintf(cmd, B, "grep -h '^Name:' %s/ssh/*.txt 2>/dev/null|sed 's/Name: //'|sed -E 's/-(lan|wan|usb|hot|relay)$//'|sort -uf|grep -ivx %s", SROOT, DEV);   /* -uf/-ivx: HSU-lan + hsu-wan are ONE box; case-split scanned it twice and split its rows across two host names */
    FILE *p = popen(cmd, "r"); char ln[128];
    while (p && fgets(ln, 128, p)) { ln[strcspn(ln, "\n")] = 0; if (ln[0]) f_spawn(ln, FRQ); }
    if (p) pclose(p);
    f_phones();
}

static int f_recv(FCh *ch) {
    int rd = (int)read(ch->fd, ch->buf + ch->blen, (int)sizeof(ch->buf) - 1 - ch->blen);
    if (rd <= 0) { close(ch->fd); ch->fd = -1; return 0; }
    ch->blen += rd; ch->buf[ch->blen] = 0; char *nl;
    while ((nl = strchr(ch->buf, '\n'))) { *nl = 0;
        if (ch->buf[0] == '@') { ch->got = 1; if (!strncmp(ch->buf, "@adb|", 5)) { ch->ph = 1; snprintf(ch->via, 28, "%s", ch->buf + 5); } }
        else f_line(ch->host, ch->buf);
        int rem = ch->blen - (int)(nl + 1 - ch->buf); memmove(ch->buf, nl + 1, (size_t)rem + 1); ch->blen = rem; }
    return 1;
}
static int f_offs(char *o, int cap) {                                    /* boxes that never answered (per host, closed channels only, DEV excluded) */
    int n = 0, ol = 0; o[0] = 0;
    for (int i = 0; i < fnch; i++) { FCh *c = &fch[i];
        if (c->fd >= 0 || c->got || !strcmp(c->host, DEV)) continue;
        int skip = 0; for (int j = 0; j < fnch && !skip; j++) if (j != i && !strcmp(fch[j].host, c->host) && (fch[j].got || j < i)) skip = 1;
        if (skip) continue; n++; ol += snprintf(o + ol, (size_t)(cap - ol), "%s%s", ol ? " " : "", c->host); }
    return n;
}

static int f_vh(void) { int rows = f_rows(); int vh = rows >= 14 ? rows - 5 : rows - 3; return vh < 1 ? 1 : vh; }
static void f_filter(void) {
    f_n = 0;
    for (int i = 0; i < FNN; i++) {
        if (f_filt[0]) { char hay[584]; snprintf(hay, sizeof hay, "%s %s %s %s %s", FL[i].lbl, FL[i].host, FL[i].cwd, FL[i].desc, FL[i].tail); if (!strcasestr(hay, f_filt)) continue; }
        f_idx[f_n++] = i;
    }
    for (int i = 1; i < f_n; i++) { int t = f_idx[i], j = i - 1;         /* newest started first (Sean 7/10) */
        while (j >= 0 && FL[f_idx[j]].mt < FL[t].mt) { f_idx[j + 1] = f_idx[j]; j--; } f_idx[j + 1] = t; }
}
static int f_csel(int sel) { f_filter(); return sel >= f_n ? (f_n ? f_n - 1 : 0) : sel; }   /* re-filter, keep sel in range */

static void f_load(void) {                                               /* seed from last run's data → first frame is the real, interactive list */
    char fp[P]; snprintf(fp, P, "%s/feed2.dat", DDIR); FILE *f = fopen(fp, "r"); if (!f) return;   /* feed2 = host|wire (feed.dat lacked tail) */
    char ln[1280]; while (fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\n")] = 0;
        char *b = strchr(ln, '|'); if (!b) continue; *b = 0; if (b[1] == '1') b[1] = '0';   /* seed as parked; a live scan re-greens it */
        f_line(ln, b + 1); }
    fclose(f);
}

static void f_save(void) {
    char fp[P]; snprintf(fp, P, "%s/feed2.dat", DDIR); FILE *f = fopen(fp, "w"); if (!f) return;
    for (int i = 0; i < FNN; i++) { FI *x = &FL[i]; if (x->seen) fprintf(f, "%s|%d|%s|%s|%ld|%s|%s|%s\n", x->host, x->live, x->sid, x->cwd, x->mt, x->mdl, x->tail, x->desc); }
    fclose(f);
}
static void f_scan_wait(int ms) {                                        /* fleet scan, drained with a deadline — CLI paths need what is running NOW, not the last run's cache */
    f_hosts();
    struct timespec d0; clock_gettime(CLOCK_MONOTONIC, &d0);
    for (;;) { struct pollfd pf[64]; int np = 0;
        for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) { pf[np].fd = fch[i].fd; pf[np++].events = POLLIN; }
        if (!np) break;
        struct timespec nw; clock_gettime(CLOCK_MONOTONIC, &nw);
        int el = (int)((nw.tv_sec - d0.tv_sec) * 1000 + (nw.tv_nsec - d0.tv_nsec) / 1000000);
        if (el >= ms || poll(pf, np, ms - el) <= 0) break;
        for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) for (int j = 0; j < np; j++)
            if (pf[j].fd == fch[i].fd && (pf[j].revents & (POLLIN | POLLHUP | POLLERR))) f_recv(&fch[i]); }
    for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) { close(fch[i].fd); fch[i].fd = -1; }
    FREAP
}
static int f_label(const char *m, const char *lb, int rule) {            /* a feed label [-p] <match> <label|-> */
    char ap[P]; snprintf(ap, P, "%s/feed_lbl.txt", DDIR);
    const char *sh = strcmp(lb, "-") ? lb : "";
    if (rule) { FILE *rf = fopen(ap, "a"); if (rf) { fprintf(rf, "~%s|%s\n", m, sh); fclose(rf); }
        printf("\xe2\x9c\x93 rule ~%s \xe2\x86\x92 '%s' \xc2\xb7 every row matching it, including sessions that do not exist yet\n", m, sh); return 0; }
    f_live_scan = 0; f_load(); f_live_scan = 1;                          /* cache = parked history stays labelable ... */
    f_scan_wait(6000);                                                   /* ... scan = an agent started since the last feed run is labelable WITHOUT restarting it */
    f_filter();
    FILE *af = fopen(ap, "a"); int nm = 0, nl = 0;
    for (int i = 0; i < FNN; i++) { char hay[600]; snprintf(hay, sizeof hay, "%s %s %s %s", FL[i].host, FL[i].cwd, FL[i].desc, FL[i].sid);
        if (!strcasestr(hay, m)) continue; nm++; nl += FL[i].live;
        if (af) fprintf(af, "%s|%s\n", FL[i].sid, sh);
        printf("%s %-12.12s %-10.10s %-10.10s %.40s\n", FL[i].live ? "\033[32m\xe2\x97\x8f\033[0m" : "\xe2\x8f\xb8",
            sh[0] ? sh : "(cleared)", FL[i].host, bname(FL[i].cwd), FL[i].desc); }
    if (af) fclose(af);
    if (nm) printf("\xe2\x9c\x93 %d row(s) (%d live) \xe2\x86\x92 '%s' \xc2\xb7 %s/feed_lbl.txt\n", nm, nl, sh, DDIR);
    else printf("x nothing running or cached matches '%s' \xc2\xb7 -p makes it a standing rule instead\n", m);
    return !nm;
}

static void f_paint(int sel, long rus, int refreshing) {
    int rows = f_rows(), vh = f_vh(), n = f_n, extra = rows >= 14;
    if (sel >= n) sel = n ? n - 1 : 0; if (sel < 0) sel = 0;
    if (sel < f_top) f_top = sel; else if (sel >= f_top + vh) f_top = sel - vh + 1;     /* edge-scroll: cursor moves in view, list scrolls only at edges */
    if (f_top > n - vh) f_top = n > vh ? n - vh : 0; if (f_top < 0) f_top = 0;
    (void)rus;
    char fb[1 << 16]; int o = 0;
    #define AP(...) o += snprintf(fb + o, (int)sizeof(fb) - o, __VA_ARGS__)
    #define EOL "\033[K\r\n"                                            /* home + per-line clear-to-EOL = in-place redraw, no flicker */
    AP(FHDR, f_tms, n ? sel + 1 : 0, n, refreshing ? " \xe2\x80\xa6" : "");
    if (n) AP(" \033[35m%.14s\033[0m", FL[f_idx[sel]].mdl); AP(EOL);
    if (extra) {
        char ob[256]; int noff = f_offs(ob, 256);
        AP("\033[32m\xe2\x97\x8f\033[0m\033[90m live \033[0m\xe2\x8f\xb8\033[90m parked \xc2\xb7 newest first\033[0m");
        for (int i = 0; i < fnch; i++) if (fch[i].ph) AP("\033[90m \xc2\xb7 \xf0\x9f\x93\xb1%s adb\033[0m", fch[i].host);
        if (noff) AP("\033[31m \xe2\x9c\x97%d off: %.48s\033[0m", noff, ob);
        AP(EOL);
        AP("\033[90m    %-6s%-10s %-8s %s\033[0m" EOL, "START", "BOX", "DIR", "PROMPT \xc2\xb7 LATEST"); }
    for (int r = 0; r < vh; r++) { int li = f_top + r;
        if (li >= n) { AP(EOL); continue; }
        FI *x = &FL[f_idx[li]]; const char *b = bname(x->cwd);
        char ag[8], lb[40]; lb[0] = 0; if (x->lbl[0]) snprintf(lb, 40, "\033[36m%.14s\033[39m ", x->lbl);
        f_when(x->mt, ag);
        char line[360]; snprintf(line, 360, "%s %-6.6s%-10.10s %-8.8s %s%.28s\033[90m%s%.56s\033[39m", x->live ? "\033[32m\xe2\x97\x8f\033[39m" : "\xe2\x8f\xb8", ag, x->host, b, lb, x->desc, x->tail[0] ? " \xc2\xb7 " : "", x->tail);
        if (li == sel) AP("\033[7m  %s\033[0m" EOL, line); else AP("  %s" EOL, line); }
    if (o >= 2 && fb[o - 1] == '\n') o -= 2;                            /* drop trailing CRLF: a newline on the last row scrolls the title off the top */
    AP("\033[J");
    #undef AP
    #undef EOL
    (void)!write(1, fb, (size_t)o);
    f_bar();
}

__attribute__((noreturn)) static void f_attach(FI *x) {                  /* parked → resume window; live → THE sid's window. local jumps via switch-client (grouped sessions don't follow select-window); remote selects on the box then attaches */
    f_rst();
    int lo = !strcmp(x->host, DEV);
    if (!x->live) { char wn[20]; snprintf(wn, 20, "r-%.8s", x->sid);
        const char *md = (x->mdl[0] && strcmp(x->mdl, "?")) ? x->mdl : (*cfget("m_model") ? cfget("m_model") : "opus");   /* default = model the session used before */
        char rc[240];   /* model field names the binary: grok-*, gpt-*=codex (config.toml owns its model), else claude */
        if (!strncmp(x->mdl, "gpt", 3)) snprintf(rc, 240, "codex resume --dangerously-bypass-approvals-and-sandbox '%s'", x->sid);
        else snprintf(rc, 240, "%s '%s' --resume '%s'", strncmp(x->mdl, "grok", 4) ? "claude --dangerously-skip-permissions --model" : "grok --always-approve -m", md, x->sid);
        if (lo) { char sc[900]; snprintf(sc, sizeof sc, "w=$(tmux new-window -dP -t 'a:' -c '%s' -n '%s' -F '#{pane_id}' -- %s);exec tmux switch-client -t \"$w\"", x->cwd, wn, rc);
            FBASH(sc) }
        if (fork() == 0) { char rr[700]; snprintf(rr, 700, "tmux new-window -t a -c '%s' -n '%s' %s", x->cwd, wn, rc);
            execlp("a", "a", "ssh", x->host, rr, (char *)0); _exit(127); }
        wait(0); }
    else { char sc[900]; FPAT(pat, x);
        if (lo) { snprintf(sc, sizeof sc, FSID "[ -n \"$i\" ]&&exec tmux switch-client -t \"$i\"", pat, pat);
            FBASH(sc) }
        snprintf(sc, sizeof sc, FSID "[ -n \"$i\" ]&&tmux select-window -t \"$i\"", pat, pat);   /* aim the box's session first; the attach below lands on it */
        f_drain(f_sh(x->host, sc)); }
    execlp("a", "a", "ssh", x->host, (char *)0); _exit(127);
}

static int cmd_feed(int c, char **v) { perf_disarm();
    init_db(); load_cfg();                                               /* m_model = resume fallback when a session has no model yet */
    FNN = fnch = 0; f_top = 0;
    { char ap[P]; snprintf(ap, P, "%s/feed_arc.txt", DDIR); FILE *af = fopen(ap, "r"); if (af) { f_arcn = (int)fread(f_arc, 1, sizeof f_arc - 1, af); f_arc[f_arcn] = 0; fclose(af); } }
    { char lp[P]; snprintf(lp, P, "%s/feed_lbl.txt", DDIR); FILE *lf = fopen(lp, "r"); if (lf) { f_lbls[fread(f_lbls, 1, sizeof f_lbls - 1, lf)] = 0; fclose(lf); } }
    if (c > 2 && !strcmp(v[2], "label")) { int pg = c > 3 && !strcmp(v[3], "-p");
        if (c < 5 + pg) { puts("a feed label [-p] <match> <label|->\n  bare: scans the fleet live, tags every session matching now (running ones included)\n  -p:   standing rule — also tags matching sessions that start later"); return 1; }
        return f_label(v[3 + pg], v[4 + pg], pg); }
    int dsm = c > 2 && !strcmp(v[2], "dismiss");                          /* dismiss = scan for live truth, then archive all parked (web/CLI form of TUI 'D') */
    f_live_scan = 0; f_load(); f_live_scan = 1;                           /* seed from last run (parked until a live scan confirms) */
    struct timespec tp; clock_gettime(CLOCK_MONOTONIC, &tp);
    long rus = (tp.tv_sec - _t0.tv_sec) * 1000000L + (tp.tv_nsec - _t0.tv_nsec) / 1000;
    f_hosts();
    if (dsm || !isatty(0) || !isatty(1)) {  /* non-tty: dump when local drains; late boxes append */
        static char pr[512];
        printf("\xe2\x97\x8f live \xc2\xb7 \xe2\x8f\xb8 parked \xc2\xb7 late boxes append\n  DEVICE        DIR        START  PROMPT \xc2\xb7 LATEST\n");
        for (;;) { struct pollfd pf[64]; int np = 0;
            for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) { pf[np].fd = fch[i].fd; pf[np++].events = POLLIN; }
            if (fch[0].fd < 0 || !np) { f_filter();   /* ch0(statx) drained -> ms dump; rest append */
                for (int i = 0; i < f_n; i++) { FI *x = &FL[f_idx[i]]; if (pr[x-FL]) continue; pr[x-FL] = 1;
                    char lb[28]; lb[0] = 0; if (x->lbl[0]) snprintf(lb, 28, "[%.14s] ", x->lbl);
                    char ag[8]; f_when(x->mt, ag);
                    printf("%s %-14.14s%-10.10s %-7.6s%s%.36s%s%.56s\n", x->live ? "\xe2\x97\x8f" : "\xe2\x8f\xb8", x->host, bname(x->cwd), ag, lb, x->desc, x->tail[0] ? " \xc2\xb7 " : "", x->tail); }
                fflush(stdout); }
            if (!np) break; poll(pf, np, -1);
            for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) for (int j = 0; j < np; j++)
                if (pf[j].fd == fch[i].fd && (pf[j].revents & (POLLIN | POLLHUP | POLLERR))) f_recv(&fch[i]); }
        { char ob[512]; int n;                                            /* trailer: adb phones + silent boxes (web folds the ✗ line into a collapsed <details>) */
          for (int i = 0; i < fnch; i++) if (fch[i].ph) printf("\xf0\x9f\x93\xb1 %s \xc2\xb7 adb via %s (%s)\n", fch[i].host, DEV, fch[i].via);
          if ((n = f_offs(ob, 512))) printf("\xe2\x9c\x97 not accessible (%d): %s\n", n, ob); }
        if (dsm) { int nd = f_dismiss(); printf("\xe2\x9c\x93 dismissed %d parked \xc2\xb7 %d live kept\n", nd, FNN); }
        f_save(); return 0; }
    f_tty = open("/dev/tty", O_RDWR); if (f_tty < 0) f_tty = 0;         /* keyboard from /dev/tty, not the clobbered fd0 */
    tcgetattr(f_tty, &f_saved); struct termios r = f_saved; cfmakeraw(&r); tcsetattr(f_tty, TCSANOW, &r);
    (void)!write(1, "\033[?7l", 5);                                     /* no auto-wrap: long lines clip, never wrap/break the layout */
    signal(SIGWINCH, f_onwinch);
    int sel = 0;
    f_filter(); f_mode = f_n ? 0 : 1; f_tms = (double)rus / 1000.0;      /* default = the actual window; empty cache -> list until data lands */
    if (!f_mode) { f_cap_start(&FL[f_idx[sel]]); f_vpaint(sel); }
    else f_paint(sel, rus, fnch > 0);
    for (;;) {
        struct pollfd pf[67]; int np = 0;
        pf[np].fd = f_tty; pf[np++].events = POLLIN;
        int ci = -1; if (f_cfd >= 0) { ci = np; pf[np].fd = f_cfd; pf[np++].events = POLLIN; }
        for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) { pf[np].fd = fch[i].fd; pf[np++].events = POLLIN; }
        int pr = poll(pf, np, -1);
        if (f_winch) { f_winch = 0; sel = f_csel(sel);
            if (!f_mode) { f_vpaint(sel); continue; }
            (void)!write(1, "\033[H\033[2J", 7); f_paint(sel, rus, f_rem() > 0); continue; }
        if (pr <= 0) continue;
        sel = f_csel(sel);
        if (ci >= 0 && f_cfd >= 0 && (pf[ci].revents & (POLLIN | POLLHUP | POLLERR))) {   /* async capture: accumulate; on EOF paint if the row is still selected */
            int r = (int)read(f_cfd, f_cb + f_cbn, sizeof f_cb - 1 - (size_t)f_cbn);
            if (r > 0) f_cbn += r;
            if (r <= 0 || f_cbn >= (int)sizeof f_cb - 1) { close(f_cfd); f_cfd = -1; FREAP
                while (f_cbn > 0 && (f_cb[f_cbn - 1] == '\n' || f_cb[f_cbn - 1] == '\r' || f_cb[f_cbn - 1] == ' ')) f_cbn--;   /* capture pads the viewport with blank lines */
                struct timespec ce; clock_gettime(CLOCK_MONOTONIC, &ce);
                f_vms = (double)(ce.tv_sec - f_ct0.tv_sec) * 1e3 + (double)(ce.tv_nsec - f_ct0.tv_nsec) / 1e6;
                if (!f_mode && f_n && !strcmp(FL[f_idx[sel]].sid, f_csid)) { memcpy(f_vb, f_cb, (size_t)f_cbn); f_vn = f_cbn; f_vb[f_vn] = 0; f_vpaint(sel); } } }
        int dirty = 0, refetch = 0; struct timespec ka; ka.tv_sec = 0; ka.tv_nsec = 0;
        if (pf[0].revents & (POLLIN | POLLHUP)) { unsigned char k; int rr = (int)read(f_tty, &k, 1);
            clock_gettime(CLOCK_MONOTONIC, &ka);
            if (rr <= 0 || k == 3) break;
            else if (k == 27) {                                       /* Esc alone = clear filter / quit; else arrow/page */
                struct pollfd e = { f_tty, POLLIN, 0 };
                if (poll(&e, 1, 30) > 0) { unsigned char s[5]; int sn = (int)read(f_tty, s, 5);
                    if (sn >= 2 && s[0] == '[') { refetch = !f_mode;
                        if (s[1] == 'A') { if (sel > 0) sel--; } else if (s[1] == 'B') { if (sel + 1 < f_n) sel++; }
                        else if (s[1] == 'D' || s[1] == '5') { sel -= f_vh(); if (sel < 0) sel = 0; }                       /* ← / PgUp */
                        else if (s[1] == 'C' || s[1] == '6') { sel += f_vh(); if (sel >= f_n) sel = f_n ? f_n - 1 : 0; } }   /* → / PgDn */
                } else break;                                            /* Esc alone always exits (filter clearing = ⌫) */
                dirty = 1; }
            else if (k == '\r' || k == '\n') { if (f_n) f_attach(&FL[f_idx[sel]]); }        /* Enter = open (come back in) */
            else if (k == '\t') { f_mode ^= 1; refetch = !f_mode; dirty = 1; }              /* Tab: window view <-> list */
            else if (!f_mode) { if (k == 'j' && sel + 1 < f_n) { sel++; refetch = 1; } else if (k == 'k' && sel > 0) { sel--; refetch = 1; }
                else if (k == 'e') { f_archive(sel); sel = f_csel(sel); if (f_n) refetch = 1; else { f_vn = 0; f_vpaint(0); } }
                else if (k == 'E' && f_n) {                              /* archive ALL shown (filtered) rows — y/n confirm on the bar row */
                    dprintf(1, "\033[%d;1H\033[K\033[93marchive all %d shown? y/n\033[0m ", f_rows() - 1, f_n);
                    unsigned char ck; int cy = read(f_tty, &ck, 1) == 1 && (ck == 'y' || ck == 'Y');
                    clock_gettime(CLOCK_MONOTONIC, &ka);                 /* confirm wait is human time — measure from the y */
                    if (cy) {
                        while (f_n) { f_archive(0); f_filter(); }
                        f_filt[0] = 0; f_filt_n = 0; f_filter(); sel = 0; f_vn = 0;   /* drop the filter: show what's left */
                        if (f_n) refetch = 1; }
                    f_vpaint(sel); }
                else if (k == 'm' && f_n) { FI *x = &FL[f_idx[sel]]; char nb[40]; int nn = 0;   /* edit resume model; empty/Esc = keep previous (the default) */
                    dprintf(1, "\033[%d;1H\033[Kmodel [%s]: ", f_rows() - 1, x->mdl);
                    for (;;) { unsigned char ck; if (read(f_tty, &ck, 1) <= 0 || ck == 27) { nn = 0; break; }
                        if (ck == '\r' || ck == '\n') break;
                        if ((ck == 127 || ck == 8) && nn) { nn--; (void)!write(1, "\b \b", 3); }
                        else if (ck > 32 && ck < 127 && nn < 39) { nb[nn++] = (char)ck; (void)!write(1, &ck, 1); } }
                    if (nn) { nb[nn] = 0; snprintf(x->mdl, 40, "%s", nb); }
                    f_vpaint(sel); }
                else if (k == 'p' && f_n) { FI *x = &FL[f_idx[sel]];     /* park: kill THE sid's window (RAM freed); sid stays on disk, resumable */
                    if (x->live) { char sc[900]; FPAT(pat, x);
                        snprintf(sc, sizeof sc, FSID "[ -n \"$i\" ]&&tmux kill-window -t \"$i\"", pat, pat);
                        f_drain(f_sh(x->host, sc)); x->live = 0; }
                    f_vpaint(sel); }
                else if (k == 'D') {                                     /* dismiss ALL parked rows (live kept) — y/n confirm; CLI/web twin: a feed dismiss */
                    int nP = 0; for (int i = 0; i < FNN; i++) if (!FL[i].live) nP++;
                    dprintf(1, "\033[%d;1H\033[K\033[93mdismiss all %d parked? y/n\033[0m ", f_rows() - 1, nP);
                    unsigned char ck; int cy = read(f_tty, &ck, 1) == 1 && (ck == 'y' || ck == 'Y');
                    clock_gettime(CLOCK_MONOTONIC, &ka);                 /* confirm wait is human time — measure from the y */
                    if (cy && nP) { f_dismiss(); sel = 0; f_vn = 0; sel = f_csel(sel); if (f_n) refetch = 1; }
                    f_vpaint(sel); }
                else if (k == '/') { f_mode = 1; dirty = 1; }            /* omnibox: filter typing lives in list mode */
                else if (k == 'q') break; }
            else if (k == 127 || k == 8) { if (f_filt_n) { f_filt[--f_filt_n] = 0; sel = 0; f_top = 0; }
                else { f_mode = 0; refetch = 1; } dirty = 1; }            /* ⌫ past empty = exit filter to window view, j/k nav back (Sean 7/9) */
            else if (k >= 32 && k < 127) { if (f_filt_n < 62) { f_filt[f_filt_n++] = (char)k; f_filt[f_filt_n] = 0; sel = 0; f_top = 0; } dirty = 1; }
            else dirty = 1; }
        for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) for (int j = 1; j < np; j++)
            if (pf[j].fd == fch[i].fd && (pf[j].revents & (POLLIN | POLLHUP | POLLERR))) { f_recv(&fch[i]); dirty = 1; }
        if (refetch && f_n) { f_vn = 0; f_cap_start(&FL[f_idx[sel]]); f_vpaint(sel); if (ka.tv_sec) f_stamp(ka); }   /* instant frame (strip moves, ⟳ placeholder); content lands async */
        else if (dirty && f_mode) { sel = f_csel(sel); f_paint(sel, rus, f_rem() > 0); if (ka.tv_sec) f_stamp(ka); }
        else if (dirty && !f_mode && f_n) { for (int i = 0; i < f_n; i++) if (!strcmp(FL[f_idx[i]].sid, f_csid)) { sel = i; break; }   /* scan rows landed: strip refreshes in place, highlight follows the fetched sid */
            f_vpaint(sel); }
    }
    f_rst();
    if (f_cfd >= 0) close(f_cfd);
    for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) close(fch[i].fd);
    FREAP
    f_save();
    return 0;
}
