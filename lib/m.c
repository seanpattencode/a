/* m — chat+agentic loop. files (no /tmp; verify externally):
 * adata/m/{m.txt,sysprompt.txt,i.txt} · adata/local/m_{status,panel,pty,st,file,editorpid,err.log,inXXXXXX}
 * archive: trim in place + git commit with preview in subject. retrieval via git log/show. no marker, no archive/ dir.
 * debug: tail adata/m/m.txt ; tmux capture-pane -t <id> -p ; cat adata/local/m_*
 * e --nosb: skip e's internal scrollbar — tmux pane-scrollbars handles vertical scroll, avoids fold-aware sb math */

static volatile pid_t g_cp = 0;
static int g_halt = 0;
static volatile sig_atomic_t g_rst = 0;
static void m_sint(int s){(void)s;if(g_cp>0)kill(g_cp,SIGTERM);}
static void m_usr1(int s){(void)s;g_rst=1;}
static void mm_w(const char *p, const char *t, const char *m);

static void m_status(const char *msg) {
    char p[P], l[256];
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    snprintf(p, P, "%s/m_status", DDIR);
    snprintf(l, 256, "[%02d:%02d:%02d] %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, msg);
    mm_w(p, l, "a");
}

static void m_commit(const char *tag) {
    if (g_halt) return;
    if (fork() == 0) {
        char c[B];
        snprintf(c, B, "cd '%s/m' && git add -A && (git diff --cached --quiet || git commit -q -m '%s'); git push -q 2>/dev/null", AROOT, tag);
        execlp("sh", "sh", "-c", c, NULL); _exit(127);
    }
    char sm[32]; snprintf(sm, 32, "git sync %s", tag); m_status(sm);
}

static int mm_extract(const char *a, char *bash, size_t bsz, size_t *eo) {
    const char *o=strstr(a,"<cmd>"); if(!o) return 0;
    const char *cl=strstr(o+5,"</cmd>"); if(!cl) return -1;
    size_t bl=(size_t)(cl-o-5); if(bl>=bsz) bl=bsz-1;
    memcpy(bash,o+5,bl); bash[bl]=0;
    if(eo) *eo=(size_t)(cl-a)+6;
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
static int mm_delta(const char *l, char *a, size_t *al, size_t sz, int ag) {
    static const char *S[]={"\"message_stop\"","\"turn.completed\"","\"type\":\"result\""};
    static const char *F[]={"\"text_delta\",\"text\":\"","\"agent_message\"","\"role\":\"assistant\""};
    static const char *K[]={0,"\"text\":\"","\"content\":\""};
    static const int O[]={21,8,11};
    if (strstr(l,S[ag])) return 1;
    const char *t=strstr(l,F[ag]); if(!t) return 0;
    if (K[ag]) { t=strstr(t,K[ag]); if(!t) return 0; }
    return mm_delta_raw(t+O[ag],a,al,sz);
}

static int mm_stream(const char *sf, const char *sp, char *a, size_t sz, char *bash, size_t bsz) {
    a[0] = bash[0] = 0;
    int pp[2]; if (pipe(pp) < 0) return -1;
    load_cfg();
    const char *ag = cfget("m_agent"); if (!*ag) ag = "claude";
    int is_codex = !strcmp(ag, "codex"), is_gemini = !strcmp(ag, "gemini");
    const char *md = cfget("m_model"); if (!*md) md = is_codex ? "gpt-5.5" : is_gemini ? "gemini-2.5-flash" : "opus";
    const char *ef = cfget("m_effort"); if (!*ef) ef = is_codex ? "xhigh" : "low";
    const char *tier = cfget("m_tier");
    int has_tier = is_codex && *tier && strcmp(tier,"default");
    char errp[P]; snprintf(errp,P,"%s/m_err.log",DDIR);
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
            snprintf(x,B,"awk '/^## a-loaded /{s=1;next} s&&/^## a-loaded-end$/{s=0;next} !s'|iconv -f UTF-8 -t UTF-8 -c|codex exec --json --skip-git-repo-check --dangerously-bypass-approvals-and-sandbox --disable shell_tool --disable unified_exec --disable tool_search --disable tool_suggest -m '%s' -c 'model_reasoning_effort=\"%s\"'%s",md,ef,t);
            execlp("sh","sh","-c",x,(char*)0);
        } else { char x[B*2];
            snprintf(x,B*2,"awk '/^## a-loaded /{s=1;next} s&&/^## a-loaded-end$/{s=0;next} !s'|claude -p --output-format stream-json --include-partial-messages --verbose --tools '' --model '%s' --effort '%s' --system-prompt \"$1\" --append-system-prompt-file <(cat %2$s/local/a_cat.txt 2>/dev/null; printf '\\n==> i.txt <==\\n'; cat %2$s/m/i.txt 2>/dev/null) --settings '{\"enabledPlugins\":{\"clangd-lsp@claude-plugins-official\":false}}'",md,ef,AROOT);
            execlp("bash","bash","-c",x,"_",sp,(char*)0);
        }
        _exit(127);
    }
    close(pp[1]); g_cp = cp;
    FILE *fp = fdopen(pp[0], "r"), *of = fopen(sf, "a");
    off_t start = ftello(of);
    char l[32768]; size_t al = 0, pl = 0, eo;
    while (fgets(l, sizeof l, fp)) {
        int stop = mm_delta(l, a, &al, sz, is_codex?1:is_gemini?2:0);
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

static void m_winch(int s){(void)s;}
static int cmd_m_panel(int c, char **v) {
    (void)c; (void)v;
    { struct sigaction sa; sa.sa_handler=m_winch; sigemptyset(&sa.sa_mask); sa.sa_flags=0; sigaction(SIGWINCH,&sa,NULL); }
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
        {"undo","a m archive undo"},{"restart","a m restart"},{"edit","a m edit"},{NULL,NULL}};
    struct termios old, raw; tcgetattr(0, &old); raw = old;
    raw.c_lflag &= ~(tcflag_t)(ICANON|ECHO|ISIG);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    write(1, "\033[?1000h\033[?1006h", 16);
    char last[160] = "";
    int bx[64],bw[64],br[64],bi[64],sel=0; char bk[64]; int nb=0;
    for (;;) {
        load_cfg();
        const char *cg = cfget("m_agent"); if (!*cg) cg = "claude";
        int xx = !strcmp(cg, "codex"), gg = !strcmp(cg, "gemini");
        const char **MODS = xx ? MODS_X : gg ? MODS_G : MODS_C;
        const char **EFFS = xx ? EFFS_X : EFFS_C;
        const char *cm = cfget("m_model"); if (!*cm) cm = xx ? "gpt-5.5" : gg ? "gemini-2.5-flash" : "opus";
        const char *cf = cfget("m_effort"); if (!*cf) cf = xx ? "xhigh" : "low";
        const char *ct = cfget("m_tier"); if (!*ct) ct = "default";
        int opsr = 4 + (!gg) + xx;
        struct stat st; long tot=0; char pa[P];
        static long mc=0; static time_t mt=0;
        snprintf(pa,P,"%s/m/m.txt",AROOT);
        if(!stat(pa,&st)){if(st.st_mtime!=mt){FILE*mf=fopen(pa,"r");mc=0;if(mf){char ln[4096];int s=0;while(fgets(ln,4096,mf)){if(!strncmp(ln,"## a-loaded",11))s=(ln[11]==' ');else if(!s)mc+=(long)strlen(ln);}fclose(mf);}mt=st.st_mtime;}tot+=mc;}
        snprintf(pa,P,"%s/local/a_cat.txt",AROOT); if(!stat(pa,&st)) tot+=st.st_size;
        snprintf(pa,P,"%s/m/i.txt",AROOT); if(!stat(pa,&st)) tot+=st.st_size;
        tot/=4; long lim=(gg||!strcmp(cm,"opus"))?1000000:200000; int pct=tot*100/lim;
        char hb[16]={0}; pcmd("tmux display -p -t \"$TMUX_PANE\" '#{pane_height}'",hb,16); int ph=atoi(hb);
        struct winsize ws={0}; ioctl(1,TIOCGWINSZ,&ws); int cw=ws.ws_col?ws.ws_col:80;
        if(ph<=5) sel=0;
        write(1, "\033[2J\033[H", 7); nb = 0;
        printf("\033[%dmtok %ldk/%s\033[0m ",pct>=80?31:pct>=50?33:32,tot/1000,lim>=1000000?"1M":"200k");
        if (gg) printf("gemini --yolo --output-format stream-json -m %s\033[K",cm);
        else if (xx) printf("codex exec --json -m %s -c model_reasoning_effort=\"%s\"%s%s%s --skip-git-repo-check --dangerously-bypass-approvals-and-sandbox\033[K",cm,cf,strcmp(ct,"default")?" -c service_tier=\"":"",strcmp(ct,"default")?ct:"",strcmp(ct,"default")?"\"":"");
        else printf("claude -p --model %s --effort %s --tools \"\" +sysprompt+settings\033[K",cm,cf);
        {const char*lbl=ph>5?"[-] collapse $ tmux resize-pane -t \"$TMUX_PANE\" -y 2":"[+] expand $ tmux resize-pane -t \"$TMUX_PANE\" -y 15";
         printf("\033[2;1H\033[%dm%s",sel==nb?7:1,lbl); int ll=(int)strlen(lbl);
         for(int i=ll;i<cw;i++)putchar(' '); printf("\033[0m");
         bx[nb]=0;bw[nb]=cw;br[nb]=1;bk[nb]='T';bi[nb]=0;nb++;}
        if (ph > 5) { printf("\033[3;1H");
        #define VROW(lbl,arr,row,kind,curv) do{int cx=printf("%s: ",lbl);\
            for(int i=0;arr[i];i++){int s=!strcmp(curv,arr[i]),f=sel==nb,w=(int)strlen(arr[i])+2;\
                bx[nb]=cx;bw[nb]=w;br[nb]=row;bk[nb]=kind;bi[nb]=i;nb++;\
                printf("%s[%s]%s ",f?"\033[7m":s?"\033[1m":"",arr[i],f||s?"\033[0m":"");cx+=w+1;}printf("\033[K\n");}while(0)
        VROW("agent",AGTS,2,'a',cg); VROW("model",MODS,3,'m',cm);
        if (!gg) VROW("effort",EFFS,4,'e',cf);
        if (xx) VROW("tier",TIERS,5,'t',ct);
        #undef VROW
        {int cx=0;for(int i=0;OPS[i].l;i++){int f=sel==nb,w=(int)strlen(OPS[i].l)+2;
            bx[nb]=cx;bw[nb]=w;br[nb]=opsr;bk[nb]='o';bi[nb]=i;nb++;
            printf("%s[%s]%s ",f?"\033[7m":"",OPS[i].l,f?"\033[0m":"");cx+=w+1;}printf("\033[K\n");} }
        if(sel>=nb)sel=nb-1; if(sel<0)sel=0;
        char sc[B];
        if (bk[sel]=='T') snprintf(sc,B,"tmux resize-pane -t \"$TMUX_PANE\" -y %d",ph>5?2:15);
        else if (bk[sel]=='a') { const char*a=AGTS[bi[sel]]; snprintf(sc,B,"a config m_agent %s; a config m_model %s; a config m_effort %s",a,!strcmp(a,"codex")?"gpt-5.5":!strcmp(a,"gemini")?"gemini-2.5-flash":"opus",!strcmp(a,"codex")?"xhigh":"low"); }
        else if (bk[sel]=='m') snprintf(sc,B,"a config m_model %s",MODS[bi[sel]]);
        else if (bk[sel]=='e') snprintf(sc,B,"a config m_effort %s",EFFS[bi[sel]]);
        else if (bk[sel]=='t') snprintf(sc,B,"a config m_tier %s",TIERS[bi[sel]]);
        else snprintf(sc,B,"%s",OPS[bi[sel]].cm);
        if(ph>5){int bot=opsr+2;
            printf("\033[%d;1H\033[36m$ %s\033[0m\033[K",bot,sc);
            printf("\033[%d;1H\033[90marrows select Enter run q exit %s\033[0m\033[K",bot+1,last);}
        fflush(stdout);
        char ch; ssize_t rr=read(0,&ch,1); if(rr==0)break; if(rr!=1)continue;
        if (ch == 3 || ch == 4 || ch == 'q') break;
        int hit=-1;
        if (ch == '\r' || ch == '\n' || ch == ' ') hit=sel;
        else if (ch != '\x1b') continue;
        else {
            int av; usleep(50000); ioctl(0, FIONREAD, &av); if (!av) break;
            char s[2]; if (read(0, s, 1) != 1 || s[0] != '[' || read(0, s+1, 1) != 1) continue;
            if (s[1]=='A' || s[1]=='D') { sel=(sel+nb-1)%nb; continue; }
            if (s[1]=='B' || s[1]=='C') { sel=(sel+1)%nb; continue; }
            if (s[1] != '<') continue;
            int btn=0; char mc;
            int mx=0,my=0;
            while (read(0,&mc,1)==1 && mc!=';') btn = btn*10+(mc-'0');
            while (read(0,&mc,1)==1 && mc!=';') mx = mx*10+(mc-'0');
            while (read(0,&mc,1)==1 && mc!='M' && mc!='m') my = my*10+(mc-'0');
            if (mc!='M' || btn!=0) continue;
            int gx = mx-1, gy = my-1;
            for (int i = 0; i < nb; i++) if (gy==br[i] && gx>=bx[i] && gx<bx[i]+bw[i]) { hit=sel=i; break; }
        }
        if (hit < 0) continue;
        int i = hit;
        if (bk[i]=='T') {
            char hb2[16]={0}; pcmd("tmux display -p -t \"$TMUX_PANE\" '#{pane_height}'",hb2,16);
            char cmd[64]; snprintf(cmd,64,"tmux resize-pane -t \"$TMUX_PANE\" -y %d",atoi(hb2)>5?2:15);
            (void)!system(cmd); }
        else if (bk[i]=='a') { const char*a=AGTS[bi[i]]; cfset("m_agent",a);
            cfset("m_model",!strcmp(a,"codex")?"gpt-5.5":!strcmp(a,"gemini")?"gemini-2.5-flash":"opus");
            cfset("m_effort",!strcmp(a,"codex")?"xhigh":"low");
            snprintf(last,160,"agent=%s",a); }
        else if (bk[i]=='m') { cfset("m_model",MODS[bi[i]]); snprintf(last,160,"model=%s",MODS[bi[i]]); }
        else if (bk[i]=='e') { cfset("m_effort",EFFS[bi[i]]); snprintf(last,160,"effort=%s",EFFS[bi[i]]); }
        else if (bk[i]=='t') { cfset("m_tier",TIERS[bi[i]]); snprintf(last,160,"tier=%s",TIERS[bi[i]]); }
        else {
            static time_t aw=0; time_t now=time(NULL);
            if (!strcmp(OPS[bi[i]].l,"archive") && now-aw>=10) {
                snprintf(last,160,"\033[33mrun [archive] again within 10s, or use [archive turn]\033[0m");
                aw=now; continue;
            }
            aw=0;
            char cmd[B],o[200]=""; snprintf(cmd,B,"%s 2>/dev/null",OPS[bi[i]].cm); int r=pcmd(cmd,o,200);
            o[strcspn(o,"\n")]=0;
            time_t tt=time(NULL); char ts[16]; strftime(ts,16,"%H:%M:%S",localtime(&tt));
            snprintf(last,160,"[%s] %s %s %s",ts,WIFEXITED(r)&&!WEXITSTATUS(r)?"\033[32mOK":"\033[31mX",OPS[bi[i]].l,o);
            if(!WEXITSTATUS(r)&&(strstr(OPS[bi[i]].l,"archive")||!strcmp(OPS[bi[i]].l,"undo"))){
                char rc[B];snprintf(rc,B,"F=$(cat %s/m_file 2>/dev/null||echo m.txt);tmux respawn-pane -k -t :.0 \"tail -Fn 200 %s/m/$F\"",DDIR,AROOT);
                (void)!system(rc);
            }
        }
    }
    write(1, "\033[?1000l\033[?1006l", 16);
    tcsetattr(0, TCSANOW, &old);
    return 0;
}

/* preview: first K + " … " + last K chars, NL/TAB→space. K=25, outsz>=64. */
static void m_preview(const char *s, size_t n, char *o, size_t osz) {
    const int K=25; size_t w=0;
    #define PUT(c) do{char x=(c);if(w+1<osz)o[w++]=(x=='\n'||x=='\r'||x=='\t')?' ':x;}while(0)
    if (n <= (size_t)(K*2+5)) { for(size_t i=0;i<n;i++)PUT(s[i]); }
    else { for(int i=0;i<K;i++)PUT(s[i]);
           if(w+5<osz){memcpy(o+w," … ",5);w+=5;}
           for(int i=0;i<K;i++)PUT(s[n-K+i]); }
    o[w]=0;
    #undef PUT
}

/* trim [s,e) from mp, git commit with `archive <file> <info> | <preview>`. */
static int m_arch_cut(const char *mp, char *txt, size_t tl, char *s, char *e, const char *info) {
    char prev[128]; m_preview(s, (size_t)(e-s), prev, sizeof prev);
    FILE *f=fopen(mp,"w"); if(!f) return 1;
    fwrite(txt,1,(size_t)(s-txt),f); fwrite(e,1,(size_t)((txt+tl)-e),f); fclose(f);
    char mf[P]; snprintf(mf,P,"%s/m_commit_msg",DDIR);
    FILE *mp_=fopen(mf,"w"); if(mp_){fprintf(mp_,"archive %s %s | %s\n",bname(mp),info,prev); fclose(mp_);}
    char c[B]; snprintf(c,B,"git -C '%1$s/m' add -A && git -C '%1$s/m' commit -q -F '%2$s'",AROOT,mf);
    (void)!system(c);
    printf("archived %s (%ld bytes) — a m archive hist\n",info,(long)(e-s));
    return 0;
}

static int m_archive(int c, char **v) {
    char mp[P];
    static const char *USAGE =
        "usage:\n"
        "  a m archive                       rotate m.txt (keep last user turn)\n"
        "  a m archive <file.txt>            rotate <file.txt>\n"
        "  a m archive N-M [file.txt]        archive lines N..M\n"
        "  a m archive turn                  archive last completed turn\n"
        "  a m archive hist                  list archives (git log with preview)\n"
        "  a m archive show <hash>           git show <hash> (diff of cut)\n"
        "  a m archive get <hash>            dump cut content to /tmp/m_unarch_<hash>.txt\n"
        "  a m archive undo                  revert most recent archive commit\n";
    if (c > 3 && (!strcmp(v[3],"--help")||!strcmp(v[3],"-h")||!strcmp(v[3],"help"))) { fputs(USAGE,stdout); return 0; }
    if (c > 3 && !strcmp(v[3],"hist")) { char x[B]; snprintf(x,B,"git -C '%s/m' log --oneline --grep='^archive '",AROOT); return system(x); }
    if (c > 4 && !strcmp(v[3],"show")) { char x[B]; snprintf(x,B,"git -C '%s/m' show %s",AROOT,v[4]); return system(x); }
    if (c > 4 && !strcmp(v[3],"get")) { char tf[P],x[B]; snprintf(tf,P,"/tmp/m_unarch_%s.txt",v[4]);
        snprintf(x,B,"git -C '%s/m' show %s | grep '^-[^-]' | cut -c2- > '%s' && echo %s",AROOT,v[4],tf,tf);
        return system(x); }
    if (c > 3 && (!strcmp(v[3],"undo")||!strcmp(v[3],"restore")||!strcmp(v[3],"unarchive"))) {
        if (c > 4) { char *nv[]={v[0],v[1],v[2],(char*)"get",v[4],NULL}; return m_archive(5,nv); }
        char x[B]; snprintf(x,B,"H=$(git -C '%1$s/m' log --grep='^archive ' -n 1 --format='%%H'); [ -n \"$H\" ] && git -C '%1$s/m' revert \"$H\" --no-edit || { echo 'no archive to undo'; exit 1; }",AROOT);
        return system(x);
    }
    if (c > 3 && !strcmp(v[3],"turn")) {
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
        int r = m_arch_cut(mp, txt, tl, prev_um, last_um, "turn"); free(txt); return r;
    }
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
        int r = m_arch_cut(mp, txt, tl, first, last, "rotate"); free(txt); return r;
    }
    if (!isdigit((unsigned char)v[3][0])) { fprintf(stderr,"unknown: %s\n",v[3]); fputs(USAGE,stderr); return 1; }
    int N = atoi(v[3]); const char *dash = strchr(v[3], '-');
    int M = dash ? atoi(dash + 1) : N;
    if (N < 1 || M < N) { fprintf(stderr, "invalid range '%s'\n", v[3]); return 1; }
    const char *fn = c > 4 ? v[4] : "m.txt";
    snprintf(mp, P, "%s/m/%s", AROOT, fn);
    size_t tl; char *txt = readf(mp, &tl);
    if (!txt) { printf("file not found: %s\n", mp); return 1; }
    char *s = txt; int ln = 1;
    while (ln < N && s < txt + tl) { if (*s++ == '\n') ln++; }
    if (ln < N) { printf("past EOF (%d lines)\n", ln); free(txt); return 1; }
    char *e = s; int cnt = 0;
    while (e < txt + tl && cnt < M - N + 1) { if (*e++ == '\n') cnt++; }
    char info[32]; snprintf(info,32,"L%d-%d",N,M);
    int r = m_arch_cut(mp, txt, tl, s, e, info); free(txt); return r;
}

static int m_reinit(const char *fn) {
    char c[B]; snprintf(c,B,"%s/m_file",DDIR); mm_w(c,fn,"w");
    snprintf(c,B,"tmux respawn-pane -k -t :.0 'tail -Fn 200 %s/m/%s'",AROOT,fn); (void)!system(c);
    snprintf(c,B,"%s/m_pid",DDIR); char *p=readf(c,NULL); if(p) kill(atoi(p),SIGUSR1); free(p); return 0;
}
static int m_restart(void){char p[P];snprintf(p,P,"%s/m_file",DDIR);char*fp=readf(p,NULL);char fn[64]="m.txt";if(fp&&*fp){fp[strcspn(fp,"\n")]=0;snprintf(fn,64,"%s",fp);}free(fp);return m_reinit(fn);}
static int m_main(void){return m_reinit("m.txt");}
static int m_new(void){char ad[P];snprintf(ad,P,"%s/m/agent",AROOT);mkdirp(ad);time_t t=time(NULL);char fn[64];strftime(fn,64,"agent/%Y-%m-%d_%H-%M-%S.txt",localtime(&t));return m_reinit(fn);}

static int m_reset(void) {
    char pf[P],c[B]; snprintf(pf,P,"%s/m_pty",DDIR);
    char *p=readf(pf,NULL); if(!p||!*p){puts("no pty (a m not running here)");free(p);return 1;}
    p[strcspn(p,"\n")]=0;
    snprintf(c,B,"tmux send-keys -t '%1$s' C-c; tmux send-keys -t '%1$s' 'clear' Enter",p);
    int r=system(c); free(p); return r;
}

static int cmd_m(int c, char **v) {
    if (c>2&&!strcmp(v[2],"edit")) { char p[P],fn[64]="m.txt",*f; const char*me=getenv("M_FILE");
        if(me&&*me)snprintf(fn,64,"%s",me);
        else{snprintf(p,P,"%s/m_file",DDIR); f=readf(p,NULL); if(f&&*f){f[strcspn(f,"\n")]=0;snprintf(fn,64,"%s",f);} free(f);}
        char cc[B]; snprintf(cc,B,"tmux set -w pane-scrollbars on;tmux split-window -fh -l 80%% -c '%s/m' 'exec e --nosb --nofold --tail %s'",AROOT,fn); return system(cc); }
    if (c > 2 && !strcmp(v[2], "archive")) return m_archive(c, v);
    if (c > 2 && !strcmp(v[2], "panel")) return cmd_m_panel(c, v);
    if (c > 2 && !strcmp(v[2], "p")) { char pf[P]; snprintf(pf,P,"%s/m_panel",DDIR); char*p=readf(pf,NULL);
        if(p){p[strcspn(p,"\n")]=0; char hc[128],hb[16]={0}; snprintf(hc,128,"tmux display -p -t %s '#{pane_height}'",p); pcmd(hc,hb,16);
            char cmd[128]; snprintf(cmd,128,"tmux resize-pane -t %s -y %d",p,atoi(hb)>5?2:15); (void)!system(cmd); free(p);} return 0; }
    if (c > 2 && !strcmp(v[2], "reset")) return m_reset();
    if (c > 2 && !strcmp(v[2], "restart")) return m_restart();
    if (c > 2 && !strcmp(v[2], "main")) return m_main();
    if (c > 2 && !strcmp(v[2], "new")) return m_new();
    if (getenv("M_IN")) { puts("already in a m (nested chat blocked) — use: a m archive | panel | reset"); return 1; }
    char b[B], sf[P], ss[P], spf[P], pty[64] = "";
    CWD(w); struct tm*tt=localtime(&(time_t){time(NULL)}); char sn[64];
    snprintf(sn,64,"m-%s-%02d%02d%02d",bname(w),tt->tm_hour,tt->tm_min,tt->tm_sec);
    if (!getenv("TMUX")) { ajoin(b,B,c,v,0); tm_new(sn,w,b); tm_go(sn); return 0; }
    tm_rename(sn);
    signal(SIGINT, m_sint);
    { struct sigaction sa; sa.sa_handler=m_usr1; sigemptyset(&sa.sa_mask); sa.sa_flags=0; sigaction(SIGUSR1,&sa,NULL); }
    { char pb[16]; snprintf(b,B,"%s/m_pid",DDIR); snprintf(pb,16,"%d",getpid()); mm_w(b,pb,"w"); }
    const char *fn = c > 2 ? v[2] : "m.txt";
    snprintf(sf, P, "%s/m/%s", AROOT, fn);
    snprintf(ss, P, "%s/m_status", DDIR);
    snprintf(spf, P, "%s/m/sysprompt.txt", AROOT);
    if (!fexists(spf)) mm_w(spf, "`a m` chat: emit <cmd>command</cmd> to run in pty (cwd=m). After </cmd>, STOP. Markdown ```bash/```sh blocks are for showing code only (NEVER executed). No Read/Write/Bash/LSP tools. Never emit ## user, ## assistant, ## tool output headers; markdown ## headings inside replies are fine.\n", "w");
    mm_w(ss, "", "w");
    setenv("M_IN", "1", 1);
    snprintf(b, B, "[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone);"
                   "(cd %1$s/m && git pull --rebase -q 2>/dev/null) & "
                   "S=\"tmux split-window -t $TMUX_PANE -e M_IN=1 -dv\";"
                   "$S -b 'tail -Fn 200 %2$s';"
                   "$S -l 3 'tail -Fn 50 %3$s';"
                   "tmux split-window -t $TMUX_PANE -e M_IN=1 -e M_FILE=%5$s -dv -l 7 -P -F '#{pane_id}' 'cd %1$s/m;exec bash' > %4$s/m_pty;"
                   "$S -l 2 -P -F '#{pane_id}' 'a m panel'",
             AROOT, sf, ss, DDIR, fn);
    pcmd(b, pty, 64); pty[strcspn(pty, "\n")] = 0;
    char pf[P];
    snprintf(pf,P,"%s/m_panel",DDIR); mm_w(pf,pty,"w");
    snprintf(pf,P,"%s/m_pty",DDIR); { char*bp=readf(pf,NULL); if(bp){strncpy(pty,bp,63);pty[63]=0;pty[strcspn(pty,"\n")]=0;free(bp);} }
    snprintf(pf,P,"%s/m_file",DDIR); mm_w(pf,fn,"w");
    for (;;) {
        g_halt = 0;
        { size_t spl=0; char *sp=readf(spf,&spl); char *cur=readf(sf,NULL);
          char hdr[B]; snprintf(hdr,B,"## system\n%s%s",sp?sp:"",(spl&&sp[spl-1]=='\n')?"":"\n");
          if (!cur || strncmp(cur,hdr,strlen(hdr))) {
            char *us=cur?strstr(cur,"\n## user\n"):0; FILE *f=fopen(sf,"w");
            if(f){fputs(hdr,f); fputs(us?us+1:"## user\n",f); fclose(f);} }
          free(sp); free(cur); }
        write(1,"\n── message (Enter sends) ──\n› ",41);
        (void)!write(1,"\x1b[?2004h",8);
        static char m[65536];size_t ml=paste_line(m,sizeof m,stdin);
        (void)!write(1,"\x1b[?2004l",8);
        if(!ml){if(g_rst){g_rst=0;continue;} break;}
        if(!m[0]) continue;
        m[ml]='\n';m[ml+1]=0;mm_w(sf, m, "a");
        m_commit("u");
        for (int i = 0; i < 10; i++) {
            m_status("thinking");
            mm_w(sf, "\n## assistant\n", "a");
            static char a[64*1024], bash[8*1024], ob[16*1024], syc[B];
            { char *sp=readf(spf,NULL); snprintf(syc,B,"%s",sp?sp:""); free(sp); }
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
