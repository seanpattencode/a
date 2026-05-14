static int cmd_m(int c,char**v){(void)c;(void)v;
    char b[B],sf[P],ss[P],pid_f[P],syspf[P],pty_id[64]="";
    snprintf(b,B,"[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone)",AROOT);system(b);
    snprintf(sf,P,"%s/m/m.txt",AROOT);snprintf(ss,P,"%s/m_status",TMP);snprintf(pid_f,P,"%s/m_pty_id",TMP);snprintf(syspf,P,"%s/m/sysprompt.txt",AROOT);
    if(!fexists(sf)){FILE*f=fopen(sf,"w");if(f){fputs("## user\n",f);fclose(f);}}
    if(!fexists(syspf)){FILE*f=fopen(syspf,"w");if(f){fputs("You are in the `a m` chat environment. To run shell commands, emit a fenced bash block (```bash ... ```). The block executes in a persistent pty (cwd=m repo). Its output is appended as '## tool output' with each line indented 4 spaces. Wait for real tool output; do not simulate. CRITICAL: never emit `## user`, `## assistant`, or `## tool output` headers yourself — those are conversation structure added by the system. Emit only your reply content plus optional bash blocks.\n",f);fclose(f);}}
    {FILE*f=fopen(ss,"w");if(f){fputs("ready",f);fclose(f);}}
    if(!getenv("TMUX")){puts("x needs tmux");return 1;}
    snprintf(b,B,"tmux split-window -dvb 'e %s'",sf);system(b);
    snprintf(b,B,"tmux split-window -dvb -P -F '#{pane_id}' 'cd %s/m;exec bash'",AROOT);pcmd(b,pty_id,64);pty_id[strcspn(pty_id,"\n")]=0;
    {FILE*f=fopen(pid_f,"w");if(f){fputs(pty_id,f);fclose(f);}}
    snprintf(b,B,"tmux split-window -dv -l 1 'P(){ printf \"\\r\\033[K%%s\" \"$(cat %1$s)\";};P;while inotifywait -qe modify %1$s 2>/dev/null;do P;done'",ss);system(b);
    snprintf(b,B,"P=$(cat %1$s);while :;do T=$(mktemp);e --box message: \"$T\";[ -s \"$T\" ]&&{ echo sending>%2$s;cat \"$T\" >>%3$s;printf '\\n## assistant\\n' >>%3$s;R=$(cat %3$s|claude -p --append-system-prompt-file %4$s);printf '%%s\\n' \"$R\"|sed '/^## \\(user\\|assistant\\|tool output\\)/d' >>%3$s;C=$(printf '%%s\\n' \"$R\"|awk '/^```(bash|sh|shell)?$/{f=!f;next}f');[ -n \"$C\" ]&&{ echo running>%2$s;tmux send-keys -t \"$P\" -l \"$C\";tmux send-keys -t \"$P\" Enter;sleep 3;O=$(tmux capture-pane -t \"$P\" -p -S -50|tail -30|sed 's/^/    /');printf '\\n## tool output %%s\\n%%s\\n' \"$(date +%%FT%%T)\" \"$O\" >>%3$s;};printf '\\n## user\\n' >>%3$s;echo done>%2$s;};rm \"$T\";done",pid_f,ss,sf,syspf);
    execlp("sh","sh","-c",b,(char*)0);return 1;
}
