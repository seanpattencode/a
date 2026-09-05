/* fallback py */
__attribute__((noreturn))
static void fallback_py(const char *mod, int argc, char **argv) {
    if (getenv("A_BENCH")) _exit(0);
    signal(SIGCHLD,SIG_DFL);   /* IGN survives exec → uv ECHILD */
    perf_disarm();char path[P],ld[P];snprintf(ld,P,"%s/lib",SDIR);snprintf(path,P,"%s/%s.py",ld,mod);
    if(!fexists(path))strcpy(path+strlen(path)-2,"c");   /* merged polyglot module (e.g. book.c: py half in #if 0) */
    setenv("PYTHONDONTWRITEBYTECODE","1",1);setenv("PYTHONPATH",ld,1);
    char*a[256];if(argc>250)argc=250;
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
    int ai = cmd && (strstr(cmd,"claude") || strstr(cmd,"codex") || strstr(cmd,"gemini") || strstr(cmd,"aider") || strstr(cmd,"grok"));
    char sid[64]="",acmd[B];
    if(cmd&&(strstr(cmd,"claude ")||strstr(cmd,"grok "))&&!strstr(cmd,"--resume")&&!strstr(cmd,"--continue"))   /* sid in argv = feed's live/pane anchor */
        {pcmd("cat /proc/sys/kernel/random/uuid 2>/dev/null||uuidgen|tr A-Z a-z",sid,64);sid[strcspn(sid,"\n")]=0;}
    snprintf(acmd,B,"%s%s%s",cmd?cmd:"",sid[0]?" --session-id ":"",sid);
    char wcmd[B*2],ctxf[P]="",csuf[512]="";
    int is_claude=ai&&strstr(acmd,"claude"),is_gemini=ai&&strstr(acmd,"gemini"),is_codex=ai&&strstr(acmd,"codex"),is_grok=ai&&strstr(acmd,"grok");
    if(ai){snprintf(ctxf,P,"%s/a_ctx_%d.txt",TMP,(int)getpid());
        /* claude/grok: ctx via file flag, extra as positional arg (auto-submits). gemini: extra into ctx file. codex: no ctx, extra as arg */
        if(!is_codex)write_prompt_file(ctxf,wd,is_gemini?extra:NULL);
        if(is_grok){char gf[P];snprintf(gf,P,"%s/a_agent_%d.md",TMP,(int)getpid());FILE*g=fopen(gf,"w");
            if(g){fputs("---\nname: a\ndescription: a agent manager context\nprompt_mode: full\nmodel: inherit\npermission_mode: default\nagents_md: true\n---\n",g);
                char*cx=readf(ctxf,NULL);if(cx){fputs(cx,g);free(cx);}fclose(g);}
            snprintf(csuf,512," --agent %s",gf);}
        else if(is_claude)snprintf(csuf,512," --append-system-prompt-file %s",ctxf);
        if((is_claude||is_codex||is_grok)&&extra&&extra[0]){size_t cl=strlen(csuf);char ef[P];snprintf(ef,P,"%s/a_xtra_%d.txt",TMP,(int)getpid());writef(ef,extra);
                snprintf(csuf+cl,512-cl," \"$(cat '%s')\"",ef);}
        else if(is_gemini)snprintf(csuf,512," --prompt-interactive \"Read %s in full now — it is your operating context + task.\"",ctxf);
    }
    /* ctx must pass as file — inline "$(cat)" dies at 128KB/arg (E2BIG) */
    char src_pfx[P+32]="";if(is_claude&&SRC_ON)snprintf(src_pfx,sizeof(src_pfx),"%s >>%s 2>/dev/null;",ACAT,ctxf);
    /* claude prompts a per-dir trust check every run (no global off-switch, --dangerously-skip doesn't bypass); pre-accept it for the launch dir */
    char tpfx[P+48]="";if(is_claude)snprintf(tpfx,sizeof(tpfx),"python3 \"%s/lib/trust.py\" 2>/dev/null;",SDIR);
    if (ai) snprintf(wcmd, sizeof(wcmd),
        "unset CLAUDECODE CLAUDE_CODE_ENTRYPOINT;%s%stmux wait-for -S rdy-%s;for _ in 1 2 3;do %s%s&&exit;echo \"$(date) $? $(pwd)\">>%s/crashes.log;sleep 1;done;exec bash", tpfx,src_pfx,sn,acmd,csuf,LOGDIR);
    else snprintf(wcmd, sizeof(wcmd), "%s", cmd ? cmd : "");
    tm_ensure_conf();
    int r = tm_new(sn, wd, wcmd);
    if (!r) sess_log(sn, wd);
    return r;
}

/* a resume / a res — resume agents (interactive pick or reboot save/restore); merged into lib/res.py */
static int cmd_resume(int c,char**v){fallback_py("res",c,v);return 0;}
