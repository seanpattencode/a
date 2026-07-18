/* a book — book list TUI per adata/git/mem/tui.md: C-speed keys, gmail-inbox model (page of
   names, inverse cursor, j/k move, spc/b page), menu pinned bottom (thumbs), omnibox / filter.
   e renames to .<name> (hidden everywhere, data stays); exit prints stat-verified receipts.
   Anything with args, or no tty, falls through to lib/book.py. */
static int bk_cmp(const void*a,const void*b){return strcasecmp((const char*)a,(const char*)b);}
static char bk_nm[4096][128],bk_ad[4096][96],bk_ak[4096][96];   /* name + resolved author display/key */
static int bk_srctag(const char*a){char b[64];int j=0;   /* download-source tag, not an author */
    for(const char*p=a;*p&&j<63;p++)if(isalnum((unsigned char)*p))b[j++]=(char)tolower((unsigned char)*p);b[j]=0;
    return strstr(b,"libgen")||strstr(b,"annasarchive")||strstr(b,"bokxyz")||strstr(b,"zlibrary")||!strcmp(b,"zlib");}
static void bk_norm(const char*a,char*o,int n){char t[12][32];int c=0;   /* alnum tokens lowercased + SORTED */
    for(const char*p=a;*p&&c<12;){while(*p&&!isalnum((unsigned char)*p))p++;if(!*p)break;
        int l=0;while(*p&&isalnum((unsigned char)*p)&&l<31)t[c][l++]=(char)tolower((unsigned char)*p),p++;t[c][l]=0;if(l)c++;}
    for(int i=1;i<c;i++){char x[32];strcpy(x,t[i]);int j=i-1;for(;j>=0&&strcmp(t[j],x)>0;j--)strcpy(t[j+1],t[j]);strcpy(t[j+1],x);}
    int k=0;for(int i=0;i<c&&k<n-2;i++){for(int z=0;t[i][z]&&k<n-2;z++)o[k++]=t[i][z];if(i+1<c)o[k++]=' ';}o[k]=0;}
/* author w/o metadata: tail after last --- (title---author); but if the tail reads like a title (>4 words)
   the name is author---title, so use the head. Same author → same segment → merges (Chernow's mixed forms). */
static const char* bk_auth(const char*nm){const char*f=strstr(nm,"---");if(!f)return "\xc2\xb7 unknown";
    const char*L=0;for(const char*q=nm;(q=strstr(q,"---"));q+=3)L=q;
    const char*t=L+3;while(*t=='-')t++;int w=0;
    for(const char*p=t;*p;){if(*p=='-'){p++;continue;}w++;while(*p&&*p!='-')p++;}
    if(w>4){static char hd[96];int k=(int)(f-nm);if(k>95)k=95;memcpy(hd,nm,(size_t)k);hd[k]=0;return hd;}
    return t;}
static void bk_resolve(char nm[][128],int n){for(int i=0;i<n;i++){const char*a=bk_auth(nm[i]);
    if(!strncmp(a,"\xc2\xb7",2)||!strncmp(a,"unknown",7)||bk_srctag(a)){strcpy(bk_ad[i],"\xc2\xb7 unknown");strcpy(bk_ak[i],"~");}
    else{snprintf(bk_ad[i],96,"%s",a);bk_norm(a,bk_ak[i],96);}}}
static char (*g_ak)[96];   /* set before qsort of an index[] by resolved-author key */
static int g_akcmp(const void*pa,const void*pb){return strcmp(g_ak[*(const int*)pa],g_ak[*(const int*)pb]);}
/* over-long names get middle-… so beginning AND end show (end-truncation would hide the title) */
static void bk_mid(char*s,int w){int l=(int)strlen(s);if(l<=w||w<8)return;int h=(w-3)/2;
    char*e=s+l-(w-3-h);while((*e&0xC0)==(char)0x80)e++;
    memmove(s+h+3,e,strlen(e)+1);memcpy(s+h,"\xe2\x80\xa6",3);}
/* cloud follows local rename (bg moveto) — else sync pull resurrects old name */
static void bk_cloudmv(const char*a,const char*b){if(fork())return;
    int dn=open("/dev/null",O_WRONLY);if(dn>=0){dup2(dn,1);dup2(dn,2);}
    execl("/bin/sh","sh","-c","r=$(rclone listremotes 2>/dev/null|grep a-gdrive|head -1);"
        "[ -n \"$r\" ]&&exec rclone moveto \"${r}adata/books/$0\" \"${r}adata/books/$1\"",a,b,(char*)0);_exit(0);}
static void bk_jget(const char*j,const char*k,char*o,int n){o[0]=0;char pat[24];snprintf(pat,24,"\"%s\":",k);   /* flat one-level state.json only */
    const char*p=strstr(j,pat);if(!p)return;p+=strlen(pat);while(*p==' ')p++;if(*p=='"')p++;   /* json.dumps pads ": " */
    int i=0;while(p[i]&&p[i]!='"'&&p[i]!=','&&p[i]!='}'&&i<n-1){o[i]=p[i];i++;}o[i]=0;}
static void bk_drip(char*o,int n){o[0]=0;char fp[P];snprintf(fp,P,"%s/local/bookdrip/state.json",AROOT);
    size_t l=0;char*j=readf(fp,&l);if(!j)return;
    char st[24],bk[64],pg[12],qd[12],qt[12],pid[16];
    bk_jget(j,"state",st,24);bk_jget(j,"book",bk,64);bk_jget(j,"page",pg,12);
    bk_jget(j,"qdone",qd,12);bk_jget(j,"qtotal",qt,12);bk_jget(j,"pid",pid,16);free(j);
    if(!st[0]||!strcmp(st,"stopped"))return;   /* quiet when intentionally off */
    if((!strcmp(st,"running")||!strcmp(st,"paused"))&&(atoi(pid)<2||kill(atoi(pid),0)))strcpy(st,"DEAD");
    char q[28]="";if(qt[0]&&strcmp(qt,"0"))snprintf(q,28," %s/%s",qd,qt);
    char lb[40];if(!strcmp(st,"running"))strcpy(lb,"transcribing");else snprintf(lb,40,"transcribe %s",st);   /* say the thing, not the codename */
    bk_mid(bk,30);snprintf(o,(size_t)n,"  \xc2\xb7 %s%s %s p%s",lb,q,bk,pg);}
static int cmd_book(int argc,char**argv){
    if(argc>2||!isatty(0)||!isatty(1))fallback_py("book",argc,argv);
    perf_disarm();init_db();signal(SIGCHLD,SIG_IGN);   /* bk_cloudmv children: no zombies while the pager runs */
    char (*nm)[128]=bk_nm;int n=0;char bd[P];snprintf(bd,P,"%s/books",AROOT);
    {DIR*d=opendir(bd);struct dirent*e;struct stat st;char p[P];
     while(d&&(e=readdir(d))&&n<4096){if(e->d_name[0]=='.'||!strcmp(e->d_name,"book.py"))continue;
        snprintf(p,P,"%s/%s",bd,e->d_name);if(!stat(p,&st)&&S_ISDIR(st.st_mode))snprintf(nm[n++],128,"%s",e->d_name);}
     if(d)closedir(d);}
    if(!n){puts("x no books — a book add <file>");return 1;}
    qsort(nm,(size_t)n,128,bk_cmp);
    bk_resolve(nm,n);g_ak=bk_ak;   /* clean authors once; per-keypress reads stay O(1) */
    char ft[64]="";int cur=0,fm=0,na=0,sm=0;static char arc[64][128];   /* sm: name|author sort */
    /* raw mode once (per-key reset would eat omnibox type-ahead) */
    struct termios ot,rt;tcgetattr(0,&ot);rt=ot;rt.c_lflag&=~(tcflag_t)(ICANON|ECHO);rt.c_cc[VMIN]=1;tcsetattr(0,TCSANOW,&rt);
    for(;;){
        static int ix[4096];int m=0;for(int i=0;i<n;i++)if(!*ft||strcasestr(nm[i],ft))ix[m++]=i;
        if(sm)qsort(ix,(size_t)m,sizeof(int),g_akcmp);
        /* author mode: header row before each author run (Lb: book idx, -1=header→Lh) */
        static int Lb[5000];static const char*Lh[5000];int nl=0;char pk[96]="";
        for(int i=0;i<m&&nl<4996;i++){
            if(sm&&strcmp(bk_ak[ix[i]],pk)){Lh[nl]=bk_ad[ix[i]];Lb[nl++]=-1;strcpy(pk,bk_ak[ix[i]]);}
            Lh[nl]=0;Lb[nl++]=ix[i];}
        if(cur>=nl)cur=nl?nl-1:0;
        while(cur<nl&&Lb[cur]<0)cur++;if(cur>=nl){cur=nl-1;while(cur>0&&Lb[cur]<0)cur--;}
        struct winsize w={0,0,0,0};ioctl(1,TIOCGWINSZ,&w);int rows=w.ws_row>10?w.ws_row:24,cols=w.ws_col>20?w.ws_col:80;
        printf("\033[H\033[2J");
        static const char mn[]="[j/k]move [spc/b]page [s]ort [o]read [c]chat [t]ranscribe [e]archive [/]filter [q]quit";
        int mr=((int)sizeof(mn)-1+cols-1)/cols;   /* wrapped menu rows (overflow scrolls row1 off) */
        int ps=rows-1-mr,p0=ps>0?(cur/ps)*ps:0;
        for(int i=p0;i<p0+ps&&i<nl;i++){
            if(Lb[i]<0){char h[128];const char*a=Lh[i];int j=0;for(;a[j]&&j<cols-3&&j<120;j++)h[j]=a[j]=='-'?' ':a[j];h[j]=0;
                printf("\033[1;36m%s\033[0m\n",h);continue;}   /* author header */
            char ln[280];snprintf(ln,280,"%s",nm[Lb[i]]);bk_mid(ln,cols-1);
            printf(i==cur?"\033[7m%s\033[0m\n":"%s\n",ln);}
        if(!m)printf("no match: %s\n",ft);
        int cb=0;for(int i=0;i<=cur&&i<nl;i++)if(Lb[i]>=0)cb++;
        /* filter mode swaps menu→typing help: action keys (c=chat…) would type into the search, not fire, so don't show them */
        char ds[96],sr[224];bk_drip(ds,96);   /* live drip segment in the status row; whole row clipped to cols */
        snprintf(sr,224,"%d/%d  %s%s%.*s%s",m?cb:0,m,sm?"by-author  ":"by-name  ",*ft?"filter: ":"",cols>30?cols-30:14,ft,ds);
        printf("\033[%d;1H\033[90m%.*s\033[0m\n%s",rows-mr,cols-1,sr,
            fm?"\033[7;33m SEARCHING \033[0m \033[1m[Enter]\033[0m=pick  [Esc]=cancel":mn);
        fflush(stdout);
        char kc=0;if(read(0,&kc,1)!=1)break;int k=kc,ar=0;
        if(k==27){struct pollfd pf={0,POLLIN,0};   /* arrows = ESC[A/B → k/j (lone ESC stays quit/exit-filter) */
            if(poll(&pf,1,10)>0){char s[2]={0,0};
                if(read(0,s,1)!=1)k=0;
                else if(s[0]=='[')k=read(0,s+1,1)==1&&s[1]=='A'?(ar=1,'k'):s[1]=='B'?(ar=1,'j'):0;
                else k=s[0];}}   /* ESC then a fast real key (or Alt+key): keep the key, drop the ESC */
        if(fm&&!ar){if(k=='\r'||k=='\n'||k==27)fm=0;
            else if(k==127||k==8){size_t l=strlen(ft);if(l)ft[l-1]=0;}
            else if(k>=32&&k<127&&strlen(ft)<63){size_t l=strlen(ft);ft[l]=(char)k;ft[l+1]=0;cur=0;}
            continue;}
        if(k=='q'||k==27)break;
        else if(k=='j'){int t=cur+1;while(t<nl&&Lb[t]<0)t++;if(t<nl)cur=t;}   /* next book, skipping header rows */
        else if(k=='k'){int t=cur-1;while(t>=0&&Lb[t]<0)t--;if(t>=0)cur=t;}
        else if(k==' '&&m){cur+=ps;if(cur>=nl)cur=nl-1;}
        else if(k=='b'&&m){cur-=ps;if(cur<0)cur=0;}
        else if(k=='s'){sm^=1;cur=0;}   /* toggle name <-> author grouping */
        else if(k=='t'||k=='d'){printf("\033[H\033[2J\033[0m");fflush(stdout);   /* transcribe pane: status + log tail + unit; r=resume-now x=stop */
            char dc[B];snprintf(dc,B,"a book drip 2>/dev/null;echo;echo '--- log ---';tail -n 16 '%s/local/bookdrip/drip.log' 2>/dev/null;printf 'unit: ';systemctl --user is-active bookdrip.service 2>/dev/null||true",AROOT);
            (void)!system(dc);
            printf("\n\033[7m [r]esume/probe now  [x]stop  [any]back \033[0m");fflush(stdout);
            char t=0;(void)!read(0,&t,1);
            if(t=='r'){char rt[12]="20",fp2[P],rc[B];snprintf(fp2,P,"%s/local/bookdrip/state.json",AROOT);   /* keep the configured rate across resume */
                size_t l2=0;char*j2=readf(fp2,&l2);if(j2){bk_jget(j2,"rate",rt,12);free(j2);if(!rt[0])strcpy(rt,"20");}
                snprintf(rc,B,"a book drip auto %s >/dev/null 2>&1;systemctl --user restart bookdrip.service 2>/dev/null;sleep 1",rt);
                (void)!system(rc);}
            else if(t=='x')(void)!system("a book drip stop >/dev/null 2>&1");}
        else if(k=='/'){fm=1;ft[0]=0;cur=0;}
        else if((k=='o'||k=='\r'||k=='\n')&&m){tcsetattr(0,TCSANOW,&ot);printf("\033[H\033[2J");char*av[]={"a","book","read",nm[Lb[cur]],0};fallback_py("book",4,av);}
        else if(k=='c'&&m){tcsetattr(0,TCSANOW,&ot);printf("\033[H\033[2J");char*av[]={"a","book","chat",nm[Lb[cur]],0};fallback_py("book",4,av);}
        else if(k=='e'&&m){char fr[P],to[P];int i=Lb[cur];
            snprintf(fr,P,"%s/%s",bd,nm[i]);snprintf(to,P,"%s/.%s",bd,nm[i]);
            if(!rename(fr,to)){char cd[140];snprintf(cd,140,".%s",nm[i]);bk_cloudmv(nm[i],cd);
                snprintf(arc[na<64?na:63],128,"%s",nm[i]);if(na<64)na++;
                memmove(nm[i],nm[i+1],(size_t)(n-1-i)*128);   /* author arrays stay in lockstep */
                memmove(bk_ad[i],bk_ad[i+1],(size_t)(n-1-i)*96);memmove(bk_ak[i],bk_ak[i+1],(size_t)(n-1-i)*96);n--;}}
    }
    tcsetattr(0,TCSANOW,&ot);
    printf("\033[H\033[2J");   /* exit receipts (tui.md rule 2): stat = ground truth, not memory of the rename */
    for(int i=0;i<na;i++){char p[P];struct stat st;snprintf(p,P,"%s/.%s",bd,arc[i]);
        printf(stat(p,&st)?"\033[31m✗ NOT archived: %s\033[0m\n":"✓ archived .%s\n",arc[i]);}
    if(na)printf("  restore: a book archive <substr>\n");
    return 0;}
