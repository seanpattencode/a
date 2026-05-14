/* m — chat with streaming cutoff + agentic loop, pure C. */

static int mm_extract(const char *a, char *bash, size_t bsz, size_t *eo) {
    const char *p = a, *o;
    while ((o = strstr(p, "```"))) {
        if (o == a || o[-1] == '\n') {
            const char *nl = strchr(o + 3, '\n');
            if (nl) {
                const char *bs = nl + 1, *cl = strstr(bs, "\n```");
                if (!cl) return -1;
                size_t bl = (size_t)(cl - bs);
                if (bl >= bsz) bl = bsz - 1;
                memcpy(bash, bs, bl); bash[bl] = 0;
                if (eo) *eo = (size_t)(cl - a) + 4 + (cl[4] == '\n');
                return 1;
            }
        }
        p = o + 3;
    }
    return 0;
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
               "--include-partial-messages","--verbose","--tools","",
               "--append-system-prompt-file", sp, (char*)0);
        _exit(127);
    }
    close(pp[1]);
    FILE *fp = fdopen(pp[0], "r"), *of = fopen(sf, "a");
    off_t start = ftello(of);
    char l[32768]; size_t al = 0, pl = 0, eo;
    while (fgets(l, sizeof l, fp)) {
        int stop = mm_delta(l, a, &al, sz);
        if (al > pl) { fwrite(a + pl, 1, al - pl, of); fflush(of); pl = al; }
        char *uh = strstr(a, "\n## "); size_t cut = uh ? (size_t)(uh - a) : 0;
        if (!cut && mm_extract(a, bash, bsz, &eo) > 0) cut = eo;
        if (cut) {
            if (cut < al) { a[cut] = 0; al = cut; ftruncate(fileno(of), start + (off_t)cut); }
            kill(cp, SIGTERM); break;
        }
        if (stop) break;
    }
    fclose(fp); fclose(of); waitpid(cp, NULL, 0);
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

static int cmd_m(int c, char **v) {
    char b[B], sf[P], spf[P], pty[64] = "";
    if (!getenv("TMUX")) { puts("x needs tmux"); return 1; }
    const char *fn = c > 2 ? v[2] : "m.txt";
    snprintf(sf, P, "%s/m/%s", AROOT, fn);
    snprintf(spf, P, "%s/m/sysprompt.txt", AROOT);
    snprintf(b, B, "[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone);tmux split-window -dvb 'e %2$s'", AROOT, sf);
    system(b);
    if (!fexists(spf)) mm_w(spf, "`a m` chat: ```bash runs in pty cwd=m. STOP after bash. No ## headers, no fake output.\n", "w");
    if (!fexists(sf)) mm_w(sf, "## user\n", "w");
    snprintf(b, B, "tmux split-window -dvb -P -F '#{pane_id}' 'cd %s/m;exec bash'", AROOT);
    pcmd(b, pty, 64); pty[strcspn(pty, "\n")] = 0;
    for (;;) {
        char tf[] = "/tmp/m_inXXXXXX";
        int fd = mkstemp(tf); if (fd < 0) continue; close(fd);
        pid_t pp = fork();
        if (!pp) { execlp("e","e","--box","message:",tf,(char*)0); _exit(127); }
        waitpid(pp, NULL, 0);
        size_t ml; char *m = readf(tf, &ml); unlink(tf);
        if (!m || !ml) { free(m); continue; }
        mm_w(sf, m, "a"); mm_w(sf, "\n", "a"); free(m);
        for (int i = 0; i < 10; i++) {
            mm_w(sf, "\n## assistant\n", "a");
            static char a[64*1024], bash[8*1024], ob[16*1024];
            if (mm_stream(sf, spf, a, sizeof a, bash, sizeof bash) < 0) break;
            if (!bash[0]) break;
            mm_bash(pty, bash, ob, sizeof ob);
            char tb[20000]; time_t t = time(NULL);
            size_t n = strftime(tb, 64, "\n## tool output %FT%T\n", localtime(&t));
            snprintf(tb + n, sizeof tb - n, "%s\n", ob);
            mm_w(sf, tb, "a");
        }
        mm_w(sf, "\n## user\n", "a");
    }
    return 0;
}
