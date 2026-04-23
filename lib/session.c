/* fallback py */
__attribute__((noreturn))
static void fallback_py(const char *mod, int argc, char **argv) {
    if (getenv("A_BENCH")) _exit(0);
    perf_disarm();char path[P],ld[P];snprintf(ld,P,"%s/lib",SDIR);snprintf(path,P,"%s/%s.py",ld,mod);
    setenv("PYTHONDONTWRITEBYTECODE","1",1);setenv("PYTHONPATH",ld,1);
    char **a = malloc(((unsigned)argc + 5) * sizeof(char *));
    {char h[32]={0};FILE*f=fopen(path,"r");if(f){(void)!fgets(h,32,f);fclose(f);}
    if(strstr(h,"/// script")){
        a[0]="uv";a[1]="run";a[2]="--script";a[3]=path;
        for(int i=1;i<argc;i++)a[i+3]=argv[i];a[argc+3]=NULL;
        snprintf(ld,P,"%s/.local/bin/uv",HOME);if(!access(ld,X_OK)){a[0]=ld;execv(ld,a);}
        execvp("uv",a);}}
    a[0]="python3";a[1]=path;
    for(int i=1;i<argc;i++)a[i+1]=argv[i];a[argc+1]=NULL;
    execvp("python3",a);
    perror("a: python3");_exit(127);
}

/* session create — returns 1 if window already existed (restored), 0 if created */
static int create_sess(const char *sn, const char *wd, const char *cmd, const char *extra) {
    int ai = cmd && (strstr(cmd,"claude") || strstr(cmd,"codex") || strstr(cmd,"gemini") || strstr(cmd,"aider"));
    char sid[64]="",acmd[B];
    if(cmd&&strstr(cmd,"claude ")&&!strstr(cmd,"--resume")&&!strstr(cmd,"--continue"))
        pcmd("cat /proc/sys/kernel/random/uuid 2>/dev/null||uuidgen|tr A-Z a-z",sid,64),sid[strcspn(sid,"\n")]=0;
    snprintf(acmd,B,"%s%s%s",cmd?cmd:"",sid[0]?" --session-id ":"",sid);
    char wcmd[B*2],ctxf[P]="",csuf[512]="";
    int is_claude=ai&&strstr(acmd,"claude"),is_gemini=ai&&strstr(acmd,"gemini"),is_codex=ai&&strstr(acmd,"codex");
    if(ai){snprintf(ctxf,P,"%s/a_ctx_%d.txt",TMP,(int)getpid());
        /* claude: extra goes as positional arg (auto-submits). others: extra appended to file (part of first message) */
        write_prompt_file(ctxf,wd,is_claude?NULL:extra);
        if(is_claude){int cl=snprintf(csuf,512," --append-system-prompt-file %s",ctxf);
            if(extra&&extra[0]){char ef[P];snprintf(ef,P,"%s/a_xtra_%d.txt",TMP,(int)getpid());writef(ef,extra);
                snprintf(csuf+cl,(size_t)(512-cl)," \"$(cat '%s')\"",ef);}}
        else if(is_gemini)snprintf(csuf,512," --prompt-interactive \"$(cat '%s')\"",ctxf);
        else if(is_codex)snprintf(csuf,512," \"$(cat '%s')\"",ctxf);
    }
    /* claude reads ctxf via file flag; codex/gemini inline $(cat) — ARG_MAX caps ~128KB, can't fit codebase */
    char src_pfx[P+32]="";if(is_claude&&SRC_ON)snprintf(src_pfx,sizeof(src_pfx),"%s >>%s 2>/dev/null;",ACAT,ctxf);
    if (ai) snprintf(wcmd, sizeof(wcmd),
        "unset CLAUDECODE CLAUDE_CODE_ENTRYPOINT;%stmux wait-for -S rdy-%s;for _ in 1 2 3;do %s%s&&exit;echo \"$(date) $? $(pwd)\">>%s/crashes.log;sleep 1;done;exec bash", src_pfx,sn,acmd,csuf,LOGDIR);
    else snprintf(wcmd, sizeof(wcmd), "%s", cmd ? cmd : "");
    tm_ensure_conf();
    int r = tm_new(sn, wd, wcmd);
    if (!r) {
        if (ai) {
            char c[B]; snprintf(c, B, "tmux split-window -v -t '%s:%s' -c '%s' 'sh -c \"ls;exec $SHELL\"'", TMS, sn, wd);
            (void)!system(c);
            snprintf(c, B, "tmux select-pane -t '%s:%s' -U", TMS, sn); (void)!system(c);
        }
        /* logging */
        mkdirp(LOGDIR); char c[B];
        char lf[P]; snprintf(lf, P, "%s/%s__%s.log", LOGDIR, DEV, sn);
        snprintf(c, B, "tmux pipe-pane -t '%s:%s' 'cat >> %s'", TMS, sn, lf); (void)!system(c);
        char al[B]; snprintf(al, B, "session:%s log:%s", sn, lf);
        alog(al, wd);
    }
    tm_save_win(sn, wd, cmd, sid);
    return r;
}

static void tm_restore(void) {
    char sf[P];snprintf(sf,P,"%s/tmux_wins.txt",DDIR);
    char*d=readf(sf,NULL);if(!d)return;
    for(char*l=d,*nl;*l;l=nl?nl+1:l+strlen(l)){
        nl=strchr(l,'\n');if(nl)*nl=0;
        char*s=strchr(l,'|');if(!s)continue;*s++=0;
        char*bc=strchr(s,'|');if(!bc)continue;*bc++=0;
        char*sd=strchr(bc,'|');if(sd)*sd++=0;
        if(!dexists(s)||!*bc||tm_has(l))continue;
        char cmd[1024];
        snprintf(cmd,1024,sd&&*sd?"claude --dangerously-skip-permissions --effort max --resume %s":"%s",sd&&*sd?sd:bc);
        create_sess(l,s,cmd,NULL);}
    free(d);}

static int cmd_restore(int c,char**v){(void)c;(void)v;
    init_db();load_cfg();load_sess();tm_restore();return 0;}
