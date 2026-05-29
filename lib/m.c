/* m — chat+agentic loop. claude(opus)+codex. data in adata/m/ (own repo): agents/<name>.txt = one convo each, live pushed/turn.
 * i.txt=identity, mem/*.txt=memory; both + M_SYS + a_cat injected at call-time (never baked → files tiny, portable).
 * switch model via / menu (next turn). debug: tail adata/m/agents/*.txt; tmux capture-pane; cat adata/local/m_* */

static const char *M_SYS = "`a m` chat: emit <cmd>command</cmd> to run one-shot (cwd=m, 60s timeout). After </cmd>, STOP. Markdown ```bash/```sh blocks are for showing code only (NEVER executed). No Read/Write/Bash/LSP tools. Never emit ## user, ## assistant, ## tool output headers; markdown ## headings inside replies are fine.";
static volatile pid_t g_cp = 0;
static int g_halt = 0;
static volatile sig_atomic_t g_rst = 0;
static void m_sint(int s){(void)s;g_halt=1;if(g_cp>0)kill(g_cp,SIGTERM);}
static void m_usr1(int s){(void)s;g_rst=1;}
static void mm_w(const char *p, const char *t, const char *m);
static const char *m_dflt_model(const char *a);

static void m_status(const char *msg) {
    char p[P], l[256];
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    snprintf(p, P, "%s/m_status", DDIR);
    snprintf(l, 256, "[%02d:%02d:%02d] %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, msg);
    mm_w(p, l, "w");
    if(getenv("M_IN")&&strncmp(msg,"git ",4))fprintf(stderr,"\033[2m· %s\033[0m\n",msg);
}

static void m_commit(const char *tag) {
    if (g_halt) return;
    /* commit + push synchronously (8s-bounded); confirm with the commit hash now on m/main */
    char c[B], out[256]="";
    snprintf(c, B,
        "cd '%1$s'&&git add m&&(git diff --cached --quiet||git commit -q -m '%2$s');"
        "h=$(git rev-parse --short HEAD);"
        "{ timeout 8 git push -q 2>/dev/null||{ git pull --rebase --autostash -q 2>/dev/null&&timeout 8 git push -q 2>/dev/null;}; }"
        "&&printf '✓ synced a-git @%%s (%2$s)' \"$h\"||printf '✗ @%%s (%2$s) NOT pushed' \"$h\"",
        SROOT, tag);
    pcmd(c, out, 256); out[strcspn(out,"\n")]=0;
    m_status(out);
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
    static const char *S[]={"\"message_stop\"","\"turn.completed\""};
    static const char *F[]={"\"text_delta\",\"text\":\"","\"agent_message\""};
    static const char *K[]={0,"\"text\":\""};
    static const int O[]={21,8};
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
    int is_codex = strstr(ag,"codex")!=0;
    const char *md = cfget("m_model"); if (!*md) md = m_dflt_model(ag);
    const char *ef = cfget("m_effort"); if (!*ef) ef = is_codex ? "xhigh" : "max";
    char errp[P]; snprintf(errp,P,"%s/m_err_%d.log",DDIR,(int)getpid());
    pid_t cp = fork();
    if (!cp) {
        int s = open(sf, O_RDONLY); if (s < 0) _exit(127);
        int d = open(errp, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        dup2(s, 0); dup2(pp[1], 1); dup2(d, 2);
        close(s); close(d); close(pp[0]); close(pp[1]);
        /* context injected fresh every call, uniformly for both providers */
        char ctx[B]; snprintf(ctx,B,"cd '%2$s' 2>/dev/null;a prompt show 2>/dev/null;cat '%1$s/local/a_cat.txt' 2>/dev/null",AROOT,SDIR);
        char x[B*2];
        if (is_codex)
            snprintf(x,B*2,"{ printf '%%s\\n' \"$1\";%s;printf '\\n';cat; }|iconv -f UTF-8 -t UTF-8 -c|codex exec --json --skip-git-repo-check --dangerously-bypass-approvals-and-sandbox --disable shell_tool --disable unified_exec --disable tool_search --disable tool_suggest -m '%s' -c 'model_reasoning_effort=\"%s\"'",ctx,md,ef);
        else
            snprintf(x,B*2,"cat|claude -p --output-format stream-json --include-partial-messages --verbose --tools '' --model '%s' --effort '%s' --system-prompt \"$1\" --append-system-prompt-file <(%s) --settings '{\"enabledPlugins\":{\"clangd-lsp@claude-plugins-official\":false}}'",md,ef,ctx);
        execlp("bash","bash","-c",x,"_",sp,(char*)0);
        _exit(127);
    }
    close(pp[1]); g_cp = cp;
    FILE *fp = fdopen(pp[0], "r"), *of = fopen(sf, "a");
    off_t start = ftello(of);
    char l[32768]; size_t al = 0, pl = 0, eo;
    while (fgets(l, sizeof l, fp)) {
        int stop = mm_delta(l, a, &al, sz, is_codex?1:0);
        if (al > pl) { if (!pl) m_status("streaming"); fwrite(a + pl, 1, al - pl, of); fflush(of); (void)!write(1, a+pl, al-pl); pl = al; }
        char *uh=strstr(a,"\n## user\n");
        if (uh || mm_extract(a, bash, bsz, &eo) > 0) { size_t cut=uh?(size_t)(uh-a):al;
            if (uh && cut<al) { a[cut]=0; al=cut; ftruncate(fileno(of),start+(off_t)cut); }
            kill(cp, SIGTERM); break; }
        if (stop) break;
    }
    fclose(fp); fclose(of); waitpid(cp, NULL, 0); g_cp = 0;
    if (!pl) { char *e=readf(errp,NULL); if(e&&*e){char m[2048]; snprintf(m,2048,"\n## error (%s)\n%s\n",ag,e); mm_w(sf,m,"a");} free(e); }
    unlink(errp);
    if (!bash[0]) mm_extract(a, bash, bsz, NULL);
    return 0;
}

static void mm_bash(const char *pty, const char *cmd, char *out, size_t sz) {
    (void)pty;
    char tp[P],sc[B*2]; snprintf(tp,P,"%s/m_transcript.log",DDIR);
    setenv("MMCMD",cmd,1);
    snprintf(sc,B*2,"cd '%s/m'&&{ printf '\\n\\033[1;33m[%%s]$\\033[0m %%s\\n' \"$(date +%%T)\" \"$MMCMD\"|tee -a '%s';setsid timeout -k 5 60 bash -c \"$MMCMD\" 2>&1|tee -a '%s'|perl -pe 's/\\e\\[[\\d;?<>=]*[A-Za-z@~]//g;s/\\e[()][AB012]//g;s/\\e[<=>]//g'|tail -50|sed 's/^/    /'; }",SROOT,tp,tp);
    pcmd(sc,out,sz);
}

static void mm_w(const char *p, const char *t, const char *m) {
    FILE *f = fopen(p, m); if (f) { fputs(t, f); fclose(f); }
}
static struct termios m_raw(void){struct termios o,r;tcgetattr(0,&o);r=o;r.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);r.c_cc[VMIN]=1;r.c_cc[VTIME]=0;tcsetattr(0,TCSANOW,&r);return o;}
static const char *m_dflt_model(const char *a){return strstr(a,"codex")?"gpt-5.5":"opus";}
static void m_sig(void){char p[P];snprintf(p,P,"%s/m_pid",DDIR);char *x=readf(p,NULL);if(x&&*x)kill(atoi(x),SIGUSR1);free(x);}
static size_t m_tail(const char *path,char *buf,size_t sz){FILE *f=fopen(path,"r");if(!f)return 0;struct stat st;fstat(fileno(f),&st);sz--;if((size_t)st.st_size>sz)fseek(f,st.st_size-(off_t)sz,SEEK_SET);size_t n=fread(buf,1,sz,f);fclose(f);buf[n]=0;return n;}

static int m_reinit(const char *fn) {
    char c[P]; snprintf(c,P,"%s/m_file",DDIR); mm_w(c,fn,"w");
    const char *e=getenv("M_PID"); if(e&&*e){kill(atoi(e),SIGUSR1); return 0;}
    m_sig(); return 0;
}
static int m_restart(void){CWD(cd);char c[B];snprintf(c,B,"tmux respawn-window -k -c '%s' 'a m'",cd);(void)!system(c);return 0;}
static int m_main(void){return m_reinit("agents/main.txt");}
static int m_new(void){char ad[P];snprintf(ad,P,"%s/m/agents",SROOT);mkdirp(ad);time_t t=time(NULL);char fn[64];strftime(fn,64,"agents/%Y-%m-%d_%H-%M-%S.txt",localtime(&t));return m_reinit(fn);}

static void m_set(const char *k,const char *v){char ck[32];snprintf(ck,32,"m_%s",k);cfset(ck,v);
  if(!strcmp(k,"agent")){cfset("m_model",m_dflt_model(v));cfset("m_effort",strstr(v,"codex")?"xhigh":"max");}
  m_sig();}
/* live-filter picker: anchors menu at bottom of pane via ABSOLUTE row positioning. */
static int m_pick(const char *cat,const char *const *items,int n,char *out,size_t osz){
    struct winsize ws; ioctl(1,TIOCGWINSZ,&ws); int rows=ws.ws_row?ws.ws_row:24;
    int rsv=n+2; if(rsv>rows-1)rsv=rows-1; if(rsv>20)rsv=20;
    int top=rows-rsv+1;
    #define CLR() printf("\033[%d;1H\033[J",top)
    char f[48]=""; int fl=0,sel=0;
    for(;;){
        int fm[64],nf=0;
        for(int i=0;i<n&&nf<64;i++) if(!fl||strcasestr(items[i],f)) fm[nf++]=i;
        if(sel>=nf)sel=nf?nf-1:0; if(sel<0)sel=0;
        CLR();
        printf("\033[36m%s:\033[0m %s",cat,f);
        for(int i=0;i<nf&&i<rsv-1;i++){
            const char *it=items[fm[i]]; const char *t=strchr(it,'\t');
            int cl=t?(int)(t-it):(int)strlen(it);
            printf("\n  %s%.*s%s",i==sel?"\033[7m> ":"  ",cl,it,i==sel?"\033[0m":"");
            if(t)printf("  \033[90m%s\033[0m",t+1);
        }
        printf("\033[%d;%dH",top,(int)strlen(cat)+3+fl); fflush(stdout);
        unsigned char c; if(read(0,&c,1)!=1){CLR();return -1;}
        if(c==27){int av; usleep(50000); ioctl(0,FIONREAD,&av);
            if(av>=2){char s[2]; (void)!read(0,s,2);
                if(s[0]=='['||s[0]=='O'){
                    if(s[1]=='A'){if(sel>0)sel--;continue;}
                    if(s[1]=='B'){sel++;continue;}
                }}
            CLR();return -1;}
        if(c==3){CLR();return -1;}
        if(c==9){CLR();return 0;}
        if(c==21){f[0]=0;fl=0;sel=0;continue;}
        if(c=='\r'||c=='\n'){if(!nf)continue; CLR(); snprintf(out,osz,"%s",items[fm[sel]]); return 1;}
        if(c==127||c==8||c==0xff){if(fl){f[--fl]=0; sel=0;} else {CLR(); return -1;}}
        else if(c>=' '&&c<127&&fl<47){f[fl++]=(char)c;f[fl]=0;sel=0;}
    }
    #undef CLR
}
static void m_menu(void);
/* raw-mode line reader: BSpace on every char, bracketed paste, '/' as instant menu hotkey. */
static size_t m_read_line(char *buf,size_t sz){
    struct termios o=m_raw();
    (void)!write(1,"\033[?2004h",8);
    size_t bl=0; int paste=0;
    while(bl<sz-1){
        unsigned char c;
        if(read(0,&c,1)!=1)break;  /* SIGUSR1 interrupts read (EINTR) → caller checks g_rst */
        if(bl==0&&!paste&&c=='/'){m_menu();break;}
        if(c=='\r'||c=='\n'){if(paste){buf[bl++]='\n';write(1,"\n",1);continue;} write(1,"\n",1); break;}
        if(!paste&&(c==127||c==8||c==0xff)){if(bl){bl--; write(1,"\b \b",3);} continue;}
        if(!paste&&c==3){bl=0; write(1,"^C\n",3); break;}
        if(c==27){int av;usleep(20000);ioctl(0,FIONREAD,&av);
            if(av>0){char s[8]; int rn=(int)read(0,s,(size_t)(av>8?8:av));
                if(rn>=5){if(!memcmp(s,"[200~",5)){paste=1;continue;} if(!memcmp(s,"[201~",5)){paste=0;continue;}}}
            continue;}
        if(c>=32){buf[bl++]=(char)c; if(!paste)write(1,&c,1);}
    }
    buf[bl]=0;
    (void)!write(1,"\033[?2004l",8);
    tcsetattr(0,TCSANOW,&o);
    return bl;
}
/* one pick → run via `a m <pick>`. */
static int m_step(const char *cat,const char *const *items,int n,char *out,size_t osz){
    struct termios o=m_raw(); out[0]=0;
    int rc=m_pick(cat,items,n,out,osz);
    tcsetattr(0,TCSANOW,&o);
    if(rc>0){char *t=strchr(out,'\t'); if(t)*t=0; char shc[256]; snprintf(shc,256,"a m %s 2>&1",out); (void)!system(shc); return 1;}
    return 0;
}
static void m_menu(void){
    static const char *const ops[]={
        "new\tnew agent (fresh file)",
        "main\topen main agent",
        "edit\tedit identity (i.txt)",
        "mem\tedit a memory file (loaded into every turn)",
        "use claude opus max\tclaude · opus · max",
        "use codex gpt-5.5 xhigh\tcodex · gpt-5.5 · xhigh",
        "use codex gpt-5 xhigh\tcodex · gpt-5 · xhigh",
        "reset\tclear transcript pane",
        "restart\trespawn window"};
    char v[128]; m_step("cmd",ops,(int)(sizeof ops/sizeof *ops),v,sizeof v);
}
/* a m mem [name] — pick/create a flat memory .txt, edit in e; all mem/*.txt inject into every turn. */
static int m_mem(int c,char**v){perf_disarm();
    char rd[P];snprintf(rd,P,"%s/mem",SROOT);mkdirp(rd);
    char fn[64]="notes.txt";
    if(c>3)snprintf(fn,64,"%s%s",v[3],strstr(v[3],".txt")?"":".txt");
    else{char cmd[B];snprintf(cmd,B,"ls '%s' 2>/dev/null",rd);static char buf[8192];pcmd(cmd,buf,sizeof buf);
        static char ib[64][128];const char*ip[64];int n=0;
        for(char*p=buf;*p&&n<64;){char*nl=strchr(p,'\n');if(nl)*nl=0;if(*p){snprintf(ib[n],128,"%s",p);ip[n]=ib[n];n++;}if(!nl)break;p=nl+1;}
        if(n){struct termios o=m_raw();char pk[128];int rc=m_pick("mem (Esc→notes.txt)",ip,n,pk,sizeof pk);tcsetattr(0,TCSANOW,&o);if(rc>0)snprintf(fn,64,"%s",pk);}}
    char cc[B];snprintf(cc,B,"tmux split-window -fh -l 80%% -c '%s' 'exec e --nosb %s'",rd,fn);return system(cc);}
static int m_reset(void) {
    char tp[P]; snprintf(tp,P,"%s/m_transcript.log",DDIR); mm_w(tp,"","w"); return 0;
}

static int cmd_m(int c, char **v) {
    if (c>2&&!strcmp(v[2],"edit")) { char cc[B]; snprintf(cc,B,"tmux split-window -fh -l 80%% -c '%s/mem' 'exec e --nosb i.txt'",SROOT); return system(cc); }
    if (c>2&&!strcmp(v[2],"mem")) return m_mem(c,v);
    if (c > 2 && (!strcmp(v[2],"model")||!strcmp(v[2],"agent")||!strcmp(v[2],"effort"))) {
        char val[256]=""; if(c>3)ajoin(val,256,c,v,3); load_cfg(); m_set(v[2],val); return 0;}
    if (c > 4 && !strcmp(v[2],"use")) { load_cfg(); m_set("agent",v[3]); cfset("m_model",v[4]); if(c>5)cfset("m_effort",v[5]); return 0;}
    if (c > 2 && !strcmp(v[2], "reset")) return m_reset();
    if (c > 2 && !strcmp(v[2], "restart")) return m_restart();
    if (c > 2 && !strcmp(v[2], "main")) return m_main();
    if (c > 2 && !strcmp(v[2], "new")) return m_new();
    if (getenv("M_IN")) { puts("already in a m — use: a m edit | mem | reset | new"); return 1; }
    perf_disarm(); /* interactive chat loop runs unbounded */
    char b[B], sf[P], pty[64] = "";
    CWD(w); struct tm*tt=localtime(&(time_t){time(NULL)}); char sn[64];
    snprintf(sn,64,"m-%s-%02d%02d%02d",bname(w),tt->tm_hour,tt->tm_min,tt->tm_sec);
    if (!getenv("TMUX")) { ajoin(b,B,c,v,0); tm_new(sn,w,b);
        tm_go(sn); return 0; }
    signal(SIGINT, m_sint);
    { struct sigaction sa; sa.sa_handler=m_usr1; sigemptyset(&sa.sa_mask); sa.sa_flags=0; sigaction(SIGUSR1,&sa,NULL); }
    { char pb[16]; snprintf(b,B,"%s/m_pid",DDIR); snprintf(pb,16,"%d",getpid()); mm_w(b,pb,"w"); setenv("M_PID",pb,1); }
    char pf[P]; snprintf(pf,P,"%s/m_file",DDIR);
    char fnb[128]="agents/main.txt"; const char *fn=fnb;
    if(c>2){snprintf(fnb,128,"agents/%s%s",v[2],strstr(v[2],".txt")?"":".txt");}
    else{char*sp=readf(pf,NULL);if(sp&&!strncmp(sp,"agents/",7)){sp[strcspn(sp,"\n")]=0;snprintf(fnb,128,"%s",sp);}free(sp);}
    snprintf(sf, P, "%s/m/%s", SROOT, fn);
    mm_w(pf,fn,"w");
    setenv("M_IN", "1", 1);
    /* m now lives in a-git (adata/git/m); convos sync via the a-git repo, no separate repo. */
    { char ad[P]; snprintf(ad,P,"%s/m/agents",SROOT); mkdirp(ad); }
    int _first=1, re=1;
    for (;;) {
        g_halt = 0;
        {char *sp=readf(pf,NULL); if(sp&&!strncmp(sp,"agents/",7)){sp[strcspn(sp,"\n")]=0;
          if(strcmp(sp,fn)){re=1;snprintf(fnb,128,"%s",sp); fn=fnb; snprintf(sf,P,"%s/m/%s",SROOT,fn);
          }} free(sp);}
        if(re){ static char tb[262144]; size_t n=m_tail(sf,tb,sizeof tb);
          if(!n){ mm_w(sf,"## user\n","w"); n=m_tail(sf,tb,sizeof tb); }
          (void)!write(1,"\033[2J\033[H",7);
          for(char *o=tb,*e=tb+n;o<e;){char *nx=memchr(o,'\n',(size_t)(e-o));size_t l=nx?(size_t)(nx-o+1):(size_t)(e-o);
            const char *col=NULL;
            if(l>=4&&!memcmp(o,"## ",3)){const char *t=o+3;
              col = !memcmp(t,"user",4)?"\033[36m":!memcmp(t,"assistant",9)?"\033[1;35m":NULL;}
            if(col){(void)!write(1,col,strlen(col));(void)!write(1,o,l);(void)!write(1,"\033[0m",4);}
            else (void)!write(1,o,l);
            o+=l;} re=0; }
        { char sp[P]; snprintf(sp,P,"%s/m_status",DDIR); char *s=readf(sp,NULL);
          if(s&&*s){s[strcspn(s,"\n")]=0; printf("\n[%s]",s);} free(s); }
        load_cfg();
        { const char *ag=cfget("m_agent"),*md=cfget("m_model"),*ef=cfget("m_effort");
          printf("\n\033[1m%s\033[0m  %s·%s·%s  \033[90m/=menu\033[0m",fn,*ag?ag:"claude",*md?md:"opus",*ef?ef:"max");
          fflush(stdout); }
        write(1,"\n> ",3);
        if(_first){_first=0; tm_rename(sn);
          /* refresh source cache (a repo) + pull a-git (carries m), async */
          snprintf(b,B,"(cd '%s'&&a cat 3 >/dev/null 2>&1;git -C '%s' pull --rebase --autostash -q 2>/dev/null)&",SDIR,SROOT);(void)!system(b); }
        static char m[65536]; size_t ml=m_read_line(m,sizeof m);
        if(g_rst){g_rst=0;re=1;continue;}
        if(!ml){re=1;continue;}
        m[ml]='\n';m[ml+1]=0;mm_w(sf, m, "a"); m_commit("u");
        static char a[64*1024], bash[8*1024], ob[16*1024];
        for (int i = 0; i < 10; i++) {
            load_cfg();
            char ag[32],md[32],ef[16];
            {const char*x=cfget("m_agent");snprintf(ag,32,"%s",*x?x:"claude");
             x=cfget("m_model");snprintf(md,32,"%s",*x?x:m_dflt_model(ag));
             x=cfget("m_effort");snprintf(ef,16,"%s",*x?x:(strstr(ag,"codex")?"xhigh":"max"));}
            m_status("thinking");
            char hdr[80]; int hl=snprintf(hdr,80,"\n## assistant [%s·%s·%s]\n",ag,md,ef);
            mm_w(sf,hdr,"a"); (void)!write(1,hdr,(size_t)hl);
            if (mm_stream(sf, M_SYS, a, sizeof a, bash, sizeof bash) < 0) break;
            m_commit("a"); if (g_halt) break;
            if (!bash[0]) break;
            if (!pty[0]) { char tp[P],sc[B]; snprintf(tp,P,"%s/m_transcript.log",DDIR); mm_w(tp,"","w");
              snprintf(sc,B,"tmux split-window -t $TMUX_PANE -dv -l 10 -P -F '#{pane_id}' 'exec tail -n 500 -f \"%s\"'",tp);
              pcmd(sc,pty,64); pty[strcspn(pty,"\n")]=0; }
            m_status("running");
            {char th[64]; time_t tr=time(NULL); size_t thl=strftime(th,64,"\n## tool running %T\n",localtime(&tr));
             mm_w(sf,th,"a"); (void)!write(1,th,thl);}
            mm_bash(pty, bash, ob, sizeof ob);
            char tb2[20000]; time_t t = time(NULL);
            size_t n = strftime(tb2, 64, "\n## tool output %FT%T\n", localtime(&t));
            snprintf(tb2 + n, sizeof tb2 - n, "%s\n", ob);
            mm_w(sf, tb2, "a"); (void)!write(1,tb2,strlen(tb2));
            m_commit("t"); if (g_halt) break;
        }
        if(g_halt) mm_w(sf, "\n## interrupted\n", "a");
        mm_w(sf, "\n## user\n", "a");
    }
    return 0;
}
