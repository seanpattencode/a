#include <poll.h>
static int cmd_sess(int argc, char **argv) {
    init_db(); load_cfg(); load_proj(); load_apps(); load_sess();
    const char *key = argv[1];
    sess_t *s = find_sess(key);
    if (!s) return -1;  /* not a session key */
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
    /* claim ghost if matches */
    {char gf[P];snprintf(gf,P,"%s/ghost",DDIR);char*gh=readf(gf,NULL);
    if(gh){gh[strcspn(gh,"\n")]=0;if(!strcmp(gh,sn)&&tm_has(sn)){unlink(gf);free(gh);
        if(is_prompt&&prompt[0]){tm_send(sn,prompt);usleep(100000);tm_key(sn,"Enter");}
        tm_go(sn);return 0;}free(gh);}}
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
    if (create_sess(sn, wd, s->cmd, xp) != 2) tm_go(sn);
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
static int fq_get(const char*s){int b=0,bl=0;
    for(int i=0;i<nfq;i++){int l=(int)strlen(fq[i].n);if(l>bl&&!strncasecmp(s,fq[i].n,(size_t)l)&&(!s[l]||s[l]=='\t')){b=fq[i].c;bl=l;}}return strstr(s,"\tproject")?(1<<30)-atoi(s):b;}
static int ln_cmp(const void*a,const void*b){return fq_get(*(char*const*)b)-fq_get(*(char*const*)a);}
static int cmd_i(int argc, char **argv) { (void)argc; (void)argv;
    AB;
    perf_disarm(); init_db();
    char cache[P];snprintf(cache,P,"%s/i_cache.txt",DDIR);
    size_t len;char*raw=readf(cache,&len);
    {struct stat c,s;char d[P];snprintf(d,P,"%s/ssh",SROOT);if(raw&&!stat(cache,&c)&&!stat(d,&s)&&s.st_mtime>c.st_mtime){free(raw);raw=0;}}
    if(!raw){gen_icache();raw=readf(cache,&len);if(!raw)return 1;}
    {char fp[P];snprintf(fp,P,"%s/freq_cache.txt",DDIR);FILE*ff=fopen(fp,"r");if(ff){char ln[128];nfq=0;
        while(nfq<1024&&fgets(ln,128,ff)){char*c=strchr(ln,':');if(!c)continue;*c=0;
            snprintf(fq[nfq].n,64,"%s",ln);fq[nfq].c=atoi(c+1);nfq++;}fclose(ff);}}
    char*lines[2048];int n=0;
    for(char*p=raw,*end=raw+len;p<end&&n<2048;){char*nl=memchr(p,'\n',(size_t)(end-p));
        if(!nl)nl=end;if(nl>p&&!strchr("<=>#",*p)){*nl=0;lines[n++]=p;}p=nl+1;}
    static char wb[32768];size_t wl=0;
    {char cm[P];snprintf(cm,P,"cat '%s/web_cache.txt' 2>/dev/null;tmux list-windows -aF '#W\twin' 2>/dev/null",DDIR);
     FILE*p=popen(cm,"r");if(p){wl=fread(wb,1,32767,p);pclose(p);wb[wl]=0;}}
    for(char*p=wb,*e=wb+wl;p<e&&n<2048;){char*nl=memchr(p,'\n',(size_t)(e-p));
        if(!nl)nl=e;if(nl>p){*nl=0;lines[n++]=p;}p=nl+1;}
    {static const char*acts[]={"tmux split-window\tpane","tmux new-window\twin","tmux kill-pane\tpane","tmux kill-window\twin","tmux detach\tquit","tmux kill-session\tquit","tmux resize-pane -Z\tpane","tmux set synchronize-panes\tpane",
        "m main\tm\topen main m.txt","m new\tm\tnew agent file in agent/","m archive\tm\tarchive whole m.txt + push","m archive turn\tm\ttrim last turn","m archive undo\tm\trevert last archive","m restart\tm\tkill+respawn window (reload layout)","m edit\tm\topen m.txt in e",
        "m agent claude\tm\tswitch to Claude","m agent codex\tm\tswitch to Codex (GPT)","m agent gemini\tm\tswitch to Gemini",
        "m model opus\tm\tClaude Opus (1M ctx, deepest)","m model sonnet\tm\tClaude Sonnet (fast/cheap)","m model haiku\tm\tClaude Haiku (fastest)","m model gpt-5\tm\tCodex GPT-5","m model gpt-5.5\tm\tCodex GPT-5.5",
        "m model gemini-2.5-flash\tm\tGemini Flash","m model gemini-2.5-pro\tm\tGemini Pro","m model gemini-3-pro-preview\tm\tGemini 3 Pro preview",
        "m effort low\tm\tlow reasoning effort","m effort medium\tm\tmedium effort","m effort high\tm\thigh effort","m effort max\tm\tmax (Claude only)","m effort xhigh\tm\txhigh (Codex only)",
        "m tier default\tm\tdefault tier","m tier fast\tm\tpriority/fast tier","m tier flex\tm\tflex tier (cheap, slow)",0};
    for(int i=0;acts[i]&&n<2048;i++)lines[n++]=(char*)acts[i];}
    if(!n){puts("Empty cache");free(raw);return 1;}
    qsort(lines,(size_t)n,sizeof*lines,ln_cmp);
    {char*f=getenv("A_FILT_TAG"),*t;int j=0;
    if(f){for(int i=0;i<n;i++)if((t=strchr(lines[i],'\t'))){
        char tg[64];char*t2=strchr(t+1,'\t');size_t tl=t2?(size_t)(t2-t-1):strlen(t+1);
        if(tl>63)tl=63;memcpy(tg,t+1,tl);tg[tl]=0;
        if(strstr(f,tg))lines[j++]=lines[i];}n=j;}}
    int m_mode=0;{char*f=getenv("A_FILT_TAG");if(f&&!strcmp(f,"m"))m_mode=1;}
    if(!isatty(STDIN_FILENO)){for(int i=0;i<n;i++)puts(lines[i]);free(raw);return 0;}
    struct winsize ws;ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws);int maxshow=ws.ws_row>6?ws.ws_row-(m_mode?4:3):10;
    struct termios old,raw_t;tcgetattr(STDIN_FILENO,&old);raw_t=old;
    raw_t.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);raw_t.c_cc[VMIN]=1;raw_t.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO,TCSANOW,&raw_t);write(STDOUT_FILENO,"\033[?1000h\033[?1006h",16);
    char buf[256]="";int blen=0,sel=0;char prefix[256]="";
    #define IRST write(STDOUT_FILENO,"\033[?1000l\033[?1006l",16);tcflush(STDIN_FILENO,TCIFLUSH);tcsetattr(STDIN_FILENO,TCSANOW,&old);(void)!system("clear");free(raw)
    while (1) {
        char*fm[2048]; int nm=0,ex=0,plen=(int)strlen(prefix);
        for (int i=0;i<n&&nm<2048;i++) {
            if (plen && strncmp(lines[i], prefix, (size_t)plen)) continue;
            if(!blen&&(strstr(lines[i],"\tdir")||!strncmp(lines[i],"web ",4)))continue;
            if(blen){char*s=lines[i]+plen,b2[256],*w;strcpy(b2,buf);int ok=1;
                for(w=strtok(b2," ");w&&ok;w=strtok(0," "))if(!strcasestr(s,w))ok=0;if(!ok)continue;
                if(s[blen]<=' '&&!strncasecmp(s,buf,(size_t)blen)){memmove(fm+ex+1,fm+ex,sizeof*fm*(size_t)(nm++-ex));fm[ex++]=lines[i];continue;}}
            fm[nm++]=lines[i];
        }
        {int mx=nm?nm:blen?2:0;if(sel>=mx)sel=mx?mx-1:0;}
        int hdr_rows=0;char hl[2048];int hll=0,Wc=ws.ws_col?ws.ws_col:80;
        if(m_mode){load_cfg();const char*cm=cfget("m_model");if(!*cm)cm="opus";
            const char*cg=cfget("m_agent");if(!*cg)cg="claude";
            const char*cf=cfget("m_effort");if(!*cf)cf="low";
            struct stat st;long tot=0;char hp[P];
            snprintf(hp,P,"%s/m/m.txt",SDIR);if(!stat(hp,&st))tot+=st.st_size;
            snprintf(hp,P,"%s/local/a_cat.txt",AROOT);if(!stat(hp,&st))tot+=st.st_size;
            snprintf(hp,P,"%s/m/i.txt",SDIR);if(!stat(hp,&st))tot+=st.st_size;
            snprintf(hp,P,"%s/m_status",DDIR);char*ms=readf(hp,NULL);
            if(ms)ms[strcspn(ms,"\n")]=0;
            hll=snprintf(hl,2048,"tok %ldk %s -m %s eff=%s%s%s",tot/4/1000,cg,cm,cf,ms&&*ms?" │ ":"",ms?ms:"");
            free(ms); hdr_rows=hll>0?(hll+Wc-1)/Wc:1;}
        int em=m_mode?(ws.ws_row>hdr_rows+3?ws.ws_row-hdr_rows-3:1):maxshow;
        int top=sel>=em?sel-em+1:0, show=nm-top<em?nm-top:em;
        {char fb[B*4];int fl=0;
        #define FP(f,...) fl+=snprintf(fb+fl,fl<B*4?(size_t)(B*4-fl):0,f,##__VA_ARGS__)
        FP("\033[H\033[?25l");
        if(m_mode){int p=0;
            do{int ch=(hll-p)>Wc?Wc:(hll-p);
                FP("\033[36m%.*s\033[0m\033[K\n",ch,hl+p);
                p+=ch?ch:1;
            }while(p<hll);}
        FP("%s> %s\033[K\n",prefix,buf);
        if(!nm&&blen){FP("%s \033[35ma c \"%s\"\033[0m\033[K\n",sel==0?" >":"  ",buf);
            FP("%s \033[36mGoogle: %s\033[0m\033[K\n",sel==1?" >":"  ",buf);}
        for(int i=0;i<show;i++){int j=top+i,W=ws.ws_col;char*t=strchr(fm[j],'\t'),*t2=t?strchr(t+1,'\t'):NULL;
            int ml=t?(int)(t-fm[j]):(int)strlen(fm[j]);
            char*desc=t2?t2+1:(t?t+1:"");int dl=(int)strlen(desc);
            if(ml>W-7-dl)ml=W-7-dl;FP("%s a %.*s\033[K",j==sel?" >":"  ",ml,fm[j]);
            if(*desc)FP("\033[%dG\033[90m%s\033[0m",W-dl,desc);FP("\n");}
        FP("\033[J\033[%d;%dH\033[?25h",m_mode?(hdr_rows+1):1,plen+blen+3);
        #undef FP
        (void)!write(STDOUT_FILENO,fb,(size_t)fl);}
        char ch;
        if(m_mode){struct pollfd pf={.fd=0,.events=POLLIN};int pr=poll(&pf,1,250);
            if(pr==0)continue; if(pr<0)break;}
        if(read(0,&ch,1)!=1) break;
        int do_pick=0;
        if(ch=='\x1b'){int av;usleep(50000);ioctl(0,FIONREAD,&av);if(!av)break;
            char seq[2];if(read(0,seq,1)!=1)break;
            if(seq[0]=='['){if(read(0,seq+1,1)!=1)break;
                if(seq[1]=='A'){if(sel>0)sel--;}
                else if(seq[1]=='B'){int mx=nm?nm-1:blen?1:0;if(sel<mx)sel++;}
                else if(seq[1]=='<'){int mb=0,my=0;char mc;
                    while(read(0,&mc,1)==1&&mc!=';')mb=mb*10+mc-'0';
                    while(read(0,&mc,1)==1&&mc!=';');
                    while(read(0,&mc,1)==1&&mc!='M'&&mc!='m')my=my*10+mc-'0';
                    if(mc=='M'){if(!mb){int ci=my-(m_mode?3:2)+top;if(ci>=0&&ci<nm){sel=ci;do_pick=1;}}
                    else if(mb==64&&sel>0)sel--;else if(mb==65&&sel<nm-1)sel++;}}
            } else if(prefix[0]||blen){prefix[0]=0;buf[0]=0;blen=0;sel=0;} else break;
        } else if(ch=='\t'){int mx=nm?nm-1:blen?1:0;if(sel<mx)sel++;}
        else if(ch=='\x7f'||ch=='\b'){if(blen)buf[--blen]=0;sel=0;}
        else if(ch=='\r'||ch=='\n'){if(!nm&&blen){IRST;
            if(sel==0){char*args[]={"a","c",buf,NULL};execvp("a",args);}
            else{char u[512];snprintf(u,512,"https://google.com/search?q=%s",buf);
                for(char*p=u;*p;p++)if(*p==' ')*p='+';bg_exec(OPENER,u);}
            return 0;}do_pick=1;}
        else if(ch==3){if(prefix[0]||blen){prefix[0]=0;buf[0]=0;blen=0;sel=0;}else if(!m_mode)break;}
        else if(ch==4)break;
        else if(isalnum(ch)||strchr(" -_.",ch)){if(blen<254){buf[blen++]=ch;buf[blen]=0;sel=0;}}
        if(do_pick&&nm){char*m=fm[sel],cmd[256];
            char*tab=strchr(m,'\t'),*colon=strchr(m,':');
            if(colon&&(!tab||colon<tab)&&strncmp(m,"web ",4)){snprintf(cmd,256,"%.*s",(int)(colon-m),m);char*s=cmd;while(*s==' ')s++;memmove(cmd,s,strlen(s)+1);}
            else{int cl=tab?(int)(tab-m):(int)strlen(m);snprintf(cmd,256,"%.*s",cl,m);}
            {char*e=cmd+strlen(cmd)-1;while(e>cmd&&*e==' ')*e--=0;}
            int hs=0,cl=(int)strlen(cmd);
            for(int i=0;i<n;i++)if(!strncmp(lines[i],cmd,(size_t)cl)&&lines[i][cl]==' '){hs=1;break;}
            if(hs){snprintf(prefix,256,"%s ",cmd);buf[0]=0;blen=0;sel=0;printf("\033[J");continue;}
            if(m_mode){char cs[512];snprintf(cs,512,"a %s >/dev/null 2>&1",cmd);(void)!system(cs);
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
