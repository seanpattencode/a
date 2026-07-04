/* m — platonic chat agent: the FILE is the agent (adata/git/m/agents/<name>.txt, synced), model = ANY shell cmd stdin→stdout.
 * a m [name]              interactive chat — Claude Code shape: model acts via CMD:, grounded, saved. C, no js.
 * a m <name> <task...>    one-shot: the mass-spawn unit (qsim sbatch / a ssh dev 'a m w1 "task"')
 * a m use <ag> <md> [ef]  fleet keys (read by a c/a j)   a m model|agent|effort <v>
 * a m cmd <raw cmd>|clear chat model override, e.g. ollama: "jq -Rs '{model:\"gemma4:12b-it-qat\",prompt:.,stream:false,think:true}'|curl -s -d @- localhost:11434/api/generate|jq -r .response" */
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
        snprintf(x,sizeof x,"{ echo 'Shell agent, cwd %s. To act, END reply with CMD:<shell cmd> — output is fed back. Else plain text = final answer.';cat '%s';}|%s",wd,sf,mc);
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
/* '/' on empty box → m_pick live-filter menu (type-to-complete, a i style). Model picks set m_cmd ONLY —
 * fleet keys m_agent/m_model stay untouched (a c/a j spawns unaffected). Returns 1=submit m, 2=keep editing m, 0=handled. */
static int m_slash(char *m,size_t sz){
    static const char *ops[]={"model\tpick model from list","new\tfresh agent","main\topen main agent","cmd\ttype raw model cmd","q\tquit"};
    char sel[96];
    int r=m_pick("cmd",ops,5,sel,sizeof sel);
    if(r<=0)return 0;
    sel[strcspn(sel,"\t")]=0;
    if(!strcmp(sel,"q")||!strcmp(sel,"new")||!strcmp(sel,"main")){snprintf(m,sz,"/%s",sel);return 1;}
    if(!strcmp(sel,"cmd")){snprintf(m,sz,"/cmd ");return 2;}
    static char ib[24][96];const char*it[24];int n=0;
    static const char*cl[]={"fable","opus","sonnet","haiku",0};
    for(int k=0;cl[k]&&n<24;k++){snprintf(ib[n],96,"%s\tclaude",cl[k]);it[n]=ib[n];n++;}
    char ol[2048];pcmd("ollama list 2>/dev/null|awk 'NR>1{print $1}'",ol,sizeof ol);
    for(char*q=ol;*q&&n<24;){char*nl=strchr(q,'\n');if(nl)*nl=0;if(*q){snprintf(ib[n],96,"%s\tollama local",q);it[n]=ib[n];n++;}if(!nl)break;q=nl+1;}
    r=m_pick("model",it,n,sel,sizeof sel);
    if(r<=0)return 0;
    char *tb=strchr(sel,'\t');int oll=tb&&strstr(tb+1,"ollama");if(tb)*tb=0;
    load_cfg();char nc[B];
    if(oll)snprintf(nc,B,"jq -Rs '{model:\"%s\",prompt:.,stream:false,think:true}'|curl -sS -d @- localhost:11434/api/generate|jq -r .response",sel);
    else{const char*ef=cfget("m_effort");snprintf(nc,B,"claude -p --tools '' --model '%s' --effort '%s'",sel,*ef?ef:"max");}
    cfset("m_cmd",nc);return 0;
}
/* box input — the CC/codex core idea in C: box = f(buffer,width), FULL repaint per keystroke (CC=react+yoga,
 * codex=ratatui wrap_ranges; nobody is incremental). Bottom-anchored ABSOLUTE rows (m_pick pattern) → zero drift. */
#define M_ST(st,fn) {load_cfg();char _mc[B];m_cmdstr(_mc,B);snprintf(st,B,"%s · %.60s · /=menu",fn,_mc);}
static size_t m_input(char *m,size_t sz,const char *fn){
    char st[B];M_ST(st,fn)
    struct termios o,r;tcgetattr(0,&o);r=o;r.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);r.c_cc[VMIN]=1;r.c_cc[VTIME]=0;tcsetattr(0,TCSANOW,&r);
    fputs("\033[?2004h",stdout);
    size_t l=0;int paste=0,q=0,ctop=1,pTR=0,mtR=1;
    for(;;){
        struct winsize ws;ioctl(1,TIOCGWINSZ,&ws);int W=ws.ws_col>8?ws.ws_col:80,H=ws.ws_row>6?ws.ws_row:24;
        int tR=1,cc=3;  /* wrap walk (codepoints, terminal's rule) → rows + cursor col */
        for(size_t k=0;k<l;k++){
            if(m[k]=='\n'){tR++;cc=1;continue;}
            if((m[k]&0xC0)==0x80)continue;
            if(cc>W){tR++;cc=1;}
            cc++;}
        int pend=cc>W;if(pend){tR++;cc=1;}
        if(tR>mtR)mtR=tR;
        if(tR>pTR){printf("\033[%d;1H",H);int sc=pTR?tR-pTR:tR+3;while(sc--)fputs("\n",stdout);pTR=tR;}  /* scroll content up: box must paint over FREED rows, never over the reply */
        int top=H-tR-2;if(top<1)top=1;ctop=H-mtR-2;if(ctop<1)ctop=1;
        printf("\033[%d;1H\033[J\033[%d;1H",ctop,top);
        for(int k=0;k<W;k++)fputs("─",stdout);
        fputs("\n> ",stdout);fwrite(m,1,l,stdout);if(pend)fputs("\n",stdout);fputs("\n",stdout);
        for(int k=0;k<W;k++)fputs("─",stdout);
        printf("\n\033[2m%.*s\033[0m",W>1?W-1:1,st);
        printf("\033[%d;%dH",top+tR,cc);fflush(stdout);
        unsigned char c;if(read(0,&c,1)!=1)break;
        if(c==27){char s[8];int av=0;usleep(2000);ioctl(0,FIONREAD,&av);if(!av)continue;  /* lone ESC */
            (void)!read(0,s,1);if(s[0]!='['&&s[0]!='O')continue;
            size_t si=0;while(si<7){if(read(0,s+1+si,1)!=1)break;char e=s[1+si];si++;if((e>='A'&&e<='Z')||(e>='a'&&e<='z')||e=='~')break;}
            if(si>=4&&!memcmp(s+1,"200~",4))paste=1;else if(si>=4&&!memcmp(s+1,"201~",4))paste=0;continue;}
        if(c=='/'&&!l&&!paste){int r=m_slash(m,sz);
            if(r==1){l=strlen(m);break;}
            if(r==2){l=strlen(m);continue;}
            M_ST(st,fn)continue;}
        if(c=='\r'||c=='\n'){if(paste){if(l<sz-1)m[l++]='\n';continue;}break;}
        if(c==127||c==8){while(l&&(m[l-1]&0xC0)==0x80)l--;if(l)l--;continue;}
        if(c==21){l=0;continue;}
        if(c==3){l=0;break;}
        if(c==4&&!l){q=1;break;}
        if(c>=32||c=='\t'||(c&0x80)){if(l<sz-1)m[l++]=(char)c;}
    }
    m[l]=0;
    printf("\033[%d;1H\033[J",ctop);  /* wipe box; caller prints from here */
    fputs("\033[?2004l",stdout);fflush(stdout);tcsetattr(0,TCSANOW,&o);
    return q?(size_t)-1:l;
}
static int cmd_m(int c,char**v){
    if(c>2&&!strcmp(v[2],"cmd")){load_cfg();if(c>3){char val[B]="";ajoin(val,B,c,v,3);cfset("m_cmd",strcmp(val,"clear")?val:"");}printf("m_cmd=%s\n",cfget("m_cmd"));return 0;}
    if(c>2&&(!strcmp(v[2],"model")||!strcmp(v[2],"agent")||!strcmp(v[2],"effort"))){char val[256]="";if(c>3)ajoin(val,256,c,v,3);load_cfg();char k[32];snprintf(k,32,"m_%s",v[2]);cfset(k,val);return 0;}
    if(c>2&&!strcmp(v[2],"use")){if(c>4){load_cfg();cfset("m_agent",v[3]);cfset("m_model",v[4]);if(c>5)cfset("m_effort",v[5]);}else puts("a m use <agent> <model> [effort]");return 0;}
    perf_disarm();init_db();load_cfg();
    char fn[128]="main",sf[P];int ai=2;CWD(wd);
    if(c>2){ai=3;if(!strcmp(v[2],"new")){time_t t=time(NULL);strftime(fn,128,"%y%m%d-%H%M%S",localtime(&t));}else snprintf(fn,128,"%s",v[2]);}
    {char ad[P];snprintf(ad,P,"%s/m/agents",SROOT);mkdirp(ad);}
    snprintf(sf,P,"%s/m/agents/%s.txt",SROOT,fn);
    if(c>ai){char pr[B]="";ajoin(pr,B,c,v,ai);m_ap(sf,"user",pr);signal(SIGINT,m_sint);m_run(sf,wd);return 0;}
    if(!getenv("TMUX")){char b2[B],sn[64];ajoin(b2,B,c,v,0);snprintf(sn,64,"m-%s",fn);tm_new(sn,wd,b2);tm_go(sn);return 0;}
    signal(SIGINT,m_sint);
    for(;;){
        {load_cfg();char mc[B];m_cmdstr(mc,B);printf("\033[1;35m⏺ model = %s\033[0m\n\n",mc);}  /* the exact cmd each turn pipes into */
        {char*tb=readf(sf,NULL);if(tb){size_t l=strlen(tb);fputs(l>4000?tb+l-4000:tb,stdout);free(tb);}}
        for(;;){
            g_halt=0;
            static char m[65536];size_t l=m_input(m,sizeof m,fn);
            if(l==(size_t)-1)return 0;
            if(!l)continue;
            printf("\033[36m> %s\033[0m\n",m);
            if(m[0]=='/'){m[strcspn(m,"\n")]=0;
                if(!strcmp(m,"/q"))return 0;
                if(!strncmp(m,"/use ",5)||!strncmp(m,"/cmd",4)){char sc[B];snprintf(sc,B,"a m %s",m+1);(void)!system(sc);continue;}
                if(!strcmp(m,"/new")){time_t t=time(NULL);strftime(fn,128,"%y%m%d-%H%M%S",localtime(&t));}
                else snprintf(fn,128,"%s",m+1);
                snprintf(sf,P,"%s/m/agents/%s.txt",SROOT,fn);break;}
            m_ap(sf,"user",m);
            m_run(sf,wd);
        }
    }
}
