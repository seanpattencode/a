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
static int _notes_build(char*h,int cap){
    char nd[P];snprintf(nd,P,"%s/git/notes",AROOT);
    int hl=0;DIR*d=opendir(nd);if(!d)return 0;struct dirent*e;
    while((e=readdir(d))){if(e->d_name[0]=='.'||!strstr(e->d_name,".txt")||hl>cap-2048)continue;
        char fp[P];snprintf(fp,P,"%s/%s",nd,e->d_name);
        FILE*f=fopen(fp,"r");if(!f)continue;char ln[512];
        while(fgets(ln,512,f)){if(!strncmp(ln,"Text: ",6)){ln[strcspn(ln,"\n")]=0;
            hl+=snprintf(h+hl,(size_t)(cap-1-hl),"<div class=ni><button onclick=\"arcn('%s',this)\" class=nx>x</button><span>%s</span></div>",e->d_name,ln+6);break;}}
        fclose(f);}closedir(d);return hl;
}
static int _tasks_build(char*h,int cap){
    char td[P];snprintf(td,P,"%s/git/tasks",AROOT);
    DIR*d=opendir(td);if(!d)return snprintf(h,(size_t)cap,"<div style=\"color:#888\">No tasks</div>");
    struct dirent*e;struct{char pri[8];char txt[256];char name[256];}rows[512];int nr=0;
    while((e=readdir(d))&&nr<512){if(e->d_name[0]=='.')continue;
        int hp=strlen(e->d_name)>5&&e->d_name[5]=='-';
        for(int i=0;hp&&i<5;i++)if(!isdigit((unsigned char)e->d_name[i]))hp=0;
        snprintf(rows[nr].pri,8,"%s",hp?(char[6]){e->d_name[0],e->d_name[1],e->d_name[2],e->d_name[3],e->d_name[4],0}:"50000");
        snprintf(rows[nr].name,256,"%s",e->d_name);
        char slug[256];snprintf(slug,256,"%s",hp?e->d_name+6:e->d_name);
        for(char*u=strchr(slug,'_');u;u=strchr(u,'_'))*u=0;
        for(char*u=slug;*u;u++)if(*u=='-')*u=' ';
        rows[nr].txt[0]=0;
        char fp[P];snprintf(fp,P,"%s/%s",td,e->d_name);struct stat st;
        const char*probes[]={fp,NULL};char sp[P];if(!stat(fp,&st)&&S_ISDIR(st.st_mode)){snprintf(sp,P,"%s/task",fp);probes[0]=dexists(sp)?sp:fp;}
        DIR*sd=opendir(probes[0]);
        if(sd){struct dirent*se;while((se=readdir(sd))){if(!strstr(se->d_name,".txt"))continue;
            char sfp[P];snprintf(sfp,P,"%s/%s",probes[0],se->d_name);FILE*sf=fopen(sfp,"r");if(!sf)continue;
            char ln[512];while(fgets(ln,512,sf))if(!strncmp(ln,"Text: ",6)){ln[strcspn(ln,"\n")]=0;snprintf(rows[nr].txt,256,"%s",ln+6);break;}
            fclose(sf);if(rows[nr].txt[0])break;}closedir(sd);}
        if(!rows[nr].txt[0])snprintf(rows[nr].txt,256,"%s",slug);
        nr++;}
    closedir(d);
    for(int i=1;i<nr;i++){typeof(rows[0]) k=rows[i];int j=i-1;while(j>=0&&strcmp(rows[j].pri,k.pri)>0){rows[j+1]=rows[j];j--;}rows[j+1]=k;}
    int hl=0;for(int i=0;i<nr&&hl<cap-512;i++){
        const char*c=strcmp(rows[i].pri,"01000")<=0?"#f44":strcmp(rows[i].pri,"10000")<=0?"#fa0":"#aaa";
        hl+=snprintf(h+hl,(size_t)(cap-1-hl),"<div class=ni><button onclick=\"arct('%s',this)\" class=nx>x</button><span style=\"color:%s\">P%s</span> %.120s</div>",rows[i].name,c,rows[i].pri,rows[i].txt);}
    if(!hl)hl=snprintf(h,(size_t)cap,"<div style=\"color:#888\">No tasks</div>");
    return hl;
}
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
                else if(!strcmp(tag,"NO")){char*nb=malloc(131072);int nl2=_notes_build(nb,131072);EMIT(nb,nl2);free(nb);}
                else if(!strcmp(tag,"TO")){char*tb=malloc(131072);int tl2=_tasks_build(tb,131072);EMIT(tb,tl2);free(tb);}
                else if(!strcmp(tag,"JO")||!strcmp(tag,"MY")||!strcmp(tag,"MV")){/* empty */}
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
    static char req[262144];int n=(int)read(c,req,262143);if(n<=0)return;
    char*cl=strstr(req,"Content-Length:"),*bb=strstr(req,"\r\n\r\n");
    if(cl&&bb){int want=(int)(bb-req+4)+atoi(cl+15);
        while(n<want&&n<262143){int r=(int)read(c,req+n,want-n);if(r<=0)break;n+=r;}}
    req[n]=0;
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
        int cap=524288;char*html=malloc((size_t)cap);if(!html)return;
        int hl=_notes_build(html,cap);_sresp(c,200,"text/html",html,hl);free(html);return;}
    if(!strncmp(req,"GET /api/tasks",14)){
        int cap=524288;char*html=malloc((size_t)cap);if(!html)return;
        int hl=_tasks_build(html,cap);_sresp(c,200,"text/html",html,hl);free(html);return;}
    if(!strncmp(req,"POST /api/note/archive",22)||!strncmp(req,"POST /api/task/archive",22)){
        int isn=req[18]=='n';char*body=strstr(req,"\r\n\r\n");if(!body){_sresp(c,400,"text/plain","bad",3);return;}
        char*k=strstr(body+4,isn?"\"f\":\"":"\"d\":\"");if(!k){_sresp(c,400,"text/plain","no name",7);return;}
        k+=5;char name[256];int ni=0;while(k[ni]&&k[ni]!='"'&&ni<255){name[ni]=k[ni];ni++;}name[ni]=0;
        for(char*p=name;*p;p++)if(*p=='/'){_sresp(c,400,"text/plain","bad name",8);return;}
        char src[P],dst[P],ad[P];
        snprintf(ad,P,"%s/git/%s/.archive",AROOT,isn?"notes":"tasks");mkdir(ad,0755);
        snprintf(src,P,"%s/git/%s/%s",AROOT,isn?"notes":"tasks",name);
        snprintf(dst,P,"%s/%s",ad,name);rename(src,dst);
        _sresp(c,200,"text/plain","ok",2);return;}
    if(!strncmp(req,"GET /op",7)&&(req[7]==' '||req[7]=='?'||req[7]=='\r')){
        static const char H[]="<!doctype html><style>body,#m{display:flex;flex-direction:column;margin:0}body{background:#000;color:#fff;font:16px system-ui;height:100vh}#m{flex:1;overflow:auto;padding:16px}#m>div{margin:6px 0;padding:10px 14px;border-radius:14px;max-width:75%;white-space:pre-wrap;background:#1f2937}.u{background:#1e3a8a!important;align-self:flex-end}</style><div id=m></div><form id=f style=display:flex;gap:4px;margin:8px><button type=button id=n style=padding:14px;background:#1f2937;color:#fff;border:1px solid #333;border-radius:10px;font:inherit;cursor:pointer title=\"new operator\">+op</button><input id=i autofocus placeholder=\"talk to a\" style=flex:1;padding:14px;background:#000;color:#fff;border:1px solid #333;border-radius:10px;font:inherit;outline:none></form><script>W=new URLSearchParams(location.search).get('w')||'';S=v=>{let a=document.createElement('div');if(v){let u=document.createElement('div');u.className='u';u.textContent=v;m.appendChild(u)}m.appendChild(a);m.scrollTop=1e9;fetch('/op/msg',{method:'POST',body:'w='+W+'&q='+encodeURIComponent(v||'')}).then(async r=>{const rd=r.body.getReader(),dc=new TextDecoder();let b='';for(;;){const{done,value}=await rd.read();if(done)break;b+=dc.decode(value,{stream:true});const k=b.lastIndexOf('\f');if(k>=0){a.textContent=b.slice(k+1);m.scrollTop=1e9}}if(!v&&!a.textContent){a.remove();setTimeout(()=>S(''),400)}})};n.onclick=()=>{let d=document.createElement('div');d.textContent='+ spawning...';m.appendChild(d);m.scrollTop=1e9;fetch('/op/new',{method:'POST'}).then(r=>r.text()).then(t=>{d.textContent='+ '+t;if(t)window.open('/op?w='+t,'_blank')})};f.onsubmit=e=>{e.preventDefault();S(i.value.trim());i.value=''};S('')</script>";
        _sresp(c,200,"text/html",H,sizeof H-1);return;}
    if(!strncmp(req,"POST /op/new",12)){
        char tc[B];snprintf(tc,B,"cd %s&&PATH=$HOME/.local/bin:$PATH nohup a o </dev/null >/dev/null 2>&1 & echo $!",SDIR);
        FILE*p=popen(tc,"r");char pid[32]={0};
        if(p){(void)!fgets(pid,32,p);pclose(p);pid[strcspn(pid,"\n")]=0;}
        const char*bn=strrchr(SDIR,'/');bn=bn?bn+1:SDIR;
        char nm[64];int nl=snprintf(nm,64,"op-%s-%s",bn,pid);
        _sresp(c,200,"text/plain",nm,(size_t)nl);return;}
    if(!strncmp(req,"POST /op/msg",12)){
        char*body=strstr(req,"\r\n\r\n");if(!body){_sresp(c,400,"text/plain","bad",3);return;}
        body+=4;char*q=strstr(body,"q=");if(!q){_sresp(c,400,"text/plain","no q",4);return;}
        q+=2;static char msg[131072];int mi=0;
        for(;q[mi]&&q[mi]!='&'&&mi<131070;mi++){
            if(q[mi]=='+')msg[mi]=' ';
            else if(q[mi]=='%'&&q[mi+1]&&q[mi+2]){char h[3]={q[mi+1],q[mi+2],0};msg[mi]=(char)strtol(h,NULL,16);q+=2;}
            else msg[mi]=q[mi];}msg[mi]=0;
        /* w=<window> pins to specific op; else newest op-* (tail -1). spawn if none. */
        char opn[64]={0};char*wp=strstr(body,"w=");
        if(wp){wp+=2;int wi=0;while(wp[wi]&&wp[wi]!='&'&&wi<63)opn[wi]=wp[wi],wi++;opn[wi]=0;}
        #define OPN_LS do{FILE*_l=popen("tmux list-windows -t a -F '#W' 2>/dev/null|grep '^op-'|tail -1","r");\
            if(_l){(void)!fgets(opn,64,_l);pclose(_l);opn[strcspn(opn,"\n")]=0;}}while(0)
        if(opn[0]){char hk[128];snprintf(hk,128,"tmux has-session -t a:%s 2>/dev/null",opn);for(int i=0;i<15&&system(hk);i++)sleep(1);}
        if(!opn[0])OPN_LS;
        if(!opn[0]){
            char tc[B];snprintf(tc,B,"cd %s&&PATH=$HOME/.local/bin:$PATH nohup a o </dev/null >/dev/null 2>&1 &",SDIR);(void)!system(tc);
            for(int i=0;i<15&&!opn[0];i++){sleep(1);OPN_LS;}
            sleep(4);}
        #define CAP 65536
        static char cur[CAP],cln[CAP],last[CAP];last[0]=0;
        #define DRAIN(buf) do{char _c[256];snprintf(_c,256,"tmux capture-pane -t a:%s.0 -p -S -200",opn);\
            FILE*pf=popen(_c,"r");size_t cl=0;\
            if(pf){size_t r;while(cl<CAP-1&&(r=fread(buf+cl,1,CAP-1-cl,pf)))cl+=r;pclose(pf);}buf[cl]=0;}while(0)
        /* FIFO + poll: native event-driven, no inotify watch limit */
        mkfifo("/tmp/op_a.fifo",0644);
        {char pc[256];snprintf(pc,256,"tmux pipe-pane -t a:%s.0;tmux pipe-pane -t a:%s.0 'cat >/tmp/op_a.fifo'",opn,opn);(void)!system(pc);}
        int ifd=open("/tmp/op_a.fifo",O_RDWR|O_NONBLOCK);
        if(mi){int bf=open("/tmp/op_a.buf",O_WRONLY|O_CREAT|O_TRUNC,0644);
            if(bf>=0){(void)!write(bf,msg,(size_t)mi);close(bf);
                char sc[256];snprintf(sc,256,"tmux load-buffer /tmp/op_a.buf&&tmux paste-buffer -t a:%s.0&&tmux send-keys -t a:%s.0 Enter",opn,opn);(void)!system(sc);}}
        /* event-driven: pipe-pane fires fifo → capture → emit delta. no polling. */
        static const char SH[]="HTTP/1.1 200 OK\r\nContent-Type:text/plain\r\nTransfer-Encoding:chunked\r\nCache-Control:no-store\r\n\r\n";
        (void)!write(c,SH,sizeof SH-1);
        int last_len=0;
        #define EMIT do{DRAIN(cur);\
            char*r=cur,*p=NULL,*s=cur;while(*msg&&(s=strstr(s,msg)))p=s,s+=strlen(msg);\
            if(p)r=p+strlen(msg);int ci=0;\
            for(int i=0;r[i]&&ci<CAP-1;i++){\
                if(r[i]==0x1b&&r[i+1]=='['){i+=2;while(r[i]&&!isalpha((unsigned char)r[i]))i++;}\
                else if(r[i]=='\n'&&(unsigned char)r[i+1]==0xE2&&(unsigned char)r[i+2]==0x94)break;\
                else if((unsigned char)r[i]==0xE2&&(unsigned char)r[i+1]==0x97&&(unsigned char)r[i+2]==0x8F&&r[i+3]==' ')i+=3;\
                else cln[ci++]=r[i];}\
            cln[ci]=0;\
            if(ci!=last_len||memcmp(cln,last,(size_t)ci)){\
                char hh[16];int hl=snprintf(hh,16,"%x\r\n",ci+1);\
                (void)!write(c,hh,(size_t)hl);(void)!write(c,"\f",1);(void)!write(c,cln,(size_t)ci);(void)!write(c,"\r\n",2);\
                memcpy(last,cln,(size_t)ci);last_len=ci;}}while(0)
        EMIT;
        struct pollfd pf={.fd=ifd,.events=POLLIN};
        for(;;){int n=poll(&pf,1,mi?30000:3000);
            if(n<=0)break;
            char b[4096];while(read(ifd,b,4096)>0);
            EMIT;}
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
