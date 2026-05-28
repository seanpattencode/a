/* help */
static const char *HELP_SHORT =
    "a j \"prompt\"     Job: worktree + agent\n"
    "a a|c|co|g|ai   Default/claude/codex/gemini/aider\n"
    "a <#>           Open project by number\n"
    "a help          All commands";

static const char *HELP_FULL =
    "a - agent manager  c=claude co=codex g=gemini ai=aider\n\n"
    "JOBS    a j \"prompt\"  a job  a done \"msg\"\n"
    "AGENTS  a c|co|g|ai  a <key>++  a all  a agent\n"
    "PROJ    a <#>  a add/remove/move/scan  a create <name>\n"
    "GIT     a push [msg]  a pr [title]  a pull/diff/revert\n"
    "NOTES   a n \"text\"  a task\n"
    "REMOTE  a ssh [<#>]  a run <#> \"task\"\n"
    "CODE    a cat [1|2|3]  (1=all 2=skip my/ 3=first10+last5)\n"
    "SYSTEM  a ls/kill/attach  a hub  a config/sync/update/perf";

static void list_all(int cache, int quiet) {
    load_proj(); load_apps();
    char pf[P];snprintf(pf,P,"%s/projects.txt",DDIR);
    FILE*fp=fopen(pf,"w");if(fp){for(int i=0;i<NPJ;i++)fprintf(fp,"%s\n",PJ[i].path);fclose(fp);}
    if(quiet&&!cache)return;
    char out[B*4]="";int o=0;
    for(int i=0;i<NPJ;i++){char mk=dexists(PJ[i].path)?'+':(PJ[i].repo[0]?'~':'x');
        o+=sprintf(out+o,"%s%d. %c %s\n",i?"":"PROJECTS:\n",i,mk,PJ[i].path);}
    for(int i=0;i<NAP;i++)o+=sprintf(out+o,"%s%d. %s -> %.60s\n",i?"":"COMMANDS:\n",NPJ+i,AP[i].name,AP[i].cmd);
    if(!quiet&&out[0])printf("%s",out);
    if(cache){char cf[P];snprintf(cf,P,"%s/help_cache.txt",DDIR);
        FILE*f=fopen(cf,"w");if(f){fprintf(f,"%s\n%s",HELP_SHORT,out);fclose(f);}
        snprintf(cf,P,"%s/i_cache.txt",DDIR);unlink(cf);}
}

static void gen_icache(void) {
    load_proj(); load_apps(); load_cfg(); load_sess();
    char ic[P]; snprintf(ic, P, "%s/i_cache.txt", DDIR);
    FILE *f = fopen(ic, "w"); if (!f) return;
    fputs("a\tdefault agent\n",f);
    int i; for(i=0;i<NPJ;i++){fprintf(f,"%d: %s\tproject\n",i,PJ[i].name);
        DIR*sd=opendir(PJ[i].path);struct dirent*se;if(sd){while((se=readdir(sd)))if(se->d_name[0]!='.'&&se->d_type==DT_DIR)fprintf(f,"%s/%s\tdir\n",PJ[i].path,se->d_name);closedir(sd);}}
    for (i=0;i<NAP;i++) fprintf(f, "%d: %s\tcmd\n", NPJ+i, AP[i].name);
    for(i=0;i<NSE;i++)fprintf(f,"%s\t%s\n",SE[i].key,SE[i].name);
#ifdef __ANDROID__
    {char af[P];snprintf(af,P,"%s/local/apps.txt",AROOT);
    size_t al;char*ad=readf(af,&al);if(ad){fwrite(ad,1,al,f);free(ad);}}
#endif
    {char ad[P];snprintf(ad,P,"%s/lib/platonic_agents",SDIR);DIR*d=opendir(ad);struct dirent*e;
    if(d){while((e=readdir(d))){char*p=strrchr(e->d_name,'.');
        if(p&&(p[1]=='p'||p[1]=='c')){*p=0;fprintf(f,"agent run %s\tagent\n",e->d_name);}}closedir(d);}}
    /* auto-discover lib .py — extract docstring desc */
    {char ld[P];snprintf(ld,P,"%s/lib",SDIR);DIR*d=opendir(ld);struct dirent*e;
    if(d){while((e=readdir(d))){char*dot=strrchr(e->d_name,'.');
        if(!dot||strcmp(dot,".py")||e->d_name[0]=='_')continue;*dot=0;
        char fp[P],desc[128]="";snprintf(fp,P,"%s/%s.py",ld,e->d_name);
        FILE*pf=fopen(fp,"r");if(pf){char h[256];if(fgets(h,256,pf)){
            char*s=strstr(h," - ");if(s){s+=3;s[strcspn(s,"\"\n")]=0;snprintf(desc,128,"%s",s);}}fclose(pf);}
        fprintf(f,"%s\t%s\n",e->d_name,desc[0]?desc:"cmd");}closedir(d);}}
    /* auto-discover my + lab + repos scripts */
    {const char*sd[]={SROOT,SDIR};const char*sl[]={"my","lab"};
    for(int si=0;si<2;si++){char md[P];snprintf(md,P,"%s/%s",sd[si],sl[si]);DIR*d=opendir(md);struct dirent*e;
    if(d){while((e=readdir(d))){if(e->d_name[0]=='.'||e->d_name[0]=='_')continue;
        char nm[64];snprintf(nm,64,"%s",e->d_name);char*dot=strrchr(nm,'.');
        if(si&&(!dot||(strcmp(dot,".py")&&strcmp(dot,".c")&&strcmp(dot,".sh")&&strcmp(dot,".html"))))continue;
        const char*tg=!si&&dot&&!strcmp(dot,".html")?"page":sl[si];if(!si&&dot)*dot=0;
        fprintf(f,"%s\t%s\n",nm,tg);}closedir(d);}}
    /* repos: scan adata/repos/ scripts */
    {char rd[P];snprintf(rd,P,"%s/repos",AROOT);DIR*d=opendir(rd);struct dirent*re;
    if(d){while((re=readdir(d))){if(re->d_name[0]=='.')continue;
        char rp[P];snprintf(rp,P,"%s/%s",rd,re->d_name);DIR*sd=opendir(rp);struct dirent*se;
        if(sd){while((se=readdir(sd))){if(se->d_name[0]=='.'||se->d_name[0]=='_')continue;
            char*dot=strrchr(se->d_name,'.');
            if(dot&&(!strcmp(dot,".py")||!strcmp(dot,".c")||!strcmp(dot,".sh"))){
                char nm[64];snprintf(nm,64,"%s",se->d_name);*strrchr(nm,'.')=0;
                fprintf(f,"%s\t%s · repo\n",nm,re->d_name);}}closedir(sd);}}closedir(d);}}}
    /* subcommands not discoverable from filenames */
    fputs("scp\tsend file (tui pick file/host/dir)\n"
    "diff\tgit diff\ncat\tcodebase dump\nfreq\tusage frequency\n"
    "jobs\tlist jobs\ndash\tdashboard\nperf\tperformance\n"
    "ui\tweb dashboard\nterm\tterminal (a ui /term)\n"
    "web status\tLLM login status\nweb signin\tLLM auto sign-in\nweb log\tmanual sign-in mode\n"
    "cal add\tadd event\nhub add\tadd\nhub run\trun\nhub rm\trm\nhub log\tlog\n"
    "note\tnotes\nnote l\tlist\nnote r\treview\ntasks\ttasks\nssh add\tadd host\nssh all\tall hosts\n"
    "task add\tadd\ntask l\tlist\ntask r\treview\ntask rank\trank\n"
    "prompt\tdefault prompt\npow\tpower\npow o\tpower off\npow r\trestart\npow s\tsuspend\npow h\thibernate\n"
    "tutorial\tguided intro\no\toperator\nop\toperator\noperator\toperator\n",f);
    /* recent downloads — tagged so TUI hides until typed */
    {char dl[P];snprintf(dl,P,"%s/Downloads",HOME);DIR*dd=opendir(dl);struct dirent*de;
    if(dd){struct{char n[256];time_t t;}df[64];int nd=0;struct stat st;
        while((de=readdir(dd))&&nd<64){if(de->d_name[0]=='.')continue;char fp[P];snprintf(fp,P,"%s/%s",dl,de->d_name);
            if(!stat(fp,&st)){snprintf(df[nd].n,256,"%s",de->d_name);df[nd].t=st.st_mtime;nd++;}}
        closedir(dd);for(int a=0;a<nd-1;a++)for(int b=a+1;b<nd;b++)if(df[b].t>df[a].t){typeof(df[0]) tmp=df[a];df[a]=df[b];df[b]=tmp;}
        for(int j=0;j<nd&&j<20;j++)fprintf(f,"file %s\tdir\n",df[j].n);}}
    char sd[P]; snprintf(sd, P, "%s/ssh", SROOT);
    char sp[32][P]; int sn = listdir(sd, sp, 32);
    for (i=0;i<sn;i++) {
        kvs_t kv = kvfile(sp[i]);
        const char *nm = kvget(&kv,"Name"); if (!nm) continue;  fprintf(f, "ssh %s\thost\n", nm);
    }
#ifdef __APPLE__
    {const char*ad[]={"/Applications","/System/Applications"};
    for(int di=0;di<2;di++){DIR*d=opendir(ad[di]);if(!d)continue;struct dirent*e;
        while((e=readdir(d))){char*p=strstr(e->d_name,".app");
            if(p&&!p[4])fprintf(f,"open %.*s\tapp\n",(int)(p-e->d_name),e->d_name);}closedir(d);}
    fputs("open Finder\tapp\n",f);}
#else
    {char hp[P];snprintf(hp,P,"%s/.local/share/applications",HOME);
    const char*ad[]={"/usr/share/applications","/usr/local/share/applications",
        "/var/lib/flatpak/exports/share/applications",hp};
    for(int di=0;di<4;di++){const char*dp=ad[di];
        DIR*d=opendir(dp);if(!d)continue;struct dirent*e;
        while((e=readdir(d))){if(!strstr(e->d_name,".desktop"))continue;
            char fp[P],nm[128]="",ln[256];int ok=0;
            snprintf(fp,P,"%s/%s",dp,e->d_name);FILE*df=fopen(fp,"r");if(!df)continue;
            while(fgets(ln,256,df)){ln[strcspn(ln,"\n")]=0;
                if(!strncmp(ln,"Name=",5)&&!nm[0])snprintf(nm,128,"%s",ln+5);
                else if(!strcmp(ln,"Type=Application"))ok=1;
                else if(!strcmp(ln,"NoDisplay=true"))ok=0;}
            fclose(df);if(ok&&nm[0]){char dn[64];snprintf(dn,64,"%s",e->d_name);
                char*dd=strrchr(dn,'.');if(dd)*dd=0;
                fprintf(f,"open %s\t%s · app\n",dn,nm);}}
        closedir(d);}}
#endif
    fclose(f);
    if(!fork()){char ad[P],fp2[P],ln[256];snprintf(ad,P,"%s/git/activity",AROOT);
        DIR*d=opendir(ad);if(!d)_exit(0);FC ct[1024];int nc=0;struct dirent*e;
        while((e=readdir(d))){if(e->d_name[0]=='.')continue;
            snprintf(fp2,P,"%s/%s",ad,e->d_name);int fd=open(fp2,O_RDONLY);if(fd<0)continue;
            int r=(int)read(fd,ln,255);close(fd);if(r<=0)continue;ln[r]=0;
            char*p=ln;for(int j=0;j<3&&*p;j++){while(*p&&*p!=' ')p++;while(*p==' ')p++;}
            char*end=p;while(*end&&*end!=' '&&*end!='\n')end++;
            if(*end==' '&&end[1]!='/'&&end[1]!='-'&&!memchr(p,':',(size_t)(end-p))){end++;while(*end&&*end!=' '&&*end!='\n')end++;}
            *end=0;if(!*p)continue;
            int j;for(j=0;j<nc;j++)if(!strcmp(ct[j].n,p)){ct[j].c++;break;}
            if(j==nc&&nc<1024){snprintf(ct[nc].n,64,"%s",p);ct[nc].c=1;nc++;}}
        closedir(d);qsort(ct,(size_t)nc,sizeof(ct[0]),ctcmp);
        snprintf(fp2,P,"%s/freq_cache.txt",DDIR);
        FILE*ff=fopen(fp2,"w");if(ff){for(int j=0;j<nc;j++)fprintf(ff,"%s:%d\n",ct[j].n,ct[j].c);fclose(ff);}
        {snprintf(fp2,P,"%s/web_cache.txt",DDIR);
        char cm[P*2];snprintf(cm,P*2,"T=/tmp/.a_h$$;Q=\"SELECT url,title FROM urls WHERE title<>'' ORDER BY visit_count DESC LIMIT 50\";"
#ifdef __APPLE__
            "for b in 'Google/Chrome' 'Google/Chrome Canary' 'BraveSoftware/Brave-Browser' "
            "'BraveSoftware/Brave-Browser-Beta' Chromium;do "
            "cp \"$HOME/Library/Application Support/\"$b'/Default/History' $T 2>/dev/null&&"
            "sqlite3 $T \"$Q\" 2>/dev/null;done;"
            "cp \"$HOME/Library/Safari/History.db\" $T 2>/dev/null&&"
            "sqlite3 $T \"SELECT h.url,v.title FROM history_items h,history_visits v WHERE h.id=v.history_item AND v.title<>'' ORDER BY h.visit_count DESC LIMIT 50\" 2>/dev/null;"
#else
            "for b in google-chrome google-chrome-unstable google-chrome-beta google-chrome-canary "
            "BraveSoftware/Brave-Browser-Beta chromium;do "
            "cp \"$HOME/.config/\"$b'/Default/History' $T 2>/dev/null&&"
            "sqlite3 $T \"$Q\" 2>/dev/null;done;"
#endif
            "rm -f $T");
        FILE*sp=popen(cm,"r");if(sp){FILE*wf=fopen(fp2,"w");if(wf){
            unsigned char uh[4096]={0};char sl[1024];
            while(fgets(sl,1024,sp)){sl[strcspn(sl,"\n")]=0;
                char*u=sl,*t=strchr(sl,'|');if(!t)continue;*t++=0;
                char*hu=u;{char*s=strstr(u,"://");if(s){hu=s+3;if(!strncmp(hu,"www.",4))memmove(hu,hu+4,strlen(hu+4)+1);}}
                unsigned h=5381;for(char*p=hu;*p;p++)h=h*33+*p;h%=32768;
                if(uh[h/8]&(1<<(h%8)))continue;uh[h/8]|=1<<(h%8);
                if(t[0])fprintf(wf,"web %s\t%s · web\n",u,t);}
            fclose(wf);}pclose(sp);}}
        _exit(0);}
}

static int cmd_help(int c,char**v){(void)c;(void)v;
    char p[P];snprintf(p,P,"%s/help_cache.txt",DDIR);
    if(catf(p)<0){init_db();load_cfg();printf("%s\n",HELP_SHORT);list_all(1,0);}return 0;}
static int cmd_help_full(int c,char**v){(void)c;(void)v;init_db();load_cfg();printf("%s\n",HELP_FULL);list_all(1,0);return 0;}
static int cmd_hi(int c,char**v){(void)c;(void)v;for(int i=1;i<=10;i++)printf("%d\n",i);puts("hi");return 0;}

static int cmd_done(int argc,char**argv){AB;
    char p[P],msg[B]="";snprintf(p,P,"%s/.done",DDIR);ajoin(msg,B,argc,argv,2);
    {FILE*f=fopen(p,"w");if(f){fputs(msg,f);fclose(f);}}
    {char wd[P];if(getcwd(wd,P)){char df[P];snprintf(df,P,"%s/.a_done",wd);
        FILE*f=fopen(df,"w");if(f){fputs(msg[0]?msg:"done",f);fclose(f);}}}
    if(getenv("TMUX")){char ts[B]="",dl[B]="",sp[P];
        #define TAG(o,t) {char*a=strstr(msg,"<"t">"),*b=a?strstr(a,"</"t">"):0;\
            if(a&&b){int n=(int)(b-a-(int)sizeof(t)-1);if(n>0&&n<B)snprintf(o,(size_t)n+1,"%s",a+sizeof(t)+1);}}
        TAG(ts,"test");TAG(dl,"diff");
        #undef TAG
        if(dl[0]){char*cms=strstr(msg,"</diff>");cms=cms?cms+7:msg;while(*cms==' ')cms++;
            char cp[P];commit_path(cp);FILE*cf=fopen(cp,"w");if(cf){fprintf(cf,"%.*s\n%s\n",(int)strcspn(cms,"\n"),cms,dl);fclose(cf);}}
        snprintf(sp,P,"%s/a_done.sh",DDIR);FILE*sf=fopen(sp,"w");
        if(sf){fputs("echo \"✓ done: $(cat .a_done 2>/dev/null)\";echo;a diff\n",sf);
            if(dl[0])fprintf(sf,"echo;printf '\\033[1;36m=== focused diff: %s ===\\033[0m\\n';a diff -- %s\n",dl,dl);
            if(ts[0])fprintf(sf,"echo;printf '\\033[1;33m$ ';cat<<'A_DONE'\n%s\nA_DONE\nprintf '\\033[0m'\n%s\n",ts,ts);
            if(dl[0])fputs("echo;printf '\\033[1;32mpush these focused changes? [y] \\033[0m';read -rsn1 k </dev/tty;echo;[ \"$k\" = y ]&&a push -f\n",sf);
            fputs("echo;printf '\\033[2mpress any key to close\\033[0m';read -rsn1 </dev/tty\n",sf);fclose(sf);
            char c[P*2];const char*tp=getenv("TMUX_PANE");
            /* unify into ONE pane: clear prior output panes (keep the agent pane), then split one */
            if(tp){snprintf(c,P*2,"tmux list-panes -t %s -F '#{pane_id}'|while read p;do [ \"$p\" != \"%s\" ]&&tmux kill-pane -t \"$p\" 2>/dev/null;done",tp,tp);(void)!system(c);}
            snprintf(c,P*2,"tmux split-window -v -l 70%% -t '%s' 'bash %s' 2>/dev/null",tp?tp:"",sp);(void)!system(c);}}
    (void)!write(STDERR_FILENO,"\a",1);
    puts("✓ done");return 0;}

static int cmd_dir(int c,char**v){(void)c;(void)v;char w[P];if(getcwd(w,P))puts(w);execlp("ls","ls",(char*)0);return 1;}
static int cmd_x(int c,char**v){(void)c;(void)v;(void)!system("tmux kill-server 2>/dev/null");puts("✓ All sessions killed");return 0;}
static int cmd_search(int c,char**v){AB;char u[B]="https://google.com";
    if(c>2){int l=snprintf(u,B,"https://google.com/search?q=");for(int i=2;i<c&&l<B-1;i++)l+=snprintf(u+l,(size_t)(B-l),"%s%s",i>2?"+":"",v[i]);}
    bg_exec(OPENER,u);return 0;}
