/* ── ssh ── */
#define SMUX " -oControlMaster=auto -oControlPath=%%d/.ssh/a-%%C -oControlPersist=300"
#define IP_CMD "(hostname -I 2>/dev/null||python3 -c 'import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.connect((\"8.8.8.8\",80));print(s.getsockname()[0])' 2>/dev/null)|awk '{print $1;exit}'"
static void ssh_parse(const char*h,char*hp,char*port){
    snprintf(hp,256,"%s",h);char*c=strrchr(hp,':');if(c){snprintf(port,8,"%s",c+1);*c=0;}else snprintf(port,8,"22");}
static const char*ssh_scope(const char*ip){/* lan if RFC1918, else wan */
    int a,b;if(sscanf(ip,"%d.%d",&a,&b)!=2)return"wan";
    return(a==10||(a==172&&b>=16&&b<=31)||(a==192&&b==168))?"lan":"wan";}
static int ssh_pre(char*c,int sz,const char*pw,const char*opts,const char*port,const char*hp){
    int n=0;if(pw&&pw[0])n=snprintf(c,(size_t)sz,"sshpass -p '%s' ",pw);
    return n+snprintf(c+n,(size_t)(sz-n),"ssh" SMUX " %s -p %s '%s'",opts,port,hp);}
static void ssh_savex(const char*dir,const char*n,const char*h,const char*pw,const char*k,const char*v){
    char f[P],d[B];snprintf(f,P,"%s/%s.txt",dir,n);
    int l=snprintf(d,B,"Name: %s\nHost: %s\n",n,h);
    if(pw&&pw[0])l+=snprintf(d+l,(size_t)(B-l),"Password: %s\n",pw);
    if(k&&v&&v[0])snprintf(d+l,(size_t)(B-l),"%s: %s\n",k,v);
    writef(f,d);snprintf(f,P,"%s/i_cache.txt",DDIR);unlink(f);}
static int ssh_idx(const char*a,const void*H_,int nh){
    typedef struct{char name[128],host[256],pw[256],path[P];}ht;const ht*H=(const ht*)H_;
    if(isdigit((unsigned char)*a))return atoi(a);
    for(int i=0;i<nh;i++)if(!strcmp(H[i].name,a))return i;return -1;}
static int cmd_ssh(int argc,char**argv){
    AB;
    char dir[P];snprintf(dir,P,"%s/ssh",SROOT);mkdirp(dir);
    typedef struct{char name[128],host[256],pw[256],jump[256],jpw[256],path[P];}host_t;
    host_t H[32];int nh=0,arc=0;
    char paths[32][P];int np=listdir(dir,paths,32);
    for(int i=np-1;i>=0&&nh<32;i--){
        kvs_t kv=kvfile(paths[i]);const char*n=kvget(&kv,"Name");if(!n)continue;
        int dup=0;for(int j=0;j<nh;j++)if(!strcmp(H[j].name,n)){dup=1;break;}
        if(dup){do_archive(paths[i]);arc++;continue;}
        snprintf(H[nh].name,128,"%s",n);const char*h=kvget(&kv,"Host"),*p=kvget(&kv,"Password");
        const char*j=kvget(&kv,"Jump"),*jp=kvget(&kv,"JumpPw");
        snprintf(H[nh].host,256,"%s",h?h:"");
        snprintf(H[nh].pw,256,"%s",p?p:"");
        snprintf(H[nh].jump,256,"%s",j?j:"");
        snprintf(H[nh].jpw,256,"%s",jp?jp:"");
        snprintf(H[nh].path,P,"%s",paths[i]);nh++;}
    const char*sub=argc>2?argv[2]:NULL;
    /* list */
    if(!sub){/* auto-refresh self */
        char ip[128]="",port[8]="22",h[256];const char*u=getenv("USER");
        char pv[64];pcmd("grep -ci microsoft /proc/version 2>/dev/null",pv,64);
        if(atoi(pv)>0){pcmd("powershell.exe -c \"ipconfig\"|grep -oP '192\\.168\\.\\d+\\.\\d+'|head -1",ip,128);ip[strcspn(ip,"\n")]=0;snprintf(port,8,"2222");}
        else{pcmd(IP_CMD,ip,128);ip[strcspn(ip,"\n")]=0;
            if(!access("/data/data/com.termux",F_OK))snprintf(port,8,"8022");}
        if(ip[0]){snprintf(h,256,!strcmp(port,"22")?"%s@%s":"%s@%s:%s",u?u:"",ip,port);
            char sn[160];snprintf(sn,160,"%s-%s",DEV,ssh_scope(ip));
            int f=-1;for(int i=0;i<nh;i++)if(!strcmp(H[i].name,sn)){f=i;break;}
            if(f<0||strcmp(H[f].host,h)){ssh_savex(dir,sn,h,f>=0?H[f].pw:NULL,0,0);
                if(f<0&&nh<32){snprintf(H[nh].name,128,"%s",sn);snprintf(H[nh].host,256,"%s",h);H[nh].pw[0]=0;nh++;}
                else if(f>=0)snprintf(H[f].host,256,"%s",h);}}
        int on=!system("pgrep -x sshd >/dev/null 2>&1");
        printf("SSH sshd:%s\n\n",on?" \033[32mon\033[0m":" \033[31moff\033[0m");
        size_t dl=strlen(DEV);
        for(int i=0;i<nh;i++){int s=!strncmp(H[i].name,DEV,dl)&&H[i].name[dl]=='-';
            printf("  %d. %s%s%s: %s%s\n",i,s?"\033[32m":"",H[i].name,s?" (self)\033[0m":"",H[i].host,H[i].pw[0]?" [pw]":"");}
        if(!nh)puts("  (none)");
        puts("\n  a=add  l=all  f=self  b=start  x=stop  c=status  u=setup\n  k=key  h=auth  r=rm  w=pw  m=mv  i=info  o=os  p=ping");
        if(!isatty(0))return 0;
        printf("\n> ");fflush(stdout);
        struct termios ot,rt;tcgetattr(0,&ot);rt=ot;rt.c_lflag&=~(tcflag_t)(ICANON|ECHO);rt.c_cc[VMIN]=1;tcsetattr(0,TCSAFLUSH,&rt);
        char ch;int rv=read(0,&ch,1);tcsetattr(0,TCSAFLUSH,&ot);
        if(rv!=1||ch=='\x1b'||ch==3){putchar('\n');return 0;}putchar('\n');
        if(ch>='0'&&ch-'0'<nh){execvp("a",(char*[]){"a","ssh",H[ch-'0'].name,NULL});}
        {static const char km[]="alfbxcukhrwmiop";static const char*kv[]={"add","all","self","start","stop","status","setup","key","auth","rm","pw","mv","info","os","ping"};
        for(int i=0;km[i];i++)if(ch==km[i]){execvp("a",(char*[]){"a","ssh",(char*)kv[i],NULL});}}
        printf("x %c\n",ch);return 1;}

    /* start/stop/status */
    if(!strcmp(sub,"start")){int r=system("sshd 2>/dev/null||service ssh start 2>/dev/null||sudo service ssh start 2>/dev/null||/usr/sbin/sshd 2>/dev/null||sudo /usr/sbin/sshd 2>/dev/null");puts(r?"x sshd":"✓ sshd");return r!=0;}
    if(!strcmp(sub,"stop")){(void)!system("pkill -x sshd 2>/dev/null||sudo pkill -x sshd");puts("✓");return 0;}
    if(!strcmp(sub,"status")||!strcmp(sub,"s")){
        int on=!system("pgrep -x sshd >/dev/null 2>&1");char ip[128];
        pcmd(IP_CMD,ip,128);ip[strcspn(ip,"\n")]=0;
        const char*u=getenv("USER");int p=access("/data/data/com.termux",F_OK)?22:8022;
        printf("%s ssh %s@%s -p %d\n",on?"✓":"x",u?u:"",ip,p);return 0;}
    /* setup — install openssh */
    if(!strcmp(sub,"setup")){
        int on=!system("pgrep -x sshd >/dev/null 2>&1");
        if(!on){printf("SSH not running. Install? (y/n): ");char yn[8];
            if(fgets(yn,8,stdin)&&(yn[0]=='y'||yn[0]=='Y')){
                if(!access("/data/data/com.termux",F_OK))(void)!system("pkg install -y openssh && sshd");
                else (void)!system("sudo apt install -y openssh-server && sudo systemctl enable --now ssh");}}
        on=!system("pgrep -x sshd >/dev/null 2>&1");
        printf("SSH: %s\n",on?"✓ running":"x not running");return 0;}
    /* key — generate ed25519 key */
    if(!strcmp(sub,"key")){char kf[P];snprintf(kf,P,"%s/.ssh/id_ed25519",HOME);
        struct stat st;if(stat(kf,&st)){char c[B];snprintf(c,B,"ssh-keygen -t ed25519 -N '' -f '%s'",kf);(void)!system(c);}
        char pub[P];snprintf(pub,P,"%s.pub",kf);catf(pub);return 0;}
    /* auth — add authorized key */
    if(!strcmp(sub,"auth")){char k[B];printf("Paste public key: ");if(!fgets(k,B,stdin))return 1;
        k[strcspn(k,"\n")]=0;char af[P];snprintf(af,P,"%s/.ssh/authorized_keys",HOME);
        char d[P];snprintf(d,P,"%s/.ssh",HOME);mkdirp(d);
        FILE*f=fopen(af,"a");if(f){fprintf(f,"\n%s\n",k);fclose(f);chmod(af,0600);puts("✓");}return 0;}
    /* push-auth — push gh/rclone creds to remote host */
    if(!strcmp(sub,"push-auth")&&argc>3){
        char tok[512];pcmd("gh auth token 2>/dev/null",tok,512);tok[strcspn(tok,"\n")]=0;
        const char*tgt=argv[3];
        /* push rclone.conf */
        {char rc[P],c[B*2];snprintf(rc,P,"%s/.config/rclone/rclone.conf",HOME);
        if(fexists(rc)){snprintf(c,B*2,"base64 '%s'|a ssh %s 'mkdir -p ~/.config/rclone&&base64 -d>~/.config/rclone/rclone.conf'",rc,tgt);(void)!system(c);}}
        /* push gh hosts.yml */
        {char gh[P],c[B*2];snprintf(gh,P,"%s/.config/gh/hosts.yml",HOME);
        if(fexists(gh)){snprintf(c,B*2,"base64 '%s'|a ssh %s 'mkdir -p ~/.config/gh&&base64 -d>~/.config/gh/hosts.yml'",gh,tgt);(void)!system(c);}}
        /* push gh token */
        if(tok[0]){char c[B*2];snprintf(c,B*2,"a ssh %s 'echo \"%s\"|gh auth login --with-token'",tgt,tok);(void)!system(c);}
        puts("✓");return 0;}
    /* add — interactive */
    if(!strcmp(sub,"add")){char h[256],n[128],pw[256];
        printf("Host: ");if(!fgets(h,256,stdin))return 1;h[strcspn(h,"\n")]=0;
        printf("Name: ");if(!fgets(n,128,stdin))return 1;n[strcspn(n,"\n")]=0;
        if(!n[0]){char*at=strchr(h,'@');snprintf(n,128,"%s",at?at+1:h);}
        printf("Password: ");if(!fgets(pw,256,stdin))return 1;pw[strcspn(pw,"\n")]=0;
        /* validate connection */
        {char hp[256],port[8];ssh_parse(h,hp,port);
        char tc[B];int l=ssh_pre(tc,B,pw,"-oConnectTimeout=5 -oStrictHostKeyChecking=no",port,hp);
        snprintf(tc+l,(size_t)(B-l)," 'echo ok' 2>&1");
        char o[64];if(pcmd(tc,o,64)||!strstr(o,"ok")){printf("x auth failed: %s",o);return 1;}}
        ssh_savex(dir,n,h,pw,0,0);printf("✓ %s\n",n);return 0;}
    /* self — register this device */
    if(!strcmp(sub,"self")){char ip[128]="",port[8]="22",h[256],dnm[160];
        const char*u=getenv("USER");const char*nm=argc>3?argv[3]:dnm;
        /* WSL detection */
        int wsl=0;{char pv[64];pcmd("grep -ci microsoft /proc/version 2>/dev/null",pv,64);wsl=atoi(pv)>0;}
        if(wsl){
            pcmd("powershell.exe -c \"ipconfig\"|grep -oP '192\\.168\\.\\d+\\.\\d+'|head -1",ip,128);ip[strcspn(ip,"\n")]=0;
            snprintf(port,8,"2222");
            (void)!system("pgrep -x sshd >/dev/null||sudo service ssh start");
            char pf[B];pcmd("powershell.exe -NoProfile -c 'netsh interface portproxy show all' 2>/dev/null",pf,B);
            if(!strstr(pf,"2222")){char wu[128];pcmd("powershell.exe -NoProfile -c '$env:USERNAME'",wu,128);wu[strcspn(wu,"\r\n")]=0;
                if(wu[0]){char ps[P],c[B];snprintf(ps,P,"/mnt/c/Users/%s/.wsl-ssh.ps1",wu);
                    writef(ps,"$ip='';1..15|%{if(!$ip){sleep 2;try{$ip=(wsl hostname -I).Trim().Split()[0]}catch{}}};if(!$ip){exit}\n"
                        "netsh interface portproxy delete v4tov4 listenport=2222 listenaddress=0.0.0.0 2>$null\n"
                        "netsh interface portproxy add v4tov4 listenport=2222 listenaddress=0.0.0.0 connectport=22 connectaddress=$ip\n"
                        "netsh advfirewall firewall delete rule name='WSL SSH' 2>$null\n"
                        "netsh advfirewall firewall add rule name='WSL SSH' dir=in action=allow protocol=tcp localport=2222\n"
                        "schtasks /create /tn 'WSL SSH Forward' /sc onlogon /rl highest /f /tr (\"powershell -WindowStyle Hidden -ExecutionPolicy Bypass -File C:/Users/$env:USERNAME/.wsl-ssh.ps1\")\n");
                    snprintf(c,B,"powershell.exe -NoProfile -c \"Start-Process powershell -Verb RunAs -Wait -ArgumentList '-ExecutionPolicy','Bypass','-File','C:/Users/%s/.wsl-ssh.ps1'\"",wu);
                    puts("WSL SSH setup (UAC)...");(void)!system(c);}}
            printf("✓ WSL port forward\n");
#ifdef __APPLE__
        }else if(1){
            pcmd("ipconfig getifaddr en0 2>/dev/null",ip,128);ip[strcspn(ip,"\n")]=0;
#else
        }else{
#endif
            pcmd(IP_CMD,ip,128);ip[strcspn(ip,"\n")]=0;
            if(!access("/data/data/com.termux",F_OK))snprintf(port,8,"8022");
            else{char pp[8];pcmd("awk '/^Port /{printf $2}' /etc/ssh/sshd_config 2>/dev/null",pp,8);if(pp[0])snprintf(port,8,"%s",pp);}}
        snprintf(h,256,!strcmp(port,"22")?"%s@%s":"%s@%s:%s",u?u:"",ip,port);
        snprintf(dnm,160,"%s-%s",DEV,ssh_scope(ip));
        char os[128];pcmd("uname -sr 2>/dev/null",os,128);os[strcspn(os,"\n")]=0;
        /* preserve existing password */
        const char*epw=NULL;for(int i=0;i<nh;i++)if(!strcmp(H[i].name,nm)){epw=H[i].pw;break;}
        ssh_savex(dir,nm,h,epw,"OS",os);printf("✓ %s %s [%s]\n",nm,h,os);return 0;}
    /* rm */
    if(!strcmp(sub,"rm")&&argc>3){int x=ssh_idx(argv[3],H,nh);
        if(x>=0&&x<nh){unlink(H[x].path);printf("✓ rm %s\n",H[x].name);}return 0;}
    /* pw — change password */
    if(!strcmp(sub,"pw")&&argc>3){int x=ssh_idx(argv[3],H,nh);
        if(x>=0&&x<nh){char pw[256];printf("Password for %s: ",H[x].name);
            if(fgets(pw,256,stdin)){pw[strcspn(pw,"\n")]=0;ssh_savex(dir,H[x].name,H[x].host,pw,0,0);printf("✓ %s\n",H[x].name);}}return 0;}
    /* mv/rename */
    if((!strcmp(sub,"mv")||!strcmp(sub,"rename"))&&argc>4){
        const char*old=argv[3],*nn=argv[4];int x=ssh_idx(old,H,nh);
        if(x>=0&&x<nh){unlink(H[x].path);
            ssh_savex(dir,nn,H[x].host,H[x].pw,0,0);printf("✓ %s -> %s\n",H[x].name,nn);}return 0;}
    /* info */
    if(!strcmp(sub,"info")||!strcmp(sub,"i")){
        for(int i=0;i<nh;i++){char hp[256],port[8];ssh_parse(H[i].host,hp,port);
            printf("%s: ssh %s%s%s%s\n",H[i].name,strcmp(port,"22")?"-p ":"",strcmp(port,"22")?port:"",strcmp(port,"22")?" ":"",hp);}return 0;}
    /* os — detect remote OS on all hosts */
    if(!strcmp(sub,"os")||!strcmp(sub,"ping")){
        struct{int fd;pid_t pid;int hi;}S[32];int ns=0;size_t dl=strlen(DEV);
        for(int i=0;i<nh&&ns<32;i++){
            if(!strncmp(H[i].name,DEV,dl))continue;
            int pfd[2];if(pipe(pfd))continue;
            pid_t p=fork();if(p==0){close(pfd[0]);char hp[256],port[8];ssh_parse(H[i].host,hp,port);
                char c[B*2];int l=ssh_pre(c,(int)sizeof c,H[i].pw,"-oConnectTimeout=5 -oStrictHostKeyChecking=no",port,hp);
                snprintf(c+l,(size_t)(sizeof(c)-(size_t)l)," 'uname -sr' 2>&1");
                char o[128];int r=pcmd(c,o,128);o[strcspn(o,"\n")]=0;
                if(!r&&o[0])(void)!write(pfd[1],o,strlen(o));close(pfd[1]);_exit(0);}
            close(pfd[1]);S[ns].fd=pfd[0];S[ns].pid=p;S[ns].hi=i;ns++;}
        for(int i=0;i<ns;i++){char o[128];int l=(int)read(S[i].fd,o,127);o[l>0?l:0]=0;close(S[i].fd);waitpid(S[i].pid,NULL,0);
            host_t*h=&H[S[i].hi];
            if(o[0]){ssh_savex(dir,h->name,h->host,h->pw,"OS",o);printf("✓ %s\n",h->name);}
            else printf("x %s\n",h->name);}
        return 0;}

    /* fleet view: enumerate remote tmux sessions in parallel, window per session */
    if((!strcmp(sub,"all")||!strcmp(sub,"*"))&&argc==3){
        struct{int fd;pid_t pid;int hi;}E[32];int ne=0;size_t dl=strlen(DEV);
        for(int i=0;i<nh;i++)if(strncmp(H[i].name,DEV,dl))ne++;
        printf("a ssh all: probing %d hosts...\n",ne);fflush(stdout);ne=0;
        for(int i=0;i<nh&&ne<32;i++){
            if(!strncmp(H[i].name,DEV,dl))continue;
            int pfd[2];if(pipe(pfd))continue;
            pid_t p=fork();
            if(p==0){close(pfd[0]);fcntl(pfd[1],F_SETFD,FD_CLOEXEC);alarm(20);
                char hp[256],port[8];ssh_parse(H[i].host,hp,port);
                char c[B*2];int l=ssh_pre(c,(int)sizeof(c),H[i].pw,"-oConnectTimeout=8 -oStrictHostKeyChecking=no",port,hp);
                snprintf(c+l,(size_t)(sizeof(c)-(size_t)l)," 'bash -lc \"tmux ls 2>/dev/null|cut -d: -f1\"' 2>/dev/null");
                char o[B];int r=pcmd(c,o,B);
                if(!r)(void)!write(pfd[1],o,strlen(o));
                close(pfd[1]);_exit(0);}
            close(pfd[1]);E[ne].fd=pfd[0];E[ne].pid=p;E[ne].hi=i;ne++;}
        int tot=0;
        for(int i=0;i<ne;i++){
            char o[B];int l=(int)read(E[i].fd,o,B-1);o[l>0?l:0]=0;
            close(E[i].fd);waitpid(E[i].pid,NULL,0);
            host_t*h=&H[E[i].hi];int any=0,ns=0;
            for(char*p=o,*nl=NULL;p&&*p;p=nl?nl+1:NULL){
                nl=strchr(p,'\n');if(nl)*nl=0;
                if(!*p)continue;any=1;ns++;
                char cm[B*2];snprintf(cm,sizeof(cm),"A_TMUX_SESSION='%s' a ssh %s",p,h->name);
                char wn[160];snprintf(wn,sizeof(wn),"%s/%s",h->name,p);
                tm_new(wn,HOME,cm);tot++;}
            if(!any){char cm[B*2];snprintf(cm,sizeof(cm),"a ssh %s",h->name);tm_new(h->name,HOME,cm);tot++;}
            if(any)printf("  \033[32m✓\033[0m %-25s %d session%s\n",h->name,ns,ns==1?"":"s");
            else printf("  \033[33m·\033[0m %-25s (no response, bare-ssh fallback)\n",h->name);
            fflush(stdout);}
        printf("→ %d window%s in a: session\n",tot,tot==1?"":"s");fflush(stdout);
        tm_go(NULL);return 0;}
    /* all/broadcast — parallel */
    if((!strcmp(sub,"all")||!strcmp(sub,"*"))&&argc>3){
        char cmd[B]="";ajoin(cmd,B,argc,argv,3);
        char qc[B];snprintf(qc,B," 'bash -c '\"'\"'export PATH=$HOME/.local/bin:$PATH; %s'\"'\"'' 2>&1",cmd);
        struct{int fd;pid_t pid;char nm[128];}S[32];int ns=0;
        for(int i=0;i<nh&&ns<32;i++){int pfd[2];if(pipe(pfd))continue;
            pid_t p=fork();if(p==0){close(pfd[0]);fcntl(pfd[1],F_SETFD,FD_CLOEXEC);alarm(20);char hp[256],port[8];ssh_parse(H[i].host,hp,port);
                char c[B*2];int l=ssh_pre(c,(int)sizeof c,H[i].pw,"-oConnectTimeout=5 -oStrictHostKeyChecking=no",port,hp);
                snprintf(c+l,(size_t)(sizeof(c)-(size_t)l),"%s",qc);char o[B];int r=pcmd(c,o,B);
                l=snprintf(c,B,"%c%s",r?'x':'+',o);(void)!write(pfd[1],c,(size_t)l);close(pfd[1]);_exit(0);}
            close(pfd[1]);snprintf(S[ns].nm,128,"%s",H[i].name);S[ns].fd=pfd[0];S[ns].pid=p;ns++;}
        for(int i=0;i<ns;i++){char o[B];int l=(int)read(S[i].fd,o,B-1);o[l>0?l:0]=0;close(S[i].fd);waitpid(S[i].pid,NULL,0);
            printf("\n%s %s\n",o[0]=='+'?"✓":"x",S[i].nm);if(o[1])printf("%s",o+1);}
        return 0;}
    /* resolve host by # or name */
    int idx=-1;
    if(isdigit((unsigned char)*sub))idx=atoi(sub);
    else{for(int i=0;i<nh;i++)if(!strcmp(H[i].name,sub)){idx=i;break;}}
    if(idx<0||idx>=nh){printf("x No host %s\n",sub);return 1;}
    char hp[256],port[8];ssh_parse(H[idx].host,hp,port);
    /* fast TCP probe; on fail switch to <name>-relay if it exists */
    if(!H[idx].jump[0]){char pb[B];const char*ph=strchr(hp,'@');ph=ph?ph+1:hp;
        snprintf(pb,B,"nc -z -w1 %s %s 2>/dev/null",ph,port);
        if(system(pb)){char rn[160];snprintf(rn,160,"%s-relay",H[idx].name);
            for(int i=0;i<nh;i++)if(!strcmp(H[i].name,rn)){idx=i;ssh_parse(H[idx].host,hp,port);break;}}}
    if(!H[idx].pw[0]){char tc[B];int l=ssh_pre(tc,B,"","-oBatchMode=yes -oConnectTimeout=3",port,hp);
        snprintf(tc+l,(size_t)(B-l)," true 2>/dev/null");
        if(system(tc)){char pw[256];printf("Password for %s: ",H[idx].name);
            if(fgets(pw,256,stdin)){pw[strcspn(pw,"\n")]=0;if(pw[0]){snprintf(H[idx].pw,256,"%s",pw);ssh_savex(dir,H[idx].name,H[idx].host,pw,0,0);}}}}
    {char cmd[B]="",c[B*3],cd[64]="",cs[B]="",opts[B];
        {char cwd[P];size_t hl=strlen(HOME);if(getcwd(cwd,P)&&!strncmp(cwd,HOME,hl)&&cwd[hl]=='/'){char*p=cwd+hl+1;char*s=strchr(p,'/');if(s)*s=0;if(*p&&*p!='.')snprintf(cd,64,"cd ~/%s 2>/dev/null;",p);}}
        for(int i=3;i<argc;i++){int l=(int)strlen(cmd);snprintf(cmd+l,(size_t)(B-l),"%s%s",l?" ":"",argv[i]);}
        if(cmd[0])snprintf(cs,B," 'bash -c '\"'\"'%sexport PATH=$HOME/.local/bin:$PATH; %s'\"'\"''",cd,cmd);
        else{const char*ts=getenv("A_TMUX_SESSION");char tx[256];
            if(ts&&ts[0])snprintf(tx,256,"tmux attach -t %s 2>/dev/null||tmux new -A -s %s",ts,ts);
            else snprintf(tx,256,"tmux attach 2>/dev/null||tmux new -A -s a");
            snprintf(cs,B," 'bash -lc \"%s\" 2>/dev/null||exec bash -l'",tx);}
        if(H[idx].jump[0]){char jhp[256],jport[8];ssh_parse(H[idx].jump,jhp,jport);
            snprintf(opts,B,"-tt -oConnectTimeout=8 -oStrictHostKeyChecking=accept-new -oProxyCommand=\"sshpass -p '%s' ssh -W %%h:%%p -p %s -oStrictHostKeyChecking=accept-new '%s'\"",H[idx].jpw,jport,jhp);
        }else snprintf(opts,B,"-tt -oConnectTimeout=2 -oStrictHostKeyChecking=accept-new");
        int n=ssh_pre(c,(int)sizeof(c),H[idx].pw,opts,port,hp);
        n+=snprintf(c+n,(size_t)(sizeof(c)-(size_t)n),"%s",cs);
        if(!cs[0])printf("Connecting to %s...\n",H[idx].name);
        if(cmd[0])alarm(30);
        execl("/bin/sh","sh","-c",c,(char*)NULL);_exit(127);}
}
