/* piped + arg output FROZEN: agents parse it (mem/tui.md) */
typedef struct{const char*n;int nl,dir;long s;}TKC;
static char*tkp[32768];static long tks[32768];static int tkn;static TKC tkch[1024];
static int tkcmp(const void*A,const void*b){const TKC*x=A,*y=b;
    if(x->s!=y->s)return y->s>x->s?1:-1;
    int m=memcmp(y->n,x->n,(size_t)(x->nl<y->nl?x->nl:y->nl));return m?m:y->nl-x->nl;}
static double tkms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (double)t.tv_sec*1e3+(double)t.tv_nsec/1e6;}
static int tkagg(const char*pf,long*tot){int nc=0;size_t pl=strlen(pf);*tot=0;
    for(int i=0;i<tkn;i++){if(strncmp(tkp[i],pf,pl))continue;
        const char*s=tkp[i]+pl,*sl=strchr(s,'/');int L=sl?(int)(sl-s):(int)strlen(s),j;
        *tot+=tks[i];
        for(j=0;j<nc;j++)if(tkch[j].nl==L&&!strncmp(tkch[j].n,s,(size_t)L))break;
        if(j==nc){if(nc>=1024)continue;tkch[j]=(TKC){s,L,sl?1:0,0};nc++;}
        tkch[j].s+=tks[i];}
    qsort(tkch,(size_t)nc,sizeof*tkch,tkcmp);return nc;}
#define TKF(d) "find "d" -type f ! -path '*/.git/*' ! -path '*/__pycache__/*' ! -name '*.pyc' -exec wc -c {} + 2>/dev/null"
static int cmd_tok(int c,char**v){perf_disarm();
    if(c>2){long total=0;char cm[P*2],buf[64];
        for(int i=2;i<c;i++){
            snprintf(cm,sizeof(cm),TKF("'%s'")"|awk 'END{print $1+0}'",v[i]);
            pcmd(cm,buf,64);long t=atol(buf)/4;total+=t;
            printf("%10ld  %s\n",t,v[i]);}
        if(c>3)printf("%10ld  total\n",total);
        return 0;}
    double t0=tkms();
    FILE*f=popen("t=$(git rev-parse --show-toplevel 2>/dev/null);if [ -n \"$t\" ];then echo \"1 $t\";cd \"$t\"&&git ls-files -z|xargs -0 -r wc -c 2>/dev/null;else echo \"0 $PWD\";"TKF(".")";fi","r");
    char*buf=NULL;size_t bl=0,bc=0,n;
    while(f){if(bl+8193>bc)buf=realloc(buf,bc=bc*2+65536);if(!(n=fread(buf+bl,1,8192,f)))break;bl+=n;}
    if(f)pclose(f);if(!buf)return 1;buf[bl]=0;
    char*he=strchr(buf,'\n');if(!he){free(buf);return 1;}*he=0;
    char*top=buf+2;
    for(char*p=he+1;p<buf+bl&&tkn<32768;){
        char*e=memchr(p,'\n',(size_t)(buf+bl-p));if(!e)break;*e=0;
        long s=strtol(p,&p,10);while(*p==' ')p++;
        if(*p&&strcmp(p,"total")){if(p[0]=='.'&&p[1]=='/')p+=2;tkp[tkn]=p;tks[tkn]=s;tkn++;}
        p=e+1;}
    long tot;
    if(!isatty(0)||!isatty(1)){ /* frozen piped format: header, entries ASC by tok (ties lex ASC like sort -n), total */
        if(isatty(2))fputs("(not a tty: static list; `a tok` in a terminal = drill-down TUI)\n",stderr);
        printf("%s (%s)\n",top,*buf=='1'?"tracked":"all files");
        int nc=tkagg("",&tot);
        for(int i=nc-1;i>=0;i--)printf("%10ld  %.*s\n",tkch[i].s/4,tkch[i].nl,tkch[i].n);
        printf("%10ld  total\n",tot/4);free(buf);return 0;}
    if(chdir(top)){free(buf);return 1;}   /* less opens paths relative to repo top */
    double ff=0,fk=0;char*q=readf(".tokrule",NULL),*fb=q?strstr(q,"fable="):0;if(fb)sscanf(fb+6,"%lf %lf",&ff,&fk);free(q);   /* fable=<fixed> <k> = the gate's unit; fixed only at root */
    double sc=tkms()-t0;
    raw_enter();
    char pf[P]="";int cur=0,off=0;
    for(;;){
        double ta=tkms();
        int nc=tkagg(pf,&tot);
        struct winsize w;int rows=24;if(!ioctl(1,TIOCGWINSZ,&w)&&w.ws_row)rows=w.ws_row;
        int H=rows-3;if(H<3)H=3;
        if(cur>=nc)cur=nc?nc-1:0;if(cur<off)off=cur;if(cur>=off+H)off=cur-H+1;
        int shown=nc-off<H?nc-off:H;
        printf("\033[H\033[J");
        for(int i=0;i<H-shown;i++)putchar('\n');            /* tui.md: content bottom-aligned, menu at bottom */
        for(int i=off;i<off+shown;i++)
            printf("%s%c%9ld  %.*s%s\033[0m\n",i==cur?"\033[7m":"",i-off<9?(char)('1'+i-off):' ',tkch[i].s/4,tkch[i].nl,tkch[i].n,tkch[i].dir?"/":"");
        char fs[40]="";if(fk>0)snprintf(fs,40," · %ld fable",(long)(fk*(tot/4))+(pf[0]?0:(long)ff));
        printf("\033[90m/%s · %ld tok%s · %d items%s\n",pf,tot/4,fs,nc,nc>shown?" (j/k scrolls)":"");
        printf("1-9/o/enter/→ open · j/k/↑↓ · u/← up · q quit · scan %.1fms · \033[37m%.4fms\033[0m",sc,tkms()-ta);
        fflush(stdout);
        char k;if(read(0,&k,1)!=1)break;
        if(k==27){int av=0;ioctl(0,FIONREAD,&av);if(!av){usleep(2000);ioctl(0,FIONREAD,&av);}char sq[2]={0,0};
            if(av<2||read(0,sq,2)!=2||(sq[0]!='['&&sq[0]!='O'))break;   /* bare ESC = quit */
            k=sq[1]>='A'&&sq[1]<='D'?"kjou"[sq[1]-'A']:0;}
        if(k=='q'||k==3)break;
        size_t pl=strlen(pf);
        if(k=='j'&&cur<nc-1)cur++;
        else if(k=='k'&&cur>0)cur--;
        else if(k=='u'){if(pl){pf[pl-1]=0;char*s2=strrchr(pf,'/');if(s2)s2[1]=0;else*pf=0;cur=off=0;}}
        else{int pick=k>='1'&&k<='9'?off+k-'1':(k=='\n'||k=='o')&&nc?cur:-1;
            if(pick>=0&&pick<nc){TKC*C=&tkch[pick];
                if(C->dir){snprintf(pf+pl,P-pl,"%.*s/",C->nl,C->n);cur=off=0;}
                else{char fp[P];snprintf(fp,P,"%s%.*s",pf,C->nl,C->n);
                    raw_exit();printf("\033[H\033[J");fflush(stdout);
                    pid_t pd=fork();if(!pd){execlp("less","less","--",fp,(char*)0);_exit(1);}
                    if(pd>0)waitpid(pd,0,0);raw_enter();}}}}
    raw_exit();putchar('\n');free(buf);return 0;}
