/* m — platonic chat agent: FILE = the agent (adata/git/m/agents/<name>.txt), model = ANY shell cmd stdin→stdout.
 * a m [name] chat · a m <name> <task...> one-shot spawn unit · use <ag> <md> [ef] fleet keys · cmd <raw>|clear · '/' menu */
static volatile sig_atomic_t g_halt;
static void m_sint(int s){(void)s;g_halt=1;}
static void m_ap(const char*sf,const char*h,const char*t){FILE*f=fopen(sf,"a");if(f){fprintf(f,"## %s\n%s\n",h,t);fclose(f);}}
static void m_cmdstr(char*o,size_t n){const char*mc=cfget("m_cmd");if(*mc){snprintf(o,n,"%s",mc);return;}
    const char*md=cfget("m_model"),*ef=cfget("m_effort");
    snprintf(o,n,"claude -p --tools '' --model '%s' --effort '%s'",*md?md:"opus",*ef?ef:"max");}
static void m_run(const char*sf,const char*wd){ /* agentic loop: model → last CMD: → run in wd → feed back */
    static char b[1<<16],x[B*4],mc[B];char last[B]="";int rep=0;
    load_cfg();m_cmdstr(mc,B);
    for(int i=0;i<25&&!g_halt;i++){  /* multiple commands until the model stops (no CMD) */
        struct timespec t0,t1;clock_gettime(CLOCK_MONOTONIC,&t0);
        snprintf(x,sizeof x,"{ echo 'Shell agent: any reply line CMD:<shell cmd> runs on this computer (cwd %s), output fed back. No CMD = final answer. Examples:\n## user\ntime?\n## assistant\nCMD:date\n## user\nshow wikipedia\n## assistant\nCMD:xdg-open https://wikipedia.org\n## tool\n(no output)\n## assistant\nOpen.\nNow:';cat '%s';}|%s",wd,sf,mc);
        FILE*p=popen(x,"r");if(!p)return;
        int fd=fileno(p),tty=isatty(1),fr=0;size_t al=0;ssize_t n;char ch[4096];
        if(tty){fputs("\033[2m⠋ 0.0s\033[0m",stdout);fflush(stdout);}  /* thinking indicator, instant */
        while(!g_halt){
            struct pollfd pf={fd,POLLIN,0};int pr=poll(&pf,1,100);
            if(pr<0)break;
            if(!pr){if(tty&&!al){struct timespec tn;clock_gettime(CLOCK_MONOTONIC,&tn);
                printf("\r\033[2m%.3s %.1fs\033[0m ","⠙⠹⠸⠼⠴⠦⠧⠇⠏⠋"+fr++%10*3,(double)(tn.tv_sec-t0.tv_sec)+(double)(tn.tv_nsec-t0.tv_nsec)/1e9);fflush(stdout);}continue;}
            n=read(fd,ch,sizeof ch);if(n<=0)break;
            if(tty&&!al)fputs("\r\033[K",stdout);
            fwrite(ch,1,(size_t)n,stdout);fflush(stdout);
            if(al+(size_t)n<sizeof b-1){memcpy(b+al,ch,(size_t)n);al+=(size_t)n;}}
        pclose(p);b[al]=0;
        if(!al){snprintf(b,64,"(empty model reply — check: a m cmd)");fputs(b,stdout);}
        clock_gettime(CLOCK_MONOTONIC,&t1);
        fprintf(stderr,"\n\033[2m[%.1fs]\033[0m\n",(double)(t1.tv_sec-t0.tv_sec)+(double)(t1.tv_nsec-t0.tv_nsec)/1e9);
        m_ap(sf,"assistant",b);
        char*t=0;for(char*s=b;(s=strstr(s,"CMD:"));s+=4)t=s;  /* LAST match: thinking models narrate CMD: before deciding */
        if(!t||g_halt)break;
        char*cm=t+4;while(*cm==' '||*cm=='`')cm++;
        char*e=strchr(cm,'\n');if(e)*e=0;
        for(e=cm+strlen(cm);e>cm&&(e[-1]=='`'||e[-1]==' ');)*--e=0;
        if(!strcmp(cm,last)){if(++rep>=3)break;}else rep=0;  /* soft repeat guard (report §8): deliberate repeats ok, 3 identical in a row = loop */
        snprintf(last,B,"%s",cm);
        printf("\033[33m$ %s\033[0m\n",cm);
        snprintf(x,sizeof x,"cd '%s'&&{ %s ;} 2>&1|tail -c 4000",wd,cm);
        p=popen(x,"r");al=p?fread(b,1,sizeof b-1,p):0;if(p)pclose(p);b[al]=0;
        if(!al)strcpy(b,"(no output)");
        fputs(b,stdout);
        m_ap(sf,"tool",b);
    }
    snprintf(x,sizeof x,"(flock /tmp/.a_git.lock -c \"cd '%s'&&git add m&&{ git diff --cached --quiet||{ git commit -q -m m&&timeout 8 git push -q;};}\")>/dev/null 2>&1 &",SROOT);
    (void)!system(x);
}
static int m_resume(char*m,size_t sz){  /* saved convos (adata/git/m/agents/*.txt) newest-first, first-msg preview; pick → m="/<name>" */
    static char ib[24][96];const char*it[24];char ls[4096],sel[96];int n=0;
    {char gc[B];snprintf(gc,B,"cd '%s/m/agents' 2>/dev/null&&ls -t|sed 's/\\.txt$//'|while read -r f;do printf '%%s\t%%.60s\n' \"$f\" \"$(sed -n 2p \"$f.txt\")\";done",SROOT);pcmd(gc,ls,sizeof ls);}
    for(char*q=ls;*q&&n<24;){char*nl=strchr(q,'\n');if(nl)*nl=0;if(*q){snprintf(ib[n],96,"%s",q);it[n]=ib[n];n++;}if(!nl)break;q=nl+1;}
    if(!n||m_pick("resume",it,n,sel,sizeof sel)<=0)return 0;
    sel[strcspn(sel,"\t")]=0;snprintf(m,sz,"/%s",sel);return 1;}
/* '/' on empty box → m_pick live-filter menu (type-to-complete, a i style). Model picks set m_cmd ONLY —
 * fleet keys m_agent/m_model stay untouched (a c/a j spawns unaffected). Returns 1=submit m, 2=keep editing m, 0=handled. */
static int m_slash(char *m,size_t sz){
    static const char *ops[]={"model\tpick model from list","resume\topen saved conversation","new\tfresh agent","cmd\ttype raw model cmd","q\tquit"};
    char sel[96];
    int r=m_pick("cmd",ops,5,sel,sizeof sel);
    if(r<=0)return 0;
    sel[strcspn(sel,"\t")]=0;
    if(!strcmp(sel,"q")||!strcmp(sel,"new")){snprintf(m,sz,"/%s",sel);return 1;}
    if(!strcmp(sel,"resume"))return m_resume(m,sz);
    if(!strcmp(sel,"cmd")){snprintf(m,sz,"/cmd ");return 2;}
    static char ib[24][96];const char*it[24];int n=0;
    static const char*cl[]={"fable","opus","sonnet","haiku",0};
    for(int k=0;cl[k]&&n<24;k++){snprintf(ib[n],96,"%s\tclaude",cl[k]);it[n]=ib[n];n++;}
    char ol[4096];{char gc[B];snprintf(gc,B,"awk -F'\t' '!/^#/&&NF>1{print $1\"\tserver\"}' '%s/m/models.txt' 2>/dev/null;ollama list 2>/dev/null|awk 'NR>1{print $1\"\tollama local\"}'",SROOT);pcmd(gc,ol,sizeof ol);}  /* models.txt label\tcmd rows; servers first — locals flood the 24 cap */
    for(char*q=ol;*q&&n<24;){char*nl=strchr(q,'\n');if(nl)*nl=0;if(*q){snprintf(ib[n],96,"%s",q);it[n]=ib[n];n++;}if(!nl)break;q=nl+1;}
    r=m_pick("model",it,n,sel,sizeof sel);
    if(r<=0)return 0;
    char *tb=strchr(sel,'\t');int oll=tb&&strstr(tb+1,"ollama"),srv=tb&&strstr(tb+1,"server");if(tb)*tb=0;
    load_cfg();char nc[B];
    if(srv){char gc[B];snprintf(gc,B,"grep -m1 '^%s\t' '%s/m/models.txt'|cut -f2-",sel,SROOT);pcmd(gc,nc,B);nc[strcspn(nc,"\n")]=0;if(!nc[0])return 0;}
    else if(oll)snprintf(nc,B,"jq -Rs '{model:\"%s\",prompt:.,stream:false,think:true}'|curl -sS -d @- localhost:11434/api/generate|jq -r .response",sel);
    else{const char*ef=cfget("m_effort");snprintf(nc,B,"claude -p --tools '' --model '%s' --effort '%s'",sel,*ef?ef:"max");}
    cfset("m_cmd",nc);snprintf(m,sz,"/new");return 1;  /* pick = fresh agent: old convo pollutes the new model */
}
#define M_ST(st,fn) {load_cfg();char _mc[B];m_cmdstr(_mc,B);snprintf(st,B,"%s · %.60s · /=menu",fn,_mc);}
/* menu=1: chat (sfn=agent name, '/' opens m_slash). menu=0: generic box (sfn=literal status), for a i etc.
 * Buffer is heap-grown (any length); *out = the text (static, valid until next call). */
static size_t m_input(char **out,const char *sfn,int menu){
    static size_t cap;static char *m;if(!m){cap=4096;m=malloc(cap);}
    char st[B];if(menu)M_ST(st,sfn)else snprintf(st,B,"%s",sfn);
    struct sigaction sa={0},osa;sa.sa_handler=m_sint;sigaction(SIGWINCH,&sa,&osa);
    struct termios o,r;tcgetattr(0,&o);r=o;r.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);r.c_cc[VMIN]=1;r.c_cc[VTIME]=0;tcsetattr(0,TCSANOW,&r);
    fputs("\033[?2004h",stdout);
    size_t l=0;int paste=0,q=0,ctop=1,pTR=0,mtR=1;
    #define MFIT do{if(l+2>cap){cap*=2;m=realloc(m,cap);}}while(0)
    for(;;){
        struct timespec p0;clock_gettime(CLOCK_MONOTONIC,&p0);  /* 1MS MANDATE: measure key→painted, show it live */
        struct winsize ws;ioctl(1,TIOCGWINSZ,&ws);int W=ws.ws_col>8?ws.ws_col:80,H=ws.ws_row>6?ws.ws_row:24;
        int tR=1,cc=3;size_t ro[128];ro[1]=0;  /* wrap walk (codepoints, terminal's rule) → rows + cursor col; ro = ring of row-start offsets for the tail anchor */
        for(size_t k=0;k<l;k++){
            if(m[k]=='\n'){tR++;cc=1;ro[tR&127]=k+1;continue;}
            if((m[k]&0xC0)==0x80)continue;
            if(cc>W){tR++;cc=1;ro[tR&127]=k;}
            cc++;}
        int pend=cc>W;if(pend){tR++;cc=1;ro[tR&127]=l;}
        int BR=H-5>120?120:H-5;if(BR<3)BR=3;  /* box row budget: overflow → 1 count row + BR-1 tail rows, end always visible */
        int eR=tR,ind=0;size_t ds=0;
        if(tR>BR){ind=1;eR=BR;ds=ro[(tR-BR+2)&127];}
        if(eR>mtR)mtR=eR;
        if(eR>pTR){printf("\033[%d;1H",H);int sc=pTR?eR-pTR:eR+3;while(sc--)fputs("\n",stdout);pTR=eR;}  /* scroll content up: box must paint over FREED rows, never over the reply */
        int top=H-eR-2;if(top<1)top=1;ctop=H-mtR-2;if(ctop<1)ctop=1;
        printf("\033[%d;1H\033[J\033[%d;1H",ctop,top);
        for(int k=0;k<W;k++)fputs("─",stdout);
        if(ind)printf("\n> \033[2m%zuc…\033[0m\n",l);else fputs("\n> ",stdout);
        fwrite(m+ds,1,l-ds,stdout);if(pend)fputs("\n",stdout);fputs("\n",stdout);
        for(int k=0;k<W;k++)fputs("─",stdout);
        printf("\n\033[2m%.*s\033[0m",W>12?W-12:1,st);
        {struct timespec p1;clock_gettime(CLOCK_MONOTONIC,&p1);
         printf(" \033[2m%.3fms\033[0m",(double)(p1.tv_sec-p0.tv_sec)*1e3+(double)(p1.tv_nsec-p0.tv_nsec)/1e6);}
        printf("\033[%d;%dH",top+eR,cc);fflush(stdout);
        unsigned char c;rd:if(read(0,&c,1)!=1){if(g_halt){g_halt=0;if(l)continue;q=2;}break;}
        if(c==27){char s[8];int av=0;usleep(2000);ioctl(0,FIONREAD,&av);if(!av){l=0;break;}  /* lone ESC = cancel */
            (void)!read(0,s,1);if(s[0]!='['&&s[0]!='O')continue;
            size_t si=0;while(si<7){if(read(0,s+1+si,1)!=1)break;char e=s[1+si];si++;if((e>='A'&&e<='Z')||(e>='a'&&e<='z')||e=='~')break;}
            if(si>=4&&!memcmp(s+1,"200~",4))paste=1;else if(si>=4&&!memcmp(s+1,"201~",4))paste=0;continue;}
        if(menu&&c=='/'&&!l&&!paste){int r=m_slash(m,cap);
            if(r==1){l=strlen(m);break;}
            if(r==2){l=strlen(m);continue;}
            M_ST(st,sfn)continue;}
        if(c=='\r'||c=='\n'){if(!paste)break;MFIT;m[l++]='\n';}  /* pasted \n = literal; falls through to the drain check */
        else if(c==127||c==8){while(l&&(m[l-1]&0xC0)==0x80)l--;if(l)l--;continue;}
        else if(c==21){l=0;continue;}
        else if(c==3){l=0;break;}
        else if(c==4&&!l){q=1;break;}
        else if(c>=32||c=='\t'||(c&0x80)){MFIT;m[l++]=(char)c;}
        if(paste){int av=0;ioctl(0,FIONREAD,&av);if(av>0)goto rd;}  /* drain the paste burst before repainting: O(n), not a repaint per byte */
    }
    m[l]=0;*out=m;
    #undef MFIT
    printf("\033[%d;1H\033[J",ctop);  /* wipe box; caller prints from here */
    fputs("\033[?2004l",stdout);fflush(stdout);tcsetattr(0,TCSANOW,&o);sigaction(SIGWINCH,&osa,0);
    return q?(size_t)-q:l;
}
static int cmd_m(int c,char**v){
    if(c>2&&!strcmp(v[2],"cmd")){load_cfg();if(c>3){char val[B]="";ajoin(val,B,c,v,3);cfset("m_cmd",strcmp(val,"clear")?val:"");}printf("m_cmd=%s\n",cfget("m_cmd"));return 0;}
    if(c>2&&(!strcmp(v[2],"model")||!strcmp(v[2],"agent")||!strcmp(v[2],"effort"))){char val[256]="";if(c>3)ajoin(val,256,c,v,3);load_cfg();char k[32];snprintf(k,32,"m_%s",v[2]);cfset(k,val);return 0;}
    if(c>2&&!strcmp(v[2],"use")){if(c>4){load_cfg();cfset("m_agent",v[3]);cfset("m_model",v[4]);if(c>5)cfset("m_effort",v[5]);}else puts("a m use <agent> <model> [effort]");return 0;}
    perf_disarm();init_db();load_cfg();
    char fn[128],sf[P];int ai=2;CWD(wd);
    {time_t t=time(NULL);strftime(fn,128,"%y%m%d-%H%M%S",localtime(&t));}  /* fresh by default; no implicit main */
    if(c>2){ai=3;if(strcmp(v[2],"new"))snprintf(fn,128,"%s",v[2]);}
    {char ad[P];snprintf(ad,P,"%s/m/agents",SROOT);mkdirp(ad);}
    snprintf(sf,P,"%s/m/agents/%s.txt",SROOT,fn);
    if(c>ai){char pr[B]="";ajoin(pr,B,c,v,ai);m_ap(sf,"user",pr);signal(SIGINT,m_sint);m_run(sf,wd);return 0;}
    if(!getenv("TMUX")){char b2[B],sn[64];ajoin(b2,B,c,v,0);snprintf(sn,64,"m-%s",fn);tm_new(sn,wd,b2);tm_go(sn);return 0;}
    signal(SIGINT,m_sint);
    for(;;){
        {load_cfg();char mc[B];m_cmdstr(mc,B);printf("\033[2J\033[H\033[1;35m⏺ model = %s\033[0m\n\n",mc);}  /* clear + the cmd each turn pipes into */
        {char*tb=readf(sf,NULL);if(tb){size_t l=strlen(tb);fputs(l>4000?tb+l-4000:tb,stdout);free(tb);}}
        for(;;){
            g_halt=0;
            char*m;size_t l=m_input(&m,fn,1);
            if(l==(size_t)-1)return 0;
            if(l==(size_t)-2)break;
            if(!l)continue;
            if(l>2048){size_t eo=l-2000;while(eo<l&&(m[eo]&0xC0)==0x80)eo++;  /* huge paste: echo count+tail, transcript file keeps it whole */
                printf("\033[36m> %zuc…%s\033[0m\n",l,m+eo);}
            else printf("\033[36m> %s\033[0m\n",m);
            if(m[0]=='/'){m[strcspn(m,"\n")]=0;
                if(!strcmp(m,"/q"))return 0;
                if(!strcmp(m,"/resume")&&!m_resume(m,128))continue;
                if(!strncmp(m,"/use ",5)||!strncmp(m,"/cmd",4)){char sc[B];snprintf(sc,B,"a m %s",m+1);(void)!system(sc);continue;}
                if(!strcmp(m,"/new")){time_t t=time(NULL);strftime(fn,128,"%y%m%d-%H%M%S",localtime(&t));}
                else snprintf(fn,128,"%s",m+1);
                snprintf(sf,P,"%s/m/agents/%s.txt",SROOT,fn);break;}
            m_ap(sf,"user",m);
            m_run(sf,wd);
        }
    }
}
