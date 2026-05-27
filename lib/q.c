/* q — tail every tmux window across devices, indicate origin */
static void q_local(int n,char*menu,int*ml,int mc){
    char out[B*4],cmd[B];
    snprintf(cmd,B,"tmux list-windows -t '%s' -F '#{window_name}' 2>/dev/null",TMS);
    pcmd(cmd,out,B*4);int idx=0;
    for(char*p=out;*p;){char*e=strchr(p,'\n');if(e)*e=0;
        printf("\n\033[35m[%s]\033[0m \033[36m%s\033[0m  \033[33m→ a %s\033[0m\n",DEV,p,p);fflush(stdout);
        snprintf(cmd,B,"tmux capture-pane -p -t '%s:%s' 2>/dev/null|awk NF|tail -n %d",TMS,p,n);
        (void)!system(cmd);
        char k[4]="";if(idx<9)snprintf(k,4,"%d",idx+1);
        *ml+=snprintf(menu+*ml,(size_t)(mc-*ml)," \"%s\" \"%s\" \"select-window -t %s\"",p,k,p);idx++;
        if(e)p=e+1;else break;}}
static void q_remote(const char*host,const char*lbl,int n){
    char cmd[B];
    snprintf(cmd,B,"a ssh %s 'for w in $(tmux list-windows -t a -F \"#{window_name}\" 2>/dev/null);do echo Q==$w;tmux capture-pane -p -t a:$w 2>/dev/null|awk NF|tail -n %d;done' 2>/dev/null",host,n);
    FILE*f=popen(cmd,"r");if(!f)return;char ln[B];
    while(fgets(ln,B,f)){
        if(!strncmp(ln,"Q==",3)){ln[strcspn(ln,"\n")]=0;
            printf("\n\033[35m[%s]\033[0m \033[36m%s\033[0m  \033[33m→ a ssh %s\033[0m\n",lbl,ln+3,host);}
        else fputs(ln,stdout);}
    pclose(f);}
static int cmd_q(int c,char**v){
    int n=c>2?atoi(v[2]):8;if(n<=0)n=8;
    char menu[B*8];int ml=snprintf(menu,B*8,"tmux display-menu -T ' a q — click to jump ' -x C -y C");
    q_local(n,menu,&ml,B*8);
    if(strncasecmp(DEV,"hsu",3))q_remote("hsu-wan","HSU",n);
    if(getenv("TMUX"))(void)!system(menu);
    return 0;}
