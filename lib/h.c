/* h / home — navigation menu: one keypress drills into submenus (›) or runs `a <cmd>`. esc/q=back. d=≤4-word description. */
typedef struct hm{char k;const char*l,*d,*c;const struct hm*s;}hm;
static const hm M_ag[]={{'c',"claude","Anthropic coding agent","c",0},{'x',"codex","OpenAI coding agent","co",0},{'g',"gemini","Google coding agent","g",0},{'A',"all agents","run all three","all",0},{'j',"job (prompt)","background agent job","j",0},{0}};
static const hm M_git[]={{'p',"push","commit and push","push",0},{'l',"pull","reset to remote","pull",0},{'d',"diff tokens","tokens vs main","diff",0},{'r',"revert","restore old commit","revert",0},{'P',"pull request","open GitHub PR","pr",0},{0}};
static const hm M_sys[]={{'l',"list projects/cmds","show all entries","ls",0},{'k',"kill sessions","stop all agents","kill",0},{'u',"update","pull and rebuild","update",0},{'g',"config","edit settings","config",0},{'p',"perf","speed limits","perf",0},{'c',"calendar","view schedule","cal",0},{0}};
static const hm M_top[]={{'/',"claude code","type → launch Claude Code",0,0},{'P',"prompt","edit/switch prompt files","prompt",0},{'a',"Agents","start AI agents",0,M_ag},{'g',"Git","sync and push",0,M_git},{'p',"Projects","open by number","ls",0},{'n',"Notes","capture ideas","note",0},{'t',"Tasks","prioritized todo list","task",0},{'h',"Hub / schedule","scheduled operations","hub",0},{'c',"Code dump","source to clipboard","cat",0},{'s',"ssh","connect to device","ssh",0},{'y',"System","status and config",0,M_sys},{0}};
static int cmd_h(int c,char**v){(void)c;(void)v;perf_disarm();
    const hm*stk[16];const char*ttl[16];int sp=0;stk[0]=M_top;ttl[0]="a · home";
    if(!isatty(0)){for(int i=0;M_top[i].l;i++)printf("%c  %-15s%s%s\n",M_top[i].k,M_top[i].l,M_top[i].d,M_top[i].s?" ›":"");return 0;}
    struct termios o,r;tcgetattr(0,&o);r=o;r.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);r.c_cc[VMIN]=1;r.c_cc[VTIME]=0;tcsetattr(0,TCSANOW,&r);
    struct timespec tk=_t0;  /* per-frame timer: cold render on first paint, key→repaint time after each keypress */
    for(;;){const hm*m=stk[sp];char b[B];int l=0;
        double fms;{struct timespec _n;clock_gettime(CLOCK_MONOTONIC,&_n);fms=(_n.tv_sec-tk.tv_sec)*1e3+(_n.tv_nsec-tk.tv_nsec)/1e6;}
        l+=snprintf(b+l,(size_t)(B-l),"\033[2J\033[H\033[1;36m%s\033[0m\n\n",ttl[sp]);
        for(int i=0;m[i].l;i++)l+=snprintf(b+l,(size_t)(B-l),"  \033[1;32m%c\033[0m  %-15s \033[90m%s\033[0m%s\n",m[i].k,m[i].l,m[i].d,m[i].s?"  \033[90m›\033[0m":"");
        l+=snprintf(b+l,(size_t)(B-l),"\n\033[90m  key=select · esc/q=%s · \033[37m%.2fms\033[0m\n",sp?"back":"quit",fms);
        (void)!write(1,b,(size_t)l);
        char ch;if(read(0,&ch,1)!=1)break;
        clock_gettime(CLOCK_MONOTONIC,&tk);  /* key arrived → time the repaint it triggers */
        if(ch==27){int av=0;ioctl(0,FIONREAD,&av);if(!av){usleep(2000);ioctl(0,FIONREAD,&av);}if(av){char d[8];(void)!read(0,d,(size_t)(av<8?av:8));continue;}}
        if(ch==27||ch=='q'||ch==3){if(sp){sp--;continue;}break;}
        if((ch=='/'||ch==' ')&&sp==0){tcsetattr(0,TCSANOW,&o);(void)!write(1,"\033[2J\033[H",7);
            char pr[B];fputs("prompt> ",stdout);fflush(stdout);
            if(fgets(pr,B,stdin)&&pr[0]&&pr[0]!='\n'){pr[strcspn(pr,"\n")]=0;
                (void)!write(1,"\033[2J\033[H",7);execlp("a","a","c",pr,(char*)0);_exit(0);}
            tcsetattr(0,TCSANOW,&r);continue;}
        for(int i=0;m[i].l;i++)if(m[i].k==ch){
            if(m[i].s){if(sp<15){ttl[sp+1]=m[i].l;stk[++sp]=m[i].s;}}
            else{tcsetattr(0,TCSANOW,&o);(void)!write(1,"\033[2J\033[H",7);execlp("a","a",m[i].c,(char*)0);_exit(0);}
            break;}
    }
    tcsetattr(0,TCSANOW,&o);(void)!write(1,"\033[2J\033[H",7);return 0;}
