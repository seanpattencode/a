/* m — chat with streaming cutoff + agentic loop, pure C. */

static long mm_open(const char *a, size_t *bo) {
    const char *p = a, *o;
    while ((o = strstr(p, "```"))) {
        if (o == a || o[-1] == '\n') {
            const char *x = o + 3;
            if (!strncmp(x,"bash\n",5))  { *bo = (size_t)(x + 5 - a); return o - a; }
            if (!strncmp(x,"sh\n",3))    { *bo = (size_t)(x + 3 - a); return o - a; }
            if (!strncmp(x,"shell\n",6)) { *bo = (size_t)(x + 6 - a); return o - a; }
            if (*x == '\n')              { *bo = (size_t)(x + 1 - a); return o - a; }
        }
        p = o + 3;
    }
    return -1;
}

static long mm_close(const char *a, size_t f, size_t *eo) {
    const char *c = strstr(a + f, "\n```\n");
    if (c) { *eo = (size_t)(c - a) + 5; return c - a; }
    c = strstr(a + f, "\n```");
    if (c && !c[4]) { *eo = (size_t)(c - a) + 4; return c - a; }
    return -1;
}

static int mm_delta(const char *l, char *a, size_t *al, size_t sz) {
    if (strstr(l, "\"message_stop\"")) return 1;
    const char *t = strstr(l, "\"text_delta\",\"text\":\"");
    if (!t) return 0;
    t += 21;
    while (*t && *al + 1 < sz) {
        if (*t == '\\') {
            t++;
            a[(*al)++] = *t == 'n' ? '\n' : *t == '"' ? '"' : *t == '\\' ? '\\' :
                         *t == 't' ? '\t' : *t == 'r' ? '\r' : *t;
            if (*t) t++;
        } else if (*t == '"') break;
        else a[(*al)++] = *t++;
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
        dup2(s, 0); close(s); dup2(pp[1], 1); close(pp[0]); close(pp[1]);
        int d = open("/dev/null", O_WRONLY); if (d >= 0) { dup2(d, 2); close(d); }
        execlp("claude","claude","-p","--output-format","stream-json",
               "--include-partial-messages","--verbose",
               "--append-system-prompt-file", sp, (char*)0);
        _exit(127);
    }
    close(pp[1]);
    FILE *fp = fdopen(pp[0], "r");
    char l[32768]; size_t al = 0; int ib = 0; size_t bo = 0;
    while (fgets(l, sizeof l, fp)) {
        int stop = mm_delta(l, a, &al, sz);
        if (!ib && mm_open(a, &bo) >= 0) ib = 1;
        if (ib) {
            size_t eo;
            if (mm_close(a, bo, &eo) >= 0) {
                if (eo < al) { a[eo] = 0; al = eo; }
                kill(cp, SIGTERM); break;
            }
        }
        if (stop) break;
    }
    fclose(fp); waitpid(cp, NULL, 0);
    size_t b2;
    if (mm_open(a, &b2) >= 0) {
        size_t eo;
        long c = mm_close(a, b2, &eo);
        if (c >= 0) {
            size_t bl = (size_t)c - b2;
            if (bl >= bsz) bl = bsz - 1;
            memcpy(bash, a + b2, bl); bash[bl] = 0;
        }
    }
    return 0;
}

static void mm_filter(const char *in, char *o, size_t sz) {
    const char *p = in; size_t oi = 0;
    while (*p && oi + 1 < sz) {
        const char *e = strchr(p, '\n');
        size_t ll = e ? (size_t)(e - p) : strlen(p);
        if (strncmp(p,"## user",7) && strncmp(p,"## assistant",12) && strncmp(p,"## tool output",14)) {
            size_t c = ll < sz - 1 - oi ? ll : sz - 1 - oi;
            memcpy(o + oi, p, c); oi += c;
            if (e && oi + 1 < sz) o[oi++] = '\n';
        }
        if (!e) break;
        p = e + 1;
    }
    o[oi] = 0;
}

static void mm_bash(const char *pty, const char *cmd, char *out, size_t sz) {
    pid_t p = fork();
    if (!p) { execlp("tmux","tmux","send-keys","-t",pty,"-l",cmd,(char*)0); _exit(127); }
    waitpid(p, NULL, 0);
    p = fork();
    if (!p) { execlp("tmux","tmux","send-keys","-t",pty,"Enter",(char*)0); _exit(127); }
    waitpid(p, NULL, 0);
    sleep(3);
    char sc[B];
    snprintf(sc, B, "tmux capture-pane -t '%s' -p -S -50|tail -30|sed 's/^/    /'", pty);
    pcmd(sc, out, sz);
}

static void mm_w(const char *p, const char *t, const char *m) {
    FILE *f = fopen(p, m); if (f) { fputs(t, f); fclose(f); }
}

static int cmd_m(int c, char **v) {
    char b[B], sf[P], ss[P], pf[P], spf[P], pty[64] = "";
    snprintf(b, B, "[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone)", AROOT);
    system(b);
    const char *fn = c > 2 ? v[2] : "m.txt";
    snprintf(sf, P, "%s/m/%s", AROOT, fn);
    snprintf(ss, P, "%s/m_status", TMP);
    snprintf(pf, P, "%s/m_pty_id", TMP);
    snprintf(spf, P, "%s/m/sysprompt.txt", AROOT);
    if (!fexists(spf)) mm_w(spf, "You are in `a m` chat. ```bash blocks run in a persistent pty (cwd=m); output appended as '## tool output <ts>' indented. After a bash block STOP. Don't emit ## headers (system adds them). Don't simulate output.\n", "w");
    if (!fexists(sf)) { snprintf(b, B, "{ echo '## system';cat '%s';echo '## user';} >'%s'", spf, sf); system(b); }
    mm_w(ss, "ready", "w");
    if (!getenv("TMUX")) { puts("x needs tmux"); return 1; }
    snprintf(b, B, "tmux split-window -dvb 'e %s'", sf); system(b);
    snprintf(b, B, "tmux split-window -dvb -P -F '#{pane_id}' 'cd %s/m;exec bash'", AROOT);
    pcmd(b, pty, 64); pty[strcspn(pty, "\n")] = 0;
    mm_w(pf, pty, "w");
    snprintf(b, B, "tmux split-window -dv -l 1 'P(){ printf \"\\r\\033[K%%s\" \"$(cat %1$s)\";};P;while inotifywait -qe modify %1$s 2>/dev/null;do P;done'", ss);
    system(b);
    for (;;) {
        char tf[] = "/tmp/m_inXXXXXX";
        int fd = mkstemp(tf); if (fd < 0) continue; close(fd);
        pid_t pp = fork();
        if (!pp) { execlp("e","e","--box","message:",tf,(char*)0); _exit(127); }
        waitpid(pp, NULL, 0);
        size_t ml; char *m = readf(tf, &ml); unlink(tf);
        if (!m || !ml) { free(m); continue; }
        FILE *f = fopen(sf, "a");
        if (f) { fwrite(m, 1, ml, f); if (m[ml-1] != '\n') fputc('\n', f); fclose(f); }
        free(m);
        for (int i = 0; i < 10; i++) {
            mm_w(ss, "sending", "w");
            mm_w(sf, "\n## assistant\n", "a");
            static char a[64*1024], fl[64*1024], bash[8*1024], ob[16*1024];
            if (mm_stream(sf, spf, a, sizeof a, bash, sizeof bash) < 0) break;
            mm_filter(a, fl, sizeof fl);
            mm_w(sf, fl, "a");
            if (!bash[0]) break;
            mm_w(ss, "running", "w");
            mm_bash(pty, bash, ob, sizeof ob);
            time_t t = time(NULL);
            char ts[32]; strftime(ts, 32, "%Y-%m-%dT%H:%M:%S", localtime(&t));
            f = fopen(sf, "a"); if (f) { fprintf(f, "\n## tool output %s\n%s\n", ts, ob); fclose(f); }
        }
        mm_w(sf, "\n## user\n", "a");
        mm_w(ss, "done", "w");
    }
    return 0;
}
