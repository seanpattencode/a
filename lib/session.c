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
        else if(is_codex&&extra&&extra[0]){char ef[P];snprintf(ef,P,"%s/a_xtra_%d.txt",TMP,(int)getpid());writef(ef,extra);
            snprintf(csuf,512," \"$(cat '%s')\"",ef);}
    }
    /* claude reads ctxf via file flag; codex/gemini inline $(cat) — ARG_MAX caps ~128KB, can't fit codebase */
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

/* a resume [mins]  interactive menu of recent claude conversations; pick # to resume, <n>m to time-filter. */
static int cmd_resume(int c,char**v){perf_disarm();
    if(!getenv("TMUX")){fputs("x need tmux\n",stderr);return 1;}
    char m[16];snprintf(m,16,"%d",c>2?atoi(v[2]):0);setenv("A_RM",m,1);
    return system(
      "P=\"$HOME/.claude/projects\";mins=${A_RM:-0};sel=\"\";tmp=$(mktemp)\n"
      "echo \"note: automated a m/scanner runs may bury real work in this list; use <n>m to narrow\"\n"
      "while :;do\n"
      " :>\"$tmp\";i=0;filt=\"\"\n"
      " [ \"$mins\" -gt 0 ] 2>/dev/null&&filt=$(find \"$P\" -name '*.jsonl' -mmin -\"$mins\" 2>/dev/null)\n"
      " echo\n"
      " for f in $(ls -t \"$P\"/*/*.jsonl 2>/dev/null|head -150);do\n"
      "  [ \"$mins\" -gt 0 ] 2>/dev/null&&{ case \"$filt\" in *\"$f\"*);;*)continue;;esac;}\n"
      "  h=$(grep -c '\"role\":\"user\",\"content\":\"' \"$f\");[ \"$h\" -ge 2 ]||continue\n"
      "  w=$(grep -m1 -oE '\"cwd\":\"[^\"]+' \"$f\"|sed 's/.*\"cwd\":\"//');[ -d \"$w\" ]||continue\n"
      "  p=$(grep -m1 -oE '\"role\":\"user\",\"content\":\"[^\"]+' \"$f\"|sed 's/.*content\":\"//'|cut -c1-50)\n"
      "  s=${f##*/};s=${s%.jsonl};i=$((i+1));echo \"$s|$w\">>\"$tmp\"\n"
      "  printf '%2d) %3dt %-12s %s\\n' \"$i\" \"$h\" \"${w##*/}\" \"$p\"\n"
      "  [ \"$i\" -ge 20 ]&&break\n"
      " done\n"
      " [ \"$i\" = 0 ]&&echo \"(none)\"\n"
      " printf '# resume, <n>m filter, q quit: '\n"
      " read -r x </dev/tty||break\n"
      " case \"$x\" in ''|q)break;;*m)mins=${x%m};;*[!0-9]*);;*)[ \"$x\" -ge 1 ]&&[ \"$x\" -le \"$i\" ]&&{ sel=$(sed -n \"${x}p\" \"$tmp\");break;};;esac\n"
      "done\n"
      "rm -f \"$tmp\"\n"
      "[ -n \"$sel\" ]&&{ s=${sel%%|*};w=${sel#*|};tmux new-window -n \"r-${w##*/}\" -c \"$w\" \"claude --dangerously-skip-permissions --resume $s\";}\n");}
