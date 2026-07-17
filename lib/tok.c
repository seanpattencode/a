/* tok — bytes/4 per entry. Bare tty = drill-down TUI (mem/tui.md); piped + arg output frozen (agents parse it). */
typedef struct{const char*n;int nl,dir;long s;}TKC;
static int tkcmp(const void*A,const void*b){const TKC*x=A,*y=b;
    if(x->s!=y->s)return y->s>x->s?1:-1;
    int m=memcmp(y->n,x->n,(size_t)(x->nl<y->nl?x->nl:y->nl));return m?m:y->nl-x->nl;}
static double tkms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (double)t.tv_sec*1e3+(double)t.tv_nsec/1e6;}
static int tkagg(char**pth,long*psz,int np,const char*pf,TKC*ch,long*tot){int nc=0;size_t pl=strlen(pf);*tot=0;
    for(int i=0;i<np;i++){if(strncmp(pth[i],pf,pl))continue;
        const char*s=pth[i]+pl,*sl=strchr(s,'/');int L=sl?(int)(sl-s):(int)strlen(s),j;
        *tot+=psz[i];
        for(j=0;j<nc;j++)if(ch[j].nl==L&&!strncmp(ch[j].n,s,(size_t)L))break;
        if(j==nc){if(nc>=1024)continue;ch[j]=(TKC){s,L,sl?1:0,0};nc++;}
        ch[j].s+=psz[i];}
    qsort(ch,(size_t)nc,sizeof*ch,tkcmp);return nc;}
static int cmd_tok(int c,char**v){perf_disarm();
    if(c>2){long total=0;
        for(int i=2;i<c;i++){
            char cm[P*2],buf[64]="";
            snprintf(cm,sizeof(cm),"find '%s' -type f ! -path '*/.git/*' ! -path '*/__pycache__/*' ! -name '*.pyc' -exec wc -c {} + 2>/dev/null|awk 'END{print $1+0}'",v[i]);
            pcmd(cm,buf,64);long t=atol(buf)/4;total+=t;
            printf("%10ld  %s\n",t,v[i]);}
        if(c>3)printf("%10ld  total\n",total);
        return 0;}
    double t0=tkms();
    FILE*f=popen("t=$(git rev-parse --show-toplevel 2>/dev/null);if [ -n \"$t\" ];then echo \"1 $t\";cd \"$t\"&&git ls-files -z|xargs -0 -r wc -c 2>/dev/null;else echo \"0 $PWD\";find . -type f ! -path '*/.git/*' ! -path '*/__pycache__/*' ! -name '*.pyc' -exec wc -c {} + 2>/dev/null;fi","r");
    char*buf=NULL;size_t bl=0,bc=0,n;char tmp[8192];
    while(f&&(n=fread(tmp,1,8192,f))>0){if(bl+n+1>bc){bc=(bl+n+65536)*2;buf=realloc(buf,bc);}memcpy(buf+bl,tmp,n);bl+=n;}
    if(f)pclose(f);if(!buf)return 1;buf[bl]=0;
    char*he=strchr(buf,'\n');if(!he)return 1;*he=0;
    int rp=buf[0]=='1';char*top=buf+2;
    static char*pth[32768];static long psz[32768];int np=0;
    for(char*p=he+1;p<buf+bl&&np<32768;){
        char*e=memchr(p,'\n',(size_t)(buf+bl-p));if(!e)break;*e=0;
        while(*p==' ')p++;long s=strtol(p,&p,10);while(*p==' ')p++;
        if(*p&&strcmp(p,"total")){if(p[0]=='.'&&p[1]=='/')p+=2;pth[np]=p;psz[np]=s;np++;}
        p=e+1;}
    static TKC ch[1024];long tot;
    if(!isatty(0)||!isatty(1)){ /* frozen piped format: header, entries ASC by tok (ties lex ASC like sort -n), total */
        if(isatty(2))fputs("(not a tty: static list; `a tok` in a terminal = drill-down TUI)\n",stderr);
        printf("%s (%s)\n",top,rp?"tracked":"all files");
        int nc=tkagg(pth,psz,np,"",ch,&tot);
        for(int i=nc-1;i>=0;i--)printf("%10ld  %.*s\n",ch[i].s/4,ch[i].nl,ch[i].n);
        printf("%10ld  total\n",tot/4);free(buf);return 0;}
    if(chdir(top)){free(buf);return 1;}   /* less opens paths relative to repo top */
    double sc=tkms()-t0;
    struct termios o,r;tcgetattr(0,&o);r=o;r.c_lflag&=~(tcflag_t)(ICANON|ECHO);tcsetattr(0,TCSANOW,&r);
    char pf[P]="";int cur=0,off=0;
    for(;;){
        double ta=tkms();
        int nc=tkagg(pth,psz,np,pf,ch,&tot);
        struct winsize w;int rows=24;if(!ioctl(1,TIOCGWINSZ,&w)&&w.ws_row)rows=w.ws_row;
        int H=rows-3;if(H<3)H=3;
        if(cur>=nc)cur=nc?nc-1:0;if(cur<off)off=cur;if(cur>=off+H)off=cur-H+1;
        int shown=nc-off<H?nc-off:H;
        printf("\033[H\033[J");
        for(int i=0;i<H-shown;i++)putchar('\n');            /* tui.md: content bottom-aligned, menu at bottom */
        for(int i=off;i<off+shown;i++)
            printf("%s%c%9ld  %.*s%s\033[0m\n",i==cur?"\033[7m":"",i-off<9?(char)('1'+i-off):' ',ch[i].s/4,ch[i].nl,ch[i].n,ch[i].dir?"/":"");
        printf("\033[90m/%s · %ld tok · %d items%s\n",pf,tot/4,nc,nc>shown?" (j/k scrolls)":"");
        printf("1-9/o/enter/→ open · j/k/↑↓ · u/← up · q quit · scan %.1fms · \033[37m%.4fms\033[0m",sc,tkms()-ta);
        fflush(stdout);
        char k;if(read(0,&k,1)!=1)break;
        if(k==27){char sq[2]={0,0};struct termios r2=r;r2.c_cc[VMIN]=0;r2.c_cc[VTIME]=1;tcsetattr(0,TCSANOW,&r2);
            ssize_t sn=read(0,sq,2);tcsetattr(0,TCSANOW,&r);
            if(sn<2||(sq[0]!='['&&sq[0]!='O'))break;   /* bare ESC = quit */
            k=sq[1]=='A'?'k':sq[1]=='B'?'j':sq[1]=='C'?'o':sq[1]=='D'?'u':0;}
        if(k=='q'||k==3)break;
        size_t pl=strlen(pf);
        if(k=='j'){if(cur<nc-1)cur++;}
        else if(k=='k'){if(cur>0)cur--;}
        else if(k=='u'){if(pl){pf[pl-1]=0;char*s2=strrchr(pf,'/');if(s2)s2[1]=0;else*pf=0;cur=off=0;}}
        else{int pick=k>='1'&&k<='9'?off+k-'1':(k=='\n'||k=='o')&&nc?cur:-1;
            if(pick>=0&&pick<nc){TKC*C=&ch[pick];
                if(C->dir){snprintf(pf+pl,P-pl,"%.*s/",C->nl,C->n);cur=off=0;}
                else{char fp[P];snprintf(fp,P,"%s%.*s",pf,C->nl,C->n);
                    tcsetattr(0,TCSANOW,&o);printf("\033[H\033[J");fflush(stdout);
                    pid_t pd=fork();if(!pd){execlp("less","less","--",fp,(char*)0);_exit(1);}
                    if(pd>0)waitpid(pd,0,0);tcsetattr(0,TCSANOW,&r);}}}}
    tcsetattr(0,TCSANOW,&o);putchar('\n');free(buf);return 0;}
