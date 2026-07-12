/* a feed — fleet agent review TUI (C): cache-seeded paint, live ps/ssh fan-out, type-to-search. */
#include <poll.h>

/* per-box scanner (runs locally; single-quoted = ssh-safe): emits  live|sid|cwd|started|model|desc */
#define FLIVE "LIVE=$(ps -eo args 2>/dev/null|grep -oE '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}')\n"
#define FRQG /* grok: session dir per sid, summary.json = meta; live = sid in argv (a grok launches --session-id) */ \
"for d in $(ls -td ~/.grok/sessions/*/*/ 2>/dev/null|head -15);do s=$(basename \"$d\");j=\"$d/summary.json\"\n" \
"c=$(grep -o '\"cwd\": \"[^\"]*' \"$j\" 2>/dev/null|head -1|cut -d'\"' -f4)\n" \
"m=$(grep -o '\"current_model_id\": \"[^\"]*' \"$j\" 2>/dev/null|cut -d'\"' -f4)\n" \
"p=$(grep -o '\"generated_title\": \"[^\"]*' \"$j\" 2>/dev/null|cut -d'\"' -f4)\n" \
"t=$(stat -c %W \"$d\" 2>/dev/null);[ \"${t:-0}\" -gt 0 ] 2>/dev/null||t=$(stat -c %Y \"$d\" 2>/dev/null||stat -f %B \"$d\" 2>/dev/null||echo 0)\n" \
"printf '%s|%s|%s|%s|%s|%s\\n' \"$(echo \"$LIVE\"|grep -q \"$s\"&&echo 1||echo 0)\" \"$s\" \"${c:-?}\" \"$t\" \"${m:-?}\" \"${p:-?}\";done\n"
static const char *FRQ =
FLIVE
"for j in $(ls -t ~/.claude/projects/*/*.jsonl 2>/dev/null|head -15);do s=$(basename \"$j\" .jsonl)\n"
"c=$(grep -o '\"cwd\":\"[^\"]*\"' \"$j\" 2>/dev/null|head -1|cut -d'\"' -f4)\n"
"m=$(grep -o '\"model\":\"[^\"]*\"' \"$j\" 2>/dev/null|tail -1|cut -d'\"' -f4)\n"
"p=$(grep -o '\"role\":\"user\",\"content\":\"[^\"]\\{1,46\\}' \"$j\" 2>/dev/null|grep -vE 'local-command|command-name'|head -1|sed 's/.*content\":\"//')\n"
"t=$(stat -c %W \"$j\" 2>/dev/null);[ \"${t:-0}\" -gt 0 ] 2>/dev/null||t=$(stat -c %Y \"$j\" 2>/dev/null||stat -f %B \"$j\" 2>/dev/null||echo 0)\n"   /* started = birth; fs/GNU without it -> mtime; mac BSD stat -f %B */
"printf '%s|%s|%s|%s|%s|%s\\n' \"$(echo \"$LIVE\"|grep -q \"$s\"&&echo 1||echo 0)\" \"$s\" \"${c:-?}\" \"$t\" \"${m:-?}\" \"${p:-?}\";done\n"
FRQG;
static const char *FRQL = FLIVE FRQG;                                    /* statx local: claude via f_lemit, grok via shell */

typedef struct { char host[40], cwd[256], sid[48], mdl[40], desc[160]; int live, seen; long mt; } FI;
static FI FL[512]; static int FNN, f_live_scan = 1, f_top, f_tty;
typedef struct { int fd; char host[40], buf[8192]; int blen; } FCh;
static FCh fch[64]; static int fnch;
static struct termios f_saved;
static volatile sig_atomic_t f_winch;
static void f_onwinch(int s) { (void)s; f_winch = 1; }
static char f_filt[64]; static int f_filt_n, f_idx[512], f_n, f_mode;
static char f_vb[131072]; static int f_vn; static double f_vms;
static char f_arc[65536]; static int f_arcn;                             /* archived sids (email 'e'): skipped on add, persisted in feed_arc.txt */

static void f_add(const char *host, int live, const char *cwd, const char *sid, long mt, const char *mdl, const char *desc) {
    if (f_arc[0] && strstr(f_arc, sid)) return;
    for (int i = 0; i < FNN; i++) if (!strcmp(FL[i].sid, sid)) {          /* dedup by session: patch in place (no rebuild → no reflash) */
        if (live && !FL[i].live) { FL[i].live = 1; snprintf(FL[i].host, 40, "%s", host); }
        if (f_live_scan && mt > 0) FL[i].mt = mt;                         /* adopt fresh start-time — cached rows carry the old mtime-era value */
        if (f_live_scan) FL[i].seen = 1; return; }
    if (FNN >= 512) return;
    FI *x = &FL[FNN++]; x->live = live; x->mt = mt; x->seen = f_live_scan;
    snprintf(x->host, 40, "%s", host); snprintf(x->cwd, 256, "%s", cwd); snprintf(x->sid, 48, "%s", sid);
    snprintf(x->mdl, 40, "%s", mdl);
    int o = 0; for (const char *p = desc; *p && o < 159; p++) {           /* sanitize: printable, collapse spaces */
        unsigned char ch = (unsigned char)*p; if (ch < 32 || ch > 126) ch = ' ';
        if (ch == ' ' && (o == 0 || x->desc[o - 1] == ' ')) continue; x->desc[o++] = (char)ch; }
    while (o && x->desc[o - 1] == ' ') o--; x->desc[o] = 0;
}

static void f_line(const char *host, char *l) {
    char *p1 = strchr(l, '|'); if (!p1) return; *p1 = 0;
    char *p2 = strchr(p1 + 1, '|'); if (!p2) return; *p2 = 0;
    char *p3 = strchr(p2 + 1, '|'); if (!p3) return; *p3 = 0;
    char *p4 = strchr(p3 + 1, '|'); if (!p4) return; *p4 = 0;
    char *p5 = strchr(p4 + 1, '|'); if (!p5) return; *p5 = 0;
    if (strlen(p1 + 1) >= 8) f_add(host, atoi(l), p2 + 1, p1 + 1, atol(p3 + 1), p4 + 1, p5 + 1);
}

static void f_b64(const char *in, char *out) {                           /* ship the scanner inline (no quotes) → any reachable box works, no deploy */
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int n = (int)strlen(in), o = 0;
    for (int i = 0; i < n; i += 3) {
        unsigned v = (unsigned)(unsigned char)in[i] << 16;
        if (i + 1 < n) v |= (unsigned)(unsigned char)in[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned)(unsigned char)in[i + 2];
        out[o++] = T[(v >> 18) & 63]; out[o++] = T[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? T[(v >> 6) & 63] : '='; out[o++] = (i + 2 < n) ? T[v & 63] : '=';
    }
    out[o] = 0;
}

/* resolve THE session's pane: sid-bearing proc (grep '[a]bc' form never matches its own argv) → climb ppids to a pane. $s = raw sid for scripts, $i = pane or empty */
#define FSID "s=$(printf %%s '%s'|tr -d '[]');p=$(grep -al '%s' /proc/[0-9]*/cmdline 2>/dev/null|cut -d/ -f3|head -1);" \
    "L=$(tmux list-panes -a -F '#{pane_pid} #{pane_id}' 2>/dev/null);" \
    "while [ -n \"$p\" ]&&[ \"$p\" -gt 1 ] 2>/dev/null;do i=$(echo \"$L\"|awk -v x=\"$p\" '$1==x{print $2;exit}');[ -n \"$i\" ]&&break;p=$(awk '{print $4}' /proc/$p/stat 2>/dev/null);done;"
#define FPAT(b, x) char b[56]; snprintf(b, 56, "[%c]%s", (x)->sid[0], (x)->sid + 1)
static int f_sh(const char *host, const char *sc) {                     /* run sc on a box (local or ssh); returns stdout fd, caller reads to EOF then reaps. setsid: detach from tty — ssh else reads /dev/tty and steals keystrokes */
    int pf[2]; if (pipe(pf)) return -1; pid_t pid = fork(); if (pid < 0) { close(pf[0]); close(pf[1]); return -1; }
    if (!pid) { setsid(); dup2(pf[1], 1); close(pf[0]); close(pf[1]); int dn = open("/dev/null", O_RDWR); if (dn >= 0) { dup2(dn, 0); dup2(dn, 2); if (dn > 2) close(dn); }
        if (strcmp(host, DEV)) { char b6[2600], cm[2700]; f_b64(sc, b6); snprintf(cm, 2700, "echo %s|base64 -d|bash", b6); execlp("a", "a", "ssh", host, cm, (char *)0); }
        execlp("bash", "bash", "-c", sc, (char *)0); _exit(127); }
    close(pf[1]); return pf[0];
}
static char f_cb[131072]; static int f_cbn, f_cfd = -1; static char f_csid[48]; static struct timespec f_ct0; static double f_tms;
static void f_cap_start(FI *x) {                                         /* 1ms mandate: spawn the fetch and return — keypress paints instantly, content lands via the poll loop */
    if (f_cfd >= 0) { close(f_cfd); f_cfd = -1; while (waitpid(-1, 0, WNOHANG) > 0) {} }   /* flipped again mid-fetch: drop the stale one */
    clock_gettime(CLOCK_MONOTONIC, &f_ct0);
    #define FTAIL "printf '\\033[33m\xe2\x8f\xb8 not live \xe2\x80\x94 saved transcript tail (\xe2\x86\xb5 resume)\\033[0m\\n';j=$(ls -t ~/.claude/projects/*/%s.jsonl ~/.grok/sessions/*/%s/chat_history.jsonl 2>/dev/null|head -1);" \
        "tail -c 80000 \"$j\" 2>/dev/null|grep -o '\"text\":\"[^\"]\\{1,400\\}'|sed 's/^\"text\":\"//;s/\\\\n/ /g'|tail -8"
    char sc[1200]; FPAT(pat, x);   /* parked: transcript only; live: THE session's pane by sid, transcript fallback */
    if (!x->live) snprintf(sc, sizeof sc, FTAIL, x->sid, x->sid);
    else snprintf(sc, sizeof sc, FSID "if [ -n \"$i\" ];then tmux capture-pane -ep -t \"$i\" 2>/dev/null;else " FTAIL ";fi", pat, pat, "$s", "$s");
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
static int f_rows(void) { struct winsize w; ioctl(1, TIOCGWINSZ, &w); return w.ws_row ? w.ws_row : 24; }
static void f_age(long t, char *o) {                                     /* time since started: 5m / 3h / 2d */
    long d = (long)time(0) - t; o[0] = 0; if (t <= 0 || d < 0) return;
    if (d < 3600) snprintf(o, 8, "%ldm", d / 60); else if (d < 86400) snprintf(o, 8, "%ldh", d / 3600); else snprintf(o, 8, "%ldd", d / 86400);
}
static void f_bar(void) {                                                /* bottom rows pinned (tui.md r3): omnibox = the place to type, menu = every live key (r4), key yellow + meaning gray */
    #define MI(k,l) "\033[1;93m" k "\033[0;90m" l
    char b[768]; int o = snprintf(b, 768, "\033[%d;1H\033[K\033[93m/%s\xe2\x96\x8f\033[0m\033[90m%s\033[0m\r\n\033[K%s\033[0m",
        f_rows() - 1, f_filt, f_filt[0] ? "" : (f_mode ? " type to filter" : " press / to filter"),
        f_mode ? MI(" \xe2\x86\xb5", " open  ") MI("Tab", " window  ") MI("\xe2\x86\x91\xe2\x86\x93", " move  ") MI("\xe2\x86\x90\xe2\x86\x92", " page  ") MI("\xe2\x8c\xab", " back  ") MI("esc", " quit")
               : MI(" \xe2\x86\xb5", " open  ") MI("j/k", " move  ") MI("p", " park  ") MI("m", " model  ") MI("e", " archive  ") MI("/", " find  ") MI("Tab", " list  ") MI("q", " quit"));
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
    struct winsize w; ioctl(1, TIOCGWINSZ, &w); int rows = w.ws_row ? w.ws_row : 24;
    int rem = 0; for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) rem++;
    char h[2048]; FI *x = f_n ? &FL[f_idx[sel]] : NULL; char ag[8]; f_age(x ? x->mt : 0, ag);
    int o = snprintf(h, 2048, "\033[H\033[1ma feed\033[0m \033[90mkey\xe2\x86\x92paint\033[0m\033[32m%8.4fms\033[0m \033[90m%d/%d%s\033[0m %s\033[97m%s\033[0m:%.8s \033[90m\xe2\x86\x91%s\033[0m \033[35m%.12s\033[0m\033[K\r\n",
        f_tms, f_n ? sel + 1 : 0, f_n, rem ? " \xe2\x80\xa6" : "", x && x->live ? "\033[32m\xe2\x97\x8f\033[0m " : "\xe2\x8f\xb8 ", x ? x->host : "?", x ? bname(x->cwd) : "", ag[0] ? ag : "?", x ? x->mdl : "");
    int ls = sel - 2; if (ls > f_n - 5) ls = f_n - 5; if (ls < 0) ls = 0; int shown = 0;
    for (int r = ls; r < ls + 5 && r < f_n; r++, shown++) { FI *y = &FL[f_idx[r]]; char ya[8]; f_age(y->mt, ya);
        o += snprintf(h + o, (size_t)(2048 - o), "%s%s %-4.4s%-12.12s %-8.8s %.40s\033[0m\033[K\r\n",
            r == sel ? "\033[7m" : "", y->live ? "\033[32m\xe2\x97\x8f\033[39m" : "\xe2\x8f\xb8", ya, y->host, bname(y->cwd), y->desc); }
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
static void f_jmeta(const char *jp, char *cwd, char *mdl, char *desc) {  /* new sid: head 256K = cwd + first real user msg; tail 256K = last model (FRQ grep parity, bounded — never the whole transcript) */
    static char hb[262144]; cwd[0] = mdl[0] = desc[0] = 0;
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
    for (int i = 0; i < nn; i++) {
        int knw = 0; for (int j = 0; j < FNN; j++) if (!strcmp(FL[j].sid, sid[i])) { knw = 1; break; }
        char cwd[256], mdl[40], desc[160]; if (knw) cwd[0] = mdl[0] = desc[0] = 0; else f_jmeta(nj[i].p, cwd, mdl, desc);   /* cached row: f_add only patches live/mt/seen — skip the reads */
        dprintf(1, "%d|%s|%s|%ld|%s|%s\n", lv[i], sid[i], cwd[0] ? cwd : "?", nj[i].bt, mdl[0] ? mdl : "?", desc[0] ? desc : "?"); }
}
static void f_spawn_local(void) {                                        /* local FRQ replacement: fork straight into a channel — no bash, no whole-file greps; scans while f_hosts popen-parses the registry */
    if (fnch >= 64) return; int pf[2]; if (pipe(pf)) return;
    pid_t pid = fork(); if (pid < 0) { close(pf[0]); close(pf[1]); return; }
    if (!pid) { dup2(pf[1], 1); close(pf[0]); close(pf[1]); f_lemit(); _exit(0); }
    close(pf[1]); FCh *ch = &fch[fnch++]; ch->fd = pf[0]; ch->blen = 0; ch->buf[0] = 0;
    snprintf(ch->host, 40, "%s", DEV);
}
#endif
static void f_spawn(const char *host, const char *sc) {                  /* host=NULL → local box */
    if (fnch >= 64) return; int fd = f_sh(host ? host : DEV, sc); if (fd < 0) return;
    FCh *ch = &fch[fnch++]; ch->fd = fd; ch->blen = 0; ch->buf[0] = 0;
    snprintf(ch->host, 40, "%s", host ? host : DEV);
}

static void f_hosts(void) {                                              /* local + every registry box (lan/wan/usb variants deduped) */
#if defined(__linux__) && defined(STATX_BTIME)
    f_spawn_local(); f_spawn(NULL, FRQL);
#else
    f_spawn(NULL, FRQ);                                                  /* no /proc|statx → local via FRQ */
#endif
    char cmd[B]; snprintf(cmd, B, "grep -h '^Name:' %s/ssh/*.txt 2>/dev/null|sed 's/Name: //'|sed -E 's/-(lan|wan|usb|hot|relay)$//'|sort -u|grep -vx %s", SROOT, DEV);
    FILE *p = popen(cmd, "r"); char ln[128];
    while (p && fgets(ln, 128, p)) { ln[strcspn(ln, "\n")] = 0; if (ln[0]) f_spawn(ln, FRQ); }
    if (p) pclose(p);
}

static int f_recv(FCh *ch) {
    int rd = (int)read(ch->fd, ch->buf + ch->blen, (int)sizeof(ch->buf) - 1 - ch->blen);
    if (rd <= 0) { close(ch->fd); ch->fd = -1; return 0; }
    ch->blen += rd; ch->buf[ch->blen] = 0; char *nl;
    while ((nl = strchr(ch->buf, '\n'))) { *nl = 0; f_line(ch->host, ch->buf);
        int rem = ch->blen - (int)(nl + 1 - ch->buf); memmove(ch->buf, nl + 1, (size_t)rem + 1); ch->blen = rem; }
    return 1;
}

static int f_vh(void) { int rows = f_rows(); int vh = rows >= 14 ? rows - 5 : rows - 3; return vh < 1 ? 1 : vh; }
static void f_filter(void) {
    f_n = 0;
    for (int i = 0; i < FNN; i++) {
        if (f_filt[0]) { char hay[480]; snprintf(hay, sizeof hay, "%s %s %s", FL[i].host, FL[i].cwd, FL[i].desc); if (!strcasestr(hay, f_filt)) continue; }
        f_idx[f_n++] = i;
    }
    for (int i = 1; i < f_n; i++) { int t = f_idx[i], j = i - 1;         /* newest started first (Sean 7/10) */
        while (j >= 0 && FL[f_idx[j]].mt < FL[t].mt) { f_idx[j + 1] = f_idx[j]; j--; } f_idx[j + 1] = t; }
}

static void f_load(void) {                                               /* seed from last run's data → first frame is the real, interactive list */
    char fp[P]; snprintf(fp, P, "%s/feed.dat", DDIR); FILE *f = fopen(fp, "r"); if (!f) return;
    char ln[1280]; while (fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\n")] = 0;
        char *p[6], *s = ln; int ok = 1; for (int i = 0; i < 6; i++) { char *q = strchr(s, '|'); if (!q) { ok = 0; break; } *q = 0; p[i] = s; s = q + 1; }
        if (ok && strlen(p[2]) >= 8) f_add(p[0], 0, p[3], p[2], atol(p[4]), p[5], s); }   /* seed as parked; a live scan re-greens it */
    fclose(f);
}

static void f_save(void) {
    char fp[P]; snprintf(fp, P, "%s/feed.dat", DDIR); FILE *f = fopen(fp, "w"); if (!f) return;
    for (int i = 0; i < FNN; i++) { FI *x = &FL[i]; if (x->seen) fprintf(f, "%s|%d|%s|%s|%ld|%s|%s\n", x->host, x->live, x->sid, x->cwd, x->mt, x->mdl, x->desc); }
    fclose(f);
}

static void f_paint(int sel, long rus, int refreshing) {
    int rows; { struct winsize ws; ioctl(1, TIOCGWINSZ, &ws); rows = ws.ws_row ? ws.ws_row : 24; }
    int vh = f_vh(), n = f_n, extra = rows >= 14;
    if (sel >= n) sel = n ? n - 1 : 0; if (sel < 0) sel = 0;
    if (sel < f_top) f_top = sel; else if (sel >= f_top + vh) f_top = sel - vh + 1;     /* edge-scroll: cursor moves in view, list scrolls only at edges */
    if (f_top > n - vh) f_top = n > vh ? n - vh : 0; if (f_top < 0) f_top = 0;
    (void)rus;
    char fb[1 << 16]; int o = 0;
    #define AP(...) o += snprintf(fb + o, (int)sizeof(fb) - o, __VA_ARGS__)
    #define EOL "\033[K\r\n"                                            /* home + per-line clear-to-EOL = in-place redraw, no flicker */
    AP("\033[H\033[1ma feed\033[0m \033[90mkey\xe2\x86\x92paint\033[0m\033[32m%8.4fms\033[0m \033[90m%d/%d%s\033[0m", f_tms, n ? sel + 1 : 0, n, refreshing ? " \xe2\x80\xa6" : "");
    if (n) AP(" \033[35m%.14s\033[0m", FL[f_idx[sel]].mdl); AP(EOL);
    if (extra) {
        AP("\033[32m\xe2\x97\x8f\033[0m\033[90m live \033[0m\xe2\x8f\xb8\033[90m parked \xc2\xb7 newest first\033[0m" EOL);
        AP("\033[90m    %-4s%-10s %-8s %s\033[0m" EOL, "AGE", "BOX", "DIR", "LATEST MESSAGE"); }
    for (int r = 0; r < vh; r++) { int li = f_top + r;
        if (li >= n) { AP(EOL); continue; }
        FI *x = &FL[f_idx[li]]; const char *b = strrchr(x->cwd, '/'); b = (b && b[1]) ? b + 1 : x->cwd;
        char ag[8]; f_age(x->mt, ag);
        char line[320]; snprintf(line, 320, "%s %-4.4s%-10.10s %-8.8s %s", x->live ? "\033[32m\xe2\x97\x8f\033[39m" : "\xe2\x8f\xb8", ag, x->host, b, x->desc);
        if (li == sel) AP("\033[7m  %s\033[0m" EOL, line); else AP("  %s" EOL, line); }
    if (o >= 2 && fb[o - 1] == '\n') o -= 2;                            /* drop trailing CRLF: a newline on the last row scrolls the title off the top */
    AP("\033[J");
    #undef AP
    #undef EOL
    (void)!write(1, fb, (size_t)o);
    f_bar();
}

static void f_attach(FI *x) {                                            /* parked → resume window; live → THE sid's window. local jumps via switch-client (grouped sessions don't follow select-window); remote selects on the box then attaches */
    tcsetattr(f_tty, TCSANOW, &f_saved); printf("\033[?7h\033[H\033[2J"); fflush(stdout);
    int lo = !strcmp(x->host, DEV);
    if (!x->live) { char wn[20]; snprintf(wn, 20, "r-%.8s", x->sid);
        const char *md = (x->mdl[0] && strcmp(x->mdl, "?")) ? x->mdl : (*cfget("m_model") ? cfget("m_model") : "opus");   /* default = model the session used before */
        int gk = !strncmp(x->mdl, "grok", 4);                            /* grok row: model field names the binary */
        if (lo) { char sc[900]; snprintf(sc, sizeof sc, "w=$(tmux new-window -dP -t 'a:' -c '%s' -n '%s' -F '#{pane_id}' -- %s '%s' --resume '%s');exec tmux switch-client -t \"$w\"", x->cwd, wn,
                gk ? "grok --always-approve -m" : "claude --dangerously-skip-permissions --model", md, x->sid);
            execlp("bash", "bash", "-c", sc, (char *)0); _exit(127); }
        if (fork() == 0) { execlp("a", "a", "ssh", x->host, "tmux", "new-window", "-t", "a", "-c", x->cwd, "-n", wn,
            gk ? "grok" : "claude", gk ? "--always-approve" : "--dangerously-skip-permissions", gk ? "-m" : "--model", md, "--resume", x->sid, (char *)0); _exit(127); }
        wait(0); }
    else { char sc[900]; FPAT(pat, x);
        if (lo) { snprintf(sc, sizeof sc, FSID "[ -n \"$i\" ]&&exec tmux switch-client -t \"$i\"", pat, pat);
            execlp("bash", "bash", "-c", sc, (char *)0); _exit(127); }
        snprintf(sc, sizeof sc, FSID "[ -n \"$i\" ]&&tmux select-window -t \"$i\"", pat, pat);   /* aim the box's session first; the attach below lands on it */
        int fd = f_sh(x->host, sc); if (fd >= 0) { char t[64]; while (read(fd, t, 64) > 0) {} close(fd); while (waitpid(-1, 0, WNOHANG) > 0) {} } }
    execlp("a", "a", "ssh", x->host, (char *)0); _exit(127);
}

static int cmd_feed(int c, char **v) { (void)c; (void)v; perf_disarm();
    init_db(); load_cfg();                                               /* m_model = resume fallback when a session has no model yet */
    FNN = fnch = 0; f_top = 0;
    { char ap[P]; snprintf(ap, P, "%s/feed_arc.txt", DDIR); FILE *af = fopen(ap, "r"); if (af) { f_arcn = (int)fread(f_arc, 1, sizeof f_arc - 1, af); f_arc[f_arcn] = 0; fclose(af); } }
    f_live_scan = 0; f_load(); f_live_scan = 1;                           /* seed from last run (parked until a live scan confirms) */
    struct timespec tp; clock_gettime(CLOCK_MONOTONIC, &tp);
    long rus = (tp.tv_sec - _t0.tv_sec) * 1000000L + (tp.tv_nsec - _t0.tv_nsec) / 1000;
    f_hosts();
    if (!isatty(0) || !isatty(1)) {                                      /* non-tty: drain all, dump */
        int alive = 1; while (alive) { struct pollfd pf[64]; int np = 0, a = 0;
            for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) { pf[np].fd = fch[i].fd; pf[np++].events = POLLIN; a++; }
            if (!a) break; poll(pf, np, 200);
            for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) for (int j = 0; j < np; j++)
                if (pf[j].fd == fch[i].fd && (pf[j].revents & (POLLIN | POLLHUP | POLLERR))) f_recv(&fch[i]); }
        printf("\xe2\x97\x8f live now \xc2\xb7 \xe2\x8f\xb8 parked, resumable \xc2\xb7 newest started first (a feed in a terminal: \xe2\x86\xb5 attaches)\n  %-14s%-10s%-5s%s\n", "DEVICE", "DIR", "AGE", "LATEST MESSAGE");
        f_filter();
        for (int i = 0; i < f_n; i++) { FI *x = &FL[f_idx[i]]; const char *b = strrchr(x->cwd, '/'); b = (b && b[1]) ? b + 1 : x->cwd;
            char ag[8]; f_age(x->mt, ag);
            printf("%s %-14.14s%-10.10s%-5.4s%.48s\n", x->live ? "\xe2\x97\x8f" : "\xe2\x8f\xb8", x->host, b, ag, x->desc); }
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
        if (f_winch) { f_winch = 0; f_filter(); if (sel >= f_n) sel = f_n ? f_n - 1 : 0;
            if (!f_mode) { f_vpaint(sel); continue; }
            (void)!write(1, "\033[H\033[2J", 7); int rem = 0; for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) rem++; f_paint(sel, rus, rem > 0); continue; }
        if (pr <= 0) continue;
        f_filter(); if (sel >= f_n) sel = f_n ? f_n - 1 : 0;
        if (ci >= 0 && f_cfd >= 0 && (pf[ci].revents & (POLLIN | POLLHUP | POLLERR))) {   /* async capture: accumulate; on EOF paint if the row is still selected */
            int r = (int)read(f_cfd, f_cb + f_cbn, sizeof f_cb - 1 - (size_t)f_cbn);
            if (r > 0) f_cbn += r;
            if (r <= 0 || f_cbn >= (int)sizeof f_cb - 1) { close(f_cfd); f_cfd = -1; while (waitpid(-1, 0, WNOHANG) > 0) {}
                while (f_cbn > 0 && (f_cb[f_cbn - 1] == '\n' || f_cb[f_cbn - 1] == '\r' || f_cb[f_cbn - 1] == ' ')) f_cbn--;   /* capture pads the viewport with blank lines */
                struct timespec ce; clock_gettime(CLOCK_MONOTONIC, &ce);
                f_vms = (double)(ce.tv_sec - f_ct0.tv_sec) * 1e3 + (double)(ce.tv_nsec - f_ct0.tv_nsec) / 1e6;
                if (!f_mode && f_n && !strcmp(FL[f_idx[sel]].sid, f_csid)) { memcpy(f_vb, f_cb, (size_t)f_cbn); f_vn = f_cbn; f_vb[f_vn] = 0; f_vpaint(sel); } } }
        int dirty = 0, refetch = 0; struct timespec ka; ka.tv_sec = 0; ka.tv_nsec = 0;
        if (pf[0].revents & (POLLIN | POLLHUP)) { unsigned char k; int rr = read(f_tty, &k, 1);
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
                else if (k == 'e') { f_archive(sel); f_filter(); if (sel >= f_n) sel = f_n ? f_n - 1 : 0; if (f_n) refetch = 1; else { f_vn = 0; f_vpaint(0); } }
                else if (k == 'm' && f_n) { FI *x = &FL[f_idx[sel]]; char nb[40]; int nn = 0;   /* edit resume model; empty/Esc = keep previous (the default) */
                    dprintf(1, "\033[%d;1H\033[Kmodel [%s]: ", f_rows() - 1, x->mdl);
                    for (;;) { unsigned char ck; if (read(f_tty, &ck, 1) <= 0 || ck == 27) { nn = 0; break; }
                        if (ck == '\r' || ck == '\n') break;
                        if ((ck == 127 || ck == 8) && nn) { nn--; (void)!write(1, "\b \b", 3); }
                        else if (ck > 32 && ck < 127 && nn < 39) { nb[nn++] = (char)ck; (void)!write(1, &ck, 1); } }
                    if (nn) { nb[nn] = 0; snprintf(x->mdl, 40, "%s", nb); }
                    f_vpaint(sel); }
                else if (k == 'p' && f_n) { FI *x = &FL[f_idx[sel]];     /* park: kill THE sid's window (RAM freed); sid stays on disk, resumable */
                    if (x->live) { char sc[900], t[64]; FPAT(pat, x);
                        snprintf(sc, sizeof sc, FSID "[ -n \"$i\" ]&&tmux kill-window -t \"$i\"", pat, pat);
                        int fd = f_sh(x->host, sc); if (fd >= 0) { while (read(fd, t, 64) > 0) {} close(fd); while (waitpid(-1, 0, WNOHANG) > 0) {} }
                        x->live = 0; }
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
        else if (dirty && f_mode) { f_filter(); if (sel >= f_n) sel = f_n ? f_n - 1 : 0; int rem = 0; for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) rem++; f_paint(sel, rus, rem > 0); if (ka.tv_sec) f_stamp(ka); }
        else if (dirty && !f_mode && f_n) { for (int i = 0; i < f_n; i++) if (!strcmp(FL[f_idx[i]].sid, f_csid)) { sel = i; break; }   /* scan rows landed: strip refreshes in place, highlight follows the fetched sid */
            f_vpaint(sel); }
    }
    tcsetattr(f_tty, TCSANOW, &f_saved); printf("\033[?7h\033[H\033[2J"); fflush(stdout);
    if (f_cfd >= 0) close(f_cfd);
    for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) close(fch[i].fd);
    while (waitpid(-1, 0, WNOHANG) > 0) {}
    f_save();
    return 0;
}
