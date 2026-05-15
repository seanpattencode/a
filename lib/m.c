/* m — chat with streaming cutoff + agentic loop, pure C. */

static volatile pid_t g_cp = 0;
static pid_t g_cmt = 0;
static int g_halt = 0;
static char g_set[P] = "";
static void m_sint(int s){(void)s;if(g_cp>0)kill(g_cp,SIGTERM);}
static void mm_w(const char *p, const char *t, const char *m);

static void m_status(const char *msg) {
    char p[P], l[256];
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    snprintf(p, P, "%s/m_status", TMP);
    snprintf(l, 256, "[%02d:%02d:%02d] %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, msg);
    mm_w(p, l, "a");
}

static void m_commit(const char *tag) {
    if (g_halt) return;
    if (g_cmt > 0) {
        int st; pid_t r;
        while ((r = waitpid(g_cmt, &st, 0)) < 0 && errno == EINTR) {}
        g_cmt = 0;
        if (r < 0 || !WIFEXITED(st) || WEXITSTATUS(st)) {
            char mp[P], m[512];
            snprintf(m, 512, "\n## error\nauto-commit failed (exit %d). working tree dirty. fix: `git -C adata/m status` then `git -C adata/m commit -m fix`. type to retry.\n", WIFEXITED(st)?WEXITSTATUS(st):-1);
            snprintf(mp, P, "%s/m/m.txt", AROOT); mm_w(mp, m, "a");
            m_status("✗ commit failed — see ## error in m.txt");
            if (g_cp > 0) kill(g_cp, SIGTERM);
            g_halt = 1; return;
        }
    }
    pid_t p = fork();
    if (!p) {
        char c[B];
        snprintf(c, B, "git -C '%s/m' add -A && (git -C '%s/m' diff --cached --quiet || git -C '%s/m' commit -q -m '%s')", AROOT, AROOT, AROOT, tag);
        execlp("sh", "sh", "-c", c, NULL); _exit(127);
    }
    if (p > 0) { g_cmt = p; char sm[32]; snprintf(sm, 32, "git saving %s", tag); m_status(sm); }
}

static int mm_extract(const char *a, char *bash, size_t bsz, size_t *eo) {
    const char *o = strstr(a, "<cmd>");
    if (!o) return 0;
    const char *bs = o + 5, *cl = strstr(bs, "</cmd>");
    if (!cl) return -1;
    size_t bl = (size_t)(cl - bs);
    if (bl >= bsz) bl = bsz - 1;
    memcpy(bash, bs, bl); bash[bl] = 0;
    if (eo) *eo = (size_t)(cl - a) + 6;
    return 1;
}

static int mm_delta(const char *l, char *a, size_t *al, size_t sz) {
    if (strstr(l, "\"message_stop\"")) return 1;
    const char *t = strstr(l, "\"text_delta\",\"text\":\"");
    if (!t) return 0;
    t += 21;
    while (*t && *t != '"' && *al + 1 < sz) {
        if (*t == '\\' && t[1]) {
            char c = t[1];
            a[(*al)++] = c == 'n' ? '\n' : c == '"' ? '"' : c == '\\' ? '\\' : c;
            t += 2;
        } else a[(*al)++] = *t++;
    }
    a[*al] = 0;
    return 0;
}

static int mm_stream(const char *sf, const char *sp, char *a, size_t sz, char *bash, size_t bsz) {
    a[0] = bash[0] = 0;
    int pp[2]; if (pipe(pp) < 0) return -1;
    pid_t cp = fork();
    if (!cp) {
        int s = open(sf, O_RDONLY); if (s < 0) _exit(127);
        int d = open("/dev/null", O_WRONLY);
        dup2(s, 0); dup2(pp[1], 1); dup2(d, 2);
        close(s); close(d); close(pp[0]); close(pp[1]);
        execlp("claude","claude","-p","--output-format","stream-json",
               "--include-partial-messages","--verbose","--tools","","--effort","low",
               "--system-prompt", sp, "--settings", g_set, (char*)0);
        _exit(127);
    }
    close(pp[1]); g_cp = cp;
    FILE *fp = fdopen(pp[0], "r"), *of = fopen(sf, "a");
    off_t start = ftello(of);
    char l[32768]; size_t al = 0, pl = 0, eo;
    while (fgets(l, sizeof l, fp)) {
        int stop = mm_delta(l, a, &al, sz);
        if (al > pl) { if (!pl) m_status("streaming"); fwrite(a + pl, 1, al - pl, of); fflush(of); pl = al; }
        char *uh = strstr(a, "\n## user\n"); size_t cut = uh ? (size_t)(uh - a) : 0;
        if (!cut && mm_extract(a, bash, bsz, &eo) > 0) cut = eo;
        if (cut) {
            if (cut < al) { a[cut] = 0; al = cut; ftruncate(fileno(of), start + (off_t)cut); }
            kill(cp, SIGTERM); break;
        }
        if (stop) break;
    }
    fclose(fp); fclose(of); waitpid(cp, NULL, 0); g_cp = 0;
    if (!bash[0]) mm_extract(a, bash, bsz, NULL);
    return 0;
}

static void mm_bash(const char *pty, const char *cmd, char *out, size_t sz) {
    char cn[B]; snprintf(cn, B, "%s\r", cmd);
    pid_t p = fork();
    if (!p) { execlp("tmux","tmux","send-keys","-t",pty,"-l",cn,(char*)0); _exit(127); }
    waitpid(p, NULL, 0);
    sleep(3);
    char sc[B];
    snprintf(sc, B, "tmux capture-pane -t '%s' -p -S -50|tail -30|sed 's/^/    /'", pty);
    pcmd(sc, out, sz);
}

static void mm_w(const char *p, const char *t, const char *m) {
    FILE *f = fopen(p, m); if (f) { fputs(t, f); fclose(f); }
}

static int cmd_m_panel(int c, char **v) {
    (void)c; (void)v;
    static const struct { const char *l, *cm; int a; } PC[] = {
        {"archive", "a m archive", 0},
        {"view", "a m archive view", 1},
        {"unarchive", "a m archive unarchive", 1},
        {"reset", "a m reset", 0},
        {NULL, NULL, 0}};
    int np = 0; while (PC[np].l) np++;
    struct termios old, raw; tcgetattr(0, &old); raw = old;
    raw.c_lflag &= ~(tcflag_t)(ICANON|ECHO|ISIG);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    write(1, "\033[?1000h\033[?1006h", 16);
    int sel = 0; char f[64] = "", last[80] = ""; int fl = 0;
    for (;;) {
        int vis[16], nv = 0;
        for (int i = 0; i < np; i++) if (!fl || strstr(PC[i].l, f)) vis[nv++] = i;
        if (sel >= nv) sel = nv ? nv - 1 : 0;
        write(1, "\033[2J\033[H", 7);
        printf("/ %s_  \033[90m%s\033[0m\033[K\n", f, last);
        for (int j = 0; j < nv; j++) { int i = vis[j];
            printf("%s [%s]%s\033[K\n", j == sel ? "\033[7m>" : " ", PC[i].l, PC[i].a ? " <arg>" : "");
            if (j == sel) printf("\033[0m");
        }
        fflush(stdout);
        char ch; if (read(0, &ch, 1) != 1) break;
        int pick = 0;
        if (ch == '\x1b') { int av; usleep(50000); ioctl(0, FIONREAD, &av);
            if (!av) break;
            char s[2]; if (read(0, s, 1) != 1) break;
            if (s[0] == '[') { if (read(0, s+1, 1) != 1) break;
                if (s[1] == 'A') { if (sel > 0) sel--; }
                else if (s[1] == 'B') { if (sel < nv - 1) sel++; }
                else if (s[1] == '<') { int b=0,x=0,y=0; char mc;
                    while (read(0,&mc,1)==1&&mc!=';') b=b*10+(mc-'0');
                    while (read(0,&mc,1)==1&&mc!=';') x=x*10+(mc-'0');
                    while (read(0,&mc,1)==1&&mc!='M'&&mc!='m') y=y*10+(mc-'0');
                    (void)x; if (mc=='M'&&b==0&&y>=2&&y<2+nv) { sel = y-2; pick = 1; }
                }}}
        else if (ch == '\r' || ch == '\n') pick = 1;
        else if (ch == 0x7f || ch == 8) { if (fl) f[--fl] = 0; sel = 0; }
        else if (ch == 3 || ch == 4) break;
        else if (isprint((unsigned char)ch)) { if (fl < 60) { f[fl++] = ch; f[fl] = 0; sel = 0; } }
        if (pick && nv) { int i = vis[sel];
            write(1, "\033[?1000l\033[?1006l\033[2J\033[H", 23);
            tcsetattr(0, TCSANOW, &old);
            char cmd[B], arg[128] = "";
            if (PC[i].a) { printf("%s <arg>: ", PC[i].l); fflush(stdout);
                if (!fgets(arg, 128, stdin)) break;
                arg[strcspn(arg, "\n")] = 0;
            }
            snprintf(cmd, B, "%s%s%s >/dev/null 2>&1", PC[i].cm, arg[0] ? " " : "", arg);
            int r = system(cmd);
            snprintf(last, sizeof last, "%s%s%s [%d]", PC[i].l, arg[0] ? " " : "", arg, WIFEXITED(r)?WEXITSTATUS(r):-1);
            tcsetattr(0, TCSANOW, &raw);
            write(1, "\033[?1000h\033[?1006h", 16);
            f[0]=0; fl=0; sel=0;
        }
    }
    write(1, "\033[?1000l\033[?1006l", 16);
    tcsetattr(0, TCSANOW, &old);
    return 0;
}

static int m_archive(int c, char **v) {
    char mp[P], adir[P], ap[P];
    snprintf(adir, P, "%s/m/archive", AROOT);
    static const char *USAGE =
        "usage:\n"
        "  a m archive                       rotate m.txt (archive whole conversation)\n"
        "  a m archive <file.txt>            rotate <file.txt>\n"
        "  a m archive N-M [file.txt]        archive lines N..M\n"
        "  a m archive view <ts> [file.txt]  print archived content\n"
        "  a m archive unarchive <ts> [file] restore archived content at marker (alias: restore)\n";
    if (c > 3 && (!strcmp(v[3], "--help") || !strcmp(v[3], "-h") || !strcmp(v[3], "help"))) {
        fputs(USAGE, stdout); return 0;
    }
    if (c > 3 && !strcmp(v[3], "restore")) v[3] = (char*)"unarchive";
    /* rotate: a m archive [file.txt]  (no args, or .txt filename) */
    if (c == 3 || (c == 4 && strstr(v[3], ".txt"))) {
        const char *fn = (c == 4) ? v[3] : "m.txt";
        snprintf(mp, P, "%s/m/%s", AROOT, fn);
        size_t tl; char *txt = readf(mp, &tl);
        if (!txt) { printf("file not found: %s\n", mp); return 1; }
        char *first = strstr(txt, "\n## user\n");
        if (!first) { puts("no conversation"); free(txt); return 0; }
        first++;
        char *last = first, *p = first;
        while ((p = strstr(p + 1, "\n## user\n")) != NULL) last = p + 1;
        if (last == first) { puts("nothing to rotate"); free(txt); return 0; }
        mkdirp(adir);
        char ts[32]; time_t t = time(NULL); strftime(ts, sizeof ts, "%Y%m%dT%H%M%S", localtime(&t));
        snprintf(ap, P, "%s/%s.txt", adir, ts);
        FILE *af = fopen(ap, "w"); if (!af) { free(txt); return 1; }
        fwrite(first, 1, (size_t)(last - first), af); fclose(af);
        char mk[256];
        snprintf(mk, 256, "[archived %s · rotate · view: a m archive view %s]\n", ts, ts);
        FILE *f = fopen(mp, "w"); if (!f) { free(txt); return 1; }
        fwrite(txt, 1, (size_t)(first - txt), f); fputs(mk, f);
        fwrite(last, 1, (size_t)((txt + tl) - last), f); fclose(f); free(txt);
        printf("rotated → archive/%s.txt\n", ts);
        return 0;
    }
    if (c < 4) { fputs(USAGE, stderr); return 1; }
    int has_ts = !strcmp(v[3], "view") || !strcmp(v[3], "unarchive");
    int is_range = isdigit((unsigned char)v[3][0]);
    if (!has_ts && !is_range) { fprintf(stderr, "unknown subcommand: %s\n", v[3]); fputs(USAGE, stderr); return 1; }
    int fn_idx = has_ts ? 5 : 4;
    const char *fn = c > fn_idx ? v[fn_idx] : "m.txt";
    snprintf(mp, P, "%s/m/%s", AROOT, fn);
    if (has_ts) {
        if (c < 5) { puts("need <ts>"); return 1; }
        snprintf(ap, P, "%s/%s.txt", adir, v[4]);
        if (!strcmp(v[3], "view")) {
            char *d = readf(ap, NULL); if (!d) { printf("not found: %s\n", ap); return 1; }
            fputs(d, stdout); free(d); return 0;
        }
        char mh[64]; snprintf(mh, 64, "[archived %s ·", v[4]);
        char *cnt = readf(ap, NULL); if (!cnt) { puts("archive missing"); return 1; }
        size_t tl; char *txt = readf(mp, &tl); if (!txt) { free(cnt); return 1; }
        char *pos = strstr(txt, mh);
        if (!pos) { puts("marker not found"); free(cnt); free(txt); return 1; }
        char *eol = strchr(pos, '\n'); eol = eol ? eol + 1 : txt + tl;
        FILE *f = fopen(mp, "w"); if (!f) { free(cnt); free(txt); return 1; }
        fwrite(txt, 1, (size_t)(pos - txt), f); fputs(cnt, f);
        fwrite(eol, 1, (size_t)((txt + tl) - eol), f); fclose(f); unlink(ap);
        free(cnt); free(txt); printf("unarchived %s\n", v[4]); return 0;
    }
    int N = atoi(v[3]); const char *dash = strchr(v[3], '-');
    int M = dash ? atoi(dash + 1) : N;
    if (N < 1 || M < N) { fprintf(stderr, "invalid range '%s': want N-M with N>=1 and M>=N\n", v[3]); return 1; }
    mkdirp(adir);
    size_t tl; char *txt = readf(mp, &tl);
    if (!txt) { printf("file not found: %s\n", mp); return 1; }
    char *s = txt; int ln = 1;
    while (ln < N && s < txt + tl) { if (*s++ == '\n') ln++; }
    if (ln < N) { printf("past EOF (%d lines)\n", ln); free(txt); return 1; }
    char *e = s; int cnt = 0;
    while (e < txt + tl && cnt < M - N + 1) { if (*e++ == '\n') cnt++; }
    char ts[32]; time_t t = time(NULL);
    strftime(ts, sizeof ts, "%Y%m%dT%H%M%S", localtime(&t));
    snprintf(ap, P, "%s/%s.txt", adir, ts);
    FILE *af = fopen(ap, "w"); if (!af) { free(txt); return 1; }
    fwrite(s, 1, (size_t)(e - s), af); fclose(af);
    char mk[256];
    snprintf(mk, 256, "[archived %s · L%d-%d · view: a m archive view %s]\n", ts, N, M, ts);
    FILE *f = fopen(mp, "w"); if (!f) { free(txt); return 1; }
    fwrite(txt, 1, (size_t)(s - txt), f); fputs(mk, f);
    fwrite(e, 1, (size_t)((txt + tl) - e), f); fclose(f); free(txt);
    printf("archived L%d-%d → archive/%s.txt (%ld bytes)\n", N, M, ts, (long)(e - s));
    return 0;
}

static int m_reset(void) {
    char ptyf[P]; snprintf(ptyf, P, "%s/m_pty", TMP);
    char *p = readf(ptyf, NULL);
    if (!p || !*p) { puts("no pty registered (a m not running here)"); free(p); return 1; }
    p[strcspn(p, "\n")] = 0;
    char cmd[B];
    snprintf(cmd, B, "tmux send-keys -t '%s' C-c; tmux send-keys -t '%s' 'clear' Enter", p, p);
    int r = system(cmd); free(p); return r;
}

static int cmd_m(int c, char **v) {
    if (c > 2 && !strcmp(v[2], "archive")) return m_archive(c, v);
    if (c > 2 && !strcmp(v[2], "panel")) return cmd_m_panel(c, v);
    if (c > 2 && !strcmp(v[2], "reset")) return m_reset();
    if (getenv("M_IN")) { puts("already in a m (nested chat blocked) — use: a m archive | panel | reset"); return 1; }
    char b[B], sf[P], ss[P], spf[P], pty[64] = "";
    if (!getenv("TMUX")) { CWD(w); ajoin(b,B,c,v,0);
        struct tm*tt=localtime(&(time_t){time(NULL)}); char sn[64];
        snprintf(sn,64,"m-%s-%02d%02d%02d",bname(w),tt->tm_hour,tt->tm_min,tt->tm_sec);
        tm_new(sn,w,b); tm_go(sn); return 0; }
    signal(SIGINT, m_sint);
    const char *fn = c > 2 ? v[2] : "m.txt";
    snprintf(sf, P, "%s/m/%s", AROOT, fn);
    snprintf(ss, P, "%s/m_status", TMP);
    snprintf(spf, P, "%s/m/sysprompt.txt", AROOT);
    snprintf(g_set, P, "%s/m_settings.json", TMP);
    { char hp[P]; snprintf(hp, P, "%s/.claude/settings.json", HOME);
      char *s = readf(hp, NULL); FILE *f = fopen(g_set, "w");
      if (f) { fputs("{\"enabledPlugins\":{", f);
        if (s) { char *ep = strstr(s, "\"enabledPlugins\""), *br = ep?strchr(ep,'{'):0, *en = br?strchr(br,'}'):0, *p = br?br+1:0; int fst = 1;
          while (p && p < en) { char *q1 = memchr(p,'"',(size_t)(en-p)); if (!q1) break;
            char *q2 = memchr(q1+1,'"',(size_t)(en-q1-1)); if (!q2) break;
            fprintf(f, "%s\"%.*s\":false", fst?"":",", (int)(q2-q1-1), q1+1); fst = 0; p = q2+1; } }
        fputs("}}\n", f); fclose(f); } free(s); }
    if (!fexists(spf)) mm_w(spf, "`a m` chat: emit <cmd>command</cmd> to run in pty (cwd=m). After </cmd>, STOP. Markdown ```bash/```sh blocks are for showing code only (NEVER executed). No Read/Write/Bash/LSP tools. Never emit ## user, ## assistant, ## tool output headers; markdown ## headings inside replies are fine.\n", "w");
    { char *cur = fexists(sf) ? readf(sf, NULL) : NULL;
      if (!cur || strncmp(cur, "## system\n", 10)) {
          size_t spl; char *sp = readf(spf, &spl); FILE *f = fopen(sf, "w");
          if (f) { fprintf(f, "## system\n%s%s%s", sp?sp:"", (sp&&spl&&sp[spl-1]=='\n')?"":"\n", cur?cur:"## user\n"); fclose(f); }
          free(sp);
      }
      free(cur); }
    mm_w(ss, "", "w"); m_status("ready ^C=interrupt");
    setenv("M_IN", "1", 1);
    snprintf(b, B, "[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone);"
                   "tmux split-window -t $TMUX_PANE -e M_PID=%4$d -e M_IN=1 -dvb 'e %2$s';"
                   "tmux split-window -t $TMUX_PANE -e M_IN=1 -dvb -l 3 'tail -Fn 50 %3$s';"
                   "tmux split-window -t $TMUX_PANE -e M_IN=1 -dvb -l 5 'a m panel'",
             AROOT, sf, ss, (int)getpid());
    system(b);
    snprintf(b, B, "tmux split-window -t $TMUX_PANE -e M_IN=1 -dvb -P -F '#{pane_id}' 'cd %s/m;exec bash'", AROOT);
    pcmd(b, pty, 64); pty[strcspn(pty, "\n")] = 0;
    { char pf[P]; snprintf(pf, P, "%s/m_pty", TMP); mm_w(pf, pty, "w"); }
    for (;;) {
        g_halt = 0;
        char tf[] = "/tmp/m_inXXXXXX";
        int fd = mkstemp(tf); if (fd < 0) continue; close(fd);
        pid_t pp = fork();
        if (!pp) { execlp("e","e","--box","message:",tf,(char*)0); _exit(127); }
        waitpid(pp, NULL, 0);
        size_t ml; char *m = readf(tf, &ml); unlink(tf);
        if (!m || !ml) { free(m); continue; }
        mm_w(sf, m, "a"); mm_w(sf, "\n", "a"); free(m);
        m_commit("u");
        for (int i = 0; i < 10; i++) {
            m_status("thinking");
            mm_w(sf, "\n## assistant\n", "a");
            static char a[64*1024], bash[8*1024], ob[16*1024], syc[B];
            { char *all = readf(sf, NULL); char *spc = all?strstr(all,"## system\n"):0; char *us = spc?strstr(spc,"\n## user\n"):0;
              if (spc && us) { size_t l = (size_t)(us - (spc + 10)); if (l >= B) l = B-1; memcpy(syc, spc+10, l); syc[l] = 0; }
              else { char *sp = readf(spf, NULL); snprintf(syc, B, "%s", sp?sp:""); free(sp); } free(all); }
            if (mm_stream(sf, syc, a, sizeof a, bash, sizeof bash) < 0) break;
            m_commit("a"); if (g_halt) break;
            if (!bash[0]) break;
            m_status("running");
            mm_bash(pty, bash, ob, sizeof ob);
            char tb[20000]; time_t t = time(NULL);
            size_t n = strftime(tb, 64, "\n## tool output %FT%T\n", localtime(&t));
            snprintf(tb + n, sizeof tb - n, "%s\n", ob);
            mm_w(sf, tb, "a");
            m_commit("t"); if (g_halt) break;
        }
        mm_w(sf, "\n## user\n", "a");
        m_status("ready ^C=interrupt");
    }
    return 0;
}
