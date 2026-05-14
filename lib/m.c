/* m — chat clone with streaming cutoff + agentic loop, all in C. */

static int mm_spawn_claude(const char *sf, const char *sysp, pid_t *out_pid) {
    int pipefd[2]; if (pipe(pipefd) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
    if (pid == 0) {
        int sfd = open(sf, O_RDONLY); if (sfd < 0) _exit(127);
        dup2(sfd, 0); close(sfd);
        dup2(pipefd[1], 1); close(pipefd[1]); close(pipefd[0]);
        int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 2); close(dn); }
        execlp("claude","claude","-p","--output-format","stream-json",
               "--include-partial-messages","--verbose",
               "--append-system-prompt-file", sysp, (char*)NULL);
        _exit(127);
    }
    close(pipefd[1]); *out_pid = pid; return pipefd[0];
}

/* Find an opening ```bash / ```sh / ```shell / ``` fence at start of line.
   Returns byte offset of fence start, or -1. Sets *body_off to offset after the trailing \n. */
static long mm_find_open(const char *acc, size_t *body_off) {
    const char *p = acc; const char *o;
    while ((o = strstr(p, "```"))) {
        if (o == acc || *(o-1) == '\n') {
            const char *a = o + 3;
            if (!strncmp(a,"bash\n",5)) { *body_off = (size_t)(a+5 - acc); return o - acc; }
            if (!strncmp(a,"sh\n",3))   { *body_off = (size_t)(a+3 - acc); return o - acc; }
            if (!strncmp(a,"shell\n",6)){ *body_off = (size_t)(a+6 - acc); return o - acc; }
            if (*a == '\n')             { *body_off = (size_t)(a+1 - acc); return o - acc; }
        }
        p = o + 3;
    }
    return -1;
}

/* Find a closing \n```\n or \n``` at end. Returns byte offset of \n in fence, or -1.
   Sets *end_off to offset just past the fence (where to truncate acc to). */
static long mm_find_close(const char *acc, size_t from, size_t *end_off) {
    const char *p = acc + from;
    const char *c = strstr(p, "\n```\n");
    if (c) { *end_off = (size_t)(c - acc) + 5; return c - acc; }
    c = strstr(p, "\n```");
    if (c && c[4] == 0) { *end_off = (size_t)(c - acc) + 4; return c - acc; }
    return -1;
}

/* Parse one stream-json delta line, extract text into acc. Returns 1 if message_stop seen. */
static int mm_extract_delta(const char *line, char *acc, size_t *acc_len, size_t acc_sz) {
    if (strstr(line, "\"type\":\"message_stop\"")) return 1;
    const char *t = strstr(line, "\"text_delta\",\"text\":\"");
    if (!t) return 0;
    t += 21;
    while (*t && *acc_len + 8 < acc_sz) {
        if (*t == '\\') {
            t++;
            switch (*t) {
                case 'n': acc[(*acc_len)++] = '\n'; break;
                case 't': acc[(*acc_len)++] = '\t'; break;
                case 'r': acc[(*acc_len)++] = '\r'; break;
                case '"': acc[(*acc_len)++] = '"'; break;
                case '\\': acc[(*acc_len)++] = '\\'; break;
                case '/': acc[(*acc_len)++] = '/'; break;
                case 'b': acc[(*acc_len)++] = '\b'; break;
                case 'f': acc[(*acc_len)++] = '\f'; break;
                case 'u': {
                    if (t[1] && t[2] && t[3] && t[4]) {
                        char hex[5] = {t[1],t[2],t[3],t[4],0};
                        unsigned cp = (unsigned)strtoul(hex, NULL, 16);
                        if (cp < 0x80) acc[(*acc_len)++] = (char)cp;
                        else if (cp < 0x800) {
                            acc[(*acc_len)++] = (char)(0xC0 | (cp>>6));
                            acc[(*acc_len)++] = (char)(0x80 | (cp&0x3F));
                        } else {
                            acc[(*acc_len)++] = (char)(0xE0 | (cp>>12));
                            acc[(*acc_len)++] = (char)(0x80 | ((cp>>6)&0x3F));
                            acc[(*acc_len)++] = (char)(0x80 | (cp&0x3F));
                        }
                        t += 4;
                    }
                    break;
                }
                default: acc[(*acc_len)++] = *t; break;
            }
            if (*t) t++;
        } else if (*t == '"') break;
        else acc[(*acc_len)++] = *t++;
    }
    acc[*acc_len] = 0;
    return 0;
}

/* Run one claude invocation with streaming cutoff. Fills acc (response up to+including
   closing fence if found) and bash (extracted block content, "" if none). */
static int mm_stream_one(const char *sf, const char *sysp, char *acc, size_t acc_sz,
                         char *bash, size_t bash_sz) {
    acc[0] = 0; bash[0] = 0;
    pid_t cpid;
    int fd = mm_spawn_claude(sf, sysp, &cpid);
    if (fd < 0) return -1;
    FILE *fp = fdopen(fd, "r");
    if (!fp) { close(fd); return -1; }
    char line[32768];
    size_t acc_len = 0;
    int in_block = 0;
    size_t body_off = 0;
    while (fgets(line, sizeof(line), fp)) {
        int stop = mm_extract_delta(line, acc, &acc_len, acc_sz);
        if (!in_block) {
            long o = mm_find_open(acc, &body_off);
            if (o >= 0) in_block = 1;
        }
        if (in_block) {
            size_t end_off;
            long c = mm_find_close(acc, body_off, &end_off);
            if (c >= 0) {
                if (end_off < acc_len) acc[end_off] = 0;
                kill(cpid, SIGTERM);
                break;
            }
        }
        if (stop) break;
    }
    fclose(fp);
    int wstat; waitpid(cpid, &wstat, 0);
    /* extract bash body */
    size_t body;
    long o = mm_find_open(acc, &body);
    if (o >= 0) {
        size_t end;
        long c = mm_find_close(acc, body, &end);
        if (c >= 0) {
            size_t blen = (size_t)(c - (long)body);
            if (blen >= bash_sz) blen = bash_sz - 1;
            memcpy(bash, acc + body, blen);
            bash[blen] = 0;
        }
    }
    return 0;
}

/* Strip lines starting with ## user / ## assistant / ## tool output (LLM-emitted fakes). */
static void mm_filter(const char *in, char *out, size_t out_sz) {
    const char *p = in; size_t oi = 0;
    while (*p && oi < out_sz - 1) {
        const char *eol = strchr(p, '\n');
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        int skip = (llen >= 7  && !strncmp(p,"## user",7)) ||
                   (llen >= 12 && !strncmp(p,"## assistant",12)) ||
                   (llen >= 14 && !strncmp(p,"## tool output",14));
        if (!skip) {
            size_t copy = llen; if (oi + copy >= out_sz - 1) copy = out_sz - 1 - oi;
            memcpy(out + oi, p, copy); oi += copy;
            if (eol && oi < out_sz - 1) out[oi++] = '\n';
        }
        if (!eol) break;
        p = eol + 1;
    }
    out[oi] = 0;
}

/* Send a bash block to the persistent pty, sleep, capture pane, return indented output. */
static void mm_run_bash(const char *pty, const char *cmd, char *out, size_t out_sz) {
    pid_t p = fork();
    if (p == 0) { execlp("tmux","tmux","send-keys","-t",pty,"-l",cmd,(char*)NULL); _exit(127); }
    waitpid(p, NULL, 0);
    p = fork();
    if (p == 0) { execlp("tmux","tmux","send-keys","-t",pty,"Enter",(char*)NULL); _exit(127); }
    waitpid(p, NULL, 0);
    sleep(3);
    char shellcmd[B];
    snprintf(shellcmd, B, "tmux capture-pane -t '%s' -p -S -50|tail -30|sed 's/^/    /'", pty);
    pcmd(shellcmd, out, out_sz);
}

static int cmd_m(int c, char **v) {
    char b[B], sf[P], ss[P], pid_f[P], syspf[P], pty_id[64] = "";
    snprintf(b, B, "[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone)", AROOT);
    system(b);
    const char *fn = (c > 2) ? v[2] : "m.txt";
    snprintf(sf, P, "%s/m/%s", AROOT, fn);
    snprintf(ss, P, "%s/m_status", TMP);
    snprintf(pid_f, P, "%s/m_pty_id", TMP);
    snprintf(syspf, P, "%s/m/sysprompt.txt", AROOT);
    if (!fexists(syspf)) {
        FILE *f = fopen(syspf, "w");
        if (f) {
            fputs("You are in `a m` chat. To run shell commands, emit a fenced bash block (```bash ... ```). The block runs in a persistent pty (cwd=m repo); its captured output is appended as '## tool output <timestamp>' with each line indented 4 spaces. After emitting a bash block, STOP your response immediately — you will be re-invoked with the real tool output. NEVER emit `## user`, `## assistant`, or `## tool output` headers yourself; those are conversation structure added by the system. NEVER simulate or imagine tool output; wait for the real captured output.\n", f);
            fclose(f);
        }
    }
    if (!fexists(sf)) {
        FILE *f = fopen(sf, "w"); FILE *sp = fopen(syspf, "r");
        if (f) {
            if (sp) { fputs("## system\n", f); char tb[4096]; size_t n;
                while ((n = fread(tb, 1, 4096, sp)) > 0) fwrite(tb, 1, n, f);
                fclose(sp); fputs("\n## user\n", f);
            } else fputs("## user\n", f);
            fclose(f);
        }
    }
    { FILE *f = fopen(ss, "w"); if (f) { fputs("ready", f); fclose(f); } }
    if (!getenv("TMUX")) { puts("x needs tmux"); return 1; }
    snprintf(b, B, "tmux split-window -dvb 'e %s'", sf); system(b);
    snprintf(b, B, "tmux split-window -dvb -P -F '#{pane_id}' 'cd %s/m;exec bash'", AROOT);
    pcmd(b, pty_id, 64); pty_id[strcspn(pty_id, "\n")] = 0;
    { FILE *f = fopen(pid_f, "w"); if (f) { fputs(pty_id, f); fclose(f); } }
    snprintf(b, B, "tmux split-window -dv -l 1 'P(){ printf \"\\r\\033[K%%s\" \"$(cat %1$s)\";};P;while inotifywait -qe modify %1$s 2>/dev/null;do P;done'", ss);
    system(b);
    /* box loop in C */
    for (;;) {
        char tf[] = "/tmp/m_inXXXXXX";
        int tfd = mkstemp(tf); if (tfd < 0) continue; close(tfd);
        pid_t bp = fork();
        if (bp == 0) { execlp("e","e","--box","message:",tf,(char*)NULL); _exit(127); }
        waitpid(bp, NULL, 0);
        size_t mlen; char *msg = readf(tf, &mlen); unlink(tf);
        if (!msg || mlen == 0) { free(msg); continue; }
        { FILE *f = fopen(sf, "a"); if (f) { fwrite(msg,1,mlen,f); if (msg[mlen-1]!='\n') fputc('\n',f); fclose(f); } }
        free(msg);
        for (int iter = 0; iter < 10; iter++) {
            { FILE *f = fopen(ss, "w"); if (f) { fputs("sending", f); fclose(f); } }
            { FILE *f = fopen(sf, "a"); if (f) { fputs("\n## assistant\n", f); fclose(f); } }
            static char acc[64*1024], filt[64*1024], bash[8*1024], outb[16*1024];
            if (mm_stream_one(sf, syspf, acc, sizeof(acc), bash, sizeof(bash)) < 0) break;
            mm_filter(acc, filt, sizeof(filt));
            { FILE *f = fopen(sf, "a"); if (f) { fputs(filt, f); fclose(f); } }
            if (!bash[0]) break;
            { FILE *f = fopen(ss, "w"); if (f) { fputs("running", f); fclose(f); } }
            mm_run_bash(pty_id, bash, outb, sizeof(outb));
            time_t now = time(NULL); struct tm *tm = localtime(&now);
            char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", tm);
            { FILE *f = fopen(sf, "a"); if (f) { fprintf(f, "\n## tool output %s\n%s\n", ts, outb); fclose(f); } }
        }
        { FILE *f = fopen(sf, "a"); if (f) { fputs("\n## user\n", f); fclose(f); } }
        { FILE *f = fopen(ss, "w"); if (f) { fputs("done", f); fclose(f); } }
    }
    return 0;
}
