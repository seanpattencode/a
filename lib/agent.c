static int cmd_review(int argc, char **argv) { (void)argc;(void)argv;
    (void)!system("gh pr list 2>/dev/null");
    char*v[]={"a","job",NULL};return cmd_jobs(2,v); }

static void doctree(const char*base,const char*rel){
    char dp[P];snprintf(dp,P,"%s/%s",base,rel);DIR*d=opendir(dp);if(!d)return;
    struct dirent*e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;
        char r2[P];snprintf(r2,P,"%s%s%s",rel,*rel?"/":"",e->d_name);
        char fp[P];snprintf(fp,P,"%s/%s",base,r2);struct stat st;
        if(!stat(fp,&st)&&S_ISDIR(st.st_mode)){printf("%s/\n",r2);doctree(base,r2);}else puts(r2);}
    closedir(d);}
static int cmd_docs(int argc, char **argv) {
    char dir[P]; snprintf(dir, P, "%s/adocs", SROOT); mkdirp(dir);
    if (argc > 3 && !strcmp(argv[2],"mkdir")) { char f[P];snprintf(f,P,"%s/%s",dir,argv[3]);mkdirp(f);printf("+ %s/\n",argv[3]);return 0; }
    if (argc > 2) {
        char f[P]; snprintf(f, P, "%s/%s%s", dir, argv[2], strchr(argv[2],'.') ? "" : ".md");
        char*sl=strrchr(f,'/');if(sl){*sl=0;mkdirp(f);*sl='/';}   /* folders: a docs sub/foo */
        int fd = open(f, O_CREAT|O_WRONLY|O_APPEND, 0644); if(fd>=0) close(fd);
        execlp("e", "e", f, (char*)NULL);
        return 0;
    }
    doctree(dir,"");return 0;
}

static int cmd_a_default(int c,char**v){
    init_db();load_cfg();load_sess();const char*k=cfget("default_agent");
    static char kb[16];snprintf(kb,16,"%s",k[0]?k:"c");v[1]=kb;return cmd_sess(c,v);}

static int agls(void){char p[P];snprintf(p,P,"%s/scan",SROOT);struct dirent**e;int n=scandir(p,&e,NULL,alphasort);  /* C, not ls|xargs basename: ~30 forks = 50ms, blew the 0.865ms gate → bare `a agent` printed nothing */
    for(int i=0;i<n;i++){char*b=e[i]->d_name,*s=strstr(b,".py");if(s&&!s[3]&&strcmp(b,"base.py"))printf("%.*s\n",(int)(s-b),b);}return n<0;}
static int cmd_agent(int argc, char **argv) {
    if(argc<3)return agls();
    if(!strcmp(argv[2],"run")&&argc>3){char f[P];
        snprintf(f,P,"%s/scan/%s.py",SROOT,argv[3]);
        if(!fexists(f)){printf("x Not found: %s\n",argv[3]);return 1;}
        perf_disarm();
        CWD(wd);
        char sn[256];snprintf(sn,256,"agent-%s-%ld",argv[3],(long)time(NULL));
        char cmd[B];
        {FILE*fp=fopen(f,"r");char h[32]={0};if(fp){(void)!fgets(h,32,fp);fclose(fp);}
            if(strstr(h,"/// script"))snprintf(cmd,B,"uv run --script '%s'",f);
            else snprintf(cmd,B,"python3 '%s'",f);}
        for(int i=4;i<argc;i++){int l=(int)strlen(cmd);snprintf(cmd+l,(size_t)(B-l)," '%s'",argv[i]);}
        if(!isatty(1))return !!system(cmd);
        tm_ensure_conf();
        if(getenv("TMUX")){
            char c[B];snprintf(c,B,"tmux new-window -P -F '#{pane_id}' -c '%s' '%s'",wd,cmd);
            char pid[64];pcmd(c,pid,64);pid[strcspn(pid,"\n")]=0;
            if(pid[0]){
                snprintf(c,B,"tmux split-window -v -t '%s' -c '%s' 'sh -c \"ls;exec $SHELL\"'",pid,wd);(void)!system(c);
                snprintf(c,B,"tmux select-pane -t '%s'",pid);(void)!system(c);
            }
            return 0;
        }
        create_sess(sn,wd,cmd,NULL);
        tm_go(sn);return 0;
    }
    init_db();load_cfg();load_sess();
    const char*wda=argv[2];sess_t*s=find_sess(wda);const char*task=s?(argc>3?argv[3]:NULL):wda;
    if(!s)s=find_sess("g");  /* default to gemini */
    if(!task||!task[0]){puts("Usage: a agent [g|c|l] <task>");return 1;}
    char taskstr[B]="";ajoin(taskstr,B,argc,argv,(s&&!strcmp(wda,s->key))?3:2);
    CWD(wd);char sn[256];snprintf(sn,256,"agent-%s-%ld",s->key,(long)time(NULL));
    printf("Agent: %s | Task: %.50s...\n",s->key,taskstr);create_sess(sn,wd,s->cmd,NULL);
    puts("Waiting for agent to start...");
    for(int i=0;i<60;i++){sleep(1);char o[B];tm_read(sn,o,B);
        if(strstr(o,"Type your message")||strstr(o,"claude")||strstr(o,"gemini"))break;}
    char prompt[B*2];snprintf(prompt,sizeof prompt,"%s\n\nCommands: \"a agent g <task>\" spawns gemini subagent, \"a agent l <task>\" spawns claude subagent. When YOUR task is fully complete, run: a done",taskstr);
    tm_send(sn,prompt);usleep(300000);tm_key(sn,"Enter");
    char donef[P];snprintf(donef,P,"%s/.done",DDIR);unlink(donef);
    puts("Waiting for completion...");
    for(time_t t0=time(NULL);!fexists(donef)&&time(NULL)-t0<300;)sleep(1);
    char out[B*4];tm_read(sn,out,sizeof out);printf("--- Output ---\n%s\n--- End ---\n",out);
    return 0;
}

static int cmd_scan(int argc, char **argv) {
    if(argc<3)return agls();
    char *nv[64];nv[0]=argv[0];nv[1]=(char*)"agent";nv[2]=(char*)"run";
    for(int i=2;i<argc&&i<61;i++)nv[i+1]=argv[i];nv[argc+1]=NULL;
    return cmd_agent(argc+1,nv);
}
