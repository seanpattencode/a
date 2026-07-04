static int cmd_set(int argc, char **argv) {
    if (argc < 3) {
        char p[P]; snprintf(p, P, "%s/n", DDIR);
        printf("1. n [%s] commands without aio prefix\n   aio set n %s\n", fexists(p)?"on":"off", fexists(p)?"off":"on");
        return 0;
    }
    if(!strcmp(argv[2],"capslock")){char cmd[P];snprintf(cmd,P,"bash %s/lib/capslock.sh %s %s",SDIR,SDIR,argc>3?argv[3]:"on");return system(cmd);}
    char p[P]; snprintf(p, P, "%s/%s", DDIR, argv[2]);
    if (argc > 3 && !strcmp(argv[3], "on")) { int fd = open(p, O_CREAT|O_WRONLY, 0644); if (fd>=0) close(fd); puts("✓ on"); }
    else if (argc > 3 && !strcmp(argv[3], "off")) { unlink(p); puts("✓ off"); }
    else printf("%s\n", fexists(p) ? "on" : "off");
    return 0;
}

static const char*CFG_KEYS[]={"default_agent","claude_prefix","source","multi_default","worktrees_dir","tmux_conf","cat_a","m_agent","m_model","m_effort","m_cmd","m_tier","i_agent","i_effort",NULL};
static void cfg_show(void){for(const char**s=CFG_KEYS;*s;s++){const char*v=cfget(*s);
    printf("  %-16s%s\n",*s,v[0]?v:!strcmp(*s,"default_agent")?"c":"-");}}
static int cmd_settings(int argc,char**argv) {
    init_db();load_cfg();load_sess();
    if(argc>3){cfset(argv[2],argv[3]);printf("✓ %s=%s\n",argv[2],argv[3]);return 0;}
    if(argc>2&&!strcmp(argv[2],"agent")){
        const char*da=cfget("default_agent");if(!da[0])da="c";
        for(int i=0;i<NSE;i++)printf("%s %-4s %s\n",!strcmp(SE[i].key,da)?"*":" ",SE[i].key,SE[i].name);
        puts("\nSwitch default: a settings default_agent g");return 0;}
    cfg_show();
    puts("\n  Switch agent: a settings default_agent g\n  List agents:  a settings agent\n  Set any:      a settings <key> <value>");
    return 0;
}

static int cmd_install(int argc, char **argv) { (void)argc;(void)argv; AB;
    char s[P]; snprintf(s, P, "%s/a.c", SDIR);
    execlp("bash", "bash", s, "install", (char*)NULL);
    return 1;
}

static int cmd_uninstall(int argc, char **argv) { (void)argc;(void)argv;
    printf("Uninstall aio? (y/n): "); char buf[16];
    if (!fgets(buf, 16, stdin) || (buf[0] != 'y' && buf[0] != 'Y')) return 0;
    char p[P];
    snprintf(p, P, "%s/.local/bin/a", HOME); unlink(p);
    puts("✓ uninstalled"); _exit(0);
}

static int cmd_deps(int argc, char **argv) { (void)argc;(void)argv; AB;
    (void)!system("which tmux >/dev/null 2>&1 || sudo apt-get install -y tmux 2>/dev/null");
    printf("%s tmux\n", system("which tmux >/dev/null 2>&1") == 0 ? "✓" : "x");
    (void)!system("which node >/dev/null 2>&1 || sudo apt-get install -y nodejs npm 2>/dev/null");
    printf("%s node\n", system("which node >/dev/null 2>&1") == 0 ? "✓" : "x");
    const char *tools[][2] = {{"codex","npm i -g @openai/codex"},{"claude","curl -fsSL https://claude.ai/install.sh | bash"},{"gemini","npm i -g @google/gemini-cli"}};
    for (int i = 0; i < 3; i++) {
        char c[256]; snprintf(c, 256, "p=$(which %s 2>/dev/null);[ -n \"$p\" ] && [ \"${p:0:5}\" != /mnt/ ] || %s 2>/dev/null", tools[i][0], tools[i][1]); (void)!system(c);
        snprintf(c, 256, "p=$(which %s 2>/dev/null);[ -n \"$p\" ] && [ \"${p:0:5}\" != /mnt/ ]", tools[i][0]);
        printf("%s %s\n", system(c) == 0 ? "✓" : "x", tools[i][0]);
    }
    return 0;
}

static int cmd_e(int argc, char **argv) { AB;
    if (argc > 2 && !strcmp(argv[2], "install")) {
        (void)!system("curl -sL https://raw.githubusercontent.com/seanpattencode/e/main/e.c|clang -xc -Wno-everything -o ~/.local/bin/e -");
        return 0;
    }
    if (getenv("TMUX")) execlp("e", "e", (char*)NULL);
    init_db(); load_cfg();
    CWD(wd);
    create_sess("edit", wd, "e", NULL);
    tm_go("edit");
    return 0;
}

static int cmd_config(int argc, char **argv) {
    init_db(); load_cfg();
    if (argc < 3) {
        cfg_show();
        puts("\n  Prompts: claude_prompt codex_prompt gemini_prompt\n  Set: a config <key> <value>  |  a config <key> off");
        return 0;
    }
    const char *key = argv[2];
    if (argc > 3) {
        char val[B]=""; ajoin(val,B,argc,argv,3);
        if (!strcmp(val,"off")||!strcmp(val,"none")||!strcmp(val,"\"\"")||!strcmp(val,"''")) val[0]=0;
        cfset(key, val); tm_ensure_conf();
        printf("✓ %s=%s\n", key, val[0] ? val : "(cleared)");
    } else printf("%s: %s\n", key, cfget(key));
    return 0;
}

static void prompt_preview(const char*path){ /* confirm load: first 6 + last 3 lines, ... between */
    size_t n=0;char*d=readf(path,&n);if(!d){puts("  (empty)");return;}
    int tot=0;for(size_t i=0;i<n;i++)tot+=d[i]=='\n';
    if(tot<10){fputs(d,stdout);if(n&&d[n-1]!='\n')putchar('\n');free(d);return;}
    char*p=d;for(int i=0;*p&&i<6;p++){putchar(*p);if(*p=='\n')i++;}
    printf("\033[2m  ... (%d lines) ...\033[0m\n",tot-9);
    char*q=d;for(int c=0;*q&&c<tot-3;q++)c+=*q=='\n';
    fputs(q,stdout);if(n&&d[n-1]!='\n')putchar('\n');free(d);}
/* a prompt (bare,tty) → viewer TUI (tui.md): j/k pick, o view file, u unified, e edit, q quit. View delegates to less (universal j/k/q); menu loop is C/1ms. Activate stays `a prompt <name>`. Mirrors /prompt html. */
static int prompt_tui(const char*d){char p[64][P];int n=listdir(d,p,64);if(n<1){puts("no prompts");return 0;}int s=0;raw_enter();
    for(int c;;){struct winsize w={0,0,0,0};ioctl(1,TIOCGWINSZ,&w);
        printf("\033[H\033[2J  prompt files\033[K\n\n");
        for(int i=0;i<n;i++)printf("  %s %s\033[K\n",i==s?">":" ",bname(p[i]));
        printf("\033[%d;1H  j/k move  o view  u unified  e edit  q quit\033[K",w.ws_row>6?w.ws_row:24);fflush(stdout);
        if((c=raw_key())=='q'||c==27||c==3||c<0)break;
        else if(c=='j')s=(s+1)%n; else if(c=='k')s=(s-1+n)%n;
        else{raw_exit();char x[P+16];
            if(c=='u')(void)!system("a prompt show 2>/dev/null|less -R");
            else if(c=='e'){execlp("e","e",p[s],(char*)0);execlp("vi","vi",p[s],(char*)0);}
            else{snprintf(x,sizeof x,"less -- '%s'",p[s]);(void)!system(x);}
            raw_enter();}}
    raw_exit();printf("\033[H\033[2J");return 0;}
static int cmd_prompt(int argc, char **argv) {
    init_db();load_cfg();
    char d[P]; snprintf(d,P,"%s/common/prompts",SROOT);
    const char*act=cfget("prompt");if(!*act)act="default";
    if(!(argc>2)&&isatty(1)){perf_disarm();return prompt_tui(d);}  /* bare `a prompt` on a tty = TUI; piped / `a prompt list` keep the plain list */
    const char*sub=argc>2?argv[2]:"";
    /* DISTINCTION: prompt CANDIDATES (a prompt c [text]) = suggested prompts that could accomplish a *task*
       — an appendable list like notes/tasks (lives in SROOT/prompts, note-file format so `a flow`/load_notes reads it).
       COMMON PROMPTS (the .txt files below, managed by `a prompt`) = common instructions of how to *act*, not specific tasks. */
    if(!strcmp(sub,"c")||!strcmp(sub,"cand")){perf_disarm();
        char cd[P];snprintf(cd,P,"%s/prompts",SROOT);mkdirp(cd);
        if(argc>3){char t[B]="";ajoin(t,B,argc,argv,3);
            struct timespec tp;clock_gettime(CLOCK_REALTIME,&tp);char ts[32],fn[P],buf[B];
            strftime(ts,32,"%Y%m%dT%H%M%S",localtime(&tp.tv_sec));
            snprintf(fn,P,"%s/%08x_%s.%09ld.txt",cd,(unsigned)(tp.tv_nsec^(unsigned)tp.tv_sec),ts,tp.tv_nsec);
            snprintf(buf,B,"Text: %s\nStatus: pending\nDevice: %s\nCreated: %s\n",t,DEV,ts);writef(fn,buf);
            puts("✓ candidate saved");return 0;}
        char pp[256][P];int n=listdir(cd,pp,256);printf("%d prompt candidates  (add: a prompt c <text>)\n",n);
        for(int i=0;i<n;i++){size_t l;char*ck=readf(pp[i],&l);if(!ck)continue;
            if(!strncmp(ck,"Text: ",6)){char*nl=strchr(ck,'\n');if(nl)*nl=0;printf("  %s\n",ck+6);}free(ck);}
        return 0;}
    if(!strcmp(sub,"show")) {
        perf_disarm();CWD(wd);char tf[P];snprintf(tf,P,"/tmp/a_prompt_show_%d.txt",(int)getpid());
        write_prompt_file(tf,wd,argc>3?argv[3]:NULL);
        char*c=readf(tf,NULL);size_t n=0;if(c){n=strlen(c);fputs(c,stdout);free(c);}unlink(tf);
        fprintf(stderr,"\n— unified prompt · %zu tok · active file:%s · files: a prompt · edit/web: a prompt edit|web\n",n/4,act);return 0;
    }
    if(!strcmp(sub,"web")){perf_disarm();(void)!system("a ui on >/dev/null 2>&1");bg_exec(OPENER,"http://localhost:1111/prompt");puts("✓ opening localhost:1111/prompt");return 0;}
    if(!*sub||!strcmp(sub,"list")||!strcmp(sub,"ls")){perf_disarm();
        char paths[64][P];int n=listdir(d,paths,64);
        puts("prompt files  \033[2m(\033[32m*\033[0m\033[2m active → feeds the unified prompt)\033[0m");
        for(int i=0;i<n;i++){char nm[64];const char*b=bname(paths[i]),*dot=strrchr(b,'.');snprintf(nm,64,"%.*s",(int)(dot?dot-b:(long)strlen(b)),b);
            struct stat st;long sz=!stat(paths[i],&st)?(long)st.st_size:0;
            printf(" %s %-12s %ld tok\n",!strcmp(nm,act)?"\033[32m*\033[0m":" ",nm,sz/4);}
        printf("\nload: a prompt <name>   edit: a prompt edit <name>   unified prompt: a prompt show\n  candidates (task-specific, appendable): a prompt c [text]\n");return 0;}
    if(!strcmp(sub,"edit")){const char*nm=argc>3?argv[3]:act;char f[P];snprintf(f,P,"%s/%s.txt",d,nm);
        execlp("e","e",f,(char*)0);execlp("vi","vi",f,(char*)0);return 1;}
    char f[P];snprintf(f,P,"%s/%s.txt",d,sub);
    if(!fexists(f)){printf("x no prompt '%s' — see: a prompt list\n",sub);return 1;}
    cfset("prompt",sub);
    printf("✓ loaded \033[1m%s\033[0m → active prompt file (feeds the unified prompt for all agents)\n",sub);
    prompt_preview(f);return 0;
}

static int cmd_add(int argc, char **argv) {
    init_db(); load_cfg();
    char *args[16]; int na = 0;
    for (int i = 2; i < argc && na < 16; i++) args[na++] = argv[i];
    if (!na) { args[na++] = "."; }
    if (na >= 2 && !dexists(args[0])) {
        char *name=args[0], cmd[B]="";
        for(int i=1,l=0;i<na;i++) l+=snprintf(cmd+l,(size_t)(B-l),"%s%s",i>1?" ":"",args[i]);
        char d[P]; snprintf(d,P,"%s/workspace/cmds",SROOT); mkdirp(d);
        char f[P]; snprintf(f,P,"%s/%s.txt",d,name);
        char data[B]; snprintf(data,B,"Name: %s\nCommand: %s\n",name,cmd);
        writef(f,data);
        printf("✓ Added: %s\n",name); list_all(1,0); return 0;
    }
    /* Project add */
    char path[P], *a = args[0];
    if (!strcmp(a,".")) { if(!getcwd(path,P)) strcpy(path,"."); }
    else if (a[0]=='~') snprintf(path,P,"%s%s",HOME,a+1);
    else snprintf(path,P,"%s",a);
    if (!dexists(path)) { printf("x Not a directory: %s\n", path); return 1; }
    const char *name = bname(path);
    char d[P]; snprintf(d, P, "%s/workspace/projects", SROOT); mkdirp(d);
    char f[P]; snprintf(f, P, "%s/%s.txt", d, name);
    char repo[512] = ""; char c[B]; snprintf(c, B, "git -C '%s' remote get-url origin 2>/dev/null", path);
    pcmd(c, repo, 512); repo[strcspn(repo,"\n")] = 0;
    char data[B]; snprintf(data, B, "Name: %s\nPath: %s\n%s%s%s", name, path, repo[0]?"Repo: ":"", repo, repo[0]?"\n":"");
    writef(f, data);
    printf("✓ Added: %s\n", name); list_all(1, 0); return 0;
}

static int cmd_create(int argc, char **argv) {
    if (argc < 3) { puts("a create foo          private repo\na create foo public   public repo"); return 1; }
    int pub=0; for(int i=3;i<argc;i++) if(strstr(argv[i],"pub")) pub=1;
    char d[P]; snprintf(d,P,"%s/%s",HOME,argv[2]);
    char c[B]; snprintf(c,B,"mkdir -p '%s'&&cd '%s'&&git init -q&&gh repo create '%s' %s --source=.",d,d,argv[2],pub?"--public":"--private");
    printf("> %s\n",d); if(system(c)!=0) return 1;
    char*a[]={"a","add",d}; cmd_add(3,a);
    return 0;
}

static int cmd_remove(int argc, char **argv) {
    init_db(); load_cfg(); load_proj(); load_apps();
    if (argc < 3) { puts("Usage: a remove <#|name>"); list_all(0, 0); return 0; }
    const char *sel = argv[2];
    if (sel[0] >= '0' && sel[0] <= '9') {
        int idx = atoi(sel);
        if (idx < NPJ) {
            char f[P]; snprintf(f, P, "%s/workspace/projects/%s.txt", SROOT, PJ[idx].name);
            unlink(f);
            printf("✓ Removed: %s\n", PJ[idx].name); list_all(1, 0); return 0;
        }
        int ai = idx - NPJ;
        if (ai >= 0 && ai < NAP) {
            char f[P]; snprintf(f, P, "%s/workspace/cmds/%s.txt", SROOT, AP[ai].name);
            unlink(f);
            printf("✓ Removed: %s\n", AP[ai].name); list_all(1, 0); return 0;
        }
    }
    printf("x Not found: %s\n", sel); list_all(0, 0); return 1;
}

/* ── move ── reorder projects, persist Order: N to .txt files */
static int cmd_move(int argc, char **argv) {
    if (argc < 4) { puts("Usage: a move <from> <to>"); return 1; }
    int fr = atoi(argv[2]), to = atoi(argv[3]);
    init_db(); load_cfg(); load_proj();
    if (fr<0||fr>=NPJ||to<0||to>=NPJ) { printf("x Invalid (0-%d)\n",NPJ-1); return 1; }
    proj_t tmp = PJ[fr];
    if (fr<to) for(int i=fr;i<to;i++) PJ[i]=PJ[i+1]; else for(int i=fr;i>to;i--) PJ[i]=PJ[i-1];
    PJ[to] = tmp;
    for(int i=0;i<NPJ;i++){char*d=readf(PJ[i].file,NULL);if(!d)continue;
        char out[B]="";int ol=0;for(char*p=d;*p;){char*nl=strchr(p,'\n');
            if(!nl){if(strncmp(p,"Order:",6))ol+=snprintf(out+ol,(size_t)(B-ol),"%s\n",p);break;}
            if(strncmp(p,"Order:",6))ol+=snprintf(out+ol,(size_t)(B-ol),"%.*s\n",(int)(nl-p),p);p=nl+1;}
        free(d);(void)snprintf(out+ol,(size_t)(B-ol),"Order: %d\n",i);writef(PJ[i].file,out);}
    {char cf[P];snprintf(cf,P,"%s/i_cache.txt",DDIR);unlink(cf);} /* stale launcher cache → next `a i` rebuilds */
    printf("✓ %d -> %d\n",fr,to); return 0;
}

