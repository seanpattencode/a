/* a cal — personal schedule = "YYYY-MM-DD HH:MM text" lines in cal/*.txt (multi-line file ok — a
   trip is one file). No prompt path reads cal/ — stays out of agent context. Bare tty → TUI per
   mem/tui.md (book.c model): j/k+arrows, e archive (line → .archive.txt, instant promote), c compose
   (dl_norm shorthand). Past grey, cursor starts at next upcoming. Exit: receipts re-read from
   .archive.txt + one cal-scoped commit URL (rule 2, ARCH #13). Args/no-tty: add | ai | <name> | dump.
   a cal g [...] → lib/gcal.py = Google Calendar (service account; a cal g setup prints the recipe). */
/* CEv typedef + CFp + prototypes live at note.c top — flow's CAL page needs them before this file */
static int cev_cmp(const void*a,const void*b){return strcmp(((const CEv*)a)->ln,((const CEv*)b)->ln);}
static int cal_load(const char*d,CEv*ev,int max){
    int n=0,nf=0;DIR*dd=opendir(d);struct dirent*e;
    while(dd&&(e=readdir(dd))&&nf<64){const char*x=strrchr(e->d_name,'.');
        if(e->d_name[0]=='.'||!x||strcmp(x,".txt"))continue;
        snprintf(CFp[nf],P,"%s/%s",d,e->d_name);char*c=readf(CFp[nf],NULL);if(!c)continue;
        for(char*p=c;*p&&n<max;){char*nl=strchr(p,'\n');if(nl)*nl=0;
            if(strlen(p)>9&&isdigit((unsigned char)*p)&&p[4]=='-'&&p[7]=='-'){snprintf(ev[n].ln,200,"%s",p);ev[n].fi=nf;n++;}
            p=nl?nl+1:p+strlen(p);}
        free(c);nf++;}
    if(dd)closedir(dd);qsort(ev,(size_t)n,sizeof(CEv),cev_cmp);return n;}
static char*cal_write(const char*d,const char*raw){   /* "[6-14] [15:00] text" — date optional, no date = today. NEVER reject text: a wiped error reads as 'broken'. Returns event file path (flow's receipt) or NULL */
    char dn[32];dl_norm(raw,dn,32);const char*t=raw;
    if(!strncmp(dn,"20",2)){t=raw+strcspn(raw," ");t+=strspn(t," ");
        size_t k=strcspn(t," ");if(memchr(t,':',k)){t+=k;t+=strspn(t," ");}}   /* time token → date, not text */
    else{time_t now=time(NULL);strftime(dn,32,"%Y-%m-%d 23:59",localtime(&now));}
    if(!*t){puts("x empty — a cal add [6-14] [15:00] text");return NULL;}
    struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);static char tf[P];char tb[32],b[256];
    strftime(tb,32,"%Y%m%dT%H%M%S",localtime(&ts.tv_sec));
    snprintf(tf,P,"%s/%s.%09ld_%s.txt",d,tb,ts.tv_nsec,DEV);
    snprintf(b,256,"%s %s\n",dn,t);writef(tf,b);printf("✓ %s %s\n",dn,t);return tf;}
static void cal_disp(const char*ln,char*o,size_t sz){int y,mo,dy,h,mi,off=0;   /* display only — storage stays sortable ISO. 23:59 = default = dateless, hide it */
    if(sscanf(ln,"%d-%d-%d %d:%d %n",&y,&mo,&dy,&h,&mi,&off)>=5&&off){struct tm t={0};t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=dy;mktime(&t);
        int l=(int)strftime(o,sz,"%b %-d",&t);int h12=h%12?h%12:12;
        if(h==23&&mi==59)snprintf(o+l,sz-(size_t)l," %s",ln+off);
        else snprintf(o+l,sz-(size_t)l," %d:%02d%s %s",h12,mi,h>=12?"pm":"am",ln+off);}
    else snprintf(o,sz,"%s",ln);}
static int cal_arch(const char*d,CEv*e){   /* drop the line from its source; append to .archive.txt */
    size_t l;char*c=readf(CFp[e->fi],&l);if(!c)return 0;
    char*nw=malloc(l+1);size_t o=0,el=strlen(e->ln);int rm=0;
    for(char*p=c;*p;){char*nl=strchr(p,'\n');size_t ll=nl?(size_t)(nl-p):strlen(p);
        if(!rm&&ll==el&&!strncmp(p,e->ln,ll))rm=1;
        else{memcpy(nw+o,p,ll);nw[o+ll]='\n';o+=ll+1;}
        p=nl?nl+1:p+ll;}
    nw[o]=0;
    if(rm){if(strspn(nw," \n")==o)unlink(CFp[e->fi]);else writef(CFp[e->fi],nw);
        char af[P];snprintf(af,P,"%s/.archive.txt",d);
        FILE*f=fopen(af,"a");if(f){fprintf(f,"%s\n",e->ln);fclose(f);}}
    free(c);free(nw);return rm;}
static int cmd_cal(int c,char**v){perf_disarm();
    char d[P];snprintf(d,P,"%s/cal",SROOT);mkdirp(d);
    if(c>2&&!strcmp(v[2],"g")){static char pf[P];snprintf(pf,P,"%s/lib/gcal.py",SDIR);
        static char*nv[32];int m=0;nv[m++]="python3";nv[m++]=pf;for(int i=3;i<c&&m<31;i++)nv[m++]=v[i];nv[m]=0;
        execvp("python3",nv);perror("python3");return 1;}
    if(c>2&&!strcmp(v[2],"add")){char raw[B]="";ajoin(raw,B,c,v,3);return!cal_write(d,raw);}
    if(c>2&&!strcmp(v[2],"ai")){char ex[B]="",pr[B]="",pm[B*2],tdy[16];ajoin(pr,B,c,v,3);
        {char cm[P];snprintf(cm,P,"cat '%s'/*.txt 2>/dev/null",d);pcmd(cm,ex,B);}
        time_t now=time(NULL);strftime(tdy,16,"%Y-%m-%d",localtime(&now));
        snprintf(pm,sizeof pm,"Today is %s. Calendar = dated lines in %s\nEvents:\n%s\nUser request: %s\n\nAdd: a cal add [2026-]M-D [HH:MM] text. Remove/change: edit the files.",tdy,d,ex[0]?ex:"(empty)",pr[0]?pr:"Ask what to add or change.");
        execlp("a","a","c",pm,(char*)0);return 1;}
    if(c>2){char f[P];snprintf(f,P,"%s/%s%s",d,v[2],strchr(v[2],'.')?"":".txt");
        int fd=open(f,O_CREAT|O_WRONLY|O_APPEND,0644);if(fd>=0)close(fd);execlp("e","e",f,(char*)0);return 1;}
    static CEv ev[512];int n=cal_load(d,ev,512);
    if(!isatty(0)||!isatty(1)){for(int i=0;i<n;i++)puts(ev[i].ln);return 0;}
    char td[16];{time_t now=time(NULL);strftime(td,16,"%Y-%m-%d",localtime(&now));}
    int cur=n?n-1:0;for(int i=0;i<n;i++)if(strncmp(ev[i].ln,td,10)>=0){cur=i;break;}   /* next upcoming */
    static char arc[64][200];int na=0,nadd=0;char st[224]="";   /* compose receipt survives the redraw in the status row */
    struct termios ot,rt;tcgetattr(0,&ot);rt=ot;rt.c_lflag&=~(tcflag_t)(ICANON|ECHO);rt.c_cc[VMIN]=1;tcsetattr(0,TCSANOW,&rt);
    for(;;){
        if(cur>=n)cur=n?n-1:0;
        struct winsize w={0,0,0,0};ioctl(1,TIOCGWINSZ,&w);int rows=w.ws_row>10?w.ws_row:24,cols=w.ws_col>20?w.ws_col:80;
        printf("\033[H\033[2J");
        int ps=rows-3,p0=ps>0?(cur/ps)*ps:0;
        for(int i=p0;i<p0+ps&&i<n;i++){char ln[200];cal_disp(ev[i].ln,ln,200);
            if((int)strlen(ln)>cols-1)ln[cols-1]=0;
            printf(i==cur?"\033[7m%s\033[0m\n":strncmp(ev[i].ln,td,10)<0?"\033[90m%s\033[0m\n":"%s\n",ln);}
        if(!n)printf("no events — [c]ompose\n");
        {char sb[256];snprintf(sb,256,"%d/%d %s",n?cur+1:0,n,*st?st:(n?bname(CFp[ev[cur].fi]):""));   /* receipt replaces filename — a wrapped status row at the bottom scrolls the list off by one */
        printf("\033[%d;1H\033[90m%.*s\033[0m\n[j/k]move [c]ompose [e]archive [q]uit",rows-1,cols-1,sb);*st=0;}
        fflush(stdout);
        char kc=0;if(read(0,&kc,1)!=1)break;int k=kc;
        if(k==27){struct pollfd pf={0,POLLIN,0};   /* arrows = ESC[A/B → k/j */
            if(poll(&pf,1,10)>0){char s[2]={0,0};
                if(read(0,s,1)==1&&s[0]=='['&&read(0,s+1,1)==1)k=s[1]=='A'?'k':s[1]=='B'?'j':0;else k=0;}}
        if(k=='q'||k==27)break;
        else if(k=='j'&&cur<n-1)cur++;
        else if(k=='k'&&cur>0)cur--;
        else if(k=='e'&&n){if(cal_arch(d,&ev[cur])&&na<64)snprintf(arc[na++],200,"%s",ev[cur].ln);
            n=cal_load(d,ev,512);}   /* instant promote */
        else if(k=='c'){tcsetattr(0,TCSANOW,&ot);char ib[200];*st=0;
            printf("\n+ text (date first optional: 6-14 15:00)> ");fflush(stdout);
            if(fgets(ib,200,stdin)&&ib[0]!='\n'){ib[strcspn(ib,"\n")]=0;
                if(cal_write(d,ib)){nadd++;snprintf(st,224,"\033[32m✓ added\033[0m");}else snprintf(st,224,"\033[31mx empty\033[0m");}
            tcsetattr(0,TCSANOW,&rt);n=cal_load(d,ev,512);}
    }
    tcsetattr(0,TCSANOW,&ot);printf("\033[H\033[2J");
    {char af[P];snprintf(af,P,"%s/.archive.txt",d);char*ac=readf(af,NULL);   /* ground truth, not memory of the write */
    for(int i=0;i<na;i++)printf(ac&&strstr(ac,arc[i])?"✓ archived %s\n":"\033[31m✗ NOT archived: %s\033[0m\n",arc[i]);
    free(ac);}
    if(na||nadd){char cm[B],rp[128]="";   /* flocked like edit_st: a bare exit commit races flow/sync (index.lock); blob URL is deterministic, no hash dance */
        snprintf(cm,B,"flock /tmp/.a_git.lock -c \"git -C '%s' add cal&&git -C '%s' commit -qm 'cal: %d archived, %d added'\" >/dev/null 2>&1 &",SROOT,SROOT,na,nadd);(void)!system(cm);
        snprintf(cm,B,"git -C '%s' remote get-url origin 2>/dev/null|sed 's#.*github.com[:/]##;s#\\.git$##'",SROOT);pcmd(cm,rp,128);rp[strcspn(rp,"\n")]=0;
        if(*rp)printf("  ✓ cal: %d archived, %d added → https://github.com/%s/blob/main/cal/.archive.txt  (syncing)\n",na,nadd,rp);
        else printf("  ✓ cal: %d archived, %d added (local)\n",na,nadd);
        sync_bg();}
    return 0;}
