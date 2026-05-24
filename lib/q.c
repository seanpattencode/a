/* q — quick tail of every tmux window (review agent state) */
static int cmd_q(int c,char**v){
    int n=c>2?atoi(v[2]):8;if(n<=0)n=8;
    char out[B*4],cmd[B];
    snprintf(cmd,B,"tmux list-windows -t '%s' -F '#{window_name}' 2>/dev/null",TMS);
    pcmd(cmd,out,B*4);
    if(!*out){puts("No windows");return 0;}
    char menu[B*8];int ml=snprintf(menu,B*8,"tmux display-menu -T ' a q — click to jump ' -x C -y C");
    int idx=0;
    for(char*p=out;*p;){char*e=strchr(p,'\n');if(e)*e=0;
        printf("\n\033[36m=== %s ===\033[0m  \033[33m→ a %s\033[0m\n",p,p);fflush(stdout);
        snprintf(cmd,B,"tmux capture-pane -p -t '%s:%s' 2>/dev/null|grep -v '^$'|tail -n %d",TMS,p,n);
        (void)!system(cmd);
        char k[4]="";if(idx<9)snprintf(k,4,"%d",idx+1);
        ml+=snprintf(menu+ml,(size_t)(B*8-ml)," \"%s\" \"%s\" \"select-window -t %s\"",p,k,p);idx++;
        if(e)p=e+1;else break;}
    if(getenv("TMUX"))(void)!system(menu);
    return 0;}
