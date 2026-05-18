/* tmux — one session "a", windows are jobs */
#define TMS "a"
#define ACAT "a cat"
static void tm_gc(void){(void)!system("tmux ls -F'#{session_name}:#{session_attached}' 2>/dev/null|awk -F: '/^"TMS"-[0-9]+:0/{print$1}'|xargs -I{} tmux kill-session -t{} 2>/dev/null");
    (void)!system("tmux list-clients -F'#{client_tty}' 2>/dev/null|while read t;do [ -e \"$t\" ]||tmux detach-client -t \"$t\" 2>/dev/null;done");}
static void tm_ensure_sess(void){
    {int r=system("timeout 1 tmux info >/dev/null 2>&1");
    if(WIFEXITED(r)&&WEXITSTATUS(r)==124){(void)!system("pkill -9 tmux 2>/dev/null; sleep 1");}}
    tm_gc();
    if(!system("tmux has-session -t '"TMS"' 2>/dev/null"))return;
    (void)!system("tmux new-session -d -s '"TMS"'");}
static int tm_has(const char *w) {
    char c[B];snprintf(c,B,"tmux list-windows -t '"TMS"' -F '#{window_name}' 2>/dev/null|grep -qx '%s'",w);
    return !system(c);
}
static void tm_t(const char*w,char*t){snprintf(t,256,*w=='%'?"%s":TMS":%s",w);}
static void tm_go(const char *w) {
    perf_disarm();tm_gc();tm_ensure_sess();char g[64];snprintf(g,64,TMS"-%d",(int)getpid());
    if(getenv("TMUX")){char t[256];if(w)tm_t(w,t);
        execlp("tmux","tmux","switch-client","-t",w?t:TMS,(char*)NULL);}
    if(w){char t[256];tm_t(w,t);execlp("tmux","tmux","new-session","-t",TMS,"-s",g,";","select-window","-t",t,(char*)NULL);}
    execlp("tmux","tmux","new-session","-t",TMS,"-s",g,(char*)NULL);}
static int tm_new(const char *w, const char *wd, const char *cmd) {
    tm_ensure_sess();if(tm_has(w))return 1;char c[B*2],ev[P+16]="";
    const char*xa=getenv("A_CTX");if(xa&&xa[0])snprintf(ev,sizeof(ev),"-e A_CTX='%s' ",xa);
    if(cmd&&*cmd)snprintf(c,sizeof(c),"tmux new-window %s-t '"TMS":' -n '%s' -c '%s' '%s'",ev,w,wd,cmd);
    else snprintf(c,sizeof(c),"tmux new-window %s-t '"TMS":' -n '%s' -c '%s'",ev,w,wd);
    return system(c);
}
static void tm_sk(const char*w,const char*s,int l){char t[256];tm_t(w,t);pid_t p=fork();
    if(p==0){if(l)execlp("tmux","tmux","send-keys","-l","-t",t,s,(char*)NULL);
    else execlp("tmux","tmux","send-keys","-t",t,s,(char*)NULL);_exit(1);}
    if(p>0)waitpid(p,NULL,0);}
#define tm_send(w,s) tm_sk(w,s,1)
#define tm_key(w,s) tm_sk(w,s,0)
static int tm_read(const char*w,char*buf,int len){char t[256];tm_t(w,t);
    char c[B];snprintf(c,B,"tmux capture-pane -t '%s' -p 2>/dev/null",t);return pcmd(c,buf,len);}
/* write default prompt + tools info to file. source=off skips intro+a-cat. */
#define SRC_ON strcmp(cfget("source"),"off")
static int write_prompt_file(const char *path, const char *wd, const char *extra) {
    FILE *f=fopen(path,"w");if(!f)return 0;
    const char *dp=dprompt(),*cp=cfget("claude_prefix");
    if(dp[0])fprintf(f,"%s\n",dp);
    if(cp[0])fprintf(f,"%s\n",cp);
    fprintf(f,"When work finished, run the a done command with a message to notify human."
        " a tools: a done <msg> a help a diff a push [msg] a note <text> a cat 2|3 a ssh\n");
    if(SRC_ON){char ip[P];snprintf(ip,P,"%s/common/prompts/intro.txt",SROOT);
        char*in=readf(ip,NULL);if(!in){const char*d="This is the code for a, the agent manager system you have been spawned from. You can use these tools and any other to acomplish your goal. User specific tools are in /adata/git/my. If you find an error in one of the /lib tools or /a, it is recommended to fix it on the spot to be shorter or the same length and send a pull request so all users may benefit from the fix. You can also use \"a task\" to help the user track deadlines for things which are in the future and can't be done in this session.";writef(ip,d);in=strdup(d);}
        fprintf(f,"%s\n",in);free(in);}
    char af[P];snprintf(af,P,"%s/AGENTS.md",wd);
    char *amd=readf(af,NULL);if(amd){fprintf(f,"%s\n",amd);free(amd);}
    if(extra&&extra[0])fprintf(f,"\nTask: %s\n",extra);
    fprintf(f,"\nInstalled tools on this device:\n");
    FILE*tp=popen("ls $(echo \"$PATH\"|tr : ' ') 2>/dev/null|sort -u","r");
    if(tp){char b[8192];size_t n;while((n=fread(b,1,8192,tp))>0)fwrite(b,1,n,f);pclose(tp);}
    fclose(f);return 1;
}
/* job cmd */
static void jcmd_fill(char*b,int cont,const char*wd,const char*extra){
    char ctxf[P],xsuf[512]="";snprintf(ctxf,P,"%s/a_ctx_%d.txt",TMP,(int)getpid());
    write_prompt_file(ctxf,wd,NULL);
    if(extra&&extra[0]){char ef[P];snprintf(ef,P,"%s/a_xtra_%d.txt",TMP,(int)getpid());writef(ef,extra);
        snprintf(xsuf,512," \"$(cat '%s')\"",ef);}
    const char*sid=getenv("SID");char sp[96]="";if(sid&&*sid)snprintf(sp,96,"--session-id %s ",sid);
    snprintf(b,B,"tmux splitw -vd -p50 -t $TMUX_PANE;" ACAT " >>%s 2>/dev/null;claude %s--dangerously-skip-permissions --effort max --append-system-prompt-file %s%s%s;e=$?;[ $e -ne 0 ]&&echo \"$(date) $e $(pwd)\">>%s/crashes.log;exec bash",ctxf,sp,ctxf,cont?" --continue":"",xsuf,LOGDIR);}

static void tm_ensure_conf(void) {
    if (strcmp(cfget("tmux_conf"), "y") != 0) return;
    if(fork())return;setsid();
    char adir[P]; snprintf(adir, P, "%s/.a", HOME);
    mkdirp(adir);
    char cpath[P]; snprintf(cpath, P, "%s/tmux.conf", adir);
    FILE *f = fopen(cpath, "w");
    if (!f) return;
    const char *cc = clip_cmd();
    fputs("# aio-managed-config\n"
        "set -g history-limit 1000000\n"
        "set -ga update-environment \"WAYLAND_DISPLAY\"\n"
        "set -ga update-environment \"SWAYSOCK\"\n"
        "set -g mouse on\n"
        "set -g focus-events on\n"
        "set -g set-titles on\n"
        "set -g set-titles-string \"#S:#W\"\n"
        "set -s set-clipboard on\n"
        "set -g visual-bell off\n"
        "set -g bell-action any\n"
        "set-hook -g alert-bell 'run-shell \"osascript -e \\\"display notification \\\\\\\"#{hook_window_name}\\\\\\\" with title \\\\\\\"a: done\\\\\\\"\\\"\"'\n"
        "set -g window-size latest\n"
        "set -g automatic-rename off\n"
        "set -g repeat-time 0\n"
        "set -s extended-keys on\n"
        "set -as terminal-features 'xterm*:extkeys'\n"
        "set -g assume-paste-time 0\n"
        "set -g window-style bg=default\n"
        "set -g window-active-style bg=default\n"
        "set -g pane-border-style fg=colour238\n"
        "set -g pane-active-border-style fg=green\n"
        "set -g status-style 'bg=default,fg=white,fill=default'\n"
        "set -g status-position bottom\n"
        "set -g status 2\n"
        "set -g status-right \"\"\n"
        "set -g status-format[0] \"#[align=left]#{?#{e|>:#{session_windows},1},#[range=user|prev]  <  #[norange],}#[align=centre]#{W:#[range=window|#{window_index}]#{?window_bell_flag,#[fg=white bg=red bold],#{?window_active,#[fg=colour232 bg=colour231 bold],#[fg=colour231 bg=colour243]}} #{?window_bell_flag,\\U0001F534 ,}#I:#W #{?window_active, , }#[default]#[norange]}#[align=right]#{?#{e|>:#{session_windows},1},#[range=user|next]  >  #[norange],}\"\n"
        "set -g status-format[1] \"#[align=centre]#[range=user|aa]a#[norange] #[range=user|win]Win#[norange] #[range=user|new]Pane#[norange] #[range=user|x]X#[norange] #[range=user|close]Close#[norange] #[range=user|menu] ... #[norange]#[align=right]#[range=user|kbd]Kb#[norange]\"\n"
        "bind-key -n M-Right next-window\n"
        "bind-key -n M-Left previous-window\n"
        /* C-Tab/C-S-Tab won't work: Tab=0x09=C-i, so C-Tab is indistinguishable from Tab */
        "bind -n C-k if-shell 'ps -o comm= -t #{pane_tty} 2>/dev/null|grep -qE \"^ssh\"' 'send C-k' 'next-window'\n"
        "bind -n C-j if-shell 'ps -o comm= -t #{pane_tty} 2>/dev/null|grep -qE \"^ssh\"' 'send C-j' 'previous-window'\n"
        "bind-key -n C-n new-window\n"
        "bind-key -n C-t new-window\n"
        "bind-key -n C-y split-window -fh\n"
        "bind -n C-w if -F '#{==:#{pane_current_command},ssh}' 'send C-w' 'selectw -n;killw -t:!'\n"
        "bind -n M-w if -F '#{==:#{pane_current_command},ssh}' 'send M-w' kill-pane\n"
        "bind-key -n C-q detach\n"
        "bind-key -n C-x kill-session\n"
        "bind -n WheelUpStatus selectw -p\n"
        "bind -n WheelDownStatus selectw -n\n"
        "bind-key -T root MouseDown1Status if -F '#{==:#{mouse_status_range},window}' "
        "{ select-window } { run-shell 'r=\"#{mouse_status_range}\"; case \"$r\" in "
        "prev) tmux previous-window;; next) tmux next-window;; "
        "aa) tmux new-window \"a\";; "
        "win) tmux display-popup -E -w90% -h80% 'a i';; new) tmux split-window;; "
        "x) tmux kill-pane;; close) tmux kill-window;; "
        "menu) tmux display-menu Pane 1 \"split-window -fh\" Zoom 2 \"resize-pane -Z\" Sync 3 \"set synchronize-panes\" Rename 4 \"command-prompt \\\"rename-window %%\\\"\" Quit 5 detach Kill 6 kill-session;; "
        "kbd) tmux set -g mouse off; tmux display-message \"Mouse off 3s\"; "
        "(sleep 3; tmux set -g mouse on) &;; esac' }\n", f);
    if (access("/data/data/com.termux",F_OK)==0)
        fprintf(f,"set-environment -g CLAUDE_CODE_TMPDIR \"%s/.tmp\"\n",HOME);
    if (cc) fprintf(f, "set -s copy-command \"%s\"\n", cc);
    {const char*cm[]={"copy-mode","copy-mode-vi",NULL};
    for(int i=0;cm[i];i++) cc?fprintf(f,"bind -T %s MouseDragEnd1Pane send -X copy-pipe-and-cancel \"%s\"\n",cm[i],cc)
        :fprintf(f,"bind -T %s MouseDragEnd1Pane send -X copy-pipe-and-cancel\n",cm[i]);}
    char vbuf[64] = ""; int vmaj = 0, vmin = 0;
    pcmd("tmux -V 2>/dev/null", vbuf, 64);
    { char *v = strstr(vbuf, "tmux "); if (v) sscanf(v + 5, "%d.%d", &vmaj, &vmin); }
    if (vmaj > 3 || (vmaj == 3 && vmin >= 6))
        fputs("set -g pane-scrollbars on\nset -g pane-scrollbars-position right\n", f);
    fclose(f);
    char uconf[P]; snprintf(uconf, P, "%s/.tmux.conf", HOME);
    char *uc = readf(uconf, NULL);
    if (!uc || !strstr(uc, "~/.a/tmux.conf")) {
        FILE *uf = fopen(uconf, "a");
        if (uf) { fputs("\nsource-file -q ~/.a/tmux.conf  # a\n", uf); fclose(uf); }
    }
    free(uc);
    {char cmd[B];snprintf(cmd,B,"tmux source-file '%s' 2>/dev/null&&tmux refresh-client -S 2>/dev/null",cpath);(void)!system(cmd);}
    _exit(0);
}
