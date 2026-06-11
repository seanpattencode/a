/* a book — book list TUI per adata/git/mem/tui.md: C-speed keys, gmail-inbox model (page of
   names, inverse cursor, j/k move, spc/b page), menu pinned bottom (thumbs), omnibox / filter.
   e renames to .<name> (hidden everywhere, data stays); exit prints stat-verified receipts.
   Anything with args, or no tty, falls through to lib/book.py. */
static int bk_cmp(const void*a,const void*b){return strcasecmp((const char*)a,(const char*)b);}
static int cmd_book(int argc,char**argv){
    if(argc>2||!isatty(0)||!isatty(1))fallback_py("book",argc,argv);
    perf_disarm();init_db();
    static char nm[4096][128];int n=0;char bd[P];snprintf(bd,P,"%s/books",AROOT);
    {DIR*d=opendir(bd);struct dirent*e;struct stat st;char p[P];
     while(d&&(e=readdir(d))&&n<4096){if(e->d_name[0]=='.'||!strcmp(e->d_name,"book.py"))continue;
        snprintf(p,P,"%s/%s",bd,e->d_name);if(!stat(p,&st)&&S_ISDIR(st.st_mode))snprintf(nm[n++],128,"%s",e->d_name);}
     if(d)closedir(d);}
    if(!n){puts("x no books — a book add <file>");return 1;}
    qsort(nm,(size_t)n,128,bk_cmp);
    char ft[64]="";int cur=0,fm=0,na=0;static char arc[64][128];
    /* raw mode once for the whole loop — per-key TCSAFLUSH (key1) discards type-ahead, eating omnibox chars */
    struct termios ot,rt;tcgetattr(0,&ot);rt=ot;rt.c_lflag&=~(tcflag_t)(ICANON|ECHO);rt.c_cc[VMIN]=1;tcsetattr(0,TCSANOW,&rt);
    for(;;){
        static int ix[4096];int m=0;for(int i=0;i<n;i++)if(!*ft||strcasestr(nm[i],ft))ix[m++]=i;
        if(cur>=m)cur=m?m-1:0;
        struct winsize w={0,0,0,0};ioctl(1,TIOCGWINSZ,&w);int rows=w.ws_row>10?w.ws_row:24,cols=w.ws_col>20?w.ws_col:80;
        printf("\033[H\033[2J");
        int ps=rows-3,p0=ps>0?(cur/ps)*ps:0;   /* gmail inbox: a page of the filtered list, cursor inverse */
        for(int i=p0;i<p0+ps&&i<m;i++){char ln[160];snprintf(ln,160,"%s",nm[ix[i]]);
            if((int)strlen(ln)>cols-1)ln[cols-1]=0;printf(i==cur?"\033[7m%s\033[0m\n":"%s\n",ln);}
        if(!m)printf("no match: %s\n",ft);
        printf("\033[%d;1H\033[90m%d/%d%s%s%s\033[0m\n[j/k]move [spc/b]page [o]read [c]chat [e]archive [/]filter [q]quit",
            rows-1,m?cur+1:0,m,(*ft||fm)?"  filter:":"",ft,fm?"_":"");
        fflush(stdout);
        char kc=0;if(read(0,&kc,1)!=1)break;int k=kc,ar=0;
        if(k==27){struct pollfd pf={0,POLLIN,0};   /* arrows = ESC[A/B → k/j (lone ESC stays quit/exit-filter) */
            if(poll(&pf,1,10)>0){char s[2]={0,0};
                if(read(0,s,1)==1&&s[0]=='['&&read(0,s+1,1)==1)k=s[1]=='A'?(ar=1,'k'):s[1]=='B'?(ar=1,'j'):0;else k=0;}}
        if(fm&&!ar){if(k=='\r'||k=='\n'||k==27)fm=0;
            else if(k==127||k==8){size_t l=strlen(ft);if(l)ft[l-1]=0;}
            else if(k>=32&&k<127&&strlen(ft)<63){size_t l=strlen(ft);ft[l]=(char)k;ft[l+1]=0;cur=0;}
            continue;}
        if(k=='q'||k==27)break;
        else if(k=='j'&&cur<m-1)cur++;
        else if(k=='k'&&cur>0)cur--;
        else if(k==' '&&m){cur+=rows-3;if(cur>=m)cur=m-1;}
        else if(k=='b'&&m){cur-=rows-3;if(cur<0)cur=0;}
        else if(k=='/'){fm=1;ft[0]=0;cur=0;}
        else if((k=='o'||k=='\r'||k=='\n')&&m){tcsetattr(0,TCSANOW,&ot);printf("\033[H\033[2J");char*av[]={"a","book","read",nm[ix[cur]],0};fallback_py("book",4,av);}
        else if(k=='c'&&m){tcsetattr(0,TCSANOW,&ot);printf("\033[H\033[2J");char*av[]={"a","book","chat",nm[ix[cur]],0};fallback_py("book",4,av);}
        else if(k=='e'&&m){char fr[P],to[P];int i=ix[cur];
            snprintf(fr,P,"%s/%s",bd,nm[i]);snprintf(to,P,"%s/.%s",bd,nm[i]);
            if(!rename(fr,to)){snprintf(arc[na<64?na:63],128,"%s",nm[i]);if(na<64)na++;
                memmove(nm[i],nm[i+1],(size_t)(n-1-i)*128);n--;}}   /* instant promote: next book fills the screen */
    }
    tcsetattr(0,TCSANOW,&ot);
    printf("\033[H\033[2J");   /* exit receipts (tui.md rule 2): stat = ground truth, not memory of the rename */
    for(int i=0;i<na;i++){char p[P];struct stat st;snprintf(p,P,"%s/.%s",bd,arc[i]);
        printf(stat(p,&st)?"\033[31m✗ NOT archived: %s\033[0m\n":"✓ archived .%s\n",arc[i]);}
    if(na)printf("  restore: a book archive <substr>\n");
    return 0;}
