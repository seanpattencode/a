/* q — tail/search every tmux window across devices, indicate origin. a q [n] | a q <text> */
static void q_local(int n,const char*qy,char*menu,int*ml,int mc){
    char out[B*4],cmd[B],cw[128]="";
    if(qy)pcmd("tmux display-message -pt \"$TMUX_PANE\" '#W' 2>/dev/null",cw,128);
    cw[strcspn(cw,"\n")]=0;
    snprintf(cmd,B,"tmux list-windows -t '%s' -F '#{window_name}' 2>/dev/null",TMS);
    pcmd(cmd,out,B*4);int idx=0;char*nx=out;
    for(char*p=out;*p;p=nx){char*e=strchr(p,'\n');nx=e?e+1:p+strlen(p);if(e)*e=0;
        char mt[B*4]="";
        if(qy){if(!strcmp(p,cw))continue;
            snprintf(cmd,B,"tmux capture-pane -pJS -2000 -t '%s:%s' 2>/dev/null|grep -iaF -- \"$AQ\"|tail -n %d",TMS,p,n);
            pcmd(cmd,mt,B*4);if(!*mt)continue;}
        printf("\n\033[35m[%s]\033[0m \033[36m%s\033[0m  \033[33m→ a %s\033[0m\n",DEV,p,p);fflush(stdout);
        if(qy)fputs(mt,stdout);
        else{snprintf(cmd,B,"tmux capture-pane -p -t '%s:%s' 2>/dev/null|awk NF|tail -n %d",TMS,p,n);(void)!system(cmd);}
        char k[4]="";if(idx<9)snprintf(k,4,"%d",idx+1);
        *ml+=snprintf(menu+*ml,(size_t)(mc-*ml)," \"%s\" \"%s\" \"select-window -t %s\"",p,k,p);idx++;}}
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
    int n=0;static char qb[256];const char*qy=NULL;
    if(c==3){char*e;long x=strtol(v[2],&e,10);if(!*e&&x>0)n=(int)x;}
    if(c>2&&!n){perf_disarm();ajoin(qb,256,c,v,2);qy=qb;setenv("AQ",qb,1);}
    if(!n)n=8;
    char menu[B*8];int ml=snprintf(menu,B*8,"tmux display-menu -T ' a q — click to jump ' -x C -y C"),ml0=ml;
    q_local(n,qy,menu,&ml,B*8);
    if(!qy&&strncasecmp(DEV,"hsu",3))q_remote("hsu-wan","HSU",n);
    if(ml==ml0){if(qy)printf("x nothing mentions '%s'\n",qy);return qy?1:0;}
    if(getenv("TMUX"))(void)!system(menu);
    return 0;}
