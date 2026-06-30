/* a feed — fleet agent work to judge. Native C: instant cache-paint on launch, then live rows
   refresh in as each box answers (fork `a ssh <box> a feedscan` per box, parse pipe rows).
   ● live (running now) · ⏸ parked (killed, resumable). j/k page · ↑↓ item · d diff · enter/a attach · q quit. */
#include <poll.h>

/* per-box scanner (runs LOCALLY on each box → single-quoted shell, no ssh-wrap escaping): emits  live|sid|cwd|mtime|desc */
static const char *FRQ =
"LIVE=$(ps -eo args 2>/dev/null|grep -oE '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}')\n"
"for j in $(ls -t ~/.claude/projects/*/*.jsonl 2>/dev/null|head -15);do s=$(basename \"$j\" .jsonl)\n"
"c=$(grep -o '\"cwd\":\"[^\"]*\"' \"$j\" 2>/dev/null|head -1|cut -d'\"' -f4)\n"
"p=$(grep -o '\"role\":\"user\",\"content\":\"[^\"]\\{1,46\\}' \"$j\" 2>/dev/null|grep -vE 'local-command|command-name'|head -1|sed 's/.*content\":\"//')\n"
"printf '%s|%s|%s|%s|%s\\n' \"$(echo \"$LIVE\"|grep -q \"$s\"&&echo 1||echo 0)\" \"$s\" \"${c:-?}\" \"$(stat -c %Y \"$j\" 2>/dev/null||echo 0)\" \"${p:-?}\";done";

static int cmd_feedscan(int c, char **v) { (void)c; (void)v; execlp("bash", "bash", "-c", FRQ, (char *)0); _exit(127); }

typedef struct { char host[40], cwd[256], sid[48], desc[160]; int live, seen; long mt; char diff[1600]; } FI;
static FI FL[512]; static int FNN, f_live_scan = 1, f_top, f_tty;   /* seen=confirmed by a live scan this run (else pruned); f_top persists the viewport; f_tty=/dev/tty input */
typedef struct { int fd; char host[40], buf[8192]; int blen; } FCh;
static FCh fch[64]; static int fnch;
static struct termios f_saved;
static volatile sig_atomic_t f_winch;                                    /* pane was resized → repaint at the new size */
static void f_onwinch(int s) { (void)s; f_winch = 1; }

static void f_add(const char *host, int live, const char *cwd, const char *sid, long mt, const char *desc) {
    for (int i = 0; i < FNN; i++) if (!strcmp(FL[i].sid, sid)) {          /* dedup by session: patch the existing row IN PLACE (no rebuild → no reflash) */
        if (live && !FL[i].live) { FL[i].live = 1; snprintf(FL[i].host, 40, "%s", host); }
        if (f_live_scan) FL[i].seen = 1; return; }
    if (FNN >= 512) return;
    FI *x = &FL[FNN++]; x->live = live; x->mt = mt; x->diff[0] = 0; x->seen = f_live_scan;
    snprintf(x->host, 40, "%s", host); snprintf(x->cwd, 256, "%s", cwd); snprintf(x->sid, 48, "%s", sid);
    int o = 0; for (const char *p = desc; *p && o < 159; p++) {           /* sanitize: printable, collapse runs of space */
        unsigned char ch = (unsigned char)*p; if (ch < 32 || ch > 126) ch = ' ';
        if (ch == ' ' && (o == 0 || x->desc[o - 1] == ' ')) continue; x->desc[o++] = (char)ch; }
    while (o && x->desc[o - 1] == ' ') o--; x->desc[o] = 0;
}

static void f_line(const char *host, char *l) {                          /* parse one  live|sid|cwd|mt|desc  row */
    char *p1 = strchr(l, '|'); if (!p1) return; *p1 = 0;
    char *p2 = strchr(p1 + 1, '|'); if (!p2) return; *p2 = 0;
    char *p3 = strchr(p2 + 1, '|'); if (!p3) return; *p3 = 0;
    char *p4 = strchr(p3 + 1, '|'); if (!p4) return; *p4 = 0;
    if (strlen(p1 + 1) >= 8) f_add(host, atoi(l), p2 + 1, p1 + 1, atol(p3 + 1), p4 + 1);
}

static void f_spawn(const char *host) {                                  /* host=NULL → local box */
    if (fnch >= 64) return; int pf[2]; if (pipe(pf)) return;
    pid_t pid = fork(); if (pid < 0) { close(pf[0]); close(pf[1]); return; }
    if (pid == 0) { setsid();                                            /* detach from the controlling tty — ssh else reads /dev/tty and steals keystrokes */
        dup2(pf[1], 1); close(pf[0]); close(pf[1]);
        int dn = open("/dev/null", O_RDWR); if (dn >= 0) { dup2(dn, 0); dup2(dn, 2); if (dn > 2) close(dn); }
        if (host) execlp("a", "a", "ssh", host, "a", "feedscan", (char *)0);
        else execlp("bash", "bash", "-c", FRQ, (char *)0);
        _exit(127); }
    close(pf[1]); FCh *ch = &fch[fnch++]; ch->fd = pf[0]; ch->blen = 0; ch->buf[0] = 0;
    snprintf(ch->host, 40, "%s", host ? host : DEV);
}

static void f_hosts(void) {                                              /* local + every registry box (lan/wan/usb variants deduped) */
    f_spawn(NULL);
    char cmd[B]; snprintf(cmd, B, "grep -h '^Name:' %s/ssh/*.txt 2>/dev/null|sed 's/Name: //'|sed -E 's/-(lan|wan|usb|hot|relay)$//'|sort -u|grep -vx %s", SROOT, DEV);
    FILE *p = popen(cmd, "r"); char ln[128];
    while (p && fgets(ln, 128, p)) { ln[strcspn(ln, "\n")] = 0; if (ln[0]) f_spawn(ln); }
    if (p) pclose(p);
}

static int f_recv(FCh *ch) {                                             /* drain ready bytes; parse complete lines. ret 0 on EOF */
    int rd = (int)read(ch->fd, ch->buf + ch->blen, (int)sizeof(ch->buf) - 1 - ch->blen);
    if (rd <= 0) { close(ch->fd); ch->fd = -1; return 0; }
    ch->blen += rd; ch->buf[ch->blen] = 0; char *nl;
    while ((nl = strchr(ch->buf, '\n'))) { *nl = 0; f_line(ch->host, ch->buf);
        int rem = ch->blen - (int)(nl + 1 - ch->buf); memmove(ch->buf, nl + 1, (size_t)rem + 1); ch->blen = rem; }
    return 1;
}

static char f_filt[64]; static int f_filt_n, f_idx[512], f_n;            /* type-to-search filter + the matching item indices */
static int f_vh(void) { struct winsize w; ioctl(1, TIOCGWINSZ, &w); int rows = (w.ws_row ? w.ws_row : 24); int vh = (rows >= 14 ? rows - 7 : rows - 1); return vh < 1 ? 1 : vh; }
static void f_filter(void) {                                             /* rebuild f_idx[] = items whose host/dir/msg contain the filter */
    f_n = 0;
    for (int i = 0; i < FNN; i++) {
        if (f_filt[0]) { char hay[480]; snprintf(hay, sizeof hay, "%s %s %s", FL[i].host, FL[i].cwd, FL[i].desc); if (!strcasestr(hay, f_filt)) continue; }
        f_idx[f_n++] = i;
    }
}

static void f_load(void) {                                               /* seed the list from last run's DATA so the first frame is the real, interactive list */
    char fp[P]; snprintf(fp, P, "%s/feed.dat", DDIR); FILE *f = fopen(fp, "r"); if (!f) return;
    char ln[1280]; while (fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\n")] = 0;
        char *p[5], *s = ln; int ok = 1; for (int i = 0; i < 5; i++) { char *q = strchr(s, '|'); if (!q) { ok = 0; break; } *q = 0; p[i] = s; s = q + 1; }
        if (ok && strlen(p[2]) >= 8) f_add(p[0], 0, p[3], p[2], atol(p[4]), s); }   /* host|live|sid|cwd|mt|desc — seed as parked; a live scan re-greens it */
    fclose(f);
}

static void f_save(void) {                                               /* persist current rows (only those a live scan confirmed → stale sessions get pruned) */
    char fp[P]; snprintf(fp, P, "%s/feed.dat", DDIR); FILE *f = fopen(fp, "w"); if (!f) return;
    for (int i = 0; i < FNN; i++) { FI *x = &FL[i]; if (x->seen) fprintf(f, "%s|%d|%s|%s|%ld|%s\n", x->host, x->live, x->sid, x->cwd, x->mt, x->desc); }
    fclose(f);
}

static void f_paint(int sel, long rus, int refreshing) {
    struct winsize ws; ioctl(1, TIOCGWINSZ, &ws); int rows = ws.ws_row ? ws.ws_row : 24;
    int vh = f_vh(), n = f_n, extra = rows >= 14;
    if (sel >= n) sel = n ? n - 1 : 0; if (sel < 0) sel = 0;
    if (sel < f_top) f_top = sel; else if (sel >= f_top + vh) f_top = sel - vh + 1;     /* edge-scroll: highlight moves within the view, list scrolls only at the edges */
    if (f_top > n - vh) f_top = n > vh ? n - vh : 0; if (f_top < 0) f_top = 0;
    char rt[24]; if (rus < 1000) snprintf(rt, 24, "%ld\xc2\xb5s", rus); else snprintf(rt, 24, "%ldms", rus / 1000);  /* sub-ms shows truthfully */
    char fb[1 << 16]; int o = 0;
    #define AP(...) o += snprintf(fb + o, (int)sizeof(fb) - o, __VA_ARGS__)
    #define EOL "\033[K\r\n"                                            /* home + per-line clear-to-EOL = in-place redraw, never a full-clear → no flicker */
    AP("\033[H\033[1ma feed\033[0m \033[97m%s\033[0m ", rt);                                /* title is ALWAYS the first line — never scrolls off a small panel */
    if (f_filt[0]) AP("\033[93m/%s\xe2\x96\x8f\033[0m ", f_filt);
    else AP("\033[90mtype=find \xe2\x86\x91\xe2\x86\x93 move \xe2\x86\x90\xe2\x86\x92 page \xe2\x86\xb5 open tab diff esc\033[0m ");
    AP("\033[90m%d/%d%s\033[0m" EOL, n ? sel + 1 : 0, n, refreshing ? " \xe2\x80\xa6" : "");
    if (extra) {                                                        /* legend + columns only when there's vertical room */
        AP("\033[32m\xe2\x97\x8f\033[0m\033[90m live \033[0m\xe2\x8f\xb8\033[90m parked\033[0m" EOL);
        AP("\033[90m"); for (int i = 0; i < 60; i++) AP("\xe2\x94\x80"); AP("\033[0m" EOL);
        AP("\033[90m    %-10s %-8s %s\033[0m" EOL, "BOX", "DIR", "LATEST MESSAGE"); }
    for (int r = 0; r < vh; r++) { int li = f_top + r;
        if (li >= n) { AP(EOL); continue; }
        FI *x = &FL[f_idx[li]]; const char *b = strrchr(x->cwd, '/'); b = (b && b[1]) ? b + 1 : x->cwd;
        char line[320]; snprintf(line, 320, "%s %-10.10s %-8.8s %s", x->live ? "\033[32m\xe2\x97\x8f\033[39m" : "\xe2\x8f\xb8", x->host, b, x->desc);
        if (li == sel) AP("\033[7m  %s\033[0m" EOL, line); else AP("  %s" EOL, line); }   /* same 2-col gutter both → aligned */
    if (extra) {
        AP("\033[90m"); for (int i = 0; i < 60; i++) AP("\xe2\x94\x80"); AP("\033[0m" EOL);
        if (n) { FI *x = &FL[f_idx[sel]];
            AP("\033[1m%s\033[0m:%s %s \xc2\xb7 %.8s" EOL, x->host, x->cwd, x->live ? "\033[32mlive\033[0m" : "parked", x->sid);
            if (x->diff[0]) { int k = 0; for (char *p = x->diff; *p && k < 1; p++) { if (*p == '\n') { AP(EOL); k++; } else AP("%c", *p); } if (!k) AP(EOL); }
            else AP("  \033[90m%.58s\033[0m" EOL, x->desc);
        } else AP("\033[90m  (no match)\033[0m" EOL); }
    if (o >= 2 && fb[o - 1] == '\n') o -= 2;                            /* drop the trailing CRLF: a newline on the last screen row scrolls the title off the top */
    AP("\033[J");                                                       /* erase anything below the last line (list shrank / diff cleared) */
    #undef AP
    #undef EOL
    (void)!write(1, fb, (size_t)o);
}

static void f_loaddiff(FI *x) {                                          /* on [d]: what this box changed (judge: worth using?) */
    if (x->diff[0]) return; char cmd[B];
    snprintf(cmd, B, "a ssh %s \"cd %s 2>/dev/null&&git -c color.ui=always diff --stat 2>/dev/null|tail -15;git --no-pager log --oneline -3 2>/dev/null\" 2>/dev/null", x->host, x->cwd);
    FILE *p = popen(cmd, "r"); if (!p) { snprintf(x->diff, sizeof x->diff, "(diff failed)"); return; }
    int o = 0, ch; while ((ch = fgetc(p)) != EOF && o < (int)sizeof(x->diff) - 1) x->diff[o++] = (char)ch; x->diff[o] = 0; pclose(p);
    if (!o) snprintf(x->diff, sizeof x->diff, "(no changes)");
}

static void f_attach(FI *x) {                                            /* parked → resume into the box's tmux, then attach; live → attach */
    tcsetattr(f_tty, TCSANOW, &f_saved); printf("\033[?7h\033[H\033[2J"); fflush(stdout);   /* restore auto-wrap */
    if (!x->live) { char wn[20]; snprintf(wn, 20, "r-%.8s", x->sid);
        if (fork() == 0) { execlp("a", "a", "ssh", x->host, "tmux", "new-window", "-t", "a", "-c", x->cwd, "-n", wn,
            "claude", "--dangerously-skip-permissions", "--model", "opus", "--resume", x->sid, (char *)0); _exit(127); }
        wait(0); }
    execlp("a", "a", "ssh", x->host, (char *)0); _exit(127);
}

static int cmd_feed(int c, char **v) { (void)c; (void)v; perf_disarm();
    FNN = fnch = 0; f_top = 0;
    f_live_scan = 0; f_load(); f_live_scan = 1;                           /* seed the real list from last run's data (presumed parked until a live scan confirms) */
    struct timespec tp; clock_gettime(CLOCK_MONOTONIC, &tp);
    long rus = (tp.tv_sec - _t0.tv_sec) * 1000000L + (tp.tv_nsec - _t0.tv_nsec) / 1000;  /* main()→first paint (same _t0 the other menus time from) */
    f_hosts();
    if (!isatty(0) || !isatty(1)) {                                      /* non-tty: drain all, dump */
        int alive = 1; while (alive) { struct pollfd pf[64]; int np = 0, a = 0;
            for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) { pf[np].fd = fch[i].fd; pf[np++].events = POLLIN; a++; }
            if (!a) break; poll(pf, np, 200);
            for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) for (int j = 0; j < np; j++)
                if (pf[j].fd == fch[i].fd && (pf[j].revents & (POLLIN | POLLHUP | POLLERR))) f_recv(&fch[i]); }
        for (int i = 0; i < FNN; i++) { FI *x = &FL[i]; const char *b = strrchr(x->cwd, '/'); b = (b && b[1]) ? b + 1 : x->cwd;
            printf("%s %-11.11s%-10.10s%.50s\n", x->live ? "\xe2\x97\x8f" : "\xe2\x8f\xb8", x->host, b, x->desc); }
        f_save(); return 0; }
    f_tty = open("/dev/tty", O_RDWR); if (f_tty < 0) f_tty = 0;         /* keyboard from /dev/tty, NOT the inherited fd0 (child plumbing clobbers it) */
    tcgetattr(f_tty, &f_saved); struct termios r = f_saved; cfmakeraw(&r); tcsetattr(f_tty, TCSANOW, &r);
    (void)!write(1, "\033[?7l", 5);                                     /* disable auto-wrap: long lines CLIP at the edge, never wrap → never break the layout */
    signal(SIGWINCH, f_onwinch);                                        /* repaint on pane resize */
    int sel = 0;
    f_filter(); f_paint(sel, rus, fnch > 0);                            /* FIRST PAINT: the seeded real list, immediately (~1ms) */
    for (;;) {
        struct pollfd pf[66]; int np = 0;
        pf[np].fd = f_tty; pf[np++].events = POLLIN;
        for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) { pf[np].fd = fch[i].fd; pf[np++].events = POLLIN; }
        int pr = poll(pf, np, -1);                                     /* block until a key, a new row, a child finishing, or a resize (SIGWINCH→EINTR) */
        if (f_winch) { f_winch = 0; f_filter(); if (sel >= f_n) sel = f_n ? f_n - 1 : 0;
            (void)!write(1, "\033[H\033[2J", 7); int rem = 0; for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) rem++; f_paint(sel, rus, rem > 0); continue; }  /* resize: full-clear once, repaint at new size */
        if (pr <= 0) continue;
        f_filter(); if (sel >= f_n) sel = f_n ? f_n - 1 : 0;
        int dirty = 0;
        if (pf[0].revents & (POLLIN | POLLHUP)) { unsigned char k; int rr = read(f_tty, &k, 1);
            if (rr <= 0 || k == 3) break;                              /* tty EOF/error or Ctrl-C → exit */
            else if (k == 27) {                                       /* Esc alone = clear filter / quit ; else an arrow/page sequence */
                struct pollfd e = { f_tty, POLLIN, 0 };
                if (poll(&e, 1, 30) > 0) { unsigned char s[5]; int sn = (int)read(f_tty, s, 5);
                    if (sn >= 2 && s[0] == '[') {
                        if (s[1] == 'A') { if (sel > 0) sel--; } else if (s[1] == 'B') { if (sel + 1 < f_n) sel++; }
                        else if (s[1] == 'D' || s[1] == '5') { sel -= f_vh(); if (sel < 0) sel = 0; }                       /* ← / PgUp = page up */
                        else if (s[1] == 'C' || s[1] == '6') { sel += f_vh(); if (sel >= f_n) sel = f_n ? f_n - 1 : 0; } }   /* → / PgDn = page down */
                } else if (f_filt[0]) { f_filt[0] = 0; f_filt_n = 0; sel = 0; f_top = 0; } else break;
                dirty = 1; }
            else if (k == '\r' || k == '\n') { if (f_n) f_attach(&FL[f_idx[sel]]); }        /* Enter = open it (come back in) */
            else if (k == 9) { if (f_n) f_loaddiff(&FL[f_idx[sel]]); dirty = 1; }            /* Tab = diff */
            else if (k == 127 || k == 8) { if (f_filt_n) { f_filt[--f_filt_n] = 0; sel = 0; f_top = 0; } dirty = 1; }  /* Backspace = edit filter */
            else if (k >= 32 && k < 127) { if (f_filt_n < 62) { f_filt[f_filt_n++] = (char)k; f_filt[f_filt_n] = 0; sel = 0; f_top = 0; } dirty = 1; }  /* type to search */
            else dirty = 1; }
        for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) for (int j = 1; j < np; j++)
            if (pf[j].fd == fch[i].fd && (pf[j].revents & (POLLIN | POLLHUP | POLLERR))) { f_recv(&fch[i]); dirty = 1; }
        if (dirty) { f_filter(); if (sel >= f_n) sel = f_n ? f_n - 1 : 0; int rem = 0; for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) rem++; f_paint(sel, rus, rem > 0); }  /* in-place redraw only on real change */
    }
    tcsetattr(f_tty, TCSANOW, &f_saved); printf("\033[?7h\033[H\033[2J"); fflush(stdout);   /* restore auto-wrap */
    for (int i = 0; i < fnch; i++) if (fch[i].fd >= 0) close(fch[i].fd);
    while (waitpid(-1, 0, WNOHANG) > 0) {}
    f_save();                                                          /* persist the data (not a picture) so next launch seeds the real list */
    return 0;
}
