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
#define SYNC_HTML "<span style=color:#888>sync <span class=sa>%s</span></span> <button style=\"background:#000;color:#888;border:1px solid #333;padding:0 6px;font:inherit;cursor:pointer\" onclick=\"fetch('/api/sync',{method:'POST'});let p=setInterval(()=>fetch('/api/sync-status').then(r=>r.text()).then(t=>{document.querySelectorAll('.sa').forEach(s=>s.textContent=t);if(t!='syncing')clearInterval(p)}),1000)\">sync</button>"
static int _ncmp(const void*a,const void*b){const char*x=strrchr((const char*)a,'_'),*y=strrchr((const char*)b,'_');return strcmp(y?y:"",x?x:"");}
static int _notes_build(char*h,int cap){
    char nd[P];snprintf(nd,P,"%s/git/notes",AROOT);
    int hl=snprintf(h,(size_t)cap,SYNC_HTML,sync_age());
    DIR*d=opendir(nd);if(!d)return hl;struct dirent*e;
    static char names[2048][64];int nn=0;
    while((e=readdir(d))&&nn<2048){if(e->d_name[0]=='.'||!strstr(e->d_name,".txt"))continue;
        snprintf(names[nn++],64,"%s",e->d_name);}closedir(d);
    qsort(names,(size_t)nn,sizeof(names[0]),_ncmp);
    for(int i=0;i<nn&&hl<cap-2048;i++){
        char fp[P];snprintf(fp,P,"%s/%s",nd,names[i]);
        FILE*f=fopen(fp,"r");if(!f)continue;char ln[512];
        while(fgets(ln,512,f)){if(!strncmp(ln,"Text: ",6)){ln[strcspn(ln,"\n")]=0;
            hl+=snprintf(h+hl,(size_t)(cap-1-hl),"<div class=ni><button onclick=\"arcn('%s',this)\" class=nx>x</button><span>%s</span></div>",names[i],ln+6);break;}}
        fclose(f);}return hl;
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
    int hl=snprintf(h,(size_t)cap,SYNC_HTML,sync_age());
    for(int i=0;i<nr&&hl<cap-512;i++){
        const char*c=strcmp(rows[i].pri,"01000")<=0?"#f44":strcmp(rows[i].pri,"10000")<=0?"#fa0":"#aaa";
        hl+=snprintf(h+hl,(size_t)(cap-1-hl),"<div class=ni><button onclick=\"arct('%s',this)\" class=nx>x</button><span style=\"color:%s\">P%s</span> %.120s</div>",rows[i].name,c,rows[i].pri,rows[i].txt);}
    if(!hl)hl=snprintf(h,(size_t)cap,"<div style=\"color:#888\">No tasks</div>");
    return hl;
}
static void _html_gen(void){
    char tf[P];snprintf(tf,P,"%s/lib/ui_full.html",SDIR);
    char*src=readf(tf,NULL);if(!src)return;
    char*s=src;
    /* build commands JSON from a i */
    char cmds[65536]="[]";
    {char out[65536];int pp[2];pipe(pp);pid_t ch=fork();
    if(!ch){dup2(pp[1],1);close(pp[0]);close(pp[1]);execlp("a","a","i",(char*)0);_exit(1);}
    close(pp[1]);int ol=0;{int r;while((r=(int)read(pp[0],out+ol,(size_t)(65535-ol)))>0)ol+=r;}
    close(pp[0]);waitpid(ch,NULL,0);out[ol]=0;
    /* parse tab-separated lines into JSON array */
    int cl=1;cmds[0]='[';
    for(char*l=out;*l;){char*nl=strchr(l,'\n');if(nl)*nl=0;
        char*tab=strchr(l,'\t');char*name=l,*desc="";
        if(tab){*tab=0;desc=tab+1;}
        while(*name==' ')name++;
        if(*name&&cl<65000){
            if(cl>1)cmds[cl++]=',';
            cl+=snprintf(cmds+cl,(size_t)(65535-cl),"[\"%s\",\"%s\"]",name,desc);}
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
static void _sresph(int c,int code,const char*ct,const char*body,int bl,const char*cache){
    char h[256];int hl=snprintf(h,256,"HTTP/1.1 %d OK\r\nContent-Type:%s\r\nContent-Length:%d\r\nConnection:close\r\nCache-Control:%s\r\nAccess-Control-Allow-Origin:*\r\n\r\n",code,ct,bl,cache);
    (void)!write(c,h,(size_t)hl);if(bl)(void)!write(c,body,(size_t)bl);
}
/* static doc pages: NOT no-store, so the browser back/forward cache restores them instantly (0ms, no refetch) */
static void _sresp(int c,int code,const char*ct,const char*body,int bl){_sresph(c,code,ct,body,bl,"no-store");}
static void _sdoc(int c,const char*body,int bl){_sresph(c,200,"text/html; charset=utf-8",body,bl,"no-cache");}
static int _ws_upgrade(int c,const char*req){
    const char*k=strstr(req,"Sec-WebSocket-Key: ");if(!k)return 0;
    k+=19;char key[64];int i=0;while(k[i]&&k[i]!='\r'&&i<60){key[i]=k[i];i++;}
    snprintf(key+i,64-i,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
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
static void _ws_term(int c,const char*target){
    int m,s;if(openpty(&m,&s,NULL,NULL,NULL)<0)return;
    pid_t p=fork();
    if(!p){close(m);setsid();ioctl(s,TIOCSCTTY,0);dup2(s,0);dup2(s,1);dup2(s,2);close(s);
        setenv("TERM","xterm-256color",0);
        if(target&&!strcmp(target,"cloudadd"))execlp("a","a","cloud",(char*)0);
        if(target&&target[0])execlp("a","a","tmux",target,(char*)0);
        else execlp("a","a","tmux",(char*)0);
        char*b[]={"bash","-l",NULL};execvp("bash",b);
        char*cc[]={"sh","-l",NULL};execvp("sh",cc);execl("/system/bin/sh","sh",(char*)0);_exit(1);}
    close(s);
    struct pollfd pf[2]={{c,POLLIN,0},{m,POLLIN,0}};char buf[4096];
    while(poll(pf,2,-1)>0){
        if(pf[1].revents&POLLIN){int n=(int)read(m,buf,4096);if(n<=0)break;_ws_send(c,buf,n);}
        if(pf[0].revents&POLLIN){int n=_ws_recv(c,buf,4096);if(n<0)break;
            if(buf[0]=='{'){char*co=strstr(buf,"\"cols\":");char*ro=strstr(buf,"\"rows\":");
                if(co&&ro){struct winsize w={.ws_row=(unsigned short)atoi(ro+7),.ws_col=(unsigned short)atoi(co+7)};ioctl(m,TIOCSWINSZ,&w);continue;}}
            (void)!write(m,buf,(size_t)n);}
        if(pf[0].revents&(POLLHUP|POLLERR)||pf[1].revents&(POLLHUP|POLLERR))break;
    }
    kill(p,SIGHUP);close(m);waitpid(p,NULL,0);
}
static char*_cloud_html(void){
    char cmd[P];snprintf(cmd,P,"sh '%s/lib/cloudls.sh'",SDIR);FILE*p=popen(cmd,"r");
    char rl[4096]={0};if(p){(void)!fread(rl,1,4095,p);pclose(p);}
    int cap=32768;char*h=malloc(cap);int hl=snprintf(h,cap,"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><style>body{background:#000;color:#fff;font:16px system-ui;margin:16px}a{display:block;text-decoration:none;color:inherit;cursor:pointer}a:hover div{background:#111}div{padding:14px;border-bottom:1px solid #222}.e{font-weight:600}.s{color:#888;font-size:13px}.o{display:inline-block;margin-top:8px;background:#1a73e8;color:#fff;border-radius:6px;padding:6px 12px;font-size:14px}select{background:#111;color:#fff;border:1px solid #333;padding:6px;font:inherit;border-radius:6px;margin:4px 0}button.cp{background:#a142f4;color:#fff;border:0;border-radius:6px;padding:7px 14px;font:inherit;cursor:pointer}#cpstat{margin-top:10px;font-size:14px}</style><h3 style=color:#888>Cloud storage</h3>");
    char opts[2048];int ol=0;
    for(char*l=rl,*nl;(nl=strchr(l,'\n'));l=nl+1){*nl=0;if(!*l)continue;
        char*ty=strchr(l,'|'),*id=0,*s=0;if(ty)*ty++=0;if(ty)id=strchr(ty,'|');if(id)*id++=0;if(id)s=strchr(id,'|');if(s)*s++=0;
        int icl=ty&&!strcmp(ty,"iclouddrive");char url[256];
        if(icl)snprintf(url,256,"https://www.icloud.com/iclouddrive/");
        else snprintf(url,256,"https://drive.google.com/drive/u/0/?authuser=%s",id&&*id?id:"");
        hl+=snprintf(h+hl,(size_t)(cap-hl),"<a href=\"%s\" target=_blank><div><span class=e>%s</span><br><span class=s>%s</span><br><span class=o>%s</span></div></a>",url,id&&*id?id:l,s?s:"",icl?"Open iCloud Drive":"Open Google Drive");
        char nm[64];snprintf(nm,64,"%s",l);char*cl=strrchr(nm,':');if(cl)*cl=0;
        ol+=snprintf(opts+ol,(size_t)(2048-ol),"<option value=\"%s\">%s%s%s</option>",nm,nm,id&&*id?" - ":"",id&&*id?id:"");}
    hl+=snprintf(h+hl,(size_t)(cap-hl),"<a href=\"/op?w=cloudadd\"><div><span class=o style=background:#34a853>+ Add cloud service</span></div></a>");
    hl+=snprintf(h+hl,(size_t)(cap-hl),"<div><h3 style=color:#888>Copy between clouds</h3>from <select id=src>%s</select> to <select id=dst>%s</select> <button class=cp onclick=startcp()>Copy</button> <button class=cp onclick=stopcp() style=background:#d33>Stop</button><div id=cpstat></div></div>",opts,opts);
    hl+=snprintf(h+hl,(size_t)(cap-hl),"%s","<script>function cpoll(){fetch('/api/cloudcp-status').then(r=>r.text()).then(t=>{var e=document.getElementById('cpstat'),p=t.split('|');if(p[0]=='running'){e.innerHTML='Copying '+(p[1]||'')+' ...';setTimeout(cpoll,2000)}else if(p[0]=='done'){e.innerHTML='Finished - <a target=_blank style=color:#4af href=\"'+p[1]+'\">'+p[1]+'</a>'}else if(p[0]=='stopped'){e.innerHTML='<span style=color:#fa0>Stopped: '+(p[1]||'')+'</span>'}else if(p[0]=='error'){e.innerHTML='<span style=color:#f44>Error: '+(p[1]||'')+'</span>'}else{e.textContent=''}})}function startcp(){var s=document.getElementById('src').value,d=document.getElementById('dst').value;if(s==d){alert('pick two different remotes');return}document.getElementById('cpstat').textContent='Starting ...';fetch('/api/cloudcp?src='+encodeURIComponent(s)+'&dst='+encodeURIComponent(d),{method:'POST'}).then(function(){setTimeout(cpoll,800)})}function stopcp(){fetch('/api/cloudcp-stop',{method:'POST'}).then(function(){setTimeout(cpoll,400)})}cpoll();</script>");
    return h;}
static char _rl[160];
static void _handle(int c){
    static char req[262144];int n=0;
    while(n<262143){int r=(int)read(c,req+n,262143-n);if(r<=0)break;n+=r;req[n]=0;if(strstr(req,"\r\n\r\n"))break;}
    if(n<=0)return;
    char*cl=strstr(req,"Content-Length:"),*bb=strstr(req,"\r\n\r\n");
    if(cl&&bb){int want=(int)(bb-req+4)+atoi(cl+15);
        while(n<want&&n<262143){int r=(int)read(c,req+n,want-n);if(r<=0)break;n+=r;}}
    req[n]=0;
    {char*e=strchr(req,'\r');int L=e?(int)(e-req):0;if(L>159)L=159;memcpy(_rl,req,(size_t)L);_rl[L]=0;}
    int one=1;setsockopt(c,IPPROTO_TCP,TCP_NODELAY,&one,4);
    /* new full-page route? add a GET handler below + one nav link in ui_full.html line 9 (<div id=wm>). docs auto-list via /docs. */
    if(!strncmp(req,"GET / ",6)||!strncmp(req,"GET /jobs",9)||!strncmp(req,"GET /note ",10)||!strncmp(req,"GET /tasks",10)||!strncmp(req,"GET /term",9)){
        if(_shtml)_sresp(c,200,"text/html",_shtml,_shlen);else _sresp(c,503,"text/plain","starting",8);return;}
    if(!strncmp(req,"GET /ws",7)&&(strstr(req,"Upgrade: websocket")||strstr(req,"upgrade: websocket"))){
        char tgt[64]={0};const char*qw=strstr(req,"?w=");
        if(qw){qw+=3;int i=0;while(qw[i]&&qw[i]!=' '&&qw[i]!='&'&&qw[i]!='\r'&&i<63){tgt[i]=qw[i];i++;}tgt[i]=0;}
        if(_ws_upgrade(c,req))_ws_term(c,tgt);return;}
    if(!strncmp(req,"GET /api/u-status",17)){_sresp(c,200,"application/json","{\"ok\":true}",11);return;}
    if(!strncmp(req,"GET /doc",8)&&(req[8]=='?'||req[8]==' ')){
        char rel[P]={0};const char*q=strstr(req,"?f=");
        if(q){q+=3;int j=0;for(;*q&&*q!=' '&&*q!='&'&&j<P-1;q++){
            if(*q=='%'&&q[1]&&q[2]){char x[3]={q[1],q[2],0};rel[j++]=(char)strtol(x,0,16);q+=2;}
            else rel[j++]=*q=='+'?' ':*q;}rel[j]=0;}
        if(!rel[0]||strstr(rel,"..")){_sresp(c,400,"text/plain","? GET /doc?f=<path under adata/git>",36);return;}
        char fp[P];snprintf(fp,P,"%s/%s",SROOT,rel);size_t fl=0;char*fd=readf(fp,&fl);
        if(!fd){_sresp(c,404,"text/plain","not found",9);return;}
        char*h=malloc(fl*6+1024);if(!h){free(fd);_sresp(c,500,"text/plain","oom",3);return;}
        int hl=snprintf(h,1024,"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>%s</title><style>body{margin:0;background:#0b0b0b}textarea{width:100%%;height:100vh;box-sizing:border-box;background:#0b0b0b;color:#ddd;border:0;outline:none;padding:16px;font:13px/1.5 ui-monospace,monospace;resize:none;white-space:pre-wrap;word-break:break-word}</style><textarea spellcheck=false>",rel);
        for(size_t i=0;i<fl;i++){char k=fd[i];
            if(k=='<'){memcpy(h+hl,"&lt;",4);hl+=4;}
            else if(k=='&'){memcpy(h+hl,"&amp;",5);hl+=5;}
            else h[hl++]=k;}
        memcpy(h+hl,"</textarea>",11);hl+=11;
        _sdoc(c,h,hl);free(fd);free(h);return;}
    if(!strncmp(req,"GET /docs",9)){
        /* auto-list: every file under these dirs links to /doc?f= — drop a file in, it appears, no menu upkeep */
        char*h=malloc(1<<18);if(!h){_sresp(c,500,"text/plain","oom",3);return;}
        int hl=snprintf(h,1<<18,"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>docs</title><style>body{background:#0b0b0b;color:#ddd;margin:0;font:14px/1.6 ui-monospace,monospace}h3{color:#6cf;padding:12px 16px 4px;margin:0}a{display:block;color:#9cf;text-decoration:none;padding:4px 16px}a:hover{background:#161616}</style>");
        const char*dirs[]={"mem","adocs"};
        for(int k=0;k<2;k++){char dp[P];snprintf(dp,P,"%s/%s",SROOT,dirs[k]);
            static char nm[2048][80];int n=0;DIR*d=opendir(dp);struct dirent*e;
            while(d&&(e=readdir(d))&&n<2048){if(e->d_name[0]=='.')continue;snprintf(nm[n++],80,"%s",e->d_name);}if(d)closedir(d);
            for(int i=1;i<n;i++){char t[80];snprintf(t,80,"%s",nm[i]);int j=i-1;while(j>=0&&strcmp(nm[j],t)>0){snprintf(nm[j+1],80,"%s",nm[j]);j--;}snprintf(nm[j+1],80,"%s",t);}
            hl+=snprintf(h+hl,(size_t)((1<<18)-hl),"<h3>%s/</h3>",dirs[k]);
            for(int i=0;i<n&&hl<(1<<18)-256;i++)hl+=snprintf(h+hl,(size_t)((1<<18)-hl),"<a href=\"/doc?f=%s/%s\">%s</a>",dirs[k],nm[i],nm[i]);}
        _sdoc(c,h,hl);free(h);return;}
    if(!strncmp(req,"GET /prompt",11)){
        int pp[2];if(pipe(pp)){_sresp(c,500,"text/plain","err",3);return;}pid_t ch=fork();
        if(!ch){dup2(pp[1],1);close(pp[0]);close(pp[1]);(void)!chdir(SDIR);execlp("a","a","prompt","show",(char*)0);_exit(1);}
        close(pp[1]);size_t cap=1<<19,ol=0;char*o=malloc(cap);
        if(o)for(int r;(r=(int)read(pp[0],o+ol,cap-1-ol))>0;){ol+=(size_t)r;
            if(ol+8192>cap){char*t=realloc(o,cap*=2);if(!t){free(o);o=NULL;break;}o=t;}}
        close(pp[0]);waitpid(ch,NULL,0);
        if(!o){_sresp(c,500,"text/plain","oom",3);return;}
        long cboff=-1;{char ap[P];snprintf(ap,P,"%s/local/a_cat.txt",AROOT);size_t al=0;char*ac=readf(ap,&al);
            if(ac&&al){char*t=realloc(o,ol+al+1);if(t){o=t;cboff=(long)ol;memcpy(o+ol,ac,al);ol+=al;}free(ac);}}
        o[ol]=0;
        char*h=malloc(ol*6+2048);if(!h){free(o);_sresp(c,500,"text/plain","oom",3);return;}
        char cm[4096];int cl;size_t HL=strlen(HOME);
        struct{const char*lbl,*fmt,*root,*mk;long off;}CP[]={{"default.txt","%s/common/prompts/default.txt",SROOT,0,-1},{"AGENTS.md","%s/AGENTS.md",SDIR,0,-1},{"mem index","%s/mem/index.txt",SROOT,"==> mem index <==",-1},{"installed tools","",0,"Installed tools on this device:",-1},{"codebase (a cat 3)","%s/local/a_cat.txt",AROOT,0,cboff}};
        int N=5;
        for(int i=0;i<N;i++){if(CP[i].off>=0)continue;char key[160]={0};
            if(CP[i].mk)snprintf(key,160,"%s",CP[i].mk);
            else if(CP[i].fmt[0]){char fp[P];snprintf(fp,P,CP[i].fmt,CP[i].root);FILE*f=fopen(fp,"r");
                if(f){char ln[160];while(fgets(ln,160,f)){ln[strcspn(ln,"\n")]=0;if((int)strlen(ln)>8){snprintf(key,160,"%s",ln);break;}}fclose(f);}}
            if(key[0]){char*pq=strstr(o,key);if(pq)CP[i].off=(long)(pq-o);}}
        cl=snprintf(cm,4096,"<div class=c><b>components</b> <span class=g>= write_prompt_file (lib/tmux.c) + a cat · click to jump · also inline: git-status, a-done line</span><br>");
        for(int i=0;i<N;i++){char fp[P]="";if(CP[i].fmt[0])snprintf(fp,P,CP[i].fmt,CP[i].root);
            struct stat st;long sz=fp[0]&&!stat(fp,&st)?(long)st.st_size:-1;
            const char*d=fp[0]?fp:"(generated)";if(fp[0]&&!strncmp(d,HOME,HL)&&d[HL]=='/')d+=HL+1;
            if(CP[i].off>=0)cl+=snprintf(cm+cl,(size_t)(4096-cl),"<a class=k href=\"#c%d\">%s</a> <span class=p>%s</span> %ldB<br>",i,CP[i].lbl,d,sz);
            else cl+=snprintf(cm+cl,(size_t)(4096-cl),"<span class=k style=color:#777>%s</span> <span class=p>%s</span> %ldB<br>",CP[i].lbl,d,sz);}
        cl+=snprintf(cm+cl,(size_t)(4096-cl),"</div>");
        int hl=snprintf(h,(size_t)(ol*6+2048),"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>unified prompt</title><style>body{background:#0b0b0b;color:#ddd;margin:0;font:13px/1.5 ui-monospace,monospace}header{position:sticky;top:0;background:#000;color:#6cf;padding:8px 16px;border-bottom:1px solid #222;z-index:2}.c{padding:10px 16px;border-bottom:1px solid #222;background:#0d0d0d;font-size:12px;line-height:1.8}.g{color:#888}.k{color:#6cf;text-decoration:none}.k:hover{text-decoration:underline}.p{color:#9c9}b{color:#fff}pre{white-space:pre-wrap;word-break:break-word;padding:16px;margin:0}pre span{scroll-margin-top:46px}</style><header><b>unified prompt</b> — every agent (claude·codex·gemini·m) · %zu bytes</header>%s<pre>",ol,cm);
        for(size_t i=0;i<ol;i++){
            for(int z=0;z<N;z++)if(CP[z].off==(long)i)hl+=snprintf(h+hl,40,"<span id=c%d></span>",z);
            char k=o[i];
            if(k=='<'){memcpy(h+hl,"&lt;",4);hl+=4;}
            else if(k=='>'){memcpy(h+hl,"&gt;",4);hl+=4;}
            else if(k=='&'){memcpy(h+hl,"&amp;",5);hl+=5;}
            else h[hl++]=k;}
        memcpy(h+hl,"</pre>",6);hl+=6;
        _sdoc(c,h,hl);free(o);free(h);return;}
    if(!strncmp(req,"GET /p",6)&&(req[6]==' '||req[6]=='\r'||req[6]=='?')){
        char out[B]="";int ol=0,pp[2];pipe(pp);pid_t ch=fork();
        if(!ch){dup2(pp[1],1);close(pp[0]);close(pp[1]);execlp("a","a","i",(char*)0);_exit(1);}
        close(pp[1]);{int r;while((r=(int)read(pp[0],out+ol,(size_t)(B-1-ol)))>0)ol+=r;}close(pp[0]);waitpid(ch,NULL,0);out[ol]=0;
        char h[B*2];int hl=snprintf(h,sizeof h,"<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><style>body{background:#000;color:#fff;font:18px system-ui;margin:16px}h3{color:#888;font-weight:400}.r{padding:10px;border-bottom:1px solid #222;display:flex;align-items:center;gap:10px}.o{color:#555;min-width:1.4em;text-align:right}.r b{flex:1;font-weight:400}button{background:#1a1a1a;color:#4af;border:1px solid #333;border-radius:6px;padding:7px 13px;font:inherit;cursor:pointer}#st{color:#666;padding:4px 0 12px;min-height:1.2em;font-size:15px}</style><h3>projects</h3><div id=st>ready \xe2\x80\x94 \xe2\x86\x91\xe2\x86\x93 reorder, saves via CLI</div>");
        for(char*l=out;*l;){char*nl=strchr(l,'\n');if(nl)*nl=0;char*tab=strstr(l,"\tproject");
            if(tab){*tab=0;int i=atoi(l);char*nm=strchr(l,' ');nm=nm?nm+1:l;
                hl+=snprintf(h+hl,(size_t)(sizeof h-(size_t)hl),"<div class=r><span class=o>%d</span><b>%s</b><button onclick='mv(%d,%d)'>↑</button><button onclick='mv(%d,%d)'>↓</button></div>",i,nm,i,i-1,i,i+1);}
            l=nl?nl+1:l+strlen(l);}
        hl+=snprintf(h+hl,(size_t)(sizeof h-(size_t)hl),"<script>var st=document.getElementById('st'),m=sessionStorage.pmsg;if(m){st.innerHTML=m;sessionStorage.removeItem('pmsg')}function mv(f,t){var n=document.querySelectorAll('.r').length;if(t<0||t>=n)return;st.textContent='saving… a move '+f+' '+t;st.style.color='#fa0';fetch('/api/omni',{method:'POST',body:'q=move '+f+' '+t}).then(r=>r.text()).then(x=>{x=x.replace(/<[^>]*>/g,'').split('\\n').map(l=>l.trim()).filter(l=>l&&l.indexOf('tokens')<0).pop()||'(no output)';var ok=x.indexOf('✓')>=0;sessionStorage.pmsg='<span style=color:'+(ok?'#4a4':'#f44')+'>'+(ok?'saved · ':'NOT saved · ')+x+'</span>';location.reload()}).catch(e=>{st.textContent='✗ CLI unreachable';st.style.color='#f44'})}</script>");
        _sresp(c,200,"text/html",h,hl);return;}
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
        else{char resp[16384];int rl=ol?snprintf(resp,16384,"<pre style=\"color:#fff\">%.*s</pre>",ol,out):0;
            _sresp(c,200,"text/html",resp,rl);}
        return;}
    if(!strncmp(req,"POST /api/sync",14)){sync_bg();_sresp(c,200,"text/plain","ok",2);return;}
    if(!strncmp(req,"GET /api/sync-status",20)){int fd=open("/tmp/.a_git.lock",O_RDONLY);
        int busy=fd>=0&&flock(fd,LOCK_EX|LOCK_NB)<0;if(fd>=0){if(!busy)flock(fd,LOCK_UN);close(fd);}
        const char*r=busy?"syncing":sync_age();_sresp(c,200,"text/plain",r,(int)strlen(r));return;}
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
    if(!strncmp(req,"GET /dash",9)&&(req[9]==' '||req[9]=='?'||req[9]=='\r')){
        static const char H[]="<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><style>body{background:#000;color:#fff;font:16px system-ui;margin:16px}a{color:#4af;text-decoration:none;display:block;padding:10px;border-bottom:1px solid #222}h3{color:#888;font-weight:400;font-size:14px;margin:16px 0 4px}</style><div id=d>...</div><script>new EventSource('/dash/s').onmessage=m=>{var g={},o=[];m.data.split('|').filter(l=>l).forEach(w=>{var p=w.split('\\t');if(!g[p[0]])o.push(p[0]),g[p[0]]='';g[p[0]]+='<a href=\"/op?w='+encodeURIComponent(p[1])+'\">'+(p[2]||p[1])+'</a>'});d.innerHTML=o.map(x=>'<h3>'+x+'</h3>'+g[x]).join('')}</script>";
        _sresp(c,200,"text/html",H,sizeof H-1);return;}
    if(!strncmp(req,"GET /dash/s",11)){
        pid_t fp=fork();if(fp<0){_sresp(c,500,"text/plain","fork",4);return;}
        if(fp>0)return;
        signal(SIGCHLD,SIG_DFL);
        int ifd=open("/tmp/a_dash.fifo",O_RDONLY|O_NONBLOCK);
        static const char SH[]="HTTP/1.1 200 OK\r\nContent-Type:text/event-stream\r\nCache-Control:no-store\r\n\r\n";
        if(write(c,SH,sizeof SH-1)<0){close(ifd);_exit(0);}
        static char lb[8192];static size_t ll=0;
        char _dc[1024];snprintf(_dc,1024,"tmux list-windows -t a -F '#I|#{pane_pid}|#W' 2>/dev/null|awk -F'|' -v d='%s' '{p=$2;c=\"\";f=\"/proc/\"p\"/task/\"p\"/children\";if((getline s<f)>0){close(f);split(s,a,\" \");if(a[1]){f=\"/proc/\"a[1]\"/comm\";getline c<f;close(f)}}n=$3;if(c&&n~/^(bash|zsh|sh|fish)$/)n=c;print d\"\\t\"$1\"\\t\"n}'|tr '\\n' '|'",DEV);
        #define DEMIT do{FILE*lp=popen(_dc,"r");\
            char b[8192];size_t bl=0,r;if(lp){while(bl<sizeof b-1&&(r=fread(b+bl,1,sizeof b-1-bl,lp)))bl+=r;pclose(lp);}b[bl]=0;\
            if(bl!=ll||memcmp(b,lb,bl)){memcpy(lb,b,bl);ll=bl;\
                char o[8224];int oi=snprintf(o,sizeof o,"data: %s\n\n",b);\
                if(write(c,o,(size_t)oi)<0){close(ifd);_exit(0);}}}while(0)
        DEMIT;
        struct pollfd pf[2]={{ifd,POLLIN,0},{c,POLLIN,0}};
        for(;;){int n=poll(pf,2,5000);if(n<=0||(pf[1].revents&(POLLHUP|POLLERR|POLLIN)))break;
            if(pf[0].revents&POLLIN){char b[4096];while(read(ifd,b,4096)>0){}DEMIT;}}
        close(ifd);close(c);_exit(0);}
    if(!strncmp(req,"GET /op",7)&&(req[7]==' '||req[7]=='?'||req[7]=='\r')){
        const char*qw=strstr(req,"?w=");int idx=(qw&&isdigit((unsigned char)qw[3]))?atoi(qw+3):-1;
        if(idx>=0){char tc[256];
            snprintf(tc,256,"p=$(tmux display-message -t a:%d -p '#{pane_pid}' 2>/dev/null);c=$(cat /proc/$p/task/$p/children 2>/dev/null|cut -d' ' -f1);[ -n \"$c\" ]&&cat /proc/$c/comm 2>/dev/null",idx);
            FILE*pp=popen(tc,"r");char nm[64]={0};
            if(pp){if(fgets(nm,64,pp))nm[strcspn(nm,"\n")]=0;pclose(pp);}
            if(strcmp(nm,"claude")&&strcmp(nm,"codex")&&strcmp(nm,"gemini")&&strcmp(nm,"aider")){
                static const char NO[]="<!doctype html><style>body{background:#000;color:#fff;font:16px system-ui;text-align:center;padding-top:40vh}a{color:#4af}</style>no agent<br><br><a href=/dash>← dash</a>";
                _sresp(c,200,"text/html",NO,sizeof NO-1);return;}}
        static const char H[]="<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><link rel=stylesheet href=\"https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css\"><script src=\"https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js\"></script><script src=\"https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js\"></script><style>html,body{margin:0;height:100%;background:#000;overflow:hidden}#t{height:100vh;width:100vw}</style><div id=t></div><script>var W=new URLSearchParams(location.search).get('w')||'',T=new Terminal({scrollback:10000,cursorBlink:true}),F=new FitAddon.FitAddon(),E=document.getElementById('t');T.loadAddon(F);T.open(E);var fit=()=>{try{F.fit();if(ws.readyState===1)ws.send(JSON.stringify({cols:T.cols,rows:T.rows}))}catch(e){}};var ws=new WebSocket((location.protocol==='https:'?'wss:':'ws:')+'//'+location.host+'/ws'+(W?'?w='+encodeURIComponent(W):''));ws.onopen=()=>{F.fit();ws.send(JSON.stringify({cols:T.cols,rows:T.rows}));T.focus()};ws.onmessage=e=>T.write(e.data);T.onData(d=>ws.readyState===1&&ws.send(d));addEventListener('click',()=>T.focus());addEventListener('resize',fit);new ResizeObserver(fit).observe(E);requestAnimationFrame(()=>{F.fit();T.focus()})</script>";
        _sresp(c,200,"text/html",H,sizeof H-1);return;}
    if(!strncmp(req,"GET /cloud",10)){
        char cf[P];snprintf(cf,P,"%s/local/.cloud.html",AROOT);char*cached=readf(cf,NULL);
        if(cached){if(!fork()){close(c);char*h=_cloud_html();writef(cf,h);free(h);_exit(0);}
            _sresp(c,200,"text/html",cached,(int)strlen(cached));free(cached);return;}
        char*h=_cloud_html();writef(cf,h);_sresp(c,200,"text/html",h,(int)strlen(h));free(h);return;}
    if(!strncmp(req,"GET /api/cloudcp-status",23)){
        char*s=readf("/tmp/.a_cloudcp.status",NULL);
        if(s){int n=(int)strlen(s);while(n>0&&(s[n-1]=='\n'||s[n-1]=='\r'))s[--n]=0;_sresp(c,200,"text/plain",s,n);free(s);}
        else _sresp(c,200,"text/plain","idle",4);return;}
    if(!strncmp(req,"POST /api/cloudcp-stop",22)){
        (void)!system("pkill -f 'rclone copy' 2>/dev/null;pkill -f cloudcp.sh 2>/dev/null");
        int fd=open("/tmp/.a_cloudcp.status",O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(fd>=0){(void)!write(fd,"stopped|cancelled by user",25);close(fd);}
        _sresp(c,200,"text/plain","stopped",7);return;}
    if(!strncmp(req,"POST /api/cloudcp",17)){
        char src[64]={0},dst[64]={0};const char*sp=strstr(req,"src="),*dp=strstr(req,"dst=");
        if(sp){sp+=4;int i=0;for(;sp[i]&&sp[i]!='&'&&sp[i]!=' '&&i<63;i++){if(!isalnum((unsigned char)sp[i])&&sp[i]!='-'&&sp[i]!='_')break;src[i]=sp[i];}src[i]=0;}
        if(dp){dp+=4;int i=0;for(;dp[i]&&dp[i]!='&'&&dp[i]!=' '&&i<63;i++){if(!isalnum((unsigned char)dp[i])&&dp[i]!='-'&&dp[i]!='_')break;dst[i]=dp[i];}dst[i]=0;}
        if(src[0]&&dst[0]){if(!fork()){close(c);execlp("a","a","cloudcp",src,dst,(char*)0);_exit(1);}_sresp(c,200,"text/plain","started",7);}
        else _sresp(c,400,"text/plain","need src&dst",12);return;}
    if(!strncmp(req,"POST /op/new",12)){
        char tc[B];snprintf(tc,B,"cd %s&&PATH=$HOME/.local/bin:$PATH nohup a o </dev/null >/dev/null 2>&1 & echo $!",SDIR);
        FILE*p=popen(tc,"r");char pid[32]={0};
        if(p){(void)!fgets(pid,32,p);pclose(p);pid[strcspn(pid,"\n")]=0;}
        const char*bn=strrchr(SDIR,'/');bn=bn?bn+1:SDIR;
        char nm[64];int nl=snprintf(nm,64,"op-%s-%s",bn,pid);
        _sresp(c,200,"text/plain",nm,(size_t)nl);return;}
    _sresp(c,404,"text/plain","not found",9);
}
static int cmd_serve(int argc,char**argv){perf_disarm();signal(SIGPIPE,SIG_IGN);signal(SIGCHLD,SIG_IGN);
    mkfifo("/tmp/a_dash.fifo",0644);(void)!open("/tmp/a_dash.fifo",O_RDWR|O_NONBLOCK);
    (void)!system("for h in after-new-window after-rename-window after-kill-pane session-window-changed;do tmux set-hook -g $h 'run-shell -b \"echo x > /tmp/a_dash.fifo\"' 2>/dev/null;done");
    {const char*op=getenv("PATH")?:"";char np[P];snprintf(np,P,"%s/.local/bin:/opt/homebrew/bin:/usr/local/bin:%s",HOME,op);setenv("PATH",np,1);}
    int port=argc>2?atoi(argv[2]):1111;
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
        if(!fork()){close(fd);
            struct timespec t0,t1;clock_gettime(CLOCK_MONOTONIC,&t0);
            _handle(c);close(c);
            clock_gettime(CLOCK_MONOTONIC,&t1);
            double ms=(double)(t1.tv_sec-t0.tv_sec)*1e3+(double)(t1.tv_nsec-t0.tv_nsec)/1e6;
            fprintf(stderr,"%7.3fms  %s\n",ms,_rl);
            char lg[P];snprintf(lg,P,"%s/local/serve.log",AROOT);FILE*lf=fopen(lg,"a");if(lf){fprintf(lf,"%.3f %s\n",ms,_rl);fclose(lf);}
            _exit(0);}close(c);}
}
