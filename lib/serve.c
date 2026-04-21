/* a serve [port] — pure C HTTP server for UI. no python dependency. */
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#ifndef __APPLE__
#include <pty.h>
#else
#include <util.h>
#endif
static void _sha1(const unsigned char*d,size_t n,unsigned char out[20]){
    uint32_t h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
    size_t pl=((56-((n+1)%64))%64),tl=n+1+pl+8;
    unsigned char*m=calloc(tl,1);memcpy(m,d,n);m[n]=0x80;
    size_t ml=n*8;for(int i=0;i<8;i++)m[tl-1-i]=(unsigned char)(ml>>(i*8));
    for(size_t i=0;i<tl;i+=64){
        uint32_t w[80],a=h0,b=h1,c=h2,dd2=h3,e=h4;
        for(int j=0;j<16;j++)w[j]=(uint32_t)(m[i+j*4]<<24|m[i+j*4+1]<<16|m[i+j*4+2]<<8|m[i+j*4+3]);
        for(int j=16;j<80;j++){uint32_t t=w[j-3]^w[j-8]^w[j-14]^w[j-16];w[j]=(t<<1)|(t>>31);}
        for(int j=0;j<80;j++){uint32_t f,k;
            if(j<20){f=(b&c)|((~b)&dd2);k=0x5A827999;}else if(j<40){f=b^c^dd2;k=0x6ED9EBA1;}
            else if(j<60){f=(b&c)|(b&dd2)|(c&dd2);k=0x8F1BBCDC;}else{f=b^c^dd2;k=0xCA62C1D6;}
            uint32_t t=((a<<5)|(a>>27))+f+e+k+w[j];e=dd2;dd2=c;c=(b<<30)|(b>>2);b=a;a=t;}
        h0+=a;h1+=b;h2+=c;h3+=dd2;h4+=e;}
    free(m);uint32_t hh[]={h0,h1,h2,h3,h4};
    for(int i=0;i<5;i++)for(int j=0;j<4;j++)out[i*4+j]=(unsigned char)(hh[i]>>(24-j*8));
}
static char*_shtml;static int _shlen;
static void _html_gen(void){
    /* read HTML template from ui_full.py */
    char tf[P];snprintf(tf,P,"%s/lib/ui/ui_full.py",SDIR);
    char*src=readf(tf,NULL);if(!src)return;
    char*s=strstr(src,"'''<!doctype");char*e=s?strstr(s+3,"'''"):NULL;
    if(!s||!e){free(src);return;}
    s+=3;*e=0;
    /* build commands JSON from a i */
    char cmds[4096]="[]";
    {char out[8192];int pp[2];pipe(pp);pid_t ch=fork();
    if(!ch){dup2(pp[1],1);close(pp[0]);close(pp[1]);execl("/proc/self/exe","a","i",(char*)0);_exit(1);}
    close(pp[1]);int ol=0;{int r;while((r=(int)read(pp[0],out+ol,(size_t)(8191-ol)))>0)ol+=r;}
    close(pp[0]);waitpid(ch,NULL,0);out[ol]=0;
    /* parse tab-separated lines into JSON array */
    int cl=1;cmds[0]='[';
    for(char*l=out;*l;){char*nl=strchr(l,'\n');if(nl)*nl=0;
        char*tab=strchr(l,'\t');char*name=l,*desc="";
        if(tab){*tab=0;desc=tab+1;}
        while(*name==' ')name++;
        /* skip project/dir lines */
        if(*name&&!strstr(desc,"dir")&&!strstr(desc,"project")){
            if(cl>1)cmds[cl++]=',';
            cl+=snprintf(cmds+cl,(size_t)(4095-cl),"[\"%s\",\"%s\"]",name,desc);}
        l=nl?nl+1:l+strlen(l);}
    cmds[cl++]=']';cmds[cl]=0;}
    /* substitute placeholders */
    int cap=131072;_shtml=malloc((size_t)cap);_shlen=0;
    #define EMIT(p,n) {if(_shlen+(n)>=cap){cap*=2;_shtml=realloc(_shtml,(size_t)cap);}memcpy(_shtml+_shlen,p,(size_t)(n));_shlen+=(n);}
    for(char*p=s;*p;){
        if(*p=='_'&&p[1]=='_'){
            char*end=strstr(p+2,"__");
            if(end&&(end-p)<16){
                char tag[16];memcpy(tag,p+2,(size_t)(end-p-2));tag[end-p-2]=0;
                if(!strcmp(tag,"CMDS")){EMIT(cmds,(int)strlen(cmds));}
                else if(!strcmp(tag,"PO")){EMIT("<option value=\"\">~ (home)</option>",(int)strlen("<option value=\"\">~ (home)</option>"));}
                else if(!strcmp(tag,"DO")){EMIT("<option value=\"\">local</option>",(int)strlen("<option value=\"\">local</option>"));}
                else if(!strcmp(tag,"JO")||!strcmp(tag,"NO")||!strcmp(tag,"MY")||!strcmp(tag,"MV")){/* empty */}
                else{EMIT(p,(int)(end+2-p));p=end+2;continue;}
                p=end+2;continue;}}
        EMIT(p,1);p++;
    }
    #undef EMIT
    _shtml[_shlen]=0;free(src);
}
static void _sresp(int c,int code,const char*ct,const char*body,int bl){
    char h[256];int hl=snprintf(h,256,"HTTP/1.1 %d OK\r\nContent-Type:%s\r\nContent-Length:%d\r\nConnection:close\r\nCache-Control:no-store\r\nAccess-Control-Allow-Origin:*\r\n\r\n",code,ct,bl);
    (void)!write(c,h,(size_t)hl);if(bl)(void)!write(c,body,(size_t)bl);
}
static int _ws_upgrade(int c,const char*req){
    const char*k=strstr(req,"Sec-WebSocket-Key: ");if(!k)return 0;
    k+=19;char key[64];int i=0;while(k[i]&&k[i]!='\r'&&i<60){key[i]=k[i];i++;}
    snprintf(key+i,64-i,"258EAFA5-E914-47DA-95CA-5AB5DC595305");
    unsigned char sha[20];_sha1((unsigned char*)key,(size_t)(i+36),sha);
    static const char*b64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char acc[32];int j=0;
    for(int p=0;p<18;p+=3){unsigned v=(unsigned)(sha[p]<<16|sha[p+1]<<8|sha[p+2]);
        acc[j++]=b64[v>>18&63];acc[j++]=b64[v>>12&63];acc[j++]=b64[v>>6&63];acc[j++]=b64[v&63];}
    {unsigned v=(unsigned)(sha[18]<<16|sha[19]<<8);acc[j++]=b64[v>>18&63];acc[j++]=b64[v>>12&63];acc[j++]=b64[v>>6&63];acc[j++]='=';}
    acc[j]=0;
    char r[256];int rl=snprintf(r,256,"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",acc);
    (void)!write(c,r,(size_t)rl);return 1;
}
static void _ws_send(int c,const char*d,int n){
    unsigned char h[10];int hl=2;h[0]=0x81;
    if(n<126){h[1]=(unsigned char)n;}else{h[1]=126;h[2]=(unsigned char)(n>>8);h[3]=(unsigned char)(n&0xFF);hl=4;}
    (void)!write(c,h,(size_t)hl);(void)!write(c,d,(size_t)n);
}
static int _ws_recv(int c,char*buf,int bsz){
    unsigned char h[2];if(read(c,h,2)!=2)return -1;
    int op=h[0]&0x0F,mask=h[1]&0x80,len=h[1]&0x7F;
    if(op==8)return -1;
    if(len==126){unsigned char e[2];(void)!read(c,e,2);len=(e[0]<<8)|e[1];}
    if(len>=bsz)len=bsz-1;
    unsigned char mk[4]={0,0,0,0};if(mask)(void)!read(c,mk,4);
    (void)!read(c,buf,(size_t)len);
    if(mask)for(int i=0;i<len;i++)buf[i]^=(char)mk[i%4];
    buf[len]=0;return len;
}
static void _ws_term(int c){
    int m,s;if(openpty(&m,&s,NULL,NULL,NULL)<0)return;
    pid_t p=fork();
    if(!p){close(m);setsid();ioctl(s,TIOCSCTTY,0);dup2(s,0);dup2(s,1);dup2(s,2);close(s);
        setenv("TERM","xterm-256color",0);
        const char*tb=getenv("TMUX_BIN");
        if(tb)execl(tb,"tmux","new-session","-A","-s","main",(char*)0);
        char*a[]={"tmux","new-session","-A","-s","main",NULL};execvp("tmux",a);
        char*b[]={"bash","-l",NULL};execvp("bash",b);
        char*cc[]={"sh","-l",NULL};execvp("sh",cc);execl("/system/bin/sh","sh",(char*)0);_exit(1);}
    close(s);
    struct pollfd pf[2]={{c,POLLIN,0},{m,POLLIN,0}};char buf[4096];
    while(poll(pf,2,-1)>0){
        if(pf[1].revents&POLLIN){int n=(int)read(m,buf,4096);if(n<=0)break;_ws_send(c,buf,n);}
        if(pf[0].revents&POLLIN){int n=_ws_recv(c,buf,4096);if(n<0)break;
            if(buf[0]=='{'){char*co=strstr(buf,"\"cols\":");char*ro=strstr(buf,"\"rows\":");
                if(co&&ro){struct winsize w={.ws_row=(unsigned short)atoi(ro+6),.ws_col=(unsigned short)atoi(co+6)};ioctl(m,TIOCSWINSZ,&w);continue;}}
            (void)!write(m,buf,(size_t)n);}
        if(pf[0].revents&(POLLHUP|POLLERR)||pf[1].revents&(POLLHUP|POLLERR))break;
    }
    kill(p,SIGHUP);close(m);waitpid(p,NULL,0);
}
static void _handle(int c){
    char req[8192];int n=(int)read(c,req,8191);if(n<=0)return;req[n]=0;
    int one=1;setsockopt(c,IPPROTO_TCP,TCP_NODELAY,&one,4);
    if(!strncmp(req,"GET / ",6)||!strncmp(req,"GET /jobs",9)||!strncmp(req,"GET /note ",10)||!strncmp(req,"GET /tasks",10)||!strncmp(req,"GET /term",9)){
        if(_shtml)_sresp(c,200,"text/html",_shtml,_shlen);else _sresp(c,503,"text/plain","starting",8);return;}
    if(!strncmp(req,"GET /ws",7)&&(strstr(req,"Upgrade: websocket")||strstr(req,"upgrade: websocket"))){if(_ws_upgrade(c,req))_ws_term(c);return;}
    if(!strncmp(req,"GET /api/u-status",17)){_sresp(c,200,"application/json","{\"ok\":true}",11);return;}
    if(!strncmp(req,"POST /api/omni",14)||!strncmp(req,"POST /note",10)){
        char*body=strstr(req,"\r\n\r\n");if(!body){_sresp(c,400,"text/plain","bad",3);return;}
        body+=4;
        /* POST /note: extract c= param, run a note */
        int isnote=!strncmp(req,"POST /note",10);
        char*q=strstr(body,isnote?"c=":"q=");if(!q){_sresp(c,400,"text/plain","no param",8);return;}
        q+=2;char cmd[512];int ci=0;
        for(;q[ci]&&q[ci]!='&'&&ci<510;ci++){
            if(q[ci]=='+')cmd[ci]=' ';
            else if(q[ci]=='%'&&q[ci+1]&&q[ci+2]){char h[3]={q[ci+1],q[ci+2],0};cmd[ci]=(char)strtol(h,NULL,16);q+=2;}
            else cmd[ci]=q[ci];}cmd[ci]=0;
        int pp[2];pipe(pp);pid_t ch=fork();
        if(!ch){close(pp[0]);dup2(pp[1],1);dup2(pp[1],2);close(pp[1]);
            signal(SIGALRM,SIG_DFL);signal(SIGPIPE,SIG_DFL);
            if(isnote)execlp("a","a","note",cmd,(char*)0);
            else{/* split cmd into args */
                char*args[32]={"a"};int ac=1;char*p2=cmd;
                while(*p2&&ac<31){while(*p2==' ')p2++;if(!*p2)break;args[ac++]=p2;while(*p2&&*p2!=' ')p2++;if(*p2)*p2++=0;}
                args[ac]=NULL;execvp("a",args);}
            _exit(1);}
        close(pp[1]);char out[8192];int ol=0;
        {int r;while((r=(int)read(pp[0],out+ol,(size_t)(8191-ol)))>0)ol+=r;}
        close(pp[0]);waitpid(ch,NULL,0);out[ol]=0;
        if(isnote){char np[P];snprintf(np,P,"%s/git/notes",AROOT);_sresp(c,200,"text/plain",np,(int)strlen(np));}
        else{char resp[16384];int rl=snprintf(resp,16384,"<pre style=\"color:#fff\">%.*s</pre>",ol,out);
            _sresp(c,200,"text/html",resp,rl);}
        return;}
    if(!strncmp(req,"GET /note-list",14)){
        char nd[P];snprintf(nd,P,"%s/git/notes",AROOT);
        int cap=524288;char*html=malloc((size_t)cap);if(!html)return;int hl=0;
        DIR*d=opendir(nd);struct dirent*e;
        if(d){while((e=readdir(d))){if(e->d_name[0]=='.'||!strstr(e->d_name,".txt")||hl>cap-2048)continue;
            char fp[P];snprintf(fp,P,"%s/%s",nd,e->d_name);
            FILE*f=fopen(fp,"r");if(!f)continue;char ln[512];
            while(fgets(ln,512,f)){if(!strncmp(ln,"Text: ",6)){ln[strcspn(ln,"\n")]=0;
                hl+=snprintf(html+hl,(size_t)(cap-1-hl),"<div class=ni><span>%s</span></div>",ln+6);break;}}
            fclose(f);}closedir(d);}
        _sresp(c,200,"text/html",html,hl);free(html);return;}
    if(!strncmp(req,"GET /op",7)&&(req[7]==' '||req[7]=='?'||req[7]=='\r')){
        static const char H[]="<!doctype html><style>body,#m{display:flex;flex-direction:column;margin:0}body{background:#000;color:#fff;font:16px system-ui;height:100vh}#m{flex:1;overflow:auto;padding:16px}#m>div{margin:6px 0;padding:10px 14px;border-radius:14px;max-width:75%;white-space:pre-wrap;background:#1f2937}.u{background:#1e3a8a!important;align-self:flex-end}#i{margin:8px;padding:14px;background:#000;color:#fff;border:1px solid #333;border-radius:10px;font:inherit;width:calc(100% - 16px);box-sizing:border-box;outline:none}</style><div id=m></div><form id=f><input id=i autofocus placeholder=\"talk to a\"></form><script>S=v=>{let a=document.createElement('div');if(v){let u=document.createElement('div');u.className='u';u.textContent=v;m.appendChild(u)}m.appendChild(a);m.scrollTop=1e9;fetch('/op/msg',{method:'POST',body:'q='+encodeURIComponent(v||'')}).then(async r=>{const rd=r.body.getReader(),dc=new TextDecoder();let b='';for(;;){const{done,value}=await rd.read();if(done)break;b+=dc.decode(value,{stream:true});const k=b.lastIndexOf('\f');if(k>=0){a.textContent=b.slice(k+1);m.scrollTop=1e9}}})};f.onsubmit=e=>{e.preventDefault();S(i.value.trim());i.value=''};S('')</script>";
        _sresp(c,200,"text/html",H,sizeof H-1);return;}
    if(!strncmp(req,"POST /op/msg",12)){
        char*body=strstr(req,"\r\n\r\n");if(!body){_sresp(c,400,"text/plain","bad",3);return;}
        body+=4;char*q=strstr(body,"q=");if(!q){_sresp(c,400,"text/plain","no q",4);return;}
        q+=2;char msg[2048];int mi=0;
        for(;q[mi]&&q[mi]!='&'&&mi<2046;mi++){
            if(q[mi]=='+')msg[mi]=' ';
            else if(q[mi]=='%'&&q[mi+1]&&q[mi+2]){char h[3]={q[mi+1],q[mi+2],0};msg[mi]=(char)strtol(h,NULL,16);q+=2;}
            else msg[mi]=q[mi];}msg[mi]=0;
        /* sends to live `a o` tmux window (op-a) — same session user attaches with `a o` */
        if(system("tmux has-session -t a:op-a 2>/dev/null")){
            char tc[B];snprintf(tc,B,"cd %s&&PATH=$HOME/.local/bin:$PATH nohup a o </dev/null >/dev/null 2>&1 &",SDIR);(void)!system(tc);
            for(int i=0;i<15&&system("tmux has-session -t a:op-a 2>/dev/null");i++)sleep(1);
            sleep(4);}
        #define CAP 65536
        static char cur[CAP],cln[CAP],last[CAP];last[0]=0;
        #define DRAIN(buf) do{FILE*pf=popen("tmux capture-pane -t a:op-a.0 -p -S -200","r");size_t cl=0;\
            if(pf){size_t r;while(cl<CAP-1&&(r=fread(buf+cl,1,CAP-1-cl,pf)))cl+=r;pclose(pf);}buf[cl]=0;}while(0)
        /* FIFO + poll: native event-driven, no inotify watch limit */
        mkfifo("/tmp/op_a.fifo",0644);
        (void)!system("tmux pipe-pane -t a:op-a.0;tmux pipe-pane -t a:op-a.0 'cat >/tmp/op_a.fifo'");
        int ifd=open("/tmp/op_a.fifo",O_RDWR|O_NONBLOCK);
        if(mi){pid_t pid=fork();
            if(!pid){execlp("tmux","tmux","send-keys","-l","-t","a:op-a.0","--",msg,(char*)0);_exit(1);}
            waitpid(pid,NULL,0);(void)!system("tmux send-keys -t a:op-a.0 Enter");}
        /* stream chunked: each inotify event → capture → emit delta. ms-level UX. */
        static const char SH[]="HTTP/1.1 200 OK\r\nContent-Type:text/plain\r\nTransfer-Encoding:chunked\r\nCache-Control:no-store\r\n\r\n";
        (void)!write(c,SH,sizeof SH-1);
        struct pollfd pf={.fd=ifd,.events=POLLIN};int got=0,last_len=0;
        for(;;){int to=got?1500:(mi?30000:50);int n=poll(&pf,1,to);
            if(n>0){char b[4096];while(read(ifd,b,4096)>0)got=1;}
            DRAIN(cur);
            char*r=cur,*p=NULL,*s=cur;while(*msg&&(s=strstr(s,msg)))p=s,s+=strlen(msg);
            if(p)r=p+strlen(msg);int ci=0;
            for(int i=0;r[i]&&ci<CAP-1;i++){
                if(r[i]==0x1b&&r[i+1]=='['){i+=2;while(r[i]&&!isalpha((unsigned char)r[i]))i++;}
                else if(r[i]=='\n'&&(unsigned char)r[i+1]==0xE2&&(unsigned char)r[i+2]==0x94)break;
                else if((unsigned char)r[i]==0xE2&&(unsigned char)r[i+1]==0x97&&(unsigned char)r[i+2]==0x8F&&r[i+3]==' ')i+=3;
                else cln[ci++]=r[i];}
            cln[ci]=0;
            int diff=ci!=last_len||memcmp(cln,last,(size_t)ci);
            if(diff){
                char hh[16];int hl=snprintf(hh,16,"%x\r\n",ci+1);
                (void)!write(c,hh,(size_t)hl);(void)!write(c,"\f",1);(void)!write(c,cln,(size_t)ci);(void)!write(c,"\r\n",2);
                memcpy(last,cln,(size_t)ci);last_len=ci;}
            if(n==0)break;}
        close(ifd);(void)!write(c,"0\r\n\r\n",5);return;}
    _sresp(c,404,"text/plain","not found",9);
}
static int cmd_serve(int argc,char**argv){perf_disarm();signal(SIGPIPE,SIG_IGN);
    {const char*op=getenv("PATH")?:"";char np[P];snprintf(np,P,"%s/.local/bin:/opt/homebrew/bin:/usr/local/bin:%s",HOME,op);setenv("PATH",np,1);}
    int port=argc>2?atoi(argv[2]):1111;
    (void)!system("pkill -f 'ui.ui_full\\|ui_full.py' 2>/dev/null");usleep(200000);
    printf("> generating HTML...\n");_html_gen();
    if(!_shtml){puts("x HTML generation failed");return 1;}
    printf("+ %d bytes cached\n",_shlen);
    int fd=socket(AF_INET,SOCK_STREAM,0);
    int one=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,4);
    struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)port),.sin_addr={htonl(INADDR_ANY)}};
    if(bind(fd,(void*)&a,sizeof a)<0){perror("bind");free(_shtml);return 1;}
    listen(fd,64);printf("+ http://localhost:%d (C server, pid %d)\n",port,(int)getpid());
    for(;;){int c=accept(fd,0,0);if(c<0)continue;
        struct timeval tv={2,0};setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
        char peek[8];ssize_t pn=recv(c,peek,7,MSG_PEEK);
        if(pn>0&&!strncmp(peek,"GET /ws",7)){if(!fork()){close(fd);_handle(c);close(c);_exit(0);}close(c);}
        else{_handle(c);close(c);}}
}
