static void do_archive(const char *p) {
    const char *s=strrchr(p,'/'); char a[P],d[P]; snprintf(a,P,"%.*s/.archive",(int)(s-p),p); mkdirp(a);
    snprintf(d,P,"%s%s",a,s); rename(p,d);
}
static char* note_save(const char *d, const char *t) {
    struct timespec tp; clock_gettime(CLOCK_REALTIME,&tp); time_t now=tp.tv_sec;
    static char fn[P]; char ts[32]; strftime(ts,32,"%Y%m%dT%H%M%S",localtime(&now));
    snprintf(fn,P,"%s/%08x_%s.%09ld.txt",d,(unsigned)(tp.tv_nsec^(unsigned)now),ts,tp.tv_nsec);
    FILE*f=fopen(fn,"w"); if(f){fprintf(f,"Text: %s\nStatus: pending\nDevice: %s\nCreated: %s\n",t,DEV,ts);fclose(f);}
    return fn;
}
static char rdir[P];
/* task board = lib/task.py (adata/git/tasks.txt); the adata/git/tasks/ dir engine was deleted 2026-09-05 (Sean: "delete … old task cmd"). a task / a t -> fallback_py; child stdout silenced (callers are TUIs and print their own receipt) */
static void task_py(const char*a,const char*b){char p[P];snprintf(p,P,"%s/lib/task.py",SDIR);pid_t k=fork();
    if(!k){int n=open("/dev/null",O_WRONLY);if(n>=0)dup2(n,1);execlp("python3","python3",p,a,b,(char*)0);_exit(127);}if(k>0)waitpid(k,0,0);}
static int cmd_task(int c,char**v){fallback_py("task",c,v);return 0;}
static void ts_human(const char*,char*,size_t);
static char nfs[256][P];static int nfn;  /* notes captured this session; git-synced + url'd on exit, never mid-loop */
static void notebox(const char*t){struct winsize w={0};ioctl(1,TIOCGWINSZ,&w);  /* echo saved text in a box → confidence it was taken as typed */
    int mx=(w.ws_col>20?w.ws_col:60)-6,n=(int)strlen(t),bw=n<mx?n:mx;if(bw<4)bw=4;if(bw>500)bw=500;  /* -6 = frame width, box must not wrap */
    printf("  ┌");for(int i=0;i<bw+2;i++)fputs("─",stdout);puts("┐");
    int rows=(n+bw-1)/bw;
    for(int r2=0;r2<rows;r2++){  /* huge text: head + count + tail rows — start AND end prove the whole thing landed */
        if(rows>9&&r2==4){char mid[48];snprintf(mid,48,"… %dc total …",n);printf("  │ %-*.*s │\n",bw,bw,mid);r2=rows-5;continue;}
        char rb[512];int rl=n-r2*bw<bw?n-r2*bw:bw;
        for(int k=0;k<rl;k++)rb[k]=(char)(t[r2*bw+k]=='\n'||t[r2*bw+k]=='\t'?' ':t[r2*bw+k]);rb[rl]=0;
        printf("  │ %-*.*s │\n",bw,bw,rb);}
    printf("  └");for(int i=0;i<bw+2;i++)fputs("─",stdout);puts("┘");}
static void rapid_note(const char*t){char*f=note_save(rdir,t);if(nfn<256)snprintf(nfs[nfn++],P,"%s",f);puts("  ✓ saved:");notebox(t);}
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
        printf("%d notes  last synced %s\n  \033[90ml list · r review · m manage · /x search\033[0m\n",n,sync_age());
        if(!isatty(0))return 0;
        struct winsize w={0};ioctl(1,TIOCGWINSZ,&w);int cw=w.ws_col>4?w.ws_col:60;
        for(int i=0;i<cw;i++)fputs("─",stdout);putchar('\n');        /* line above — type between the rules */
        char lb[B];if(!fgets(lb,B,stdin)){putchar('\n');return 0;}lb[strcspn(lb,"\n")]=0;
        for(int i=0;i<cw;i++)fputs("─",stdout);putchar('\n');        /* line below */
        if(!lb[0])return 0;
        if(lb[0]=='/')lb[0]='?';   /* /x → search; bare l/r/m still route; else = add path */
        execvp("a",(char*[]){"a","n",lb,NULL});return 0;}
    if(argc>2&&(argv[2][0]=='?'||!strcmp(argv[2],"r")||!strcmp(argv[2],"review"))){
        const char *f=argv[2][0]=='?'?argv[2]+1:NULL;int n=load_notes(dir,f);
        if(!n){puts("(none)");return 0;} if(!isatty(STDIN_FILENO)){for(int i=0;i<n&&i<10;i++)puts(gn[i].t);return 0;}
        int i=0,show=1; raw_enter();
        while(i<n){if(show)printf("\n[%d/%d] %s\n",i+1,n,gn[i].t);show=1;
            printf("  [d]el [t]ask [a]dd [/]find [j/k/q]  ");fflush(stdout);
            int k=raw_key();putchar('\n');
            if(k=='t'){task_py("add",gn[i].t);do_archive(gn[i].p);puts("✓ → task");n=load_notes(dir,f);if(i>=n)i=n-1;if(i<0)break;}
            else if(k=='d'){do_archive(gn[i].p);puts("✓");n=load_notes(dir,f);if(i>=n)i=n-1;if(i<0)break;}
            else if(k=='a'){char buf[B];if(raw_line("  Text: ",buf,B)){note_save(dir,buf);n=load_notes(dir,NULL);printf("✓ [%d]\n",n);}show=0;}
            else if(k=='/'||k=='s'){char q[128];if(raw_line("  Search: ",q,128)){n=load_notes(dir,q);i=0;printf("%d results\n",n);}else show=0;}
            else if(k=='k'){if(i>0)i--;else show=0;}
            else if(k=='q'||k==3||k==27)break;else if(k=='j')i++;else show=0;}
        raw_exit();if(i>=n)puts("Done");return 0;}
    if(argc>2&&!strcmp(argv[2],"m")){
        execvp("a",(char*[]){"a","c","Run 'a n l' to see all notes. Read a.c for context. Help me archive stale/done/duplicate notes in bulk. To archive: mkdir -p <dir>/.archive && mv <file> <dir>/.archive/. Large batches, only archive what I approve.",NULL});return 1;}
    if(argc>3&&!strcmp(argv[2],"-u")){char t[B*100]="";ajoin(t,sizeof t,argc,argv,3);  /* -u: save + print commit URL via gh API (apk) */
        note_url(note_save(dir,t),"note",NULL);return 0;}
    {char t[B*100]="";ajoin(t,sizeof t,argc,argv,2);snprintf(rdir,P,"%s",dir);rapid_note(t);  /* B*100 cap → long notes ok */
        rapid("n> ",rapid_note);
        for(int i=0;i<nfn;i++){printf("[%d/%d] ",i+1,nfn);fflush(stdout);note_url(nfs[i],"note",NULL);}  /* show each note's url at end */
        return 0;}
}
static void ts_human(const char*ts,char*out,size_t sz){
    struct tm t={0};int y,mo,d,h,mi,s;
    if(!ts||sscanf(ts,"%4d%2d%2dT%2d%2d%2d",&y,&mo,&d,&h,&mi,&s)<5){snprintf(out,sz,"(original)");return;}
    t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=mi;mktime(&t);
    int l=(int)strftime(out,sz,"%b %-d",&t);int h12=h%12;if(!h12)h12=12;
    snprintf(out+l,sz-(size_t)l," %d:%02d%s",h12,mi,h>=12?"pm":"am");
}
