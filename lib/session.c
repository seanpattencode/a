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

static void sess_log(const char *sn, const char *wd) {
    mkdirp(LOGDIR);char lf[P],c[B];
    snprintf(lf,P,"%s/%s__%s.log",LOGDIR,DEV,sn);
    snprintf(c,B,"tmux pipe-pane -t '%s:%s' 'cat >> %s'",TMS,sn,lf);(void)!system(c);
    snprintf(c,B,"session:%s log:%s",sn,lf);alog(c,wd);
}

/* session create — returns 1 if window already existed, 0 if created */
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
    /* From dock pick: split main pane (not dock) — claude on top, existing bash on bottom, dock unchanged */
    {const char*tp=getenv("TMUX_PANE");if(tp&&*tp){char dk[64]="",mp[64]="",c[B*2];
        pcmd("tmux display -p '#{@omni_pane}'",dk,sizeof(dk));dk[strcspn(dk,"\n")]=0;
        if(dk[0]&&!strcmp(dk,tp)){
            snprintf(c,sizeof(c),"tmux lsp -F '#{pane_id}'|grep -xv '%s'|head -1",dk);
            pcmd(c,mp,sizeof(mp));mp[strcspn(mp,"\n")]=0;
            snprintf(c,sizeof(c),"tmux split-window -b -v -t '%s' -c '%s' '%s'",mp[0]?mp:dk,wd,wcmd);
            (void)!system(c);return 2;}}}
    int r = tm_new(sn, wd, wcmd);
    if (!r) {
        if (ai) {
            char c[B]; snprintf(c, B, "tmux split-window -v -t '%s:%s' -c '%s' 'sh -c \"ls;exec $SHELL\"'", TMS, sn, wd);
            (void)!system(c);
            snprintf(c, B, "tmux select-pane -t '%s:%s' -U", TMS, sn); (void)!system(c);
        }
        sess_log(sn, wd);
    }
    return r;
}
