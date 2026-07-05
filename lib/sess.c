#include <poll.h>
static int cmd_sess(int argc, char **argv) {
    init_db(); load_cfg(); load_proj(); load_apps(); load_sess();
    const char *key = argv[1];
    sess_t *s = find_sess(key);
    if (!s) return -1;  /* not a session key */
    perf_disarm();  /* sessions exec into long-running TUIs; perf timer is for `a` itself */
    CWD(wd);
    const char *wda = argc > 2 ? argv[2] : NULL;
    /* If wda is a project number */
    if (wda && wda[0] >= '0' && wda[0] <= '9') {
        int idx = atoi(wda);
        if (idx >= 0 && idx < NPJ) snprintf(wd, P, "%s", PJ[idx].path);
        else if (idx >= NPJ && idx < NPJ + NAP) {
            printf("> Running: %s\n", AP[idx-NPJ].name);
            const char *sh = getenv("SHELL"); if (!sh) sh = "/bin/bash";
            execlp(sh, sh, "-c", AP[idx-NPJ].cmd, (char*)NULL);
        }
    } else if (wda && dexists(wda)) {
        if (wda[0] == '~') snprintf(wd, P, "%s%s", HOME, wda+1);
        else snprintf(wd, P, "%s", wda);
    }
    /* Build prompt from remaining args */
    char prompt[B]=""; int is_prompt=0,pl=0;
    int start = wda ? 3 : 2;
    if (wda && !(wda[0]>='0'&&wda[0]<='9') && !dexists(wda)) { start = 2; is_prompt = 1; }
    for (int i = start; i < argc; i++) {
        if (!strcmp(argv[i],"-w")||!strcmp(argv[i],"--new-window")||!strcmp(argv[i],"-t")||!strcmp(argv[i],"--with-terminal")) continue;
        pl+=snprintf(prompt+pl,(size_t)(B-pl),"%s%s",pl?" ":"",argv[i]);
        is_prompt = 1;
    }
    char sn[256];{struct tm*t=localtime(&(time_t){time(NULL)});int h=t->tm_hour%12;if(!h)h=12;
        snprintf(sn,256,"%s-%s-%d%02d%s",s->name,bname(wd),h,t->tm_min,t->tm_hour>=12?"p":"a");
        if(tm_has(sn))snprintf(sn,256,"%s-%s-%d%02d%02d%s",s->name,bname(wd),h,t->tm_min,t->tm_sec,t->tm_hour>=12?"p":"a");}
    const char *xp = is_prompt ? prompt : NULL;
    /* Existing session = attach, send prompt via keys (already running) */
    if (tm_has(sn)) {
        if (is_prompt && prompt[0]) {
            tm_send(sn, prompt); usleep(100000);
            tm_key(sn, "Enter");
            puts("Prompt queued (existing session)");
        }
        tm_go(sn);
        return 0;
    }
    if (create_sess(sn, wd, s->cmd, xp) != 2) { if(isatty(1))tm_go(sn);
        else {char q[160],wi[16]="?";snprintf(q,160,"tmux list-windows -t '" TMS "' -f '#{==:#{window_name},%s}' -F '#{window_index}' 2>/dev/null",sn);pcmd(q,wi,16);wi[strcspn(wi,"\n")]=0;
            printf("→ win %s · %s\n  tmux attach -t " TMS ":%s\n",wi[0]?wi:"?",sn,wi[0]?wi:sn);} }
    return 0;
}

static int cmd_dir_file(int argc, char **argv) { (void)argc;
    const char *arg = argv[1];
    char expanded[P];
    if (arg[0] == '~') snprintf(expanded, P, "%s%s", HOME, arg+1);
    else snprintf(expanded, P, "%s", arg);
    if (!dexists(expanded)&&!fexists(expanded)&&arg[0]=='/') snprintf(expanded,P,"%s%s",HOME,arg);
    if (dexists(expanded)) { printf("%s\n", expanded); execlp("ls", "ls", expanded, (char*)NULL); }
    else if (fexists(expanded)) {
        const char *ext = strrchr(expanded, '.');
        if (ext && !strcmp(ext, ".py")) {
            char py[P]="python3"; const char *ve=getenv("VIRTUAL_ENV");
            if(ve) snprintf(py,P,"%s/bin/python",ve);
            else if(!access(".venv/bin/python",X_OK)) snprintf(py,P,".venv/bin/python");
            execvp(py, (char*[]){ py, expanded, NULL });
        }
        else{int t=ext?ext[1]:0;const char*ed=t=='c'||t=='s'?"sh":t=='h'?OPENER:getenv("EDITOR");
            if(!ed)ed="e";execlp(ed,ed,expanded,(char*)NULL);}
    }
    return 0;
}

static FC fq[1024];int nfq;
/* first-char index: a freq entry can only be a prefix of a line if their first chars match (case-insensit),
 * so bucket entries by lowercased first char and scan only that bucket — provably identical result, O(bucket)
 * not O(nfq). Most file/dir lines hit an empty bucket → instant. */
static int fqhead[256];static int fqnext[1024];static unsigned char fqlen[1024];
static void fq_index(void){for(int z=0;z<256;z++)fqhead[z]=-1;
    for(int i=0;i<nfq;i++){fqlen[i]=(unsigned char)strlen(fq[i].n);int c=(unsigned char)fq[i].n[0];if(c>='A'&&c<='Z')c+=32;fqnext[i]=fqhead[c];fqhead[c]=i;}}
static int fq_get(const char*s){int b=0,bl=0;int c=(unsigned char)s[0];if(c>='A'&&c<='Z')c+=32;
    for(int i=fqhead[c];i>=0;i=fqnext[i]){int l=fqlen[i];if(l>bl&&!strncasecmp(s,fq[i].n,(size_t)l)&&(!s[l]||s[l]=='\t')){b=fq[i].c;bl=l;}}return !strncmp(s,"home\t",5)?0x7fffffff:strstr(s,"\tproject")?(1<<30)-atoi(s):b;}
static int ln_cmp(const void*a,const void*b){return fq_get(*(char*const*)b)-fq_get(*(char*const*)a);}
typedef struct{char*s;int k,i;}LNK;  /* decorate-sort: fq_get is O(nfq); call it once per line, not per qsort comparison */
static int lnk_cmp(const void*a,const void*b){const LNK*x=a,*y=b;return x->k!=y->k?y->k-x->k:x->i-y->i;}
/* sort-cache: `a i` scores+sorts ~1100 launcher entries every launch (~0.4ms). Memoize the sorted list and
 * reuse it until an order-affecting input changes. ai_fpr = a cheap fingerprint of every such input:
 * i_cache + web_cache + freq_cache (mtime:size), the window-set (content hash, so the ≤1s win refresh's
 * spurious mtime bumps don't invalidate), and the build itself (__DATE__ __TIME__, so a rebuild — i.e. any
 * change to acts[]/this code — invalidates). Same fingerprint ⇒ identical order ⇒ skip the rescore. */
static void ai_fpr(char*out,int os,const char*icache){
    struct stat si={0},se={0},sf={0};char p[P];
    stat(icache,&si);
    snprintf(p,P,"%s/web_cache.txt",DDIR);stat(p,&se);
    snprintf(p,P,"%s/freq_cache.txt",DDIR);stat(p,&sf);
    unsigned wh=5381;char wcp[P];snprintf(wcp,P,"%s/win_cache.txt",DDIR);
    size_t wl=0;char*wb=readf(wcp,&wl);if(wb){for(size_t i=0;i<wl;i++)wh=((wh<<5)+wh)^(unsigned char)wb[i];free(wb);}
    snprintf(out,os,"%s %s|%ld:%lld|%ld:%lld|%ld:%lld|%u",__DATE__,__TIME__,
        (long)si.st_mtime,(long long)si.st_size,(long)se.st_mtime,(long long)se.st_size,
        (long)sf.st_mtime,(long long)sf.st_size,wh);
}
/* write the fingerprint (line 0) + sorted lines to one file, atomically (temp+rename → no torn read). */
static void ai_savesorted(char**lines,int n,const char*fpr){
    char tp[P],dp[P];snprintf(tp,P,"%s/i_sorted.txt.%ld",DDIR,(long)getpid());
    FILE*f=fopen(tp,"w");if(!f)return;
    fputs(fpr,f);fputc('\n',f);
    for(int i=0;i<n;i++){fputs(lines[i],f);fputc('\n',f);}
    if(!fclose(f)){snprintf(dp,P,"%s/i_sorted.txt",DDIR);rename(tp,dp);}else unlink(tp);
}
static int cmd_i(int argc, char **argv) { (void)argc; (void)argv;
    AB;
    perf_disarm(); init_db(); load_cfg();
    CWD(cwd);
    char cache[P];snprintf(cache,P,"%s/i_cache.txt",DDIR);
    char*lines[2048];int n=0;const char*ft0=getenv("A_FILT_TAG");char ai_fprbuf[256];int ai_write=0;
    if(!ft0){  /* sort-cache fast path: reuse the memoized sorted list unless an order-affecting input changed */
        struct stat si,ss;char sshp[P];snprintf(sshp,P,"%s/ssh",SROOT);
        if(stat(cache,&si)==0 && !(stat(sshp,&ss)==0&&ss.st_mtime>si.st_mtime)){
            ai_fpr(ai_fprbuf,sizeof ai_fprbuf,cache);
            char scp[P];snprintf(scp,P,"%s/i_sorted.txt",DDIR);FILE*cf=fopen(scp,"r");
            if(cf){char l0[256];if(fgets(l0,sizeof l0,cf)){l0[strcspn(l0,"\n")]=0;
                if(!strcmp(l0,ai_fprbuf)){static char sbuf[1<<20];size_t sl=fread(sbuf,1,sizeof sbuf-1,cf);sbuf[sl]=0;
                    for(char*p=sbuf,*e=sbuf+sl;p<e&&n<2048;){char*nl=memchr(p,'\n',(size_t)(e-p));if(!nl)nl=e;if(nl>p){*nl=0;lines[n++]=p;}p=nl+1;}}}
            fclose(cf);}}
    }
    size_t len=0;char*raw=0;
    if(!n){  /* miss / filtered view: full read + score + sort, then memoize the result (saved after first paint) */
    raw=readf(cache,&len);
    {struct stat c,s;char d[P];snprintf(d,P,"%s/ssh",SROOT);if(raw&&!stat(cache,&c)&&!stat(d,&s)&&s.st_mtime>c.st_mtime){free(raw);raw=0;}}
    if(!raw){gen_icache();raw=readf(cache,&len);if(!raw)return 1;}
    {char fp[P];snprintf(fp,P,"%s/freq_cache.txt",DDIR);FILE*ff=fopen(fp,"r");if(ff){char ln[128];nfq=0;
        while(nfq<1024&&fgets(ln,128,ff)){char*c=strchr(ln,':');if(!c)continue;*c=0;
            snprintf(fq[nfq].n,64,"%s",ln);fq[nfq].c=atoi(c+1);nfq++;}fclose(ff);}}
    fq_index();
    for(char*p=raw,*end=raw+len;p<end&&n<2048;){char*nl=memchr(p,'\n',(size_t)(end-p));
        if(!nl)nl=end;if(nl>p&&!strchr("<=>#",*p)){*nl=0;lines[n++]=p;}p=nl+1;}
    {char wp[P];snprintf(wp,P,"%s/web_cache.txt",DDIR);size_t wl;char*wr=readf(wp,&wl);
     if(wr)for(char*p=wr,*e=wr+wl;p<e&&n<2048;){char*nl=memchr(p,'\n',(size_t)(e-p));
        if(!nl)nl=e;if(nl>p){*nl=0;lines[n++]=p;}p=nl+1;}}
    static char wb[32768];size_t wl=0;
    {const char*ft=getenv("A_FILT_TAG");int winview=ft&&strstr(ft,"win");  /* -t TMS not -a: grouped a-<pid> sessions re-list the same windows. win view adds pane tail */
    if(winview){FILE*p=popen("tmux lsw -t '"TMS"' -F '#{window_id} #W' 2>/dev/null|while read -r i w;do printf '%s\twin\t%s · %s\n' \"$w\" \"$i\" \"$(tmux capturep -pt $i -S -50 2>/dev/null|awk '/[a-z]/&&!/tokens|bypass/{gsub(/^ +| +$/,\"\");s=$0\" \"s}END{print substr(s,1,250)}')\";done","r");if(p){wl=fread(wb,1,32767,p);pclose(p);wb[wl]=0;}}
    else{/* fork-free fast path: read cached window list; a detached ≤1/s bg refresh keeps it warm so the tmux fork stays off the launch path */
        char wc[P];snprintf(wc,P,"%s/win_cache.txt",DDIR);size_t cl;char*cw=readf(wc,&cl);
        if(cw){if(cl>32767)cl=32767;memcpy(wb,cw,cl);wb[cl]=0;wl=cl;free(cw);}
        else{FILE*p=popen("tmux lsw -t '"TMS"' -F '#W\twin' 2>/dev/null","r");if(p){wl=fread(wb,1,32767,p);pclose(p);wb[wl]=0;}}
        struct stat ws;if(stat(wc,&ws)!=0||time(0)-ws.st_mtime>=1){if(fork()==0){setsid();int dn=open("/dev/null",O_WRONLY);if(dn>=0){dup2(dn,1);dup2(dn,2);}
            char cm[P*2];snprintf(cm,sizeof cm,"tmux lsw -t '"TMS"' -F '#W\twin' 2>/dev/null>'%s.%ld'&&mv '%s.%ld' '%s'",wc,(long)getpid(),wc,(long)getpid(),wc);
            execlp("sh","sh","-c",cm,(char*)0);_exit(0);}}}}
    for(char*p=wb,*e=wb+wl;p<e&&n<2048;){char*nl=memchr(p,'\n',(size_t)(e-p));
        if(!nl)nl=e;if(nl>p){*nl=0;lines[n++]=p;}p=nl+1;}
    {static const char*acts[]={"home\tmenu","tmux split-window\tpane","tmux new-window\twin","tmux kill-pane\tpane","tmux kill-window\twin","tmux detach\tquit","tmux kill-session\tquit","tmux resize-pane -Z\tpane","tmux set synchronize-panes\tpane",
        "m main\tm\topen main m.txt","m new\tm\tnew agent file in agent/","m archive\tm\tarchive whole m.txt + push","m archive turn\tm\ttrim last turn","m archive undo\tm\trevert last archive","m restart\tm\tkill+respawn window (reload layout)","m edit\tm\topen m.txt in e",
        "m agent claude\tm\tswitch to Claude","m agent codex\tm\tswitch to Codex (GPT)","m agent gemini\tm\tswitch to Gemini",
        "m model opus\tm\tClaude Opus (1M ctx, deepest)","m model sonnet\tm\tClaude Sonnet (fast/cheap)","m model haiku\tm\tClaude Haiku (fastest)","m model gpt-5\tm\tCodex GPT-5","m model gpt-5.5\tm\tCodex GPT-5.5",
        "m model gemini-2.5-flash\tm\tGemini Flash","m model gemini-2.5-pro\tm\tGemini Pro","m model gemini-3-pro-preview\tm\tGemini 3 Pro preview",
        "m effort low\tm\tlow reasoning effort","m effort medium\tm\tmedium effort","m effort high\tm\thigh effort","m effort max\tm\tmax (Claude only)","m effort xhigh\tm\txhigh (Codex only)",
        "m tier default\tm\tdefault tier","m tier fast\tm\tpriority/fast tier","m tier flex\tm\tflex tier (cheap, slow)",0};
    for(int i=0;acts[i]&&n<2048;i++)lines[n++]=(char*)acts[i];}
    if(!n){puts("Empty cache");free(raw);return 1;}
    {static LNK lk[2048];for(int i=0;i<n;i++){lk[i].s=lines[i];lk[i].k=fq_get(lines[i]);lk[i].i=i;}
     qsort(lk,(size_t)n,sizeof*lk,lnk_cmp);for(int i=0;i<n;i++)lines[i]=lk[i].s;}
    if(!ft0){ai_fpr(ai_fprbuf,sizeof ai_fprbuf,cache);ai_write=1;}  /* re-fingerprint (i_cache may have just regenerated) + arm the deferred save */
    }
    {char*f=getenv("A_FILT_TAG"),*t;int j=0;
    if(f){for(int i=0;i<n;i++)if((t=strchr(lines[i],'\t'))){
        char tg[64];char*t2=strchr(t+1,'\t');size_t tl=t2?(size_t)(t2-t-1):strlen(t+1);
        if(tl>63)tl=63;memcpy(tg,t+1,tl);tg[tl]=0;
        if(strstr(f,tg))lines[j++]=lines[i];}n=j;}}
    int m_mode=0;{char*f=getenv("A_FILT_TAG");if(f&&!strcmp(f,"m"))m_mode=1;}
    if(!isatty(STDIN_FILENO)){for(int i=0;i<n;i++)puts(lines[i]);if(ai_write)ai_savesorted(lines,n,ai_fprbuf);free(raw);return 0;}
    struct winsize ws;
    struct termios old,raw_t;tcgetattr(STDIN_FILENO,&old);raw_t=old;
    raw_t.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);raw_t.c_cc[VMIN]=1;raw_t.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO,TCSANOW,&raw_t);write(STDOUT_FILENO,"\033[?1049h\033[?1000h\033[?1006h\033[?2004h",32);
    char buf[256]="";int blen=0,sel=0,cfgmode=0,paste=0;char prefix[256]="",jstat[96]="",lastwin[16]="",lastidx[8]="";
    static const char*ICFG[]={"agent claude","agent codex","effort low","effort medium","effort high","effort max","effort xhigh",0};
    #define IRST write(STDOUT_FILENO,"\033[?1000l\033[?1006l\033[?2004l",24);tcflush(STDIN_FILENO,TCIFLUSH);tcsetattr(STDIN_FILENO,TCSANOW,&old);(void)!write(STDOUT_FILENO,"\033[?1049l",8);free(raw)
    struct timespec tk=_t0; const char*act="render";  /* tk = per-frame timer (first paint = cold render since _t0; after a key = key→repaint); act = WHAT produced this frame, so the footer says what it just measured */
    while (1) {
        ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws);int maxshow=ws.ws_row>8?ws.ws_row-(m_mode?6:5):10;  /* +2: input-box rules */
        char*fm[2048]; int nm=0,ex=0,plen=(int)strlen(prefix);
        if(cfgmode){for(int i=0;ICFG[i]&&nm<2048;i++){if(blen&&!strcasestr(ICFG[i],buf))continue;fm[nm++]=(char*)ICFG[i];}}
        else for (int i=0;i<n&&nm<2048;i++) {
            if (plen && strncmp(lines[i], prefix, (size_t)plen)) continue;
            if(!blen&&(strstr(lines[i],"\tdir")||!strncmp(lines[i],"web ",4)))continue;
            if(blen){char*s=lines[i]+plen,b2[256],*w;strcpy(b2,buf);int ok=1;
                for(w=strtok(b2," ");w&&ok;w=strtok(0," "))if(!strcasestr(s,w))ok=0;if(!ok)continue;
                if(s[blen]<=' '&&!strncasecmp(s,buf,(size_t)blen)){memmove(fm+ex+1,fm+ex,sizeof*fm*(size_t)(nm++-ex));fm[ex++]=lines[i];continue;}}
            fm[nm++]=lines[i];
        }
        const char*ag=cfget("i_agent");if(!*ag)ag="claude";const char*ef=cfget("i_effort");if(!*ef)ef=strstr(ag,"codex")?"xhigh":"max";
        int na=(lastwin[0]&&!cfgmode&&!blen&&!plen)?1:0,tot=nm+na;if(sel>=tot)sel=tot?tot-1:0;
        int hdr_rows=0;char hl[2048];int hll=0,Wc=ws.ws_col?ws.ws_col:80;
        if(m_mode){load_cfg();const char*cm=cfget("m_model");if(!*cm)cm="opus";
            const char*cg=cfget("m_agent");if(!*cg)cg="claude";
            const char*cf=cfget("m_effort");if(!*cf)cf="low";
            struct stat st;long tot=0;char hp[P];
            snprintf(hp,P,"%s/m/m.txt",SDIR);if(!stat(hp,&st))tot+=st.st_size;
            snprintf(hp,P,"%s/local/a_cat.txt",AROOT);if(!stat(hp,&st))tot+=st.st_size;
            snprintf(hp,P,"%s/mem/index.txt",SROOT);if(!stat(hp,&st))tot+=st.st_size;
            snprintf(hp,P,"%s/m_status",DDIR);char*ms=readf(hp,NULL);
            if(ms)ms[strcspn(ms,"\n")]=0;
            hll=snprintf(hl,2048,"tok %ldk %s -m %s eff=%s%s%s",tot/4/1000,cg,cm,cf,ms&&*ms?" │ ":"",ms?ms:"");
            free(ms); hdr_rows=hll>0?(hll+Wc-1)/Wc:1;}
        int em=m_mode?(ws.ws_row>hdr_rows+5?ws.ws_row-hdr_rows-5:1):maxshow;  /* -2 more: input box rules */
        int avail=em-na>0?em-na:1,ms=sel-na,selm=ms<0?0:ms>=nm?(nm?nm-1:0):ms;
        int top=selm>=avail?selm-avail+1:0, show=nm-top<avail?nm-top:avail;
        double fms;{struct timespec _n;clock_gettime(CLOCK_MONOTONIC,&_n);fms=(_n.tv_sec-tk.tv_sec)*1e3+(_n.tv_nsec-tk.tv_nsec)/1e6;}
        {char fb[B*4];int fl=0;
        #define FP(f,...) fl+=snprintf(fb+fl,fl<B*4?(size_t)(B*4-fl):0,f,##__VA_ARGS__)
        FP("\033[H\033[?25l");
        if(m_mode){int p=0;
            do{int ch=(hll-p)>Wc?Wc:(hll-p);
                FP("\033[36m%.*s\033[0m\033[K\n",ch,hl+p);
                p+=ch?ch:1;
            }while(p<hll);}
        int Wr=ws.ws_col>8?ws.ws_col:80;char hint[320]="";  /* input BOX: bar above+below the > line → safe to type long (prompts, note text) */
        #define RULE do{for(int _r=0;_r<Wr;_r++)FP("─");FP("\033[K\n");}while(0)
        RULE;
        if(cfgmode)FP("config> %s\033[90m  pick agent / effort · ESC back\033[0m\033[K\n",buf);
        else if(jstat[0]&&!blen&&!plen)FP("> \033[90m%s · \033[37m%s %.3fms\033[0m\033[K\n",jstat,act,fms);
        else if(!blen&&!plen)FP("> \033[90m↵ home · type to filter · ^G config · \033[37m%s %.3fms\033[0m\033[K\n",act,fms);
        else if(plen)FP("%s> %s\033[K\n",prefix,buf);
        else{int W=ws.ws_col?ws.ws_col:80,mi=sel-na;FP("> %s\033[K\n",buf);
            if(mi>=0&&mi<nm){char*m=fm[mi],*tb=strchr(m,'\t');snprintf(hint,320,"↵ run: %.*s",tb?(int)(tb-m):(int)strlen(m),m);}
            else snprintf(hint,320,"↵ new tmux win → %s eff=%s : \"%.*s\"",ag,ef,W>44?W-44:20,buf);}
        RULE;
        #undef RULE
        if(hint[0])FP("\033[90m%s\033[0m\033[K\n",hint);
        if(na)FP("%s \033[36m⮌ switch → win %s (just fired)\033[0m\033[K\n",sel==0?" >":"  ",lastidx);
        for(int i=0;i<show;i++){int j=top+i,gj=j+na,W=ws.ws_col;char*t=strchr(fm[j],'\t'),*t2=t?strchr(t+1,'\t'):NULL;
            int ml=t?(int)(t-fm[j]):(int)strlen(fm[j]);
            char*desc=t2?t2+1:(t?t+1:"");int dl=(int)strnlen(desc,50);
            if(ml>W-7-dl)ml=W-7-dl;FP(cfgmode?"%s %.*s\033[K":"%s a %.*s\033[K",gj==sel?" >":"  ",ml,fm[j]);
            if(*desc)FP("\033[%dG\033[90m%.*s\033[0m",W-dl,dl,desc);FP("\n");}
        FP("\033[J\033[%d;%dH\033[?25h",m_mode?(hdr_rows+2):2,plen+blen+3);  /* +1: top rule of input box */
        #undef FP
        (void)!write(STDOUT_FILENO,fb,(size_t)fl);if(ai_write){ai_savesorted(lines,n,ai_fprbuf);ai_write=0;}}
        char ch;
        if(m_mode){struct pollfd pf={.fd=0,.events=POLLIN};int pr=poll(&pf,1,250);
            if(pr==0){clock_gettime(CLOCK_MONOTONIC,&tk);continue;} if(pr<0)break;}
        if(read(0,&ch,1)!=1) break;
        clock_gettime(CLOCK_MONOTONIC,&tk);  /* key arrived → time the repaint it triggers */
        int do_pick=0;
        if(ch=='\x1b'){int av;ioctl(0,FIONREAD,&av);if(!av){usleep(2000);ioctl(0,FIONREAD,&av);}  /* arrow-seq bytes are already buffered → instant; only a genuine lone ESC waits 2ms (was 50ms on every arrow) */
            if(!av){if(m_mode||prefix[0]||cfgmode){cfgmode=0;prefix[0]=0;buf[0]=0;blen=0;sel=0;continue;}break;}
            char seq[2];if(read(0,seq,1)!=1)break;
            if(seq[0]=='['){if(read(0,seq+1,1)!=1)break;
                if(seq[1]=='A'){if(sel>0)sel--;act="↑";}
                else if(seq[1]=='B'){if(sel<tot-1)sel++;act="↓";}
                else if(seq[1]=='<'){int mb=0,my=0;char mc;act="mouse";
                    while(read(0,&mc,1)==1&&mc!=';')mb=mb*10+mc-'0';
                    while(read(0,&mc,1)==1&&mc!=';');
                    while(read(0,&mc,1)==1&&mc!='M'&&mc!='m')my=my*10+mc-'0';
                    if(mc=='M'){if(!mb){int rr=my-(m_mode?3:2);if(na&&rr==0){sel=0;do_pick=1;}
                        else{int ci=top+rr-na;if(ci>=0&&ci<nm){sel=ci+na;do_pick=1;}}}
                    else if(mb==64&&sel>0){sel--;}else if(mb==65&&sel<tot-1){sel++;}}}
                else if(seq[1]=='2'){char d0=0,d1=0;act="paste";  /* bracketed paste marks \033[200~ / \033[201~ */
                    if(read(0,&d0,1)==1&&d0!='~'&&read(0,&d1,1)==1){char t=d1;while(t!='~'&&read(0,&t,1)==1);
                        if(d0=='0'&&d1=='0')paste=1;else if(d0=='0'&&d1=='1')paste=0;}}
            } else if(prefix[0]||blen||cfgmode){cfgmode=0;prefix[0]=0;buf[0]=0;blen=0;sel=0;act="esc";} else if(!m_mode)break;
        } else if(ch=='\t'){if(sel<tot-1)sel++;act="↓";}
        else if(ch=='\x7f'||ch=='\b'){if(blen)buf[--blen]=0;sel=0;act="⌫";}
        else if(ch=='\r'||ch=='\n'){if(paste){if(blen&&blen<254&&buf[blen-1]!=' '){buf[blen++]=' ';buf[blen]=0;}}else do_pick=1;}  /* pasted \n = space, never Enter (pasted email fired web entry → firefox) */
        else if(ch==7&&!cfgmode){cfgmode=1;sel=0;buf[0]=0;blen=0;act="config";(void)!write(STDOUT_FILENO,"\033[2J\033[H",7);continue;}
        else if(ch==3){if(prefix[0]||blen||cfgmode){cfgmode=0;prefix[0]=0;buf[0]=0;blen=0;sel=0;}else if(!m_mode)break;}
        else if(ch==4)break;
        else if(isalnum(ch)||strchr(" -_.",ch)){if(blen<254){buf[blen++]=ch;buf[blen]=0;sel=0;act="filter";}}
        if(do_pick&&cfgmode&&nm&&sel<nm){char fld[16]="",val[32]="",ck[24];
            sscanf(fm[sel],"%15s %31s",fld,val);snprintf(ck,24,"i_%s",fld);cfset(ck,val);load_cfg();
            buf[0]=0;blen=0;sel=0;continue;}
        if(do_pick&&!cfgmode&&!prefix[0]&&blen&&!nm){  /* no command matched → fire the prompt as a new agent win */
            const char*ia=cfget("i_agent");if(!*ia)ia="claude";const char*ie=cfget("i_effort");if(!*ie)ie=strstr(ia,"codex")?"xhigh":"max";
            char run[B+64],cm[B+256],wi[16]="?";
            if(strstr(ia,"codex"))snprintf(run,sizeof run,"codex -c model_reasoning_effort=%s --dangerously-bypass-approvals-and-sandbox \"%s\"",ie,buf);
            else snprintf(run,sizeof run,"claude --dangerously-skip-permissions --effort %s \"%s\"",ie,buf);
            snprintf(cm,sizeof cm,"tmux new-window -dP -F '#{window_index} #{window_id}' -c '%s' '%s;exec bash'",cwd,run);
            pcmd(cm,wi,16);sscanf(wi,"%7s %15s",lastidx,lastwin);
            snprintf(jstat,sizeof jstat,"→ win %s · %s/%s",lastidx,ia,ie);buf[0]=0;blen=0;sel=-1;continue;}  /* sel=-1: nothing selected, so one ↓ lands on the switch row (no accidental switch) */
        if(do_pick&&na&&sel==0){IRST;char c[64];snprintf(c,64,"tmux select-window -t %s",lastwin);(void)!system(c);return 0;}
        if(do_pick&&nm&&sel>=na&&sel-na<nm){char*m=fm[sel-na],cmd[256];
            {char*wt=strstr(m,"\twin\t@");if(wt){IRST;char wc[64];snprintf(wc,64,"tmux selectw -t %.*s",(int)strcspn(wt+5," "),wt+5);(void)!system(wc);return 0;}}
            char*tab=strchr(m,'\t'),*colon=strchr(m,':');
            if(colon&&(!tab||colon<tab)&&strncmp(m,"web ",4)){snprintf(cmd,256,"%.*s",(int)(colon-m),m);char*s=cmd;while(*s==' ')s++;memmove(cmd,s,strlen(s)+1);}
            else{int cl=tab?(int)(tab-m):(int)strlen(m);snprintf(cmd,256,"%.*s",cl,m);}
            {char*e=cmd+strlen(cmd)-1;while(e>cmd&&*e==' ')*e--=0;}
            int hs=0,cl=(int)strlen(cmd);
            for(int i=0;i<n;i++)if(!strncmp(lines[i],cmd,(size_t)cl)&&lines[i][cl]==' '){hs=1;break;}
            if(hs){snprintf(prefix,256,"%s ",cmd);buf[0]=0;blen=0;sel=0;printf("\033[J");continue;}
            if(m_mode||!strncmp(cmd,"m model ",8)||!strncmp(cmd,"m agent ",8)||!strncmp(cmd,"m effort ",9)){char cs[512];snprintf(cs,512,"a %s >/dev/null 2>&1",cmd);(void)!system(cs);load_cfg();
                sel=0;buf[0]=0;blen=0;
                if(!strncmp(cmd,"m agent ",8))snprintf(prefix,256,"m effort ");
                else if(!strncmp(cmd,"m effort ",9))snprintf(prefix,256,"m model ");
                else prefix[0]=0;
                (void)!write(STDOUT_FILENO,"\033[2J\033[H",7);continue;}
            IRST;
            if(dexists(cmd)){char tf[P];snprintf(tf,P,"%s/cd_target",DDIR);writef(tf,cmd);return 0;}
            {int wo=!strncmp(cmd,"open ",5)?5:!strncmp(cmd,"web ",4)?4:0;
            if(wo){alog(cmd,"");if(wo==5){char ac[512];const char*app=cmd+5;
                if(getenv("SWAYSOCK")){
                    char df[P]="",hd[P];snprintf(hd,P,"%s/.local/share/applications",HOME);
                    const char*ad[]={"/usr/share/applications","/usr/local/share/applications","/var/lib/flatpak/exports/share/applications",hd};
                    for(int i=0;i<4;i++){snprintf(df,P,"%s/%s.desktop",ad[i],app);if(fexists(df))break;df[0]=0;}
                    if(df[0])snprintf(ac,512,"swaymsg exec \"gio launch '%s'\"",df);
                    else snprintf(ac,512,"swaymsg exec '%s'",app);
                } else snprintf(ac,512,APP_CMD " '%s'",app);
                (void)!system(ac);}
                else bg_exec(OPENER,cmd+4);return 0;}}
            char*args[32];int ac=0;args[ac++]="a";
            for(char*p=cmd;*p&&ac<31;){while(*p==' ')p++;if(!*p)break;args[ac++]=p;while(*p&&*p!=' ')p++;if(*p)*p++=0;}
            args[ac]=NULL;setenv("A_TUI","1",1);execvp("a",args);return 0;}
    }
    IRST;
    #undef IRST
    return 0;
}
