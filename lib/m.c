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
        snprintf(c, B, "cd '%s/m' && git pull --rebase -q 2>/dev/null; git add -A && (git diff --cached --quiet || git commit -q -m '%s'); git push -q 2>/dev/null", AROOT, tag);
        execlp("sh", "sh", "-c", c, NULL); _exit(127);
    }
    if (p > 0) { g_cmt = p; char sm[32]; snprintf(sm, 32, "git sync %s", tag); m_status(sm); }
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
static int mm_delta_gemini(const char *l, char *a, size_t *al, size_t sz) {
    if (strstr(l, "\"type\":\"result\"")) return 1;
    const char *t = strstr(l, "\"role\":\"assistant\"");
    if (!t) return 0;
    t = strstr(t, "\"content\":\""); if (!t) return 0;
    return mm_delta_raw(t+11, a, al, sz);
}

static int mm_stream(const char *sf, const char *sp, char *a, size_t sz, char *bash, size_t bsz) {
    a[0] = bash[0] = 0;
    int pp[2]; if (pipe(pp) < 0) return -1;
    char acat[P]; snprintf(acat, P, "%s/m_combo.txt", TMP);
    int has_acat = fexists(acat);
    load_cfg();
    const char *ag = cfget("m_agent"); if (!*ag) ag = "claude";
    int is_codex = !strcmp(ag, "codex"), is_gemini = !strcmp(ag, "gemini");
    const char *md = cfget("m_model"); if (!*md) md = is_codex ? "gpt-5.5" : is_gemini ? "gemini-2.5-flash" : "opus";
    const char *ef = cfget("m_effort"); if (!*ef) ef = is_codex ? "xhigh" : "low";
    const char *tier = cfget("m_tier");
    int has_tier = is_codex && *tier && strcmp(tier,"default");
    char errp[P]; snprintf(errp,P,"%s/m_err.log",TMP);
    pid_t cp = fork();
    if (!cp) {
        int s = open(sf, O_RDONLY); if (s < 0) _exit(127);
        int d = open(errp, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        dup2(s, 0); dup2(pp[1], 1); dup2(d, 2);
        close(s); close(d); close(pp[0]); close(pp[1]);
        if (is_gemini) { char g[B];
            snprintf(g,B,"awk '/^## a-loaded /{s=1;next} s&&/^## a-loaded-end$/{s=0;next} !s'|gemini -p '' --skip-trust --approval-mode plan -o stream-json -m '%s'",md);
            execlp("sh","sh","-c",g,(char*)0); }
        else if (is_codex) { char x[B],t[80]="";
            if (has_tier) snprintf(t,80," -c 'service_tier=\"%s\"'",tier);
            snprintf(x,B,"iconv -f UTF-8 -t UTF-8 -c|codex exec --json --skip-git-repo-check --dangerously-bypass-approvals-and-sandbox -m '%s' -c 'model_reasoning_effort=\"%s\"'%s",md,ef,t);
            execlp("sh","sh","-c",x,(char*)0);
        } else { char x[B*2],ap[P]="";
            if (has_acat) snprintf(ap,P," --append-system-prompt-file '%s'",acat);
            snprintf(x,B*2,"awk '/^## a-loaded /{s=1;next} s&&/^## a-loaded-end$/{s=0;next} !s'|claude -p --output-format stream-json --include-partial-messages --verbose --tools '' --model '%s' --effort '%s' --system-prompt \"$1\"%s --settings '%s'",md,ef,ap,g_set);
            execlp("sh","sh","-c",x,"_",sp,(char*)0);
        }
        _exit(127);
    }
    close(pp[1]); g_cp = cp;
    FILE *fp = fdopen(pp[0], "r"), *of = fopen(sf, "a");
    off_t start = ftello(of);
    char l[32768]; size_t al = 0, pl = 0, eo;
    while (fgets(l, sizeof l, fp)) {
        int stop = is_codex ? mm_delta_codex(l, a, &al, sz) : is_gemini ? mm_delta_gemini(l, a, &al, sz) : mm_delta(l, a, &al, sz);
        if (al > pl) { if (!pl) m_status("streaming"); fwrite(a + pl, 1, al - pl, of); fflush(of); pl = al; }
        char *uh=strstr(a,"\n## user\n");
        if (uh || mm_extract(a, bash, bsz, &eo) > 0) { size_t cut=uh?(size_t)(uh-a):al;
            if (uh && cut<al) { a[cut]=0; al=cut; ftruncate(fileno(of),start+(off_t)cut); }
            kill(cp, SIGTERM); break; }
        if (stop) break;
    }
    fclose(fp); fclose(of); waitpid(cp, NULL, 0); g_cp = 0;
    if (!pl) { char *e=readf(errp,NULL); if(e&&*e){char m[2048]; snprintf(m,2048,"\n## error (%s)\n%s\n",ag,e); mm_w(sf,m,"a");} free(e); }
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
    static const char *AGTS[] = {"claude","codex","gemini",NULL};
    static const char *MODS_C[] = {"opus","sonnet","haiku",NULL};
    static const char *MODS_X[] = {"gpt-5","gpt-5.5",NULL};
    static const char *MODS_G[] = {"gemini-2.5-flash","gemini-2.5-pro","gemini-3-pro-preview",NULL};
    static const char *EFFS_C[] = {"low","medium","high","max",NULL};
    static const char *EFFS_X[] = {"low","medium","high","xhigh",NULL};
    static const char *TIERS[] = {"default","fast","flex",NULL};
    static const struct { const char *l, *cm; } OPS[] = {
        {"main agent","a m main"},{"new agent","a m new"},
        {"archive turn","a m archive turn"},{"archive","a m archive"},
        {"undo","a m archive undo"},{"restart","a m restart"},{NULL,NULL}};
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
        int xx = !strcmp(cg, "codex"), gg = !strcmp(cg, "gemini");
        const char **MODS = xx ? MODS_X : gg ? MODS_G : MODS_C;
        const char **EFFS = xx ? EFFS_X : EFFS_C;
        const char *cm = cfget("m_model"); if (!*cm) cm = xx ? "gpt-5.5" : gg ? "gemini-2.5-flash" : "opus";
        const char *cf = cfget("m_effort"); if (!*cf) cf = xx ? "xhigh" : "low";
        const char *ct = cfget("m_tier"); if (!*ct) ct = "default";
        int opsr = 3 + (!gg) + xx;
        struct stat st; long tot=0; char pa[P];
        snprintf(pa,P,"%s/m/m.txt",AROOT); if(!stat(pa,&st)) tot+=st.st_size;
        snprintf(pa,P,"%s/m_combo.txt",TMP); if(!stat(pa,&st)) tot+=st.st_size;
        tot/=4; long lim=(gg||!strcmp(cm,"opus"))?1000000:200000; int pct=tot*100/lim;
        write(1, "\033[2J\033[H", 7); nb = 0;
        printf("\033[%dmtok %ldk/%s\033[0m ",pct>=80?31:pct>=50?33:32,tot/1000,lim>=1000000?"1M":"200k");
        if (gg) printf("gemini --yolo --output-format stream-json -m %s\033[K",cm);
        else if (xx) printf("codex exec --json -m %s -c model_reasoning_effort=\"%s\"%s%s%s --skip-git-repo-check --dangerously-bypass-approvals-and-sandbox\033[K",cm,cf,strcmp(ct,"default")?" -c service_tier=\"":"",strcmp(ct,"default")?ct:"",strcmp(ct,"default")?"\"":"");
        else printf("claude -p --model %s --effort %s --tools \"\" +sysprompt+settings\033[K",cm,cf);
        printf("\033[2;1H");
        #define VROW(lbl,arr,row,kind,curv) do{int cx=printf("%s: ",lbl);\
            for(int i=0;arr[i];i++){int s=!strcmp(curv,arr[i]),w=(int)strlen(arr[i])+2;\
                bx[nb]=cx;bw[nb]=w;br[nb]=row;bk[nb]=kind;bi[nb]=i;nb++;\
                printf("%s[%s]%s ",s?"\033[7m":"",arr[i],s?"\033[0m":"");cx+=w+1;}printf("\033[K\n");}while(0)
        VROW("agent",AGTS,1,'a',cg); VROW("model",MODS,2,'m',cm);
        if (!gg) VROW("effort",EFFS,3,'e',cf);
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
            if (bk[i]=='a') { const char*a=AGTS[bi[i]]; cfset("m_agent",a);
                cfset("m_model",!strcmp(a,"codex")?"gpt-5.5":!strcmp(a,"gemini")?"gemini-2.5-flash":"opus");
                cfset("m_effort",!strcmp(a,"codex")?"xhigh":"low");
                snprintf(last,80,"agent=%s",a); }
            else if (bk[i]=='m') { cfset("m_model",MODS[bi[i]]); snprintf(last,80,"model=%s",MODS[bi[i]]); }
            else if (bk[i]=='e') { cfset("m_effort",EFFS[bi[i]]); snprintf(last,80,"effort=%s",EFFS[bi[i]]); }
            else if (bk[i]=='t') { cfset("m_tier",TIERS[bi[i]]); snprintf(last,80,"tier=%s",TIERS[bi[i]]); }
            else { static time_t aw=0; time_t now=time(NULL);
                if (!strcmp(OPS[bi[i]].l,"archive") && now-aw>=10) {
                    snprintf(last,80,"\033[33m⚠ main is for curation, not bulk-archive — click again within 10s, or use [archive turn]\033[0m");
                    aw=now; break; }
                aw=0;
                char cmd[B],o[200]=""; snprintf(cmd,B,"%s 2>/dev/null",OPS[bi[i]].cm); int r=pcmd(cmd,o,200);
                o[strcspn(o,"\n")]=0; time_t tt=time(NULL); char ts[16]; strftime(ts,16,"%H:%M:%S",localtime(&tt));
                snprintf(last,80,"[%s] %s %s %s",ts,WIFEXITED(r)&&!WEXITSTATUS(r)?"\033[32m✓":"\033[31m✗",OPS[bi[i]].l,o);
                if(!WEXITSTATUS(r)&&(strstr(OPS[bi[i]].l,"archive")||!strcmp(OPS[bi[i]].l,"undo"))){char rc[B];snprintf(rc,B,"F=$(cat %s/m_file 2>/dev/null||echo m.txt);tmux respawn-pane -k -t '{last}.0' \"e --tail %s/m/$F\"",TMP,AROOT);
                    (void)!system(rc);} }
            break;
        }
    }
    write(1, "\033[?1000l\033[?1006l", 16);
    tcsetattr(0, TCSANOW, &old);
    return 0;
}

static int cmd_m_sw(int c, char **v) {
    int it = c>3 && !strcmp(v[3],"main");  /* arg "main" means we're in tools, button shows "← main" */
    struct termios old, raw; tcgetattr(0,&old); raw=old;
    raw.c_lflag &= ~(tcflag_t)(ICANON|ECHO|ISIG); raw.c_cc[VMIN]=1; raw.c_cc[VTIME]=0;
    tcsetattr(0,TCSANOW,&raw); write(1,"\033[?1000h\033[?1006h",16);
    write(1,"\033[2J\033[H",7);
    printf("\033[7;1m %s \033[0m\033[K",it?"← main":"tools →"); fflush(stdout);
    for(;;) {
        char ch; if(read(0,&ch,1)!=1) break;
        if(ch==3||ch==4) break;
        if(ch!='\x1b') continue;
        char s[2]; if(read(0,s,1)!=1||s[0]!='['||read(0,s+1,1)!=1||s[1]!='<') continue;
        int btn=0; char mc;
        while(read(0,&mc,1)==1&&mc!=';') btn=btn*10+(mc-'0');
        while(read(0,&mc,1)==1&&mc!=';'){} while(read(0,&mc,1)==1&&mc!='M'&&mc!='m'){}
        if(mc!='M'||btn!=0) continue;
        (void)!system(it?"tmux select-window -l":"tmux select-window -t :m-tools");
    }
    write(1,"\033[?1000l\033[?1006l",16); tcsetattr(0,TCSANOW,&old); return 0;
}

static void m_arch_commit(const char *kind, const char *ts) {
    char c[B]; snprintf(c,B,"git -C '%1$s/m' add -A 2>/dev/null && (git -C '%1$s/m' diff --cached --quiet 2>/dev/null || git -C '%1$s/m' commit -q -m 'archive %2$s %3$s' 2>/dev/null)",AROOT,kind,ts);
    (void)!system(c);
}

/* write [s,e) of txt to archive/<ts>.txt, replace section in mp with marker, commit. returns ts (static). */
static char *m_arch_save(char *txt, size_t tl, char *s, char *e, const char *mp, const char *adir, const char *kind, const char *info) {
    static char ts[32]; char ap[P],mk[256];
    mkdirp(adir);
    time_t tt=time(NULL); strftime(ts,32,"%Y%m%dT%H%M%S",localtime(&tt));
    snprintf(ap,P,"%s/%s.txt",adir,ts);
    FILE *af=fopen(ap,"w"); if(af){fwrite(s,1,(size_t)(e-s),af); fclose(af);}
    snprintf(mk,256,"[archived %s · %s · view: a m archive view %s]\n",ts,info,ts);
    FILE *f=fopen(mp,"w"); if(f){fwrite(txt,1,(size_t)(s-txt),f); fputs(mk,f); fwrite(e,1,(size_t)((txt+tl)-e),f); fclose(f);}
    m_arch_commit(kind, ts);
    return ts;
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
    if (c > 3 && !strcmp(v[3], "turn")) {
        const char *fn = c > 4 ? v[4] : "m.txt";
        snprintf(mp, P, "%s/m/%s", AROOT, fn);
        size_t tl; char *txt = readf(mp, &tl);
        if (!txt) { printf("not found: %s\n", mp); return 1; }
        char *last_um=NULL, *prev_um=NULL, *p=txt;
        char *aend = strstr(txt, "## a-loaded-end\n"); const char *start = aend?aend:txt;
        while ((p = strstr(p+1, "\n## user\n")) && p < txt + tl) {
            if (p+1 < start) continue;
            prev_um = last_um; last_um = p + 1;
        }
        if (!prev_um || !last_um || prev_um == last_um) { puts("no completed turn to archive"); free(txt); return 0; }
        printf("archived 1 turn → archive/%s.txt\n", m_arch_save(txt, tl, prev_um, last_um, mp, adir, "turn", "turn"));
        free(txt); return 0;
    }
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
        char *first=NULL, *aend=strstr(txt, "## a-loaded-end\n");
        if (aend) first = strstr(aend, "\n## user\n");
        if (!first) first = strstr(txt, "\n## user\n");
        if (!first) { puts("no conversation"); free(txt); return 0; }
        first++;
        char *last = first, *p = first;
        while ((p = strstr(p + 1, "\n## user\n")) != NULL) last = p + 1;
        if (last == first) { puts("nothing to rotate"); free(txt); return 0; }
        printf("rotated → archive/%s.txt\n", m_arch_save(txt, tl, first, last, mp, adir, "rotate", "rotate"));
        free(txt); return 0;
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
        free(cnt); free(txt); printf("unarchived %s\n", v[4]); m_arch_commit("unarchive", v[4]); return 0;
    }
    int N = atoi(v[3]); const char *dash = strchr(v[3], '-');
    int M = dash ? atoi(dash + 1) : N;
    if (N < 1 || M < N) { fprintf(stderr, "invalid range '%s': want N-M with N>=1 and M>=N\n", v[3]); return 1; }
    size_t tl; char *txt = readf(mp, &tl);
    if (!txt) { printf("file not found: %s\n", mp); return 1; }
    char *s = txt; int ln = 1;
    while (ln < N && s < txt + tl) { if (*s++ == '\n') ln++; }
    if (ln < N) { printf("past EOF (%d lines)\n", ln); free(txt); return 1; }
    char *e = s; int cnt = 0;
    while (e < txt + tl && cnt < M - N + 1) { if (*e++ == '\n') cnt++; }
    char info[32]; snprintf(info,32,"L%d-%d",N,M);
    printf("archived %s → archive/%s.txt (%ld bytes)\n", info, m_arch_save(txt, tl, s, e, mp, adir, "range", info), (long)(e-s));
    free(txt); return 0;
}

static int m_reinit(const char *fn) {
    char c[B]; snprintf(c,B,"%s/m_file",TMP); mm_w(c,fn,"w");
    snprintf(c,B,"tmux respawn-pane -k -t '{last}.0' 'e --tail %s/m/%s'",AROOT,fn); (void)!system(c);
    snprintf(c,B,"%s/m_editorpid",TMP); char*p=readf(c,NULL);
    if(p)kill(atoi(p),SIGTERM); free(p); return 0;
}
static int m_restart(void){char p[P];snprintf(p,P,"%s/m_file",TMP);char*fp=readf(p,NULL);char fn[64]="m.txt";if(fp&&*fp){fp[strcspn(fp,"\n")]=0;snprintf(fn,64,"%s",fp);}free(fp);return m_reinit(fn);}
static int m_main(void){return m_reinit("m.txt");}
static int m_new(void){time_t t=time(NULL);char fn[64];strftime(fn,64,"agent-%Y%m%dT%H%M%S.txt",localtime(&t));return m_reinit(fn);}

static int m_reset(void) {
    char pf[P],c[B]; snprintf(pf,P,"%s/m_pty",TMP);
    char *p=readf(pf,NULL); if(!p||!*p){puts("no pty (a m not running here)");free(p);return 1;}
    p[strcspn(p,"\n")]=0;
    snprintf(c,B,"tmux send-keys -t '%1$s' C-c; tmux send-keys -t '%1$s' 'clear' Enter",p);
    int r=system(c); free(p); return r;
}

static void m_render(const char *sf, const char *spf) {
    /* materialized view: rebuild combo from latest a_cat + i.txt, append a-loaded block if hash changed */
    char combo[P]; snprintf(combo,P,"%s/m_combo.txt",TMP);
    { char c[B]; snprintf(c,B,"{ cat %1$s/local/a_cat.txt 2>/dev/null; printf '\\n==> i.txt (meta-agent identity) <==\\n'; cat %1$s/m/i.txt 2>/dev/null; } > %2$s",AROOT,combo);
      (void)!system(c); }
    load_cfg();
    size_t sl=0,al=0,gl=0; char *sc=readf(spf,&sl);
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
            fprintf(f,"\n%s %s\nflags: --model %s --effort %s --tools \"\"\n--system-prompt:\n%s\n",mk,ts,md,ef,sc?sc:"");
            if(ac) fprintf(f,"--append-system-prompt-file: %s (%zu b)\n%s\n",combo,al,ac);
            fprintf(f,"--settings: %s\n%s\n## a-loaded-end\n## user\n",g_set,gc?gc:"");
            fclose(f); }
    }
    free(cur); free(sc); free(ac); free(gc);
}

static int cmd_m(int c, char **v) {
    if (c > 2 && !strcmp(v[2], "archive")) return m_archive(c, v);
    if (c > 2 && !strcmp(v[2], "panel")) return cmd_m_panel(c, v);
    if (c > 2 && !strcmp(v[2], "sw")) return cmd_m_sw(c, v);
    if (c > 2 && !strcmp(v[2], "reset")) return m_reset();
    if (c > 2 && !strcmp(v[2], "restart")) return m_restart();
    if (c > 2 && !strcmp(v[2], "main")) return m_main();
    if (c > 2 && !strcmp(v[2], "new")) return m_new();
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
    mm_w(ss, "", "w");
    setenv("M_IN", "1", 1);
    load_cfg(); const char *layout = cfget("m_layout"); if(!*layout) layout="2";
    if (!strcmp(layout,"1")) {
        snprintf(b, B, "[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone);"
                       "(cd %1$s/m && git pull --rebase -q 2>/dev/null) & "
                       "S=\"tmux split-window -t $TMUX_PANE -e M_IN=1 -dvb\";"
                       "$S 'e --tail %2$s';$S -l 3 'tail -Fn 50 %3$s';$S -l 9 'a m panel';"
                       "tmux split-window -t $TMUX_PANE -e M_IN=1 -dvb -P -F '#{pane_id}' 'cd %1$s/m;exec bash'",
                 AROOT, sf, ss);
    } else {
        snprintf(b, B, "[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone);"
                       "(cd %1$s/m && git pull --rebase -q 2>/dev/null) & "
                       "S=\"tmux split-window -e M_IN=1 -dv\";"
                       "$S -t $TMUX_PANE -b 'e --tail %2$s';$S -t $TMUX_PANE -l 1 'a m sw tools';"
                       "W=$(tmux new-window -d -P -F '#{window_id}' -e M_IN=1 -n m-tools 'a m panel');"
                       "$S -t $W -l 1 'a m sw main';$S -t $W -l 7 -P -F '#{pane_id}' 'cd %1$s/m;exec bash';"
                       "$S -t $W -l 3 'tail -Fn 50 %3$s'",
                 AROOT, sf, ss);
    }
    pcmd(b, pty, 64); pty[strcspn(pty, "\n")] = 0;
    char pf[P];
    snprintf(pf,P,"%s/m_pty",TMP); mm_w(pf,pty,"w");
    snprintf(pf,P,"%s/m_file",TMP); mm_w(pf,fn,"w");
    for (;;) {
        g_halt = 0;
        snprintf(pf,P,"%s/m_file",TMP); char *fp=readf(pf,NULL);
        if(fp&&*fp){fp[strcspn(fp,"\n")]=0;snprintf(sf,P,"%s/m/%s",AROOT,fp);} free(fp);
        { char *cur=fexists(sf)?readf(sf,NULL):NULL;
          if (!cur||strncmp(cur,"## system\n",10)) {
            size_t spl;char *sp=readf(spf,&spl);FILE *f=fopen(sf,"w");
            if(f){fprintf(f,"## system\n%s%s%s",sp?sp:"",(sp&&spl&&sp[spl-1]=='\n')?"":"\n",cur?cur:"## user\n");fclose(f);}
            free(sp);} free(cur); }
        m_render(sf, spf);
        char tf[] = "/tmp/m_inXXXXXX";
        int fd = mkstemp(tf); if (fd < 0) continue; close(fd);
        pid_t pp = fork();
        if (!pp) { execlp("e","e","--box","message:",tf,(char*)0); _exit(127); }
        snprintf(pf,P,"%s/m_editorpid",TMP); char b2[16];snprintf(b2,16,"%d",pp);mm_w(pf,b2,"w");
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
        m_status("\033[1;7;31m⚠ If you typed in the conversation pane (above), Ctrl-S there FIRST to save edits ⚠\033[0m");
    }
    return 0;
}
