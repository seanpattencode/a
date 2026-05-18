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
    create_sess(sn, wd, s->cmd, xp);
    tm_go(sn);
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
    for(int i=0;i<nfq;i++){int l=(int)strlen(fq[i].n);if(l>bl&&!strncasecmp(s,fq[i].n,(size_t)l)&&(!s[l]||s[l]=='\t')){b=fq[i].c;bl=l;}}return b;}
static int ln_cmp(const void*a,const void*b){return fq_get(*(char*const*)b)-fq_get(*(char*const*)a);}
static int cmd_i(int argc, char **argv) { (void)argc; (void)argv;
    AB;
    perf_disarm(); init_db();
    char cache[P];snprintf(cache,P,"%s/i_cache.txt",DDIR);
    size_t len;char*raw=readf(cache,&len);
    if(!raw){gen_icache();raw=readf(cache,&len);if(!raw)return 1;}
    {char fp[P];snprintf(fp,P,"%s/freq_cache.txt",DDIR);FILE*ff=fopen(fp,"r");if(ff){char ln[128];nfq=0;
        while(nfq<1024&&fgets(ln,128,ff)){char*c=strchr(ln,':');if(!c)continue;*c=0;
            snprintf(fq[nfq].n,64,"%s",ln);fq[nfq].c=atoi(c+1);nfq++;}fclose(ff);}}
    char*lines[1024];int n=0;
    for(char*p=raw,*end=raw+len;p<end&&n<1024;){char*nl=memchr(p,'\n',(size_t)(end-p));
        if(!nl)nl=end;if(nl>p&&p[0]!='<'&&p[0]!='='&&p[0]!='>'&&p[0]!='#'){*nl=0;lines[n++]=p;}p=nl+1;}
    size_t wl;char wp[P];snprintf(wp,P,"%s/web_cache.txt",DDIR);char*wraw=readf(wp,&wl);
    if(wraw){for(char*p=wraw,*end=wraw+wl;p<end&&n<1024;){char*nl=memchr(p,'\n',(size_t)(end-p));
        if(!nl)nl=end;if(nl>p){*nl=0;lines[n++]=p;}p=nl+1;}}
    char*tbuf=NULL;{FILE*tp=popen("tmux list-windows -aF '#W\twin' 2>/dev/null","r");
        if(tp){tbuf=malloc(4096);size_t tl=fread(tbuf,1,4095,tp);pclose(tp);tbuf[tl]=0;
        for(char*p=tbuf,*end=tbuf+tl;p<end&&n<1024;){char*nl=memchr(p,'\n',(size_t)(end-p));
            if(!nl)break;*nl=0;if(nl>p)lines[n++]=p;p=nl+1;}}}
    if(!n){puts("Empty cache");free(raw);return 1;}
    if(nfq)qsort(lines,(size_t)n,sizeof*lines,ln_cmp);  /* freq-rank: TUI + 'a i' pipe identical */
    if(!isatty(STDIN_FILENO)){for(int i=0;i<n;i++)puts(lines[i]);free(raw);return 0;}
    struct winsize ws;ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws);int maxshow=ws.ws_row>6?ws.ws_row-3:10;
    struct termios old,raw_t;tcgetattr(STDIN_FILENO,&old);raw_t=old;
    raw_t.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);raw_t.c_cc[VMIN]=1;raw_t.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO,TCSANOW,&raw_t);write(STDOUT_FILENO,"\033[?1000h\033[?1006h",16);
    char buf[256]="";int blen=0,sel=0;char prefix[256]="";
    #define IRST write(STDOUT_FILENO,"\033[?1000l\033[?1006l",16);tcflush(STDIN_FILENO,TCIFLUSH);tcsetattr(STDIN_FILENO,TCSANOW,&old);(void)!system("clear");free(raw);free(wraw);free(tbuf)
    while (1) {
        char*fm[1024]; int nm = 0; int plen = (int)strlen(prefix);
        for (int i=0;i<n&&nm<1024;i++) {
            if (plen && strncmp(lines[i], prefix, (size_t)plen)) continue;
            if(!blen&&(strstr(lines[i],"\tdir")||!strncmp(lines[i],"web ",4)))continue;
            if(blen){char*s=lines[i]+plen,b2[256],*w;snprintf(b2,256,"%s",buf);int ok=1;
                for(w=strtok(b2," ");w&&ok;w=strtok(0," "))if(!strcasestr(s,w))ok=0;if(!ok)continue;}
            fm[nm++]=lines[i];
        }
        {int mx=nm?nm:blen?2:0;if(sel>=mx)sel=mx?mx-1:0;}
        int top=sel>=maxshow?sel-maxshow+1:0, show=nm-top<maxshow?nm-top:maxshow;
        {char fb[B*4];int fl=0;
        #define FP(f,...) fl+=snprintf(fb+fl,fl<B*4?(size_t)(B*4-fl):0,f,##__VA_ARGS__)
        FP("\033[H\033[?25l%s> %s\033[K\n",prefix,buf);
        if(!nm&&blen){FP("%s \033[35ma c \"%s\"\033[0m\033[K\n",sel==0?" >":"  ",buf);
            FP("%s \033[36mGoogle: %s\033[0m\033[K\n",sel==1?" >":"  ",buf);}
        for(int i=0;i<show;i++){int j=top+i,W=ws.ws_col;char*t=strchr(fm[j],'\t');int ml=t?(int)(t-fm[j]):(int)strlen(fm[j]);
            if(ml>W-5)ml=W-5;FP("%s a %.*s\033[K",j==sel?" >":"  ",ml,fm[j]);
            if(t&&ml+5+(int)strlen(t+1)<W)FP("\033[%dG\033[90m%s\033[0m",W-(int)strlen(t+1),t+1);FP("\n");}
        FP("\033[J\033[1;%dH\033[?25h",plen+blen+3);
        #undef FP
        (void)!write(STDOUT_FILENO,fb,(size_t)fl);}
        char ch; if(read(0,&ch,1)!=1) break;
        int do_pick=0;
        if(ch=='\x1b'){int av;usleep(50000);ioctl(0,FIONREAD,&av);if(!av)break;
            char seq[2];if(read(0,seq,1)!=1)break;
            if(seq[0]=='['){if(read(0,seq+1,1)!=1)break;
                if(seq[1]=='A'){if(sel>0)sel--;}
                else if(seq[1]=='B'){int mx=nm?nm-1:blen?1:0;if(sel<mx)sel++;}
                else if(seq[1]=='<'){int mb=0,my=0;char mc;
                    while(read(0,&mc,1)==1&&mc!=';')mb=mb*10+mc-'0';
                    while(read(0,&mc,1)==1&&mc!=';'){}
                    while(read(0,&mc,1)==1&&mc!='M'&&mc!='m')my=my*10+mc-'0';
                    if(mc=='M'&&mb==0){int ci=my-2+top;if(ci>=0&&ci<nm){sel=ci;do_pick=1;}}
                    else if(mc=='M'&&(mb==64||mb==65)){if(mb==64&&sel>0)sel--;if(mb==65&&sel<nm-1)sel++;}}
            } else if(prefix[0]){prefix[0]=0;buf[0]=0;blen=0;sel=0;} else break;
        } else if(ch=='\t'){int mx=nm?nm-1:blen?1:0;if(sel<mx)sel++;}
        else if(ch=='\x7f'||ch=='\b'){if(blen)buf[--blen]=0;sel=0;}
        else if(ch=='\r'||ch=='\n'){if(!nm&&blen){IRST;
            if(sel==0){char*args[]={"a","c",buf,NULL};execvp("a",args);}
            else{char u[512];snprintf(u,512,"https://google.com/search?q=%s",buf);
                for(char*p=u;*p;p++)if(*p==' ')*p='+';bg_exec(OPENER,u);}
            return 0;}do_pick=1;}
        else if(ch==3||ch==4)break;
        else if(isalnum(ch)||ch=='-'||ch=='_'||ch==' '||ch=='.'){if(blen<254){buf[blen++]=ch;buf[blen]=0;sel=0;}}
        if(do_pick&&nm){char*m=fm[sel],cmd[256];
            char*tab=strchr(m,'\t'),*colon=strchr(m,':');
            if(colon&&(!tab||colon<tab)&&strncmp(m,"web ",4)){snprintf(cmd,256,"%.*s",(int)(colon-m),m);char*s=cmd;while(*s==' ')s++;memmove(cmd,s,strlen(s)+1);}
            else{int cl=tab?(int)(tab-m):(int)strlen(m);snprintf(cmd,256,"%.*s",cl,m);}
            {char*e=cmd+strlen(cmd)-1;while(e>cmd&&*e==' ')*e--=0;}
            int hs=0,cl=(int)strlen(cmd);
            for(int i=0;i<n;i++)if(!strncmp(lines[i],cmd,(size_t)cl)&&lines[i][cl]==' '){hs=1;break;}
            if(hs){snprintf(prefix,256,"%s ",cmd);buf[0]=0;blen=0;sel=0;printf("\033[J");continue;}
            IRST;
            if(dexists(cmd)){char tf[P];snprintf(tf,P,"%s/cd_target",DDIR);writef(tf,cmd);return 0;}
            {int wo=!strncmp(cmd,"open ",5)?5:!strncmp(cmd,"web ",4)?4:0;
            if(wo){alog(cmd,"");if(wo==5){char ac[512];const char*app=cmd+5;
                if(getenv("SWAYSOCK")){
                    /* find Exec= from .desktop, strip %U etc, run via swaymsg */
                    char df[P]="";snprintf(df,P,"/usr/share/applications/%s.desktop",app);
                    if(!fexists(df))snprintf(df,P,"/usr/local/share/applications/%s.desktop",app);
                    if(fexists(df)){char*d=readf(df,NULL);char*ex=d?strstr(d,"Exec="):NULL;
                        if(ex){ex+=5;char*nl=strchr(ex,'\n');if(nl)*nl=0;
                            char*pct=strchr(ex,'%');if(pct)*pct=0;
                            while(ex[strlen(ex)-1]==' ')ex[strlen(ex)-1]=0;
                            snprintf(ac,512,"swaymsg 'exec %s'",ex);}
                        else snprintf(ac,512,"swaymsg 'exec %s'",app);
                        free(d);
                    } else snprintf(ac,512,"swaymsg 'exec %s'",app);
                } else snprintf(ac,512,APP_CMD " '%s'",app);
                (void)!system(ac);}
                else bg_exec(OPENER,cmd+4);return 0;}}
            printf("Running: a %s\n",cmd);
            char*args[32];int ac=0;args[ac++]="a";
            for(char*p=cmd;*p&&ac<31;){while(*p==' ')p++;if(!*p)break;args[ac++]=p;while(*p&&*p!=' ')p++;if(*p)*p++=0;}
            args[ac]=NULL;execvp("a",args);return 0;}
    }
    IRST;
    #undef IRST
    return 0;
}
