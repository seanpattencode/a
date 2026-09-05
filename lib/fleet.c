/* a fleet — device status table: sync / agents / ram, one row per box, rows stream as boxes answer.
   ssh + adb probes ALL launch at t0 and race in one poll loop (Sean 7/15: parallel, fast as possible);
   ssh keeps row priority: an adb result buffers until its ssh sibling succeeds (dropped) or fails (printed).
   TUI is the primary surface; serve's GET /fleet mirrors the cache this writes (adata/local/fleet.txt). */
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
static const char *FLQ =
"m=$(if [ -r /proc/meminfo ];then awk '/MemAvailable/{a=$2}/MemTotal/{t=$2}END{printf \"%.1f/%.1fG\",a/1048576,t/1048576}' /proc/meminfo;"
"else echo \"$(vm_stat 2>/dev/null|awk '/Pages free|Pages inactive/{s+=$3}END{printf \"%.1f\",s*4096/1073741824}')/$(sysctl -n hw.memsize 2>/dev/null|awk '{printf \"%.1fG\",$1/1073741824}')\";fi)\n"
"ag=$(( $(pgrep -x claude 2>/dev/null|wc -l)+$(pgrep -x grok 2>/dev/null|wc -l) ))\n"
"s=$(cd ~/a/adata/git 2>/dev/null&&{ w=;[ -d .git/rebase-merge ]&&w=wedged;[ -f .git/MERGE_HEAD ]&&w=mid-merge;"
"set -- $(git rev-list --left-right --count origin/main...HEAD 2>/dev/null);b=${1:-0};a=${2:-0};"
"if [ -n \"$w\" ];then echo \"STUCK: $w\";elif [ \"$b\" -gt 200 ]||[ \"$a\" -gt 50 ];then echo \"STUCK: $a to push, $b to pull\";"
"elif [ \"$b\" = 0 ]&&[ \"$a\" = 0 ];then echo synced;else echo \"$a to push, $b to pull\";fi;}||echo no-adata)\n"
"printf '%s|%s|%s\\n' \"$s\" \"$ag\" \"$m\"";

typedef struct { int fd, typ, st; char h[40], buf[160]; } FLCH;          /* typ 0=ssh/loc 1=adb · st 0=pending 1=shown 2=failed 3=buffered */
static FLCH fchn[64]; static int fln; static FILE *flcf;
static void fl_row(const char *nm, int typ, char *b) {                   /* b = "sync|agents|ram" */
    char *q1 = strchr(b, '|'), *q2 = q1 ? strchr(q1 + 1, '|') : 0;
    const char *via = typ ? "adb" : (strcmp(nm, DEV) ? "ssh" : "loc");
    #define FROW(...) do { printf(__VA_ARGS__); if (flcf) fprintf(flcf, __VA_ARGS__); } while (0)
    if (q1 && q2) { *q1 = *q2 = 0; FROW("%-14s %-4s %-26s %6s  %s\n", nm, via, b, q1 + 1, q2 + 1); }
    else FROW("%-14s %-4s %s\n", nm, via, b);
}
static FLCH *fl_sib(FLCH *x) { for (int i = 0; i < fln; i++) if (&fchn[i] != x && !strcasecmp(fchn[i].h, x->h)) return &fchn[i]; return 0; }
static int fl_shown(const char *nm) { for (int i = 0; i < fln; i++) if (fchn[i].st == 1 && !strcasecmp(fchn[i].h, nm)) return 1; return 0; }
static void fl_off(FLCH *x, int adbtried) {
    if (fl_shown(x->h)) return;
    FROW("%-14s %-4s offline%s\n", x->h, "-", adbtried ? " (adb tried)" : ""); x->st = 1;
}

static int cmd_fleet(int argc, char **argv) { (void)argc; (void)argv; perf_disarm();
    if(argc>2&&!strcmp(argv[2],"web")){(void)!system("a ui on >/dev/null 2>&1");bg_exec(OPENER,"http://localhost:1111/fw");puts("\xe2\x9c\x93 localhost:1111/fw \xe2\x80\x94 every device's tmux, one page");return 0;}
    init_db(); load_cfg();                                               /* SROOT/DDIR live behind init */
    fln = 0; char cmd[B], ln[128];
    int fdl = f_sh(DEV, FLQ); if (fdl >= 0) { fchn[fln].fd = fdl; fchn[fln].typ = 0; fchn[fln].st = 0; snprintf(fchn[fln].h, 40, "%s", DEV); fln++; }
    snprintf(cmd, B, "grep -h '^Name:' %s/ssh/*.txt 2>/dev/null|sed 's/Name: //'|sed -E 's/-(lan|wan|usb|hot|relay)$//'|sort -fu", SROOT);   /* -f: HSU/hsu are one box */
    FILE *p = popen(cmd, "r");
    while (p && fgets(ln, 128, p) && fln < 48) { ln[strcspn(ln, "\n")] = 0;
        if (!ln[0] || !strcasecmp(ln, DEV)) continue;
        int fd = f_sh(ln, FLQ); if (fd >= 0) { fchn[fln].fd = fd; fchn[fln].typ = 0; fchn[fln].st = 0; snprintf(fchn[fln].h, 40, "%s", ln); fln++; } }
    if (p) pclose(p);
    snprintf(cmd, B, "ls %s/adb/*.txt 2>/dev/null", SROOT); p = popen(cmd, "r");   /* adb probes launch NOW too — same poll loop, no serial phase */
    while (p && fgets(ln, 128, p) && fln < 64) { ln[strcspn(ln, "\n")] = 0;
        char nm[40] = "", sr[64] = "", wl[64] = ""; FILE *df = fopen(ln, "r"); if (!df) continue;
        char l2[128]; while (fgets(l2, 128, df)) { sscanf(l2, "Name: %39s", nm); sscanf(l2, "Serial: %63s", sr); sscanf(l2, "Wireless: %63[0-9.:]", wl); }
        fclose(df); if (!nm[0] || !sr[0]) continue;
        char sc[B]; const char *w = wl[0] ? wl : "@@none@@";
        snprintf(sc, B, "command -v adb >/dev/null||exit 0;dl=$(adb devices 2>/dev/null|awk '/\\tdevice$/{print $1}');"
            "echo \"$dl\"|grep -qE '%s|%s'||{ timeout 4 adb connect '%s' >/dev/null 2>&1;dl=$(adb devices 2>/dev/null|awk '/\\tdevice$/{print $1}');};"
            "S='%s';echo \"$dl\"|grep -q \"$S\"||S='%s';echo \"$dl\"|grep -q \"$S\"||exit 0;"
            "timeout 6 adb -s \"$S\" shell \"awk '/MemAvailable/{a=\\$2}/MemTotal/{t=\\$2}END{printf \\\"%%.1f/%%.1fG\\\",a/1048576,t/1048576}' /proc/meminfo;echo -n '|';ps -A 2>/dev/null|grep -cE 'claude|grok'\" 2>/dev/null"
            "|awk -F'|' 'NF>1{printf \"(no sync info over adb)|%%s|%%s\\n\",$2,$1}'", sr, w, w, sr, w);
        int fd = f_sh(DEV, sc); if (fd < 0) continue;
        fchn[fln].fd = fd; fchn[fln].typ = 1; fchn[fln].st = 0; snprintf(fchn[fln].h, 40, "%s", nm); fln++; }
    if (p) pclose(p);
    char cp[P]; snprintf(cp, P, "%s/fleet.txt", DDIR); flcf = fopen(cp, "w");
    FROW("%-14s %-4s %-26s %6s  %s\n", "DEVICE", "VIA", "SYNC (vs last fetch)", "AGENTS", "RAM free/total");
    time_t t0 = time(0); int open_ = fln;
    while (open_ > 0 && time(0) - t0 < 25) {
        struct pollfd pf[64]; for (int i = 0; i < fln; i++) { pf[i].fd = fchn[i].fd; pf[i].events = POLLIN; }
        if (poll(pf, (nfds_t)fln, 1000) < 0) break;
        for (int i = 0; i < fln; i++) { FLCH *x = &fchn[i]; if (x->fd < 0 || !(pf[i].revents & (POLLIN | POLLHUP))) continue;
            char b[256]; int r = (int)read(x->fd, b, 255); close(x->fd); x->fd = -1; open_--;
            FLCH *s = fl_sib(x);
            if (r > 0) { b[r] = 0; b[strcspn(b, "\n")] = 0;
                if (x->typ == 0) { if (!fl_shown(x->h)) { fl_row(x->h, 0, b); x->st = 1; } }   /* ssh wins; late buffered adb is dropped */
                else if (fl_shown(x->h)) x->st = 2;
                else if (s && s->st == 0) { snprintf(x->buf, 160, "%s", b); x->st = 3; }       /* hold for the ssh verdict */
                else { fl_row(x->h, 1, b); x->st = 1; } }
            else { x->st = 2;
                if (x->typ == 0) { if (s && s->st == 3) { fl_row(s->h, 1, s->buf); s->st = 1; }   /* ssh dead → show the buffered adb row NOW */
                    else if (!s) fl_off(x, 0); else if (s->st == 2) fl_off(x, 1); }
                else if (s && s->st == 2) fl_off(x, 1); } } }
    for (int i = 0; i < fln; i++) { FLCH *x = &fchn[i]; if (x->fd >= 0) { close(x->fd); x->st = 2; } }
    for (int i = 0; i < fln; i++) { FLCH *x = &fchn[i]; if (x->typ == 0 && !fl_shown(x->h)) { FLCH *s = fl_sib(x);
        if (s && s->st == 3) { fl_row(s->h, 1, s->buf); s->st = 1; } else fl_off(x, s ? 1 : 0); } }
    #undef FROW
    if (flcf) { fclose(flcf); flcf = 0; }
    while (waitpid(-1, 0, WNOHANG) > 0) {}
    return 0;
}
