static char fa_last[P];   /* last archived src path — flow batches these into one exit commit */
static void do_archive(const char *p) {
    const char *s=strrchr(p,'/'); char a[P],d[P]; snprintf(a,P,"%.*s/.archive",(int)(s-p),p); mkdirp(a);
    snprintf(d,P,"%s%s",a,s); rename(p,d); snprintf(fa_last,P,"%s",p);
}
static char* note_save(const char *d, const char *t) {
    struct timespec tp; clock_gettime(CLOCK_REALTIME,&tp); time_t now=tp.tv_sec;
    static char fn[P]; char ts[32],buf[B]; strftime(ts,32,"%Y%m%dT%H%M%S",localtime(&now));
    snprintf(fn,P,"%s/%08x_%s.%09ld.txt",d,(unsigned)(tp.tv_nsec^(unsigned)now),ts,tp.tv_nsec);
    snprintf(buf,B,"Text: %s\nStatus: pending\nDevice: %s\nCreated: %s\n",t,DEV,ts); writef(fn,buf);
    return fn;
}
static char rdir[P],ltd[P]="";
static const char*g_by=NULL;   /* set by `a task add --by <model>`: optional LLM provenance. priority + deadlines are assumed to be sorted by the human, not the model. */
static void dl_norm(const char*,char*,size_t);
static char* task_add(const char*,const char*,int);
static void ts_human(const char*,char*,size_t);
static char nfs[256][P];static int nfn;  /* notes captured this session; git-synced + url'd on exit, never mid-loop */
static void rapid_note(const char*t){char*f=note_save(rdir,t);if(nfn<256)snprintf(nfs[nfn++],P,"%s",f);puts("  ✓ saved locally");}
typedef struct{char p[P];char t[2048];}GN;
static GN*gn;static int gn_cap;
static int gncmp(const void*a,const void*b){return strcmp(strrchr(((const GN*)a)->p,'_'),strrchr(((const GN*)b)->p,'_'));}
static int load_notes(const char *dir, const char *f) {
    DIR *d=opendir(dir); if(!d) return 0; struct dirent *e; int n=0;
    while((e=readdir(d))) { if(e->d_name[0]=='.'||!strstr(e->d_name,".txt")) continue;
        char fp[P]; snprintf(fp,P,"%s/%s",dir,e->d_name); kvs_t kv=kvfile(fp);
        const char *t=kvget(&kv,"Text"),*s=kvget(&kv,"Status");
        if(t&&(!s||!strcmp(s,"pending"))&&(!f||strcasestr(t,f))){
            if(n>=gn_cap){gn_cap=gn_cap?gn_cap*2:2048;gn=realloc(gn,(size_t)gn_cap*sizeof*gn);}
            snprintf(gn[n].p,P,"%s",fp);snprintf(gn[n].t,2048,"%s",t);n++;}
    } closedir(d); return n;
}
static int cmd_note(int argc, char **argv) {
    AB;perf_disarm();
    char dir[P]; snprintf(dir,P,"%s/notes",SROOT); mkdirp(dir);
    if(argc>2&&!strcmp(argv[2],"l")){int n=load_notes(dir,NULL);
        if(!n){puts("(none)");return 0;}
        qsort(gn,(size_t)n,sizeof(GN),gncmp);   /* gncmp = oldest→newest; newest live at the end */
        int k=argc>3?(!strcmp(argv[3],"all")?n:atoi(argv[3])):4;if(k<=0||k>n)k=n;  /* default top 4 newest; a n l N|all = more */
        for(int i=0;i<k;i++){int ix=n-1-i;const char*b=strrchr(gn[ix].p,'/'),*u=b?strrchr(b,'_'):0;char hu[48]="?";
            if(u){char ts[16];snprintf(ts,16,"%.15s",u+1);ts_human(ts,hu,48);}
            printf("%3d. \033[90m%-13s\033[0m %s\n",i+1,hu,gn[ix].t);}
        if(k<n)printf("    \033[90m… %d more · a n l all\033[0m\n",n-k);return 0;}
    if(argc<=2){int n=0;DIR*d=opendir(dir);if(d){struct dirent*e;while((e=readdir(d)))if(e->d_name[0]!='.'&&strstr(e->d_name,".txt"))n++;closedir(d);}
        printf("%d notes  last synced %s\n  l=list  r=review  m=manage  /=search   a n <text>=add\n",n,sync_age());
        if(!isatty(0))return 0;
        printf("> ");fflush(stdout);
        struct termios ot,rt;tcgetattr(0,&ot);rt=ot;rt.c_lflag&=~(tcflag_t)(ICANON|ECHO);rt.c_cc[VMIN]=1;tcsetattr(0,TCSAFLUSH,&rt);
        char ch;int rv=read(0,&ch,1);tcsetattr(0,TCSAFLUSH,&ot);putchar('\n');
        if(rv!=1||ch==27||ch==3)return 0;
        if(ch=='/'||ch=='s'){char q[128];printf("search: ");fflush(stdout);if(fgets(q,128,stdin)){q[strcspn(q,"\n")]=0;char a2[130];snprintf(a2,130,"?%s",q);execvp("a",(char*[]){"a","n",a2,NULL});}return 0;}
        {static const char km[]="lrm";static const char*kv[]={"l","r","m"};for(int i=0;km[i];i++)if(ch==km[i])execvp("a",(char*[]){"a","n",(char*)kv[i],NULL});}
        return 0;}
    if(argc>2&&(argv[2][0]=='?'||!strcmp(argv[2],"r")||!strcmp(argv[2],"review"))){
        const char *f=argv[2][0]=='?'?argv[2]+1:NULL;int n=load_notes(dir,f);
        if(!n){puts("(none)");return 0;} if(!isatty(STDIN_FILENO)){for(int i=0;i<n&&i<10;i++)puts(gn[i].t);return 0;}
        int i=0,show=1; raw_enter();
        while(i<n){if(show)printf("\n[%d/%d] %s\n",i+1,n,gn[i].t);show=1;
            printf("  [d]el [t]ask [a]dd [/]find [j/k/q]  ");fflush(stdout);
            int k=raw_key();putchar('\n');
            if(k=='t'){char td[P];snprintf(td,P,"%s/tasks",SROOT);task_add(td,gn[i].t,50000);do_archive(gn[i].p);puts("✓ → task");n=load_notes(dir,f);if(i>=n)i=n-1;if(i<0)break;}
            else if(k=='d'){do_archive(gn[i].p);puts("✓");n=load_notes(dir,f);if(i>=n)i=n-1;if(i<0)break;}
            else if(k=='a'){char buf[B];if(raw_line("  Text: ",buf,B)){note_save(dir,buf);n=load_notes(dir,NULL);printf("✓ [%d]\n",n);}show=0;}
            else if(k=='/'||k=='s'){char q[128];if(raw_line("  Search: ",q,128)){n=load_notes(dir,q);i=0;printf("%d results\n",n);}else show=0;}
            else if(k=='k'){if(i>0)i--;else show=0;}
            else if(k=='q'||k==3||k==27)break;else if(k=='j')i++;else show=0;}
        raw_exit();if(i>=n)puts("Done");return 0;}
    if(argc>2&&!strcmp(argv[2],"m")){
        execvp("a",(char*[]){"a","c","Run 'a n l' to see all notes. Read a.c for context. Help me archive stale/done/duplicate notes in bulk. To archive: mkdir -p <dir>/.archive && mv <file> <dir>/.archive/. Large batches, only archive what I approve.",NULL});return 1;}
    if(argc>3&&!strcmp(argv[2],"-u")){char t[B]="";ajoin(t,B,argc,argv,3);  /* -u: save + print commit URL via gh API (apk) */
        note_url(note_save(dir,t),"note",NULL);return 0;}
    {char t[B]="";ajoin(t,B,argc,argv,2);snprintf(rdir,P,"%s",dir);rapid_note(t);
        rapid("n> ",rapid_note);
        for(int i=0;i<nfn;i++){printf("[%d/%d] ",i+1,nfn);fflush(stdout);note_url(nfs[i],"note",NULL);}  /* show each note's url at end */
        return 0;}
}
static int is5d(const char*s){return strspn(s,"0123456789")==5&&!s[5];}
typedef struct{char d[P],t[1024],p[8];}Tk;
static Tk T[1024];
static int tcmp(const void*a,const void*b){int c=strcmp(((const Tk*)a)->p,((const Tk*)b)->p);return c?c:strcmp(strrchr(((const Tk*)a)->d,'_'),strrchr(((const Tk*)b)->d,'_'));}
static int tcmp_new(const void*a,const void*b){const char*x=strrchr(((const Tk*)a)->d,'_'),*y=strrchr(((const Tk*)b)->d,'_');return strcmp(y?y:"",x?x:"");}  /* created, newest first */
static int load_tasks(const char*dir){
    DIR*d=opendir(dir);if(!d)return 0;struct dirent*e;int n=0;
    while((e=readdir(d))&&n<1024){
        if(e->d_name[0]=='.'||!strcmp(e->d_name,"README.md"))continue;
        const char*nm=e->d_name;snprintf(T[n].d,P,"%s/%s",dir,nm);
        int hp=nm[5]=='-'&&strspn(nm,"0123456789")==5;
        if(hp){memcpy(T[n].p,nm,5);T[n].p[5]=0;}else strcpy(T[n].p,"50000");
        {char r[B]="",tp[P];FILE*f=NULL;snprintf(tp,P,"%s/task",T[n].d);
            DIR*td=opendir(tp);struct dirent*te;
            if(td){while((te=readdir(td)))if(te->d_type==DT_REG&&strstr(te->d_name,".txt")){snprintf(tp,P,"%s/task/%s",T[n].d,te->d_name);f=fopen(tp,"r");break;}closedir(td);}
            if(!f)f=fopen(T[n].d,"r");if(f){(void)!fgets(r,B,f);fclose(f);r[strcspn(r,"\n")]=0;if(!strncmp(r,"Text: ",6))memmove(r,r+6,strlen(r+6)+1);}
            if(!*r){const char*s=hp?nm+6:nm;const char*u=strchr(s,'_');int tl=u?(int)(u-s):(int)strlen(s);
                if(tl>255)tl=255;for(int i=0;i<tl;i++)r[i]=s[i]=='-'?' ':s[i];r[tl]=0;}
            snprintf(T[n].t,1024,"%s",r);}
        n++;
    }closedir(d);qsort(T,(size_t)n,sizeof(Tk),tcmp);return n;
}
static char* task_add(const char*dir,const char*t,int pri){
    char sl[64];snprintf(sl,64,"%.32s",t);for(char*p=sl;*p;p++)*p=*p==' '||*p=='/'?'-':*p>='A'&&*p<='Z'?*p+32:*p;
    struct timespec tp;clock_gettime(CLOCK_REALTIME,&tp);
    static char fn[P];char ts[32],buf[B];strftime(ts,32,"%Y%m%dT%H%M%S",localtime(&tp.tv_sec));
    snprintf(ltd,P,"%s/%05d-%s_%s",dir,pri,sl,ts);mkdir(ltd,0755);
    char sd[P];snprintf(sd,P,"%s/task",ltd);mkdir(sd,0755);
    snprintf(fn,P,"%s/task/%s.%09ld_%s.txt",ltd,ts,tp.tv_nsec,DEV);
    int bo=snprintf(buf,B,"Text: %s\nDevice: %s\nCreated: %s\n",t,DEV,ts);
    if(g_by)snprintf(buf+bo,(size_t)(B-bo),"By: %s\n",g_by);   /* LLM provenance line, only when --by given */
    writef(fn,buf);
    return fn;
}
#define THINT "  \033[90mp <pri>  d MM-DD  enter=done\033[0m\n"
static char tfs[256][P];static int tfn;  /* tasks captured this session; git-synced + url'd on exit */
static void rapid_task(const char*t){
    if((*t=='p'||*t=='d')&&t[1]==' '){char*bn=strrchr(ltd,'/');
        if(*t=='p'){int pv=atoi(t+2);pv=pv<0?0:pv>99999?99999:pv;char np[8];snprintf(np,8,"%05d",pv);
            char nw[P];snprintf(nw,P,"%.*s/%s%s",(int)(bn-ltd),ltd,np,bn+6);
            rename(ltd,nw);if(tfn)memcpy(tfs[tfn-1],nw,strlen(nw));snprintf(ltd,P,"%s",nw);printf("✓ P%s\n",np);}  /* 5-digit pri: nw,ltd same len */
        else{char dn[32],df[P];dl_norm(t+2,dn,32);snprintf(df,P,"%s/deadline.txt",ltd);writef(df,dn);printf("✓ %s\n",dn);}
        return;}
    char*f=task_add(rdir,t,50000);if(tfn<256)snprintf(tfs[tfn++],P,"%s",f);printf("✓ saved locally P50000 %s\n" THINT,t);}
static int task_add_p(const char*dir,int argc,char**argv,int si){
    int pri=50000;if(si<argc&&is5d(argv[si])){pri=atoi(argv[si]);si++;if(si>=argc){puts("a task [PPPPP] <text>");return 1;}}
    char t[B]="";ajoin(t,B,argc,argv,si);char*f=task_add(dir,t,pri);if(tfn<256)snprintf(tfs[tfn++],P,"%s",f);printf("✓ saved locally P%05d %s\n" THINT,pri,t);
    snprintf(rdir,P,"%s",dir);rapid("t> ",rapid_task);
    for(int i=0;i<tfn;i++){printf("[%d/%d] ",i+1,tfn);fflush(stdout);note_url(tfs[i],"task",NULL);}  /* show each task's url at end */
    return 0;}
static void task_printbody(const char*path){
    size_t l;char*r=readf(path,&l);if(!r)return;if(!strncmp(r,"Text: ",6))r+=6;
    for(char*p=r;;){char*nl=strchr(p,'\n');if(nl)*nl=0;
        if(*p&&strncmp(p,"Device: ",8)&&strncmp(p,"Created: ",9))printf("    %s\n",p);
        if(!nl)break;p=nl+1;}
}
typedef struct{char n[64];int c;}tcnt_t;
static int tcnt_cmp(const void*a,const void*b){return strcmp(((const tcnt_t*)a)->n,((const tcnt_t*)b)->n);}
static int task_counts(const char*dir,char*out,int sz){
    DIR*d=opendir(dir);if(!d){*out=0;return 0;}struct dirent*e;
    tcnt_t s[32];int nd=0;
    while((e=readdir(d))&&nd<32){if(e->d_name[0]=='.'||e->d_type!=DT_DIR)continue;
        char sd[P];snprintf(sd,P,"%s/%s",dir,e->d_name);DIR*ds=opendir(sd);if(!ds)continue;
        struct dirent*f;int c=0;while((f=readdir(ds)))if(f->d_type==DT_REG&&strstr(f->d_name,".txt"))c++;
        closedir(ds);if(c){snprintf(s[nd].n,64,"%s",e->d_name);s[nd].c=c;nd++;}
    }closedir(d);if(!nd){*out=0;return 0;}
    qsort(s,(size_t)nd,sizeof(tcnt_t),tcnt_cmp);
    int p=snprintf(out,(size_t)sz," [");for(int i=0;i<nd;i++)p+=snprintf(out+p,(size_t)(sz-p),"%s%d %s",i?", ":"",s[i].c,s[i].n);
    snprintf(out+p,(size_t)(sz-p),"]");return nd;
}
static void dl_norm(const char*in,char*out,size_t sz){
    int y,m,d,h=23,mi=59;time_t now=time(NULL);struct tm*t=localtime(&now);
    if(sscanf(in,"%d-%d-%d %d:%d",&y,&m,&d,&h,&mi)>=3){snprintf(out,sz,"%04d-%02d-%02d %02d:%02d",y,m,d,h,mi);}
    else if(sscanf(in,"%d-%d %d:%d",&m,&d,&h,&mi)>=2){snprintf(out,sz,"%04d-%02d-%02d %02d:%02d",t->tm_year+1900,m,d,h,mi);}
    else snprintf(out,sz,"%s",in);}
static int task_dl(const char*td){char df[P];snprintf(df,P,"%s/deadline.txt",td);
    size_t l;char*c=readf(df,&l);if(!c)return-1;struct tm d={0};int h=23,mi=59;
    if(sscanf(c,"%d-%d-%d %d:%d",&d.tm_year,&d.tm_mon,&d.tm_mday,&h,&mi)<3){free(c);return-1;}
    d.tm_year-=1900;d.tm_mon--;d.tm_hour=h;d.tm_min=mi;free(c);return(int)((mktime(&d)-time(NULL))/86400);}
static void ts_date(const char*ts,char*out,size_t sz){struct tm t={0};int y,mo,d,h=0,mi=0,s=0;  /* YYYYmmddTHHMMSS -> "Jun 8 3:26pm" */
    if(ts&&sscanf(ts,"%4d%2d%2dT%2d%2d%2d",&y,&mo,&d,&h,&mi,&s)>=3){t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=d;mktime(&t);
        int l=(int)strftime(out,sz,"%b %-d",&t);int h12=h%12;if(!h12)h12=12;snprintf(out+l,sz-(size_t)l," %d:%02d%s",h12,mi,h>=12?"pm":"am");}
    else snprintf(out,sz,"-");}
static void task_front(const char*dir,char*out,int sz){  /* front column: ⚑deadline (day+time) if set, else created day+time; padded to 16 display cols */
    char b[40]="-";int isdl=0;struct tm t={0};int y,mo,d,h=0,mi=0;
    char df[P];snprintf(df,P,"%s/deadline.txt",dir);size_t l;char*c=readf(df,&l);
    if(c&&sscanf(c,"%d-%d-%d %d:%d",&y,&mo,&d,&h,&mi)>=3){t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=d;mktime(&t);
        int ln=(int)strftime(b,40,"%b %-d",&t);int h12=h%12;if(!h12)h12=12;snprintf(b+ln,40-(size_t)ln," %d:%02d%s",h12,mi,h>=12?"pm":"am");isdl=1;}
    else{const char*u=strrchr(dir,'_');char ts[16];if(u)snprintf(ts,16,"%.15s",u+1);ts_date(u?ts:NULL,b,40);}
    if(c)free(c);
    int cols=(isdl?1:0)+(int)strlen(b),pad=16-cols;if(pad<1)pad=1;
    snprintf(out,(size_t)sz,"%s%s%*s",isdl?"⚑":"",b,pad,"");}
typedef struct{char n[256];char ts[32];}Ent;
static int entcmp(const void*a,const void*b){return strcmp(((const Ent*)a)->ts,((const Ent*)b)->ts);}
static void ts_human(const char*ts,char*out,size_t sz){
    struct tm t={0};int y,mo,d,h,mi,s;
    if(!ts||sscanf(ts,"%4d%2d%2dT%2d%2d%2d",&y,&mo,&d,&h,&mi,&s)<5){snprintf(out,sz,"(original)");return;}
    t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=mi;mktime(&t);
    int l=(int)strftime(out,sz,"%b %-d",&t);int h12=h%12;if(!h12)h12=12;
    snprintf(out+l,sz-(size_t)l," %d:%02d%s",h12,mi,h>=12?"pm":"am");
}
typedef struct{char sid[128];char tmx[128];char ts[32];char wd[P];int st;}Sess;
static int sess_ts_cmp(const void*a,const void*b){return strcmp(((const Sess*)a)->ts,((const Sess*)b)->ts);}
static int load_sessions(const char*td,Sess*ss,int max){
    DIR*d=opendir(td);if(!d)return 0;struct dirent*e;int ns=0;
    while((e=readdir(d))&&ns<max){
        if(strncmp(e->d_name,"session_",8)||!strstr(e->d_name,".txt"))continue;
        char fp[P];snprintf(fp,P,"%s/%s",td,e->d_name);FILE*f=fopen(fp,"r");if(!f)continue;
        memset(&ss[ns],0,sizeof(Sess));ss[ns].st=2;char ln[P];
        while(fgets(ln,P,f)){ln[strcspn(ln,"\n")]=0;
            if(!strncmp(ln,"SessionID: ",11))snprintf(ss[ns].sid,128,"%s",ln+11);
            else if(!strncmp(ln,"TmuxSession: ",13))snprintf(ss[ns].tmx,128,"%s",ln+13);
            else if(!strncmp(ln,"Started: ",9))snprintf(ss[ns].ts,32,"%s",ln+9);
            else if(!strncmp(ln,"Cwd: ",5))snprintf(ss[ns].wd,P,"%s",ln+5);}
        fclose(f);ns++;}
    closedir(d);qsort(ss,(size_t)ns,sizeof(Sess),sess_ts_cmp);return ns;
}
static void task_todir(char*p){struct stat st;if(!stat(p,&st)&&S_ISDIR(st.st_mode))return;
    char tmp[P];snprintf(tmp,P,"%s.tmp",p);rename(p,tmp);mkdir(p,0755);
    char dst[P];snprintf(dst,P,"%s/task.txt",p);rename(tmp,dst);}
static void task_show(int i,int n){
    Sess ss[32];int ns=load_sessions(T[i].d,ss,32);
    char sl[32];if(ns)snprintf(sl,32,"\033[33m%d sess\033[0m",ns);else snprintf(sl,32,"\033[90mnot run\033[0m");
    int dd=task_dl(T[i].d);char dv[32]="";if(dd>=0)snprintf(dv,32,"  %s%dd\033[0m",dd<=1?"\033[31m":dd<=7?"\033[33m":"\033[90m",dd);
    printf("\n\033[1m━━━ %d/%d [P%s] %.50s\033[0m  %s%s\n",i+1,n,T[i].p,T[i].t,sl,dv);
    struct stat st;if(stat(T[i].d,&st)||!S_ISDIR(st.st_mode)){task_printbody(T[i].d);return;}
    /* collect all non-session .txt files with timestamps for chrono sort */
    Ent all[256];int na=0;
    DIR*d=opendir(T[i].d);if(!d)return;struct dirent*e;
    while((e=readdir(d))&&na<256){if(e->d_name[0]=='.'||!strncmp(e->d_name,"session_",8)||!strncmp(e->d_name,"prompt_",7))continue;
        char fp[P];snprintf(fp,P,"%s/%s",T[i].d,e->d_name);
        if(e->d_type==DT_REG&&strstr(e->d_name,".txt")){
            snprintf(all[na].n,256,"%s",fp);
            const char*u=strchr(e->d_name,'_');
            if(u&&strlen(u+1)>=15)snprintf(all[na].ts,32,"%.15s",u+1);
            else snprintf(all[na].ts,32,"0");
            na++;
        }else if(e->d_type==DT_DIR&&strncmp(e->d_name,"prompt_",7)){
            DIR*s=opendir(fp);if(!s)continue;struct dirent*f;
            while((f=readdir(s))&&na<256){if(f->d_type!=DT_REG||!strstr(f->d_name,".txt"))continue;
                snprintf(all[na].n,256,"%s/%s",fp,f->d_name);
                const char*v=f->d_name;if(strlen(v)>=15&&v[8]=='T')snprintf(all[na].ts,32,"%.15s",v);
                else snprintf(all[na].ts,32,"0");
                na++;}
            closedir(s);}}
    closedir(d);qsort(all,(size_t)na,sizeof(Ent),entcmp);
    for(int j=0;j<na;j++){char ht[48];
        if(all[j].ts[0]!='0')ts_human(all[j].ts,ht,48);else snprintf(ht,48,"(original)");
        printf("\n  \033[90m%s\033[0m  text\n",ht);task_printbody(all[j].n);}
    /* show prompt candidates (dirs or legacy .txt files) */
    int pc=2;DIR*pd=opendir(T[i].d);struct dirent*pe;
    while(pd&&(pe=readdir(pd))){
        if(strncmp(pe->d_name,"prompt_",7))continue;
        char pp[P];snprintf(pp,P,"%s/%s",T[i].d,pe->d_name);
        struct stat ps;if(stat(pp,&ps))continue;
        char ht[48];struct tm*mt=localtime(&ps.st_mtime);
        int h=mt->tm_hour%12;if(!h)h=12;
        strftime(ht,48,"%b %-d",mt);char tmp[32];snprintf(tmp,32," %d:%02d%s",h,mt->tm_min,mt->tm_hour>=12?"pm":"am");
        strncat(ht,tmp,48-strlen(ht)-1);
        if(S_ISDIR(ps.st_mode)){
            char fv[P]="",mv[64]="",cfp[P];
            snprintf(cfp,P,"%s/folder.txt",pp);{FILE*f=fopen(cfp,"r");if(f){(void)!fgets(fv,P,f);fclose(f);fv[strcspn(fv,"\n")]=0;}}
            snprintf(cfp,P,"%s/model.txt",pp);{FILE*f=fopen(cfp,"r");if(f){(void)!fgets(mv,64,f);fclose(f);mv[strcspn(mv,"\n")]=0;}}
            snprintf(cfp,P,"%s/prompt.txt",pp);
            printf("\n  \033[90m%s\033[0m  \033[35mprompt #%d\033[0m  \033[90m%s  %s\033[0m\n",ht,pc,mv,fv);
            task_printbody(cfp);
        }else if(S_ISREG(ps.st_mode)){
            printf("\n  \033[90m%s\033[0m  \033[35mprompt #%d\033[0m\n",ht,pc);
            task_printbody(pp);
        }else continue;
        pc++;}
    if(pd)closedir(pd);
    /* show all sessions */
    for(int j=0;j<ns;j++){char ht[48];ts_human(ss[j].ts,ht,48);
        if(ss[j].wd[0])printf("  \033[33msess\033[0m  %s  cd %s && claude -r %s\n",ht,ss[j].wd,ss[j].sid);
        else printf("  \033[33msess\033[0m  %s  claude -r %s\n",ht,ss[j].sid);}
}
static void task_repri(int x,int pv){
    if(pv<0)pv=0;if(pv>99999)pv=99999;char np[8];snprintf(np,8,"%05d",pv);
    char*bn=strrchr(T[x].d,'/');if(!bn)return;bn++;char nw[P];
    if(strlen(bn)>5&&bn[5]=='-'&&isdigit(bn[0]))snprintf(nw,P,"%s-%s",np,bn+6);else snprintf(nw,P,"%s-%s",np,bn);
    char dst[P];snprintf(dst,P,"%.*s/%s",(int)(bn-1-T[x].d),T[x].d,nw);
    rename(T[x].d,dst);printf("✓ P%s %.40s\n",np,T[x].t);
}
static int cmd_task(int argc,char**argv){
    perf_disarm();
    char dir[P];snprintf(dir,P,"%s/tasks",SROOT);mkdirp(dir);const char*sub=argc>2?argv[2]:NULL;
    if(!sub||!strcmp(sub,"top")||!strcmp(sub,"new")){int n=load_tasks(dir);
        int bynew=sub&&!strcmp(sub,"new");if(bynew)qsort(T,(size_t)n,sizeof(Tk),tcmp_new);
        int k=argc>3&&isdigit((unsigned char)argv[3][0])?atoi(argv[3]):4;if(k>n)k=n;  /* default top 4 */
        printf("%d tasks  \033[90msort: %s\033[0m\n",n,bynew?"created":"priority");
        for(int i=0;i<k;i++){char fr[24];task_front(T[i].d,fr,24);printf("  %2d %s \033[90mP%s\033[0m %s\n",i+1,fr,T[i].p,T[i].t);}
        printf("\n  l=list  r=review  v=vision  n=new  d=due  k=rank  m=manage\n  f=flag  s=sync  h=help  1-9=open task\n  add: a task add [--by <model>] <text>   (--by = LLM proposal; human sets priority/deadline)\n  last synced %s\n",sync_age());
        if(!isatty(0))return 0;
        printf("\n> ");fflush(stdout);
        struct termios ot,rt;tcgetattr(0,&ot);rt=ot;rt.c_lflag&=~(tcflag_t)(ICANON|ECHO);rt.c_cc[VMIN]=1;tcsetattr(0,TCSAFLUSH,&rt);
        char ch;int rv=read(0,&ch,1);tcsetattr(0,TCSAFLUSH,&ot);
        if(rv!=1||ch=='\x1b'||ch==3){putchar('\n');return 0;}putchar('\n');
        if(ch>='1'&&ch<='9'){char d[2]={ch,0};execvp("a",(char*[]){"a","t",d,NULL});}
        {static const char km[]="lrvndkmfsh";static const char*kv[]={"l","r","v","new","due","rank","m","f","sync","h"};
        for(int i=0;km[i];i++)if(ch==km[i]){execvp("a",(char*[]){"a","t",(char*)kv[i],NULL});}}
        printf("x %c\n",ch);return 1;}
    if(!strcmp(sub,"v")||!strcmp(sub,"vision")){
        char vf[P];snprintf(vf,P,"%s/vision.txt",SROOT);
        size_t vl;char*vc=readf(vf,&vl);
        static const char*vk[]={"Focus","Saves","Daily"};
        kvs_t vkv=vc?kvparse(vc):(kvs_t){.n=0};if(vc)free(vc);
        printf("\033[1m━━━ Vision\033[0m");
        if(vkv.n){struct stat vs;if(!stat(vf,&vs)){char vd[16];strftime(vd,16,"%b %d",localtime(&vs.st_mtime));printf(" \033[90m(%s)\033[0m",vd);}}
        putchar('\n');
        for(int j=0;j<3;j++){const char*v=kvget(&vkv,vk[j]);printf("  \033[1m%-6s\033[0m %s\n",vk[j],v?v:"\033[90m-\033[0m");}
        printf("\n");for(int j=0;j<3;j++){const char*v=kvget(&vkv,vk[j]);
            printf("  %s [%s]: ",vk[j],v?v:"");fflush(stdout);
            char lb[512];if(fgets(lb,512,stdin)&&lb[0]!='\n'){lb[strcspn(lb,"\n")]=0;
                int found=0;for(int k=0;k<vkv.n;k++)if(!strcmp(vkv.i[k].k,vk[j])){snprintf(vkv.i[k].v,512,"%s",lb);found=1;break;}
                if(!found&&vkv.n<16){snprintf(vkv.i[vkv.n].k,32,"%s",vk[j]);snprintf(vkv.i[vkv.n].v,512,"%s",lb);vkv.n++;}}}
        char wb[B]="";int wl=0;for(int j=0;j<vkv.n;j++)wl+=snprintf(wb+wl,(size_t)(B-wl),"%s: %s\n",vkv.i[j].k,vkv.i[j].v);
        writef(vf,wb);puts("✓");return 0;}
    int grn=0;
    if(!strcmp(sub,"help")||!strcmp(sub,"-h")||!strcmp(sub,"h")){
        puts("  a task          vision + scream + #1\n  a task v        edit vision\n  a task l        list\n  a task r        review (navigate)\n  a task rank     reprioritize walk-through\n  a task add [--by <model>] <t>  add (prefix 5-digit pri; --by marks an LLM proposal, optional)\n  a task d #      archive\n  a task pri # N  set priority\n  a task m        AI manage\n  a task deadline # MM-DD\n  a task due      by deadline\n  a task sync     sync");
        return 0;}
    if(!strcmp(sub,"rank")){int n=load_tasks(dir);if(!n){puts("No tasks");return 0;}
        int changed=0;
        for(int i=0;i<n;i++){
            printf("  %d/%d [P%s] %.60s  pri (enter=keep): ",i+1,n,T[i].p,T[i].t);fflush(stdout);
            char buf[16];if(!fgets(buf,16,stdin)||buf[0]=='q')break;
            if(buf[0]!='\n'){int pv=atoi(buf);if(pv>0){task_repri(i,pv);changed=1;}}
        }if(changed){n=load_tasks(dir);puts("\nNew order:");
            for(int i=0;i<n;i++)printf("  %d. P%s %.50s\n",i+1,T[i].p,T[i].t);}
        return 0;}
    if(!strcmp(sub,"m")){execvp("a",(char*[]){"a","c","Manage tasks: 'a t l' list, 'a t d <dir>...' archive, 'a t pri # N' repri. Batch, approve only.",NULL});return 1;}
    if(*sub=='l'){int n=load_tasks(dir);if(!n){puts("No tasks");return 0;}
        for(int i=0;i<n;i++)task_show(i,n);return 0;}
    if(0){review:;} /* due r jumps here with T[] pre-loaded */
    if(grn||isdigit(*sub)||!strcmp(sub,"rev")||!strcmp(sub,"review")||!strcmp(sub,"r")||!strcmp(sub,"t")){
        int n=grn?grn:load_tasks(dir);if(!n){puts("No tasks");return 0;}
        {int i=isdigit(*sub)?atoi(sub)-1:argc>3?atoi(argv[3])-1:0;if(i<0||i>=n)i=0;int show=1;
        raw_enter();
        while(i<n){if(show)task_show(i,n);show=1;
            printf("\n  [e]del [a]dd [c]prompt [r]un [g]o [d]line [p]ri [/]find [j/k/q]  ");fflush(stdout);
            int k=raw_key();putchar('\n');
            if(k=='e'){do_archive(T[i].d);printf("✓ Archived: %.40s\n",T[i].t);
                n=load_tasks(dir);if(i>=n)i=n-1;if(i<0)break;}
            else if(k=='a'){
                task_todir(T[i].d);
                char sd[P];snprintf(sd,P,"%s/task",T[i].d);
                char buf[B];if(raw_line("  Text: ",buf,B)){
                    mkdir(sd,0755);
                    struct timespec tp;clock_gettime(CLOCK_REALTIME,&tp);
                    char ts[32],fn[P];strftime(ts,32,"%Y%m%dT%H%M%S",localtime(&tp.tv_sec));
                    snprintf(fn,P,"%s/%s.%09ld_%s.txt",sd,ts,tp.tv_nsec,DEV);
                    char fb[B];snprintf(fb,B,"Text: %s\nDevice: %s\nCreated: %s\n",buf,DEV,ts);writef(fn,fb);
                    printf("✓ Added\n");}
                task_show(i,n);show=0;}
            else if(k=='c'){docreate:
                task_todir(T[i].d);
                char nm[64];if(!raw_line("  Name: ",nm,64)){show=0;continue;}
                char pt[B];if(!raw_line("  Prompt text: ",pt,B)){show=0;continue;}
                char fd[P];if(!raw_line("  Folder [cwd]: ",fd,P))(void)!getcwd(fd,P);
                char md[64];if(!raw_line("  Model [opus]: ",md,64))snprintf(md,64,"opus");
                char pd[P];snprintf(pd,P,"%s/prompt_%s",T[i].d,nm);mkdir(pd,0755);
                char pf[P];
                snprintf(pf,P,"%s/prompt.txt",pd);writef(pf,pt);
                snprintf(pf,P,"%s/folder.txt",pd);writef(pf,fd);
                snprintf(pf,P,"%s/model.txt",pd);writef(pf,md);
                printf("✓ Added prompt: %s\n",nm);
                task_show(i,n);show=0;}
            else if(k=='r'){
                char pb[8];if(!raw_line("  Prompt # or [n]ew: ",pb,8)){show=0;continue;}
                if(*pb=='n'||*pb=='c')goto docreate;
                int ci=atoi(pb);if(ci<1){show=0;continue;}
                /* collect task text */
                char body[B]="";int bl=0;
                struct stat ss;if(!stat(T[i].d,&ss)&&S_ISDIR(ss.st_mode)){
                    DIR*dd=opendir(T[i].d);struct dirent*ee;
                    while(dd&&(ee=readdir(dd))){if(ee->d_name[0]=='.')continue;
                        char fp[P];snprintf(fp,P,"%s/%s",T[i].d,ee->d_name);
                        if(ee->d_type==DT_REG&&strstr(ee->d_name,".txt")&&!strstr(ee->d_name,"session")&&!strstr(ee->d_name,"prompt_")){
                            size_t fl;char*fc=readf(fp,&fl);if(fc){bl+=snprintf(body+bl,(size_t)(B-bl),"%s\n",fc);free(fc);}}
                        else if(ee->d_type==DT_DIR&&strncmp(ee->d_name,"prompt_",7)){DIR*sd=opendir(fp);struct dirent*ff;
                            while(sd&&(ff=readdir(sd))){if(ff->d_type!=DT_REG||!strstr(ff->d_name,".txt"))continue;
                                char sfp[P];snprintf(sfp,P,"%s/%s",fp,ff->d_name);
                                size_t fl;char*fc=readf(sfp,&fl);if(fc){bl+=snprintf(body+bl,(size_t)(B-bl),"%s\n",fc);free(fc);}}
                            if(sd)closedir(sd);}}
                    if(dd)closedir(dd);
                }else{snprintf(body,B,"%s",T[i].t);}
                /* build prompt from candidate */
                char prompt[B],pmodel[64]="opus",pfolder[P]="";
                (void)!getcwd(pfolder,P);
                if(ci==1){snprintf(prompt,B,"%s",body);}
                else{int cp=2;DIR*pd=opendir(T[i].d);struct dirent*pe;int found=0;
                    while(pd&&(pe=readdir(pd))){
                        if(strncmp(pe->d_name,"prompt_",7))continue;
                        char pp[P];snprintf(pp,P,"%s/%s",T[i].d,pe->d_name);
                        struct stat ps;if(stat(pp,&ps))continue;
                        if(S_ISDIR(ps.st_mode)){
                            if(cp==ci){char cfp[P];
                                snprintf(cfp,P,"%s/prompt.txt",pp);
                                size_t cl;char*cc=readf(cfp,&cl);
                                if(cc){snprintf(prompt,B,"%s",cc);free(cc);found=1;}
                                snprintf(cfp,P,"%s/model.txt",pp);cc=readf(cfp,&cl);
                                if(cc){snprintf(pmodel,64,"%s",cc);pmodel[strcspn(pmodel,"\n")]=0;free(cc);}
                                snprintf(cfp,P,"%s/folder.txt",pp);cc=readf(cfp,&cl);
                                if(cc){snprintf(pfolder,P,"%s",cc);pfolder[strcspn(pfolder,"\n")]=0;free(cc);}
                                break;}
                        }else if(S_ISREG(ps.st_mode)){
                            if(cp==ci){size_t cl;char*cc=readf(pp,&cl);
                                if(cc){snprintf(prompt,B,"%s",cc);free(cc);found=1;}
                                break;}
                        }else continue;
                        cp++;}
                    if(pd)closedir(pd);
                    if(!found){printf("  x Invalid prompt #\n");show=0;continue;}}
                /* hand off to a job */
                raw_exit();
                {char pf[P];snprintf(pf,P,"%s/a_task_%d.txt",TMP,(int)getpid());writef(pf,prompt);
                char cmd[B];snprintf(cmd,B,"a job '%s' --prompt-file '%s' --no-worktree --model %s --bg",pfolder,pf,pmodel);
                (void)!system(cmd);}raw_enter();show=0;}
            else if(k=='g'){
                /* go: attach most recent live, or resume most recent dead */
                Sess ss[32];int ns=load_sessions(T[i].d,ss,32);
                if(!ns){printf("  Not run yet. Press [r] to run with claude.\n");show=0;}
                else{int pick=-1;
                    for(int j=ns-1;j>=0;j--)if(ss[j].st==1){pick=j;break;}
                    if(pick<0)pick=ns-1;
                    raw_exit();
                    if(ss[pick].st==1){char cmd[P];snprintf(cmd,P,"tmux new-session -t '%s'",ss[pick].tmx);
                        (void)!system(cmd);}
                    else{char cmd[P];snprintf(cmd,P,"claude -r %s",ss[pick].sid);
                        printf("  Resuming claude session...\n");(void)!system(cmd);}
                    raw_enter();show=0;}}
            else if(k=='p'){char buf[16];if(raw_line("  Priority (1-99999): ",buf,16)){task_repri(i,atoi(buf));n=load_tasks(dir);}}
            else if(k=='d'){
                task_todir(T[i].d);
                char db[32];if(raw_line("  Deadline (MM-DD [HH:MM]): ",db,32)){
                    char dn[32];dl_norm(db,dn,32);
                    char df[P];snprintf(df,P,"%s/deadline.txt",T[i].d);writef(df,dn);printf("✓ %s\n",dn);}
                task_show(i,n);show=0;}
            else if(k=='/'||k=='s'){
                char q[128];if(!raw_line("  Search: ",q,128)){show=0;continue;}
                int mx[1024],nm=0;for(int j=0;j<n&&nm<1024;j++){
                    if(strcasestr(T[j].t,q)){mx[nm++]=j;continue;}
                    struct stat ss;if(stat(T[j].d,&ss))continue;
                    if(!S_ISDIR(ss.st_mode)){size_t l;char*fc=readf(T[j].d,&l);
                        if(fc){if(strcasestr(fc,q))mx[nm++]=j;free(fc);}continue;}
                    char sd[P];snprintf(sd,P,"%s/task",T[j].d);DIR*dd=opendir(sd);if(!dd)continue;
                    struct dirent*de;int fd=0;while((de=readdir(dd))&&!fd){if(de->d_name[0]=='.'||!strstr(de->d_name,".txt"))continue;
                        char fp[P];snprintf(fp,P,"%s/%s",sd,de->d_name);size_t l;char*fc=readf(fp,&l);
                        if(fc){if(strcasestr(fc,q)){mx[nm++]=j;fd=1;}free(fc);}}closedir(dd);
                }if(!nm){printf("  No match\n");show=0;continue;}
                for(int j=0;j<nm;j++)printf("  %d. [P%s] %.60s\n",j+1,T[mx[j]].p,T[mx[j]].t);
                char gb[8];if(raw_line("  Go to: ",gb,8)){int gi=atoi(gb)-1;if(gi>=0&&gi<nm)i=mx[gi];else show=0;}else show=0;}
            else if(k=='k'){if(i>0)i--;else{printf("  (first task)\n");show=0;}}
            else if(k=='q'||k==3||k==27)break;else if(k=='j')i++;else{show=0;}}
        raw_exit();if(i>=n)puts("Done");return 0;}}
    if(!strcmp(sub,"pri")){if(argc<5){puts("a task pri # N");return 1;}
        int n=load_tasks(dir),x=atoi(argv[3])-1;if(x<0||x>=n){puts("x Invalid");return 1;}
        task_repri(x,atoi(argv[4]));return 0;}
    if(!strcmp(sub,"add")||!strcmp(sub,"a")){int si=3;
        if(si+1<argc&&(!strcmp(argv[si],"--by")||!strcmp(argv[si],"-b"))){g_by=argv[si+1];si+=2;}  /* optional: an LLM marks who entered it, e.g. --by claude-opus-4-8. not required. */
        if(si>=argc){puts("a task add [--by <model>] [-u] [PPPPP] <text>");return 1;}
        if(!strcmp(argv[si],"-u")){si++;int pri=50000;if(si<argc&&is5d(argv[si]))pri=atoi(argv[si++]);
            if(si>=argc){puts("a task add [--by <model>] -u [PPPPP] <text>");return 1;}
            char t[B]="";ajoin(t,B,argc,argv,si);note_url(task_add(dir,t,pri),"task",NULL);return 0;}
        return task_add_p(dir,argc,argv,si);}
    if(*sub=='d'&&!sub[1]){if(argc<4){puts("a task d <#|name>...");return 1;}int n=load_tasks(dir);
        for(int j=3;j<argc;j++){int x=-1,v=atoi(argv[j]);if(v>0&&v<=n)x=v-1;
            else{for(int i=0;i<n;i++){char*b=strrchr(T[i].d,'/');if(b&&!strcmp(b+1,argv[j])){x=i;break;}}}
            if(x<0||x>=n){printf("x %s\n",argv[j]);continue;}do_archive(T[x].d);printf("✓ %.40s\n",T[x].t);}
        return 0;}
    if(!strcmp(sub,"deadline")){if(argc<5){puts("a task deadline # MM-DD [HH:MM]");return 1;}
        int n=load_tasks(dir),x=atoi(argv[3])-1;if(x<0||x>=n){puts("x Invalid");return 1;}
        char raw[64]="";ajoin(raw,64,argc,argv,4);
        char dn[32];dl_norm(raw,dn,32);
        char df[P];snprintf(df,P,"%s/deadline.txt",T[x].d);writef(df,dn);printf("✓ %s\n",dn);return 0;}
    if(!strcmp(sub,"due")){int n=load_tasks(dir);if(!n){puts("No tasks");return 0;}
        int ix[1024],dl[1024],nd=0;
        for(int i=0;i<n;i++){int d=task_dl(T[i].d);if(d>=0){ix[nd]=i;dl[nd]=d;nd++;}}
        if(!nd){puts("No deadlines");return 0;}
        for(int a=0;a<nd-1;a++)for(int b=a+1;b<nd;b++)if(dl[a]>dl[b]){int t=ix[a];ix[a]=ix[b];ix[b]=t;t=dl[a];dl[a]=dl[b];dl[b]=t;}
        Tk D[1024];for(int j=0;j<nd;j++)D[j]=T[ix[j]];memcpy(T,D,(size_t)nd*sizeof(Tk));
        if(argc>3&&(*argv[3]=='r'||*argv[3]=='t')){sub="r";grn=nd;goto review;}
        int lim=argc>3&&isdigit(*argv[3])?atoi(argv[3]):nd;if(lim>nd)lim=nd;
        for(int j=0;j<lim;j++)printf("  %s%dd\033[0m P%s %.50s\n",dl[j]<=1?"\033[31m":dl[j]<=7?"\033[33m":"\033[90m",dl[j],T[j].p,T[j].t);return 0;}
    if(!strcmp(sub,"bench")){struct timespec t0,t1;
        clock_gettime(CLOCK_MONOTONIC,&t0);int n=0;for(int j=0;j<100;j++)n=load_tasks(dir);
        clock_gettime(CLOCK_MONOTONIC,&t1);
        printf("load_tasks(%d): %.0f us avg (x100)\n",n,((double)(t1.tv_sec-t0.tv_sec)*1e9+(double)(t1.tv_nsec-t0.tv_nsec))/100/1e3);
        fflush(stdout);int fd=dup(1);(void)!freopen("/dev/null","w",stdout);
        int m=n<10?n:10;
        clock_gettime(CLOCK_MONOTONIC,&t0);for(int j=0;j<m;j++)task_show(j,n);
        clock_gettime(CLOCK_MONOTONIC,&t1);fflush(stdout);dup2(fd,1);close(fd);stdout=fdopen(1,"w");
        double us=((double)(t1.tv_sec-t0.tv_sec)*1e9+(double)(t1.tv_nsec-t0.tv_nsec))/1e3;
        printf("task_show(x%d): %.0f us total, %.0f us/task\n",m,us,us/m);
        return 0;}
    if(!strcmp(sub,"sync")){sync_repo();puts("✓");return 0;}
    if(!strcmp(sub,"flag")||!strcmp(sub,"f")){int n=load_tasks(dir);if(!n){puts("No tasks");return 0;}
        char tf[P];snprintf(tf,P,"%s/a_flag_%d.txt",TMP,(int)getpid());
        FILE*fp=fopen(tf,"w");if(!fp)return 1;
        fprintf(fp,"Help me clean up my task list. Identify tasks to archive (duplicate, done, vague, obsolete).\n"
            "Ask me to confirm each batch. For confirmed tasks run: a task d <dirname> <dirname>...\n"
            "Use directory names (in brackets) as stable IDs. Multiple can be deleted in one command.\n"
            "Go in batches of ~10. Only archive what I approve.\n\n"
            "COMMANDS: a task d <dirname>... (archive) | a task pri # N (reprioritize) | a task sync\n\nTASK LIST:\n");
        for(int i=0;i<n;i++){char ft[B]="",td[P];snprintf(td,P,"%s/task",T[i].d);
            DIR*dd=opendir(td);if(dd){struct dirent*de;
                while((de=readdir(dd))){if(de->d_name[0]!='.'){ char fp2[P];snprintf(fp2,P,"%s/%s",td,de->d_name);
                    char*c=readf(fp2,NULL);if(c){if(!strncmp(c,"Text: ",6)){char*nl=strchr(c+6,'\n');if(nl)*nl=0;snprintf(ft,B,"%s",c+6);}free(c);break;}}}
                closedir(dd);}
            char*bn=strrchr(T[i].d,'/');fprintf(fp,"  %d. P%s %s [%s]\n",i+1,T[i].p,ft[0]?ft:T[i].t,bn?bn+1:"?");}
        fclose(fp);printf("Task list: %s (%d tasks)\n",tf,n);
        char pr[256];snprintf(pr,256,"Read %s and follow the instructions to help me triage tasks.",tf);
        execvp("a",(char*[]){"a","c",pr,NULL});return 1;}
    if(!strcmp(sub,"0")||!strcmp(sub,"s")||!strcmp(sub,"p")||!strcmp(sub,"do")){
        const char*x=*sub=='0'?"priority":!strcmp(sub,"s")?"suggest":!strcmp(sub,"p")?"plan":"do";
        char cmd[64];snprintf(cmd,64,"x.%s",x);execvp("a",(char*[]){"a",cmd,NULL});return 1;}
    if(*sub=='1'){char pf[P];snprintf(pf,P,"%s/common/prompts/task1.txt",SROOT);
        size_t l;char*r=readf(pf,&l);if(!r){printf("x No prompt: %s\n",pf);return 1;}
        while(l>0&&(r[l-1]=='\n'||r[l-1]==' '))r[--l]=0;
        printf("Prompt: %s\n",pf);execvp("a",(char*[]){"a","c",r,NULL});return 1;}
    if(argc>4&&isdigit(argv[3][0])){
        int n=load_tasks(dir),x=atoi(argv[3])-1;
        if(x>=0&&x<n){
        task_todir(T[x].d);
        char sd[P];snprintf(sd,P,"%s/%s",T[x].d,sub);mkdirp(sd);
        struct timespec tp;clock_gettime(CLOCK_REALTIME,&tp);
        char ts[32],fn[P];strftime(ts,32,"%Y%m%dT%H%M%S",localtime(&tp.tv_sec));
        char t[B]="";ajoin(t,B,argc,argv,4);
        snprintf(fn,P,"%s/%s.%09ld_%s.txt",sd,ts,tp.tv_nsec,DEV);writef(fn,t);
        printf("✓ %s: %.40s\n",sub,t);return 0;}}
    return task_add_p(dir,argc,argv,2);
}
/* a flow v: what each tmux window is about = its claude session's first user prompt (session id is in pane_start_command) */
static void win_about(const char*sid,char*out,int sz){*out=0;if(!sid||strlen(sid)<8)return;
    char cmd[600];snprintf(cmd,600,"f=$(ls -1t ~/.claude/projects/*/%s.jsonl 2>/dev/null|head -1);[ -n \"$f\" ]&&jq -r 'select(.type==\"user\" and (.message.content|type==\"string\"))|.message.content' \"$f\" 2>/dev/null|grep -vm1 '^<'",sid);
    FILE*p=popen(cmd,"r");if(!p)return;if(fgets(out,sz,p))out[strcspn(out,"\n")]=0;pclose(p);}
/* proposal spine: claude -p (single-shot, no agency) reads an editable lens prompt + a seed,
   emits NOTE:/TASK:/PROMPT: lines that land in the review surface (notes/tasks/prompts). */
static const char*PROPOSE_DEFAULT=
"Be a sharp, decorrelated idea-generator for Sean, an operator who directs AI agents.\n"
"Read the SEED and propose concrete items. Output ONLY lines, each beginning with exactly one tag:\n"
"NOTE: <a raw idea, observation, angle, or risk worth keeping>\n"
"TASK: <a concrete, actionable task>\n"
"PROMPT: <a prompt to hand a coding agent to accomplish a task>\n"
"Give 4-8 items, mixed across tags. Specific and non-obvious. No preamble, no other text.\n"
"(Edit this lens with `a flow` -> [e], e.g. act as von Neumann / Torvalds / a security paranoid.)\n";
static void propose_ensure(char*tpl,int sz){snprintf(tpl,(size_t)sz,"%s/common/prompts/propose.txt",SROOT);
    struct stat st;if(stat(tpl,&st)){char d[P];snprintf(d,P,"%s/common/prompts",SROOT);mkdirp(d);writef(tpl,PROPOSE_DEFAULT);}}
static void propose_run(const char*seed,const char*model,const char*book){
    char tpl[P];propose_ensure(tpl,P);size_t tl;char*tc=readf(tpl,&tl);if(!tc)return;
    char pf[P];snprintf(pf,P,"%s/a_propose_%d.txt",TMP,(int)getpid());
    FILE*f=fopen(pf,"w");if(!f){free(tc);return;}
    if(book&&*book){char bf[P];snprintf(bf,P,"%s/books/%s/output/explained.txt",AROOT,book);   /* load the ENTIRE book as context */
        if(access(bf,R_OK))snprintf(bf,P,"%s/books/%s/output/%s.txt",AROOT,book,book);
        if(access(bf,R_OK)){char od[P];snprintf(od,P,"%s/books/%s/output",AROOT,book);DIR*bd=opendir(od);struct dirent*be;bf[0]=0;  /* fallback: any .txt in output/ */
            if(bd){while((be=readdir(bd)))if(strstr(be->d_name,".txt")){snprintf(bf,P,"%s/%s",od,be->d_name);break;}closedir(bd);}}
        size_t bl=0;char*bc=bf[0]?readf(bf,&bl):NULL;
        if(bc){fprintf(f,"=== BOOK CONTEXT: %s ===\nDraw ideas, angles, and methods from this entire work:\n%s\n=== END BOOK ===\n\n",book,bc);free(bc);printf("  loaded book: %s (%zu KB)\n",book,bl/1024);}
        else printf("  (book '%s' had no readable text)\n",book);}
    fputs(tc,f);fprintf(f,"\nSEED: %s\n",seed);fclose(f);free(tc);
    char cmd[P];snprintf(cmd,P,"claude -p --dangerously-skip-permissions --model %s < '%s' 2>/dev/null",model,pf);
    printf("  generating with %s (single-shot)…\n",model);fflush(stdout);
    FILE*p=popen(cmd,"r");char ln[B];int cn=0,ct=0,cp=0;char d2[P];
    while(p&&fgets(ln,B,p)){ln[strcspn(ln,"\n")]=0;char*t=ln;while(*t==' ')t++;
        const char*v=0;int which=0;
        if(!strncmp(t,"NOTE:",5)){v=t+5;which=1;}else if(!strncmp(t,"TASK:",5)){v=t+5;which=2;}else if(!strncmp(t,"PROMPT:",7)){v=t+7;which=3;}
        if(!which)continue;while(*v==' ')v++;if(!*v)continue;
        if(which==2){snprintf(d2,P,"%s/tasks",SROOT);mkdirp(d2);task_add(d2,v,50000);ct++;}
        else{snprintf(d2,P,"%s/%s",SROOT,which==1?"notes":"prompts");mkdirp(d2);note_save(d2,v);which==1?cn++:cp++;}}
    if(p)pclose(p);unlink(pf);
    printf("  ✓ proposed %d notes, %d tasks, %d prompts\n",cn,ct,cp);}
/* instant edit receipt: blob URL is deterministic (repo slug cached once). The flocked add+commit is
   BACKGROUNDED — sync holds that lock for seconds during pushes, a foreground flock froze o→ESC.
   add/commit never touch the worktree (no stale redraw); the rebase that does stays exit-only. */
static void edit_st(const char*f,char*st){
    static char rp[128];if(!*rp){char c[B];snprintf(c,B,"git -C '%s' remote get-url origin 2>/dev/null|sed 's#.*github.com[:/]##;s#\\.git$##'",SROOT);
        pcmd(c,rp,128);rp[strcspn(rp,"\n")]=0;if(!*rp)snprintf(rp,128,"-");}
    const char*rel=f;size_t sl=strlen(SROOT);if(!strncmp(f,SROOT,sl)&&f[sl]=='/')rel=f+sl+1;
    char c[B];snprintf(c,B,"flock /tmp/.a_git.lock -c \"git -C '%s' add '%s'&&git -C '%s' commit -qm 'flow edit'\" >/dev/null 2>&1 &",SROOT,f,SROOT);(void)!system(c);
    if(*rp!='-')snprintf(st,256,"✓ edited → https://github.com/%s/blob/main/%s  (pushes on exit)",rp,rel);
    else snprintf(st,256,"✓ edited %s (local)",rel);}
/* archive a flow item by its display id (n1..n4 notes, t1..t4 tasks, p1..p4 prompts); re-applies the active sort so the id matches the screen */
static int flow_archive(const char*id,const char*srt){
    int num=atoi(id+1);if(num<1||num>4)return 0;char dd[P];
    if(id[0]=='t'){snprintf(dd,P,"%s/tasks",SROOT);int nt=load_tasks(dd);
        if(!strcmp(srt,"new"))qsort(T,(size_t)nt,sizeof(Tk),tcmp_new);
        else if(!strcmp(srt,"due")){int dl[1024];for(int i=0;i<nt;i++){int v=task_dl(T[i].d);dl[i]=v<0?1000000:v;}
            for(int i=1;i<nt;i++){Tk k=T[i];int kd=dl[i],j=i-1;while(j>=0&&dl[j]>kd){T[j+1]=T[j];dl[j+1]=dl[j];j--;}T[j+1]=k;dl[j+1]=kd;}}
        if(num<=nt){do_archive(T[num-1].d);printf("  ✓ archived t%d\n",num);return 1;}}
    else{snprintf(dd,P,"%s/%s",SROOT,id[0]=='n'?"notes":"prompts");int m=load_notes(dd,NULL);qsort(gn,(size_t)m,sizeof(GN),gncmp);
        int idx=m-num;if(idx>=0&&idx<m){do_archive(gn[idx].p);printf("  ✓ archived %s\n",id);return 1;}}
    return 0;}
static int key1(void){struct termios ot,rt;tcgetattr(0,&ot);rt=ot;rt.c_lflag&=~(tcflag_t)(ICANON|ECHO);rt.c_cc[VMIN]=1;tcsetattr(0,TCSAFLUSH,&rt);
    char c=0;int rv=(int)read(0,&c,1);tcsetattr(0,TCSAFLUSH,&ot);return rv==1?c:27;}   /* no \n: at the last row it scrolls the screen = menu jitter */
/* bottom-align the single shown item flush against the pinned strip: content + keys sit in one
   eye fixation, no dead gap. Measures wrapped rows (skips ANSI + utf8 continuations) to place. */
static void place1(const char*s){struct winsize w={0,0,0,0};ioctl(1,TIOCGWINSZ,&w);
    int rows=w.ws_row>8?w.ws_row:24,cols=w.ws_col?w.ws_col:80,r=1,c=0;
    for(const char*p=s;*p;p++){if(*p==27){while(*p&&*p!='m')p++;continue;}if((*p&0xC0)==(char)0x80)continue;if(++c>=cols){r++;c=0;}}
    int y=rows-4-r;printf("\033[%d;1H%s\n",y<1?1:y,s);}
/* archive proof on exit (ARCH #13): stage exactly the session's rename pairs (never add -A — other
   agents share this repo), one commit, print its github URL. Silent local moves are unverifiable. */
static int flow_exit(char af[][P],int na,int ne){if(!na){if(ne)sync_bg();return 0;}   /* edits already committed; exit is the first safe moment to pull/push */
    char*c=malloc(16384);int l=snprintf(c,16384,"cd '%s'&&git add",SROOT);
    for(int i=0;i<na&&i<64;i++){const char*s=strrchr(af[i],'/');
        l+=snprintf(c+l,(size_t)(16384-l)," '%s' '%.*s/.archive%s'",af[i],(int)(s-af[i]),af[i],s);}
    snprintf(c+l,(size_t)(16384-l)," 2>/dev/null;git commit -qm 'flow: %d archived' >/dev/null 2>&1&&echo \"$(git remote get-url origin 2>/dev/null|sed 's#.*github.com[:/]##;s#\\.git$##') $(git rev-parse --short HEAD)\"",na);
    char r[256]="";pcmd(c,r,256);free(c);r[strcspn(r,"\n")]=0;char*sp=strchr(r,' ');
    if(sp&&r[0]!=' '){*sp=0;printf("\n  ✓ %d archived → https://github.com/%s/commit/%s  (syncing)\n",na,r,sp+1);sync_bg();}
    else printf("\n  ! %d archived locally only (commit failed or no remote)\n",na);
    return 0;}
/* a flow — one place: notes + tasks + prompts, three independent flat lists (no cross-association).
   The single source of truth: `GET /flow` just renders this command's output (html is a terminal). */
static int cmd_flow(int argc,char**argv){perf_disarm();
    if(argc>3&&!strcmp(argv[2],"x")){flow_archive(argv[3],argc>4?argv[4]:"pri");return 0;}  /* a flow x <id> [sort] — non-interactive archive (html calls this) */
    if(argc>3&&!strcmp(argv[2],"gen")){const char*bk=strcmp(argv[3],"-")?argv[3]:NULL;char sd[B]="";ajoin(sd,B,argc,argv,4);  /* a flow gen <book|-> <seed…> — suggestor w/ full book context */
        if(*sd)propose_run(sd,"opus",bk);return 0;}
    int tty=isatty(1);const char*BO=tty?"\033[1m":"",*DM=tty?"\033[90m":"",*X=tty?"\033[0m":"";
    /* PAGED BY DEFAULT — phone-over-ssh scream: the full dump scrolls the one-key menu off a phone
       screen, so you could read OR act, never both. One section per screen; [j]/[k] flip pages
       (vim muscle memory, labeled next/prev in the menu). The menu is PINNED to the bottom rows of
       the terminal so across flips and actions every key sits in the same visual spot — eye and
       thumb never re-hunt, and bottom = thumb-reachable on a phone. [a]/`a flow all` keeps the
       everything-at-once view; pipe/web stay full render. Loads are gated per page so a flip costs
       only its own section (notes page never pays the tmux popen). Actions redraw in-process
       (continue, not re-exec): page + sort survive, no respawn. */
    const char*srt="pri";int verbose=0,pg=(tty&&isatty(0))?0:-1;   /* a flow [pri|new|due] [v] [all|0-4] */
    for(int i=2;i<argc;i++){if(!strcmp(argv[i],"v")||!strcmp(argv[i],"verbose"))verbose=1;else if(!strcmp(argv[i],"all"))pg=-1;
        else if(isdigit((unsigned char)argv[i][0])&&!argv[i][1])pg=(argv[i][0]-'0')%5;else srt=argv[i];}
    #define PG(i) (pg<0||pg==(i))
    char st[256]="";   /* action result, shown above the next menu — survives the clear-screen redraw */
    static char af[64][P];int na=0,ne=0;   /* archived / edited this session → receipts; push deferred to exit */
    for(;;){
    int bynew=!strcmp(srt,"new")||!strcmp(srt,"date"),bydue=!strcmp(srt,"due");
    int lim=4,cnt=-1;char dir[P],top[P];*top=0;   /* all-mode shows 4 each; cnt + top (current item's file) feed the strip + [o] */
    if(tty&&pg>=0)printf("\033[H\033[2J");
    if(PG(0)){snprintf(dir,P,"%s/notes",SROOT);int nn=load_notes(dir,NULL);cnt=nn;qsort(gn,(size_t)nn,sizeof(GN),gncmp);
    if(pg>=0){if(nn){int ix=nn-1;const char*u=strrchr(gn[ix].p,'_');char fr[24],ts[16]="",b2[B];if(u)snprintf(ts,16,"%.15s",u+1);ts_date(u?ts:NULL,fr,24);
        snprintf(b2,B,"  %sn1 %-14s%s %s",DM,fr,X,gn[ix].t);snprintf(top,P,"%s",gn[ix].p);place1(b2);}}   /* gncmp asc → newest at end */
    else{printf("%s━━ NOTES (%d)%s %snewest%s\n",BO,nn,X,DM,X);
    for(int i=0;i<nn&&i<lim;i++){int ix=nn-1-i;const char*u=strrchr(gn[ix].p,'_');char fr[24],ts[16]="";if(u)snprintf(ts,16,"%.15s",u+1);ts_date(u?ts:NULL,fr,24);
        printf("  %sn%d %-14s%s %s\n",DM,i+1,fr,X,gn[ix].t);}
    if(nn>lim)printf("  %s…+%d more%s\n",DM,nn-lim,X);}}
    if(PG(1)){snprintf(dir,P,"%s/tasks",SROOT);int nt=load_tasks(dir);cnt=nt;
    if(bynew)qsort(T,(size_t)nt,sizeof(Tk),tcmp_new);
    else if(bydue){int dl[1024];for(int i=0;i<nt;i++){int v=task_dl(T[i].d);dl[i]=v<0?1000000:v;}
        for(int i=1;i<nt;i++){Tk k=T[i];int kd=dl[i],j=i-1;while(j>=0&&dl[j]>kd){T[j+1]=T[j];dl[j+1]=dl[j];j--;}T[j+1]=k;dl[j+1]=kd;}}
    if(pg>=0){if(nt){char fr[24],b2[B];task_front(T[0].d,fr,24);
        snprintf(b2,B,"  %st1%s %s %sP%s%s %s",DM,X,fr,DM,T[0].p,X,T[0].t);snprintf(top,P,"%s",T[0].d);place1(b2);}}
    else{printf("\n%s━━ TASKS (%d)%s — %s%s%s  %ssort: a flow pri|new|due%s\n",BO,nt,X,BO,bynew?"created":bydue?"deadline":"priority",X,DM,X);
    for(int i=0;i<nt&&i<lim;i++){char fr[24];task_front(T[i].d,fr,24);
        printf("  %st%d%s %s %sP%s%s %s\n",DM,i+1,X,fr,DM,T[i].p,X,T[i].t);}
    if(nt>lim)printf("  %s…+%d more%s\n",DM,nt-lim,X);}}
    /* PROMPT CANDIDATES — suggested prompts that could accomplish a task (appendable like notes/tasks: a prompt c <text>) */
    if(PG(2)){snprintf(dir,P,"%s/prompts",SROOT);int npc=load_notes(dir,NULL);cnt=npc;qsort(gn,(size_t)npc,sizeof(GN),gncmp);
    if(pg>=0){if(npc){int ix=npc-1;const char*u=strrchr(gn[ix].p,'_');char fr[24],ts[16]="",b2[B];if(u)snprintf(ts,16,"%.15s",u+1);ts_date(u?ts:NULL,fr,24);
        snprintf(b2,B,"  %sp1 %-14s%s %s",DM,fr,X,gn[ix].t);snprintf(top,P,"%s",gn[ix].p);place1(b2);}}
    else{printf("\n%s━━ PROMPT CANDIDATES (%d)%s %ssuggested prompts for a task%s\n",BO,npc,X,DM,X);
    for(int i=0;i<npc&&i<lim;i++){int ix=npc-1-i;const char*u=strrchr(gn[ix].p,'_');char fr[24],ts[16]="";if(u)snprintf(ts,16,"%.15s",u+1);ts_date(u?ts:NULL,fr,24);
        printf("  %sp%d %-14s%s %s\n",DM,i+1,fr,X,gn[ix].t);}
    if(npc>lim)printf("  %s…+%d more%s\n",DM,npc-lim,X);
    if(!npc)printf("  %s(none — a prompt c <text>)%s\n",DM,X);}}
    /* TMUX WINDOWS — ★ = a window with an `a done` panel waiting for review, ● = active.  a flow v adds each window's purpose */
    if(PG(3)){printf("\n%s━━ TMUX WINDOWS%s %s(★=a done · ●=active%s)%s\n",BO,X,DM,verbose?"":" · a flow v=about",X);
    {char done[4096]=" ",ln[1024];struct{char wid[16],sid[80];}ws[64];int nws=0;
        FILE*pp=popen("tmux list-panes -a -F '#{window_id} #{pane_start_command}' 2>/dev/null","r");
        while(pp&&fgets(ln,sizeof ln,pp)){ln[strcspn(ln,"\n")]=0;char*sp=strchr(ln,' ');if(!sp)continue;*sp=0;char*wid=ln,*cm=sp+1;
            if(strstr(cm,"a_done")&&strlen(done)+strlen(wid)+2<sizeof done){strcat(done,wid);strcat(done," ");}
            char*rs=strstr(cm,"--resume ");if(!rs)rs=strstr(cm,"--session-id ");   /* claude session id → window purpose */
            if(rs&&nws<64){rs=strchr(rs,' ')+1;char sid[80];int k=0;while(rs[k]&&rs[k]!=' '&&rs[k]!='"'&&k<79){sid[k]=rs[k];k++;}sid[k]=0;
                if(k>=8){snprintf(ws[nws].wid,16,"%s",wid);snprintf(ws[nws].sid,80,"%s",sid);nws++;}}}
        if(pp)pclose(pp);
        FILE*wp=popen("tmux list-windows -a -F '#{window_id}\t#{window_active}\t#{session_name}:#{window_index} #{window_name}' 2>/dev/null","r");
        char seen[4096]=" ";int any=0;while(wp&&fgets(ln,sizeof ln,wp)){ln[strcspn(ln,"\n")]=0;
            char*t1=strchr(ln,'\t');if(!t1)continue;*t1=0;char*t2=strchr(t1+1,'\t');if(!t2)continue;*t2=0;
            int act=atoi(t1+1);char*lbl=t2+1;char key[40];snprintf(key,40," %s ",ln);
            if(strstr(seen,key))continue;if(strlen(seen)+strlen(key)<sizeof seen){strcat(seen,ln);strcat(seen," ");}  /* dedup linked windows */
            int hd=strstr(done,key)!=NULL;const char*cl=tty?(hd?"\033[33m":act?"\033[36m":"\033[90m"):"";
            printf("  %s%s %s%s%s\n",cl,hd?"★":act?"●":"·",lbl,hd?"  ← a done":"",X);any=1;
            if(verbose){const char*sid=NULL;for(int j=0;j<nws;j++)if(!strcmp(ws[j].wid,ln)){sid=ws[j].sid;break;}
                char ab[256]="";if(sid)win_about(sid,ab,256);
                printf("       %s↳ %.76s%s\n",DM,ab[0]?ab:"(shell)",X);}}
        if(wp)pclose(wp);if(!any)printf("  %s(no tmux windows)%s\n",DM,X);}}
    /* COMMON PROMPTS — common instructions of how to act, not specific tasks (kept; managed by `a prompt`) */
    if(PG(4)){char pdir[P];snprintf(pdir,P,"%s/common/prompts",SROOT);
    static char pf[256][P];int np=listdir(pdir,pf,256),shown=0;
    printf("\n%s━━ COMMON PROMPTS%s %s(common/prompts — how to act, not tasks)%s\n",BO,X,DM,X);
    for(int i=0;i<np;i++){char*b=strrchr(pf[i],'/');b=b?b+1:pf[i];
        if(!strstr(b,".txt"))continue;printf("  %s\n",b);shown++;}
    if(!shown)printf("  %s(none)%s\n",DM,X);}
    /* one-keypress menu — act on key-down, no Enter; always on-screen with its page's content.
       Key letters bright yellow: deliberate monochrome exception — on a phone the eye must FIND
       the key, not read labels. Keys follow GMAIL conventions (the email shortcuts billions
       already know): j/k next/prev, e=archive top item of THIS page (zero confirm, instant
       redraw — triage is dismiss-dismiss-dismiss, friction multiplies by queue length),
       x=select # then archive (one raw digit, no Enter), c=compose an item of the current
       page's type. No shifted keys: shift is a chord on a phone keyboard. */
    if(!tty||!isatty(0))return 0;   /* web/pipe = static render only */
    if(pg>=0){struct winsize w={0,0,0,0};ioctl(1,TIOCGWINSZ,&w);printf("\033[%d;1H",(w.ws_row>8?w.ws_row:24)-4);}else putchar('\n');   /* pin menu to bottom rows: fixed position across flips */
    if(pg>=0){static const char*PN[]={"NOTES","TASKS","PROMPTS","WINDOWS","COMMON PROMPTS"};   /* identity line IN the pinned strip: long wrapped items scroll the top header off a phone screen; the bottom strip is the only place guaranteed in view. \033[K per line: overflowed content scrolls old text into these rows — overprint must erase to EOL */
        printf("%s━━ %d/5 %s",BO,pg+1,PN[pg]);if(cnt>=0)printf(" (%d)",cnt);
        if(pg==1)printf(" — %s",bynew?"created":bydue?"deadline":"priority");printf("%s\033[K\n",X);}
    if(*st){printf("  \033[32m%s\033[0m",st);*st=0;}printf("\033[K\n");   /* status row always emitted (even empty) so menu rows never shift */
    static const char*CT[]={"note","task","prompt","note","note"};const char*ct=CT[pg<0?0:pg];char tc=pg==1?'t':pg==2?'p':'n';
    #define K(s) "[\033[1;33m" s "\033[0m]"
    printf("  " K("e") " archive  " K("o") " edit  " K("c") " new %s   " K("j") "next " K("k") "prev " K("a") "ll\033[K\n  " K("g") "enerate  " K("l") " g's prompt  " K("x") " archive by id  sort " K("1") "pri " K("2") "new " K("3") "due  " K("q") "/esc quit\033[K\n> \033[J",ct);fflush(stdout);
    #undef K
    int ch=key1();
    if(ch==27||ch==3||ch=='q')return flow_exit(af,na,ne);   /* esc / q / ^C exit */
    if(ch=='j'){pg=pg<0?0:(pg+1)%5;continue;}
    if(ch=='k'){pg=pg<0?4:(pg+4)%5;continue;}
    if(ch=='a'){pg=pg<0?0:-1;continue;}
    if(ch>='1'&&ch<='3'){srt=ch=='1'?"pri":ch=='2'?"new":"due";continue;}
    if(ch=='e'){if(pg>=0&&pg<=2){char id[3]={tc,'1',0};   /* gmail e: top of current page goes, next promotes into view */
            if(flow_archive(id,srt)){if(na<64)snprintf(af[na],P,"%s",fa_last);na++;snprintf(st,128,"✓ archived %s — next up",id);}else snprintf(st,128,"nothing to archive");}
        else snprintf(st,128,"e: not a list page");continue;}
    int acted=0;
    if(ch=='x'){char ib[16];printf("  archive (n1 t2 p3 …)> ");fflush(stdout);   /* typed id = power path; only one item is on screen */
        if(fgets(ib,16,stdin)&&(ib[0]=='n'||ib[0]=='t'||ib[0]=='p')){ib[strcspn(ib,"\n")]=0;
            if(flow_archive(ib,srt)){if(na<64)snprintf(af[na],P,"%s",fa_last);na++;snprintf(st,128,"✓ archived %s",ib);}else snprintf(st,128,"x no %s",ib);}continue;}
    if(ch=='g'){char sb[B];printf("  seed> ");fflush(stdout);
        if(fgets(sb,B,stdin)&&sb[0]!='\n'){sb[strcspn(sb,"\n")]=0;
            char book[256]="",bc[P];snprintf(bc,P,"ls -1 '%s/books' 2>/dev/null|grep -v '\\.py$'|fzf --prompt='book as context (esc=none)> ' --height=45%%",AROOT);
            FILE*bp=popen(bc,"r");if(bp){if(fgets(book,256,bp))book[strcspn(book,"\n")]=0;pclose(bp);}
            propose_run(sb,"opus",book[0]?book:NULL);}acted=1;}
    if(ch=='o'){if(*top){char f[P+8];struct stat sb;   /* gmail o: open the shown item in e -w (writes on quit/ESC) → instant back, edited text on screen, url receipt in status */
            if(!stat(top,&sb)&&S_ISDIR(sb.st_mode))snprintf(f,P+8,"%s/task",top);else snprintf(f,P+8,"%s",top);
            int off=0;{FILE*fp=fopen(f,"r");char h[7]={0};if(fp){(void)!fread(h,1,6,fp);fclose(fp);off=!strcmp(h,"Text: ")?6:0;}}
            /* cursor lands AFTER "Text: " (+6): o-then-type must edit the value — at byte 0 the insert
               breaks the kv key and the note silently drops from the pending list (the it-didnt-change bug) */
            char cm[P+24];snprintf(cm,P+24,"e -w %s'%s'",off?"+6 ":"",f);(void)!system(cm);
            edit_st(f,st);ne++;}
        else snprintf(st,128,"o: nothing to edit");continue;}
    if(ch=='l'){char tpl[P];propose_ensure(tpl,P);   /* edit the prompt [g] generates with */
        char cmd[P+8];snprintf(cmd,P+8,"e -w '%s'",tpl);(void)!system(cmd);
        edit_st(tpl,st);ne++;continue;}
    if(ch=='c'){char buf[B];   /* gmail c: compose — new item of the current page's type (note elsewhere) */
        printf("  new %s> ",ct);fflush(stdout);
        if(fgets(buf,B,stdin)&&buf[0]!='\n'){buf[strcspn(buf,"\n")]=0;char d2[P],*path;
            if(pg==1){snprintf(d2,P,"%s/tasks",SROOT);mkdirp(d2);path=task_add(d2,buf,50000);}
            else{snprintf(d2,P,"%s/%s",SROOT,pg==2?"prompts":"notes");mkdirp(d2);path=note_save(d2,buf);}
            printf("  ");fflush(stdout);note_url(path,ct,NULL);}acted=1;}   /* prints: saved → <commit url> */
    if(!acted)continue;   /* unknown key → just redraw */
    printf("  %sany key=back  esc=quit%s",DM,X);fflush(stdout);
    if((ch=key1())==27||ch=='q')return flow_exit(af,na,ne);
    }
    #undef PG
}
