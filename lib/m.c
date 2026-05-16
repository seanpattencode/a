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

static int mm_delta_raw(const char *t, char *a, size_t *al, size_t sz) {
    while (*t && *t != '"' && *al + 1 < sz) {
        if (*t == '\\' && t[1]) {
            char c = t[1];
            a[(*al)++] = c == 'n' ? '\n' : c == '"' ? '"' : c == '\\' ? '\\' : c;
            t += 2;
        } else a[(*al)++] = *t++;
    }
    a[*al] = 0; return 0;
}
static int mm_delta(const char *l, char *a, size_t *al, size_t sz) {
    if (strstr(l, "\"message_stop\"")) return 1;
    const char *t = strstr(l, "\"text_delta\",\"text\":\"");
    if (!t) return 0;
    return mm_delta_raw(t+21, a, al, sz);
}
static int mm_delta_codex(const char *l, char *a, size_t *al, size_t sz) {
    if (strstr(l, "\"turn.completed\"")) return 1;
    const char *t = strstr(l, "\"agent_message\"");
    if (!t) return 0;
    t = strstr(t, "\"text\":\""); if (!t) return 0;
    return mm_delta_raw(t+8, a, al, sz);
}

static int mm_stream(const char *sf, const char *sp, char *a, size_t sz, char *bash, size_t bsz) {
    a[0] = bash[0] = 0;
    int pp[2]; if (pipe(pp) < 0) return -1;
    char acat[P]; snprintf(acat, P, "%s/m_combo.txt", TMP);
    int has_acat = fexists(acat);
    load_cfg();
    const char *ag = cfget("m_agent"); if (!*ag) ag = "claude";
    int is_codex = !strcmp(ag, "codex");
    const char *md = cfget("m_model"); if (!*md) md = is_codex ? "gpt-5.5" : "opus";
    const char *ef = cfget("m_effort"); if (!*ef) ef = is_codex ? "xhigh" : "low";
    const char *tier = cfget("m_tier");
    int has_tier = is_codex && *tier && strcmp(tier,"default");
    pid_t cp = fork();
    if (!cp) {
        int s = open(sf, O_RDONLY); if (s < 0) _exit(127);
        int d = open("/dev/null", O_WRONLY);
        dup2(s, 0); dup2(pp[1], 1); dup2(d, 2);
        close(s); close(d); close(pp[0]); close(pp[1]);
        if (is_codex) {
            char ecfg[64],tcfg[64]; snprintf(ecfg,64,"model_reasoning_effort=\"%s\"",ef);
            if (has_tier) {snprintf(tcfg,64,"service_tier=\"%s\"",tier);
                execlp("codex","codex","exec","--json","--skip-git-repo-check",
                       "--dangerously-bypass-approvals-and-sandbox","-m",md,"-c",ecfg,"-c",tcfg,(char*)0);}
            else execlp("codex","codex","exec","--json","--skip-git-repo-check",
                        "--dangerously-bypass-approvals-and-sandbox","-m",md,"-c",ecfg,(char*)0);
        } else if (has_acat)
            execlp("claude","claude","-p","--output-format","stream-json",
                   "--include-partial-messages","--verbose","--tools","",
                   "--model",md,"--effort",ef,
                   "--system-prompt", sp, "--append-system-prompt-file", acat,
                   "--settings", g_set, (char*)0);
        else
            execlp("claude","claude","-p","--output-format","stream-json",
                   "--include-partial-messages","--verbose","--tools","",
                   "--model",md,"--effort",ef,
                   "--system-prompt", sp, "--settings", g_set, (char*)0);
        _exit(127);
    }
    close(pp[1]); g_cp = cp;
    FILE *fp = fdopen(pp[0], "r"), *of = fopen(sf, "a");
    off_t start = ftello(of);
    char l[32768]; size_t al = 0, pl = 0, eo;
    while (fgets(l, sizeof l, fp)) {
        int stop = is_codex ? mm_delta_codex(l, a, &al, sz) : mm_delta(l, a, &al, sz);
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
    static const char *AGTS[] = {"claude","codex",NULL};
    static const char *MODS_C[] = {"opus","sonnet","haiku",NULL};
    static const char *MODS_X[] = {"gpt-5","gpt-5.5",NULL};
    static const char *EFFS_C[] = {"low","medium","high","max",NULL};
    static const char *EFFS_X[] = {"low","medium","high","xhigh",NULL};
    static const char *TIERS[] = {"default","fast","flex",NULL};
    static const struct { const char *l, *cm; } OPS[] = {
        {"archive","a m archive"},{"undo","a m archive undo"},{"restart","a m restart"},{NULL,NULL}};
    struct termios old, raw; tcgetattr(0, &old); raw = old;
    raw.c_lflag &= ~(tcflag_t)(ICANON|ECHO|ISIG);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    write(1, "\033[?1000h\033[?1006h", 16);
    char last[80] = "";
    int bx[64],bw[64],br[64],bi[64]; char bk[64]; int nb;
    for (;;) {
        load_cfg();
        const char *cg = cfget("m_agent"); if (!*cg) cg = "claude";
        int xx = !strcmp(cg, "codex");
        const char **MODS = xx ? MODS_X : MODS_C;
        const char **EFFS = xx ? EFFS_X : EFFS_C;
        const char *cm = cfget("m_model"); if (!*cm) cm = xx ? "gpt-5.5" : "opus";
        const char *cf = cfget("m_effort"); if (!*cf) cf = xx ? "xhigh" : "low";
        const char *ct = cfget("m_tier"); if (!*ct) ct = "default";
        int opsr = xx ? 5 : 4;
        struct stat st; long tot=0; char pa[P];
        snprintf(pa,P,"%s/m/m.txt",AROOT); if(!stat(pa,&st)) tot+=st.st_size;
        snprintf(pa,P,"%s/m_combo.txt",TMP); if(!stat(pa,&st)) tot+=st.st_size;
        tot/=4; long lim=!strcmp(cm,"opus")?1000000:200000; int pct=tot*100/lim;
        write(1, "\033[2J\033[H", 7); nb = 0;
        printf("\033[%dmtok %ldk/%s\033[0m ",pct>=80?31:pct>=50?33:32,tot/1000,lim>=1000000?"1M":"200k");
        if (xx) printf("codex exec --json -m %s -c model_reasoning_effort=\"%s\"%s%s%s --skip-git-repo-check --dangerously-bypass-approvals-and-sandbox\033[K\n",cm,cf,strcmp(ct,"default")?" -c service_tier=\"":"",strcmp(ct,"default")?ct:"",strcmp(ct,"default")?"\"":"");
        else printf("claude -p --model %s --effort %s --tools \"\" +sysprompt+settings\033[K\n",cm,cf);
        #define VROW(lbl,arr,row,kind,curv) do{int cx=printf("%s: ",lbl);\
            for(int i=0;arr[i];i++){int s=!strcmp(curv,arr[i]),w=(int)strlen(arr[i])+2;\
                bx[nb]=cx;bw[nb]=w;br[nb]=row;bk[nb]=kind;bi[nb]=i;nb++;\
                printf("%s[%s]%s ",s?"\033[7m":"",arr[i],s?"\033[0m":"");cx+=w+1;}printf("\033[K\n");}while(0)
        VROW("agent",AGTS,1,'a',cg); VROW("model",MODS,2,'m',cm); VROW("effort",EFFS,3,'e',cf);
        if (xx) VROW("tier",TIERS,4,'t',ct);
        #undef VROW
        {int cx=0;for(int i=0;OPS[i].l;i++){int w=(int)strlen(OPS[i].l)+2;
            bx[nb]=cx;bw[nb]=w;br[nb]=opsr;bk[nb]='o';bi[nb]=i;nb++;
            printf("[%s] ",OPS[i].l);cx+=w+1;}printf("\033[K\n");}
        printf("\033[90m%s\033[0m\033[K",last); fflush(stdout);
        char ch; if (read(0, &ch, 1) != 1) break;
        if (ch == 3 || ch == 4) break;
        if (ch != '\x1b') continue;
        int av; usleep(50000); ioctl(0, FIONREAD, &av); if (!av) break;
        char s[2]; if (read(0, s, 1) != 1 || s[0] != '[' || read(0, s+1, 1) != 1 || s[1] != '<') continue;
        int btn=0,mx=0,my=0; char mc;
        while (read(0,&mc,1)==1 && mc!=';') btn = btn*10+(mc-'0');
        while (read(0,&mc,1)==1 && mc!=';') mx = mx*10+(mc-'0');
        while (read(0,&mc,1)==1 && mc!='M' && mc!='m') my = my*10+(mc-'0');
        if (mc!='M' || btn!=0) continue;
        int gx = mx-1, gy = my-1;
        for (int i = 0; i < nb; i++) if (gy==br[i] && gx>=bx[i] && gx<bx[i]+bw[i]) {
            if (bk[i]=='a') { cfset("m_agent",AGTS[bi[i]]);
                int cx2=!strcmp(AGTS[bi[i]],"codex");
                cfset("m_model",cx2?"gpt-5.5":"opus"); cfset("m_effort",cx2?"xhigh":"low");
                snprintf(last,80,"agent=%s",AGTS[bi[i]]); }
            else if (bk[i]=='m') { cfset("m_model",MODS[bi[i]]); snprintf(last,80,"model=%s",MODS[bi[i]]); }
            else if (bk[i]=='e') { cfset("m_effort",EFFS[bi[i]]); snprintf(last,80,"effort=%s",EFFS[bi[i]]); }
            else if (bk[i]=='t') { cfset("m_tier",TIERS[bi[i]]); snprintf(last,80,"tier=%s",TIERS[bi[i]]); }
            else { char cmd[B],o[200]=""; snprintf(cmd,B,"%s 2>/dev/null",OPS[bi[i]].cm); int r=pcmd(cmd,o,200);
                o[strcspn(o,"\n")]=0; time_t tt=time(NULL); char ts[16]; strftime(ts,16,"%H:%M:%S",localtime(&tt));
                snprintf(last,80,"[%s] %s %s %s",ts,WIFEXITED(r)&&!WEXITSTATUS(r)?"\033[32m✓":"\033[31m✗",OPS[bi[i]].l,o);
                if(!WEXITSTATUS(r)){char rc[B];snprintf(rc,B,"F=$(cat %s/m_file 2>/dev/null||echo m.txt);tmux respawn-pane -k -t :.0 \"tail -Fn99999 %s/m/$F\";tmux clear-history -t :.0",TMP,AROOT);
                    (void)!system(rc);} }
            break;
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
    if (c > 3 && !strcmp(v[3], "undo")) {
        mkdirp(adir);
        DIR *d = opendir(adir); if (!d) { puts("no archive dir"); return 1; }
        struct dirent *e; static char latest[64] = ""; time_t lt = 0;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || !strstr(e->d_name, ".txt")) continue;
            char fp[P]; snprintf(fp, P, "%s/%s", adir, e->d_name);
            struct stat st;
            if (stat(fp, &st) == 0 && st.st_mtime > lt) {
                lt = st.st_mtime; snprintf(latest, 64, "%s", e->d_name);
                char *dot = strrchr(latest, '.'); if (dot) *dot = 0;
            }
        }
        closedir(d);
        if (!latest[0]) { puts("no archive to undo"); return 1; }
        char *nv[6] = {v[0], v[1], v[2], (char*)"unarchive", latest, NULL};
        return m_archive(5, nv);
    }
    /* rotate: a m archive [file.txt]  (no args, or .txt filename) */
    if (c == 3 || (c == 4 && strstr(v[3], ".txt"))) {
        const char *fn = (c == 4) ? v[3] : "m.txt";
        snprintf(mp, P, "%s/m/%s", AROOT, fn);
        size_t tl; char *txt = readf(mp, &tl);
        if (!txt) { printf("file not found: %s\n", mp); return 1; }
        /* preserve ## a-loaded block: start archive AFTER ## a-loaded-end if present */
        char *first = NULL, *aend = strstr(txt, "## a-loaded-end\n");
        if (aend) first = strstr(aend, "\n## user\n");
        if (!first) first = strstr(txt, "\n## user\n");
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

static int m_restart(void) {
    char c[B];
    snprintf(c, B, "(w=$(tmux display-message -p -t \"$TMUX_PANE\" '#W');f=$(cat %s/m_file 2>/dev/null||echo m.txt);tmux new-window -d -t a: -n m-%ld \"env -u M_IN a m $f\";sleep 1;tmux kill-window -t \"$w\")&",
             TMP, (long)time(NULL));
    return system(c);
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
    if (c > 2 && !strcmp(v[2], "restart")) return m_restart();
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
    { /* build combo: a_cat.txt + i.txt — always-loaded meta-agent identity for the session */
      char combo[P]; snprintf(combo,P,"%s/m_combo.txt",TMP);
      char acat[P]; snprintf(acat,P,"%s/local/a_cat.txt",AROOT);
      char ipath[P]; snprintf(ipath,P,"%s/m/i.txt",AROOT);
      FILE *co=fopen(combo,"w");
      if(co){char b2[8192];size_t n;FILE*in;
        if((in=fopen(acat,"r"))){while((n=fread(b2,1,8192,in))>0)fwrite(b2,1,n,co);fclose(in);}
        if((in=fopen(ipath,"r"))){fputs("\n==> i.txt (meta-agent identity) <==\n",co);
          while((n=fread(b2,1,8192,in))>0)fwrite(b2,1,n,co);fclose(in);}
        fclose(co);}
    }
    { /* a-loaded: write the full claude payload into m.txt so it's visible (hash-gated to avoid duplicates) */
      load_cfg();
      size_t sl=0,al=0,gl=0; char *sc=readf(spf,&sl);
      char combo[P]; snprintf(combo,P,"%s/m_combo.txt",TMP);
      char *ac=fexists(combo)?readf(combo,&al):NULL;
      char *gc=readf(g_set,&gl);
      const char *md=cfget("m_model"); if(!*md)md="opus";
      const char *ef=cfget("m_effort"); if(!*ef)ef="low";
      unsigned long h=5381;
      #define H(s,n) if(s) for(size_t i=0;i<(n);i++) h=((h<<5)+h)+(unsigned char)(s)[i]
      H(sc,sl); H(ac,al); H(gc,gl); H(md,strlen(md)); H(ef,strlen(ef));
      #undef H
      char mk[48]; snprintf(mk,48,"## a-loaded sha=%08lx",h);
      char *cur=readf(sf,NULL);
      if (!cur || !strstr(cur,mk)) {
          FILE *f=fopen(sf,"a");
          if (f) { time_t t=time(NULL); char ts[32];
              strftime(ts,32,"%FT%T",localtime(&t));
              fprintf(f,"\n%s %s\nflags: --model %s --effort %s --tools \"\"\n",mk,ts,md,ef);
              fprintf(f,"--system-prompt:\n%s\n",sc?sc:"");
              if(ac) fprintf(f,"--append-system-prompt-file: %s (%zu b)\n%s\n",combo,al,ac);
              fprintf(f,"--settings: %s\n%s\n## a-loaded-end\n## user\n",g_set,gc?gc:"");
              fclose(f); }
      }
      free(cur); free(sc); free(ac); free(gc); }
    mm_w(ss, "", "w"); m_status("ready ^C=interrupt");
    setenv("M_IN", "1", 1);
    snprintf(b, B, "[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone);"
                   "S=\"tmux split-window -t $TMUX_PANE -e M_IN=1 -dvb\";"
                   "$S 'tail -Fn99999 %2$s';$S -l 3 'tail -Fn 50 %3$s';"
                   "$S -l 7 'a m panel'",
             AROOT, sf, ss);
    system(b);
    snprintf(b, B, "tmux split-window -t $TMUX_PANE -e M_IN=1 -dvb -P -F '#{pane_id}' 'cd %s/m;exec bash'", AROOT);
    pcmd(b, pty, 64); pty[strcspn(pty, "\n")] = 0;
    { char pf[P]; snprintf(pf, P, "%s/m_pty", TMP); mm_w(pf, pty, "w");
      snprintf(pf, P, "%s/m_file", TMP); mm_w(pf, fn, "w"); }
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
