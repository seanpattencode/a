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
static char*_shtml;static int _shlen;static time_t _sgen_t;
static char _sdir[P]; /* a serve <port> <dir> = static site only; UI (incl /ws shell) never exposed */
static const char*_mime(const char*p){const char*e=strrchr(p,'.');e=e?e+1:"";
    return !strcmp(e,"html")?"text/html; charset=utf-8":!strcmp(e,"css")?"text/css":!strcmp(e,"js")?"text/javascript":
        !strcmp(e,"png")?"image/png":!strcmp(e,"svg")?"image/svg+xml":!strcmp(e,"jpg")||!strcmp(e,"jpeg")?"image/jpeg":
        !strcmp(e,"ico")?"image/x-icon":!strcmp(e,"json")?"application/json":"text/plain";}
#define SYNC_HTML "<span style=color:#888>sync <span class=sa>%s</span></span> <button style=\"background:#000;color:#888;border:1px solid #333;padding:0 6px;font:inherit;cursor:pointer\" onclick=\"fetch('/api/sync',{method:'POST'});let p=setInterval(()=>fetch('/api/sync-status').then(r=>r.text()).then(t=>{document.querySelectorAll('.sa').forEach(s=>s.textContent=t);if(t!='syncing')clearInterval(p)}),1000)\">sync</button>"
/* ARCH #32: list pages navigate on finger/mouse DOWN, not lift-off. delegated so it covers links added later (e.g. /dash EventSource). skips #/onclick links. */
#define TAPJS "<script>addEventListener('pointerdown',function(e){var a=e.target.closest('a[href]');if(a&&!a.onclick&&a.getAttribute('href')[0]!='#'){e.preventDefault();a.click()}},true)</script>"
static int _ncmp(const void*a,const void*b){const char*x=strrchr((const char*)a,'_'),*y=strrchr((const char*)b,'_');return strcmp(y?y:"",x?x:"");}
static int _notes_build(char*h,int cap,const char*kind){   /* kind = "notes" or "prompts" */
    char nd[P];snprintf(nd,P,"%s/git/%s",AROOT,kind);const char*arc=!strcmp(kind,"prompts")?"arcp":"arcn";
    int hl=snprintf(h,(size_t)cap,SYNC_HTML,sync_age());
    DIR*d=opendir(nd);if(!d)return hl;struct dirent*e;
    char(*names)[64]=NULL;int nn=0,ncp=0;  /* read ALL; static[2048] dropped newest once >2048 */
    while((e=readdir(d))){if(e->d_name[0]=='.'||!strstr(e->d_name,".txt"))continue;
        if(nn>=ncp){ncp=ncp?ncp*2:2048;names=realloc(names,(size_t)ncp*64);}
        snprintf(names[nn++],64,"%s",e->d_name);}closedir(d);
    qsort(names,(size_t)nn,64,_ncmp);   /* _ncmp = newest first */
    int lim=!strcmp(kind,"prompts")?24:4;   /* prompts = the daily 11+11 candidates (Sean 7/10: visible in flow); notes stay a 4-newest glimpse */
    for(int i=0,shown=0;i<nn&&shown<lim&&hl<cap-4096;i++){   /* newest first with text (skip malformed) */
        char fp[P];snprintf(fp,P,"%s/%s",nd,names[i]);
        FILE*f=fopen(fp,"r");if(!f)continue;char ln[4096];int got=0;
        while(fgets(ln,4096,f)){if(!strncmp(ln,"Text: ",6)){ln[strcspn(ln,"\n")]=0;
            const char*u=strrchr(names[i],'_');char hu[48]="";if(u){char ts[16];snprintf(ts,16,"%.15s",u+1);ts_human(ts,hu,48);}
            hl+=snprintf(h+hl,(size_t)(cap-1-hl),"<div class=ni><button onclick=\"%s('%s',this)\" class=nx>x</button><span style=\"color:#789;display:inline-block;width:104px\">%s</span><span style=\"flex:1\">%s</span><a href=\"/doc?f=%s/%s\" style=\"color:#789;margin-left:8px;text-decoration:none\">edit</a></div>",arc,names[i],hu,ln+6,kind,names[i]);got=1;break;}}
        fclose(f);shown+=got;}free(names);return hl;
}
static int _tasks_build(char*h,int cap,const char*sort){
    char td[P];snprintf(td,P,"%s/git/tasks",AROOT);
    DIR*d=opendir(td);if(!d)return snprintf(h,(size_t)cap,"<div style=\"color:#888\">No tasks</div>");
    struct dirent*e;struct{char pri[8];char txt[1024];char name[256];char dl[20];}rows[512];int nr=0;
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
            char ln[1024];while(fgets(ln,1024,sf))if(!strncmp(ln,"Text: ",6)){ln[strcspn(ln,"\n")]=0;snprintf(rows[nr].txt,1024,"%s",ln+6);break;}
            fclose(sf);if(rows[nr].txt[0])break;}closedir(sd);}
        if(!rows[nr].txt[0])snprintf(rows[nr].txt,1024,"%s",slug);
        rows[nr].dl[0]=0;{char dlf[P];snprintf(dlf,P,"%s/%s/deadline.txt",td,e->d_name);FILE*df=fopen(dlf,"r");
            if(df){if(fgets(rows[nr].dl,20,df))rows[nr].dl[strcspn(rows[nr].dl,"\n")]=0;fclose(df);}}
        nr++;}
    closedir(d);
    int md=sort&&!strcmp(sort,"new")?1:sort&&!strcmp(sort,"due")?2:0;  /* 0=pri 1=created 2=deadline */
    for(int i=1;i<nr;i++){typeof(rows[0]) k=rows[i];int j=i-1;const char*kk=k.dl[0]?k.dl:"~";
        while(j>=0){int bf=md==1?strcmp(k.name,rows[j].name)>0:md==2?strcmp(kk,rows[j].dl[0]?rows[j].dl:"~")<0:strcmp(rows[j].pri,k.pri)>0;
            if(!bf)break;rows[j+1]=rows[j];j--;}rows[j+1]=k;}
    int hl=snprintf(h,(size_t)cap,SYNC_HTML "<div class=ni style=\"color:#789;border-bottom:1px solid #444;margin-top:10px\"><span class=nx style=\"visibility:hidden\">x</span><span style=\"display:inline-block;width:124px\">WHEN</span><span style=\"display:inline-block;width:54px\">PRI</span>TASK <span style=\"color:#456\">— ⚑=deadline else created · red P≤1000</span></div>",sync_age());
    for(int i=0;i<nr&&i<4&&hl<cap-512;i++){   /* top 4 */
        const char*c=strcmp(rows[i].pri,"01000")<=0?"#f44":strcmp(rows[i].pri,"10000")<=0?"#fa0":"#aaa";
        char fr[40];{int y,mo,dd,h=0,mi=0;struct tm t={0};
            if(rows[i].dl[0]&&sscanf(rows[i].dl,"%d-%d-%d %d:%d",&y,&mo,&dd,&h,&mi)>=3){t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=dd;mktime(&t);
                char b[32];int bl2=(int)strftime(b,32,"%b %-d",&t);int h12=h%12;if(!h12)h12=12;snprintf(b+bl2,32-(size_t)bl2," %d:%02d%s",h12,mi,h>=12?"pm":"am");snprintf(fr,40,"⚑%s",b);}
            else{const char*u=strrchr(rows[i].name,'_');char ts[16]="";if(u)snprintf(ts,16,"%.15s",u+1);ts_date(u?ts:NULL,fr,40);}}
        hl+=snprintf(h+hl,(size_t)(cap-1-hl),"<div class=ni><button onclick=\"arct('%s',this)\" class=nx>x</button><span style=\"color:#9cf;display:inline-block;width:124px\">%s</span><span style=\"color:%s;display:inline-block;width:54px\">P%s</span>%s</div>",rows[i].name,fr,c,rows[i].pri,rows[i].txt);}
    if(!hl)hl=snprintf(h,(size_t)cap,"<div style=\"color:#888\">No tasks</div>");
    return hl;
}
static void _html_gen(void){
    char tf[P];snprintf(tf,P,"%s/lib/ui_full.html",SDIR);_sgen_t=time(NULL);
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
                else if(!strcmp(tag,"DO")){char h[64]="";gethostname(h,64);char o[128];int ll=snprintf(o,128,"<option value=\"\">local: %s</option>",h);EMIT(o,ll);
                    char gc[B];snprintf(gc,B,"grep -h '^Name:' '%s/ssh/'*.txt 2>/dev/null|sed 's/Name: //'|sort -u",SROOT);
                    FILE*df=popen(gc,"r");char dln[256];while(df&&fgets(dln,256,df)){dln[strcspn(dln,"\n")]=0;if(!dln[0])continue;
                        char o[300];int ol=snprintf(o,300,"<option>%s</option>",dln);EMIT(o,ol);}if(df)pclose(df);}
                else if(!strcmp(tag,"NO")){char*nb=malloc(131072);int nl2=_notes_build(nb,131072,"notes");EMIT(nb,nl2);free(nb);}
                else if(!strcmp(tag,"TO")){char*tb=malloc(131072);int tl2=_tasks_build(tb,131072,"pri");EMIT(tb,tl2);free(tb);}
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
static int _scmp(const void*a,const void*b){return strcmp((const char*)a,(const char*)b);}
static void _qn(const char*req,char*nm){nm[0]=0;const char*q=strstr(req,"?n=");if(!q)return;q+=3;int i=0;for(;q[i]&&q[i]!='&'&&q[i]!=' '&&i<127;i++)nm[i]=q[i];nm[i]=0;}
static void _redir(int c,const char*url){char h[768];int hl=snprintf(h,768,"HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length:0\r\nConnection:close\r\n\r\n",url);(void)!write(c,h,(size_t)hl);}
/* ?f=<path> → rel (urldecoded). returns 1 if valid (non-empty, no ..) */
static int _docrel(const char*req,char*rel){rel[0]=0;const char*q=strstr(req,"?f=");if(!q)q=strstr(req,"&f=");if(!q)return 0;q+=3;
    int j=0;for(;*q&&*q!=' '&&*q!='&'&&j<P-1;q++){if(*q=='%'&&q[1]&&q[2]){char x[3]={q[1],q[2],0};rel[j++]=(char)strtol(x,0,16);q+=2;}else rel[j++]=*q=='+'?' ':*q;}rel[j]=0;
    return rel[0]&&!strstr(rel,"..");}
/* editor page: form POST + save button + escaped textarea + optional saved-banner (zero JS) */
static void _docpage(int c,const char*rel,const char*body,size_t bl,const char*saved,const char*ds){
    char*h=malloc(bl*6+2048);if(!h){_sresp(c,500,"text/plain","oom",3);return;}
    int hl=snprintf(h,2048,"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>%s</title><style>body{margin:0;background:#0b0b0b;color:#ddd;font:13px/1.5 ui-monospace,monospace}#bar{position:sticky;top:0;background:#000;padding:7px 12px;border-bottom:1px solid #222;display:flex;gap:12px;align-items:center}#bar b{color:#6cf}button{background:#13320f;color:#9f9;border:1px solid #2a5a2a;padding:3px 14px;font:inherit;cursor:pointer}#s{color:#6c6}textarea{display:block;width:100%%;height:calc(100vh - 37px);box-sizing:border-box;background:#0b0b0b;color:#ddd;border:0;outline:none;padding:12px;font:inherit;resize:none;white-space:pre-wrap;word-break:break-word}</style><form method=POST enctype=\"text/plain\" action=\"/doc?f=%s%s\"><div id=bar><b>%s</b><button>save</button><span id=s>%s</span></div><textarea name=b spellcheck=false>",rel,rel,ds,rel,saved?saved:"");
    for(size_t i=0;i<bl;i++){char k=body[i];
        if(k=='<'){memcpy(h+hl,"&lt;",4);hl+=4;}
        else if(k=='&'){memcpy(h+hl,"&amp;",5);hl+=5;}
        else h[hl++]=k;}
    memcpy(h+hl,"</textarea></form>",18);hl+=18;
    _sdoc(c,h,hl);free(h);}
/* recursive doc lister: one <a> per file, descends folders; display strips the section prefix (off) */
static int _docls(char*h,int hl,const char*rel,int off){
    char dp[P];snprintf(dp,P,"%s/%s",SROOT,rel);DIR*d=opendir(dp);if(!d)return hl;
    struct dirent*e;char nm[256][96];int n=0;
    while((e=readdir(d))&&n<256){if(e->d_name[0]=='.')continue;snprintf(nm[n++],96,"%s",e->d_name);}closedir(d);
    for(int i=1;i<n;i++){char t[96];snprintf(t,96,"%s",nm[i]);int j=i-1;while(j>=0&&strcmp(nm[j],t)>0){snprintf(nm[j+1],96,"%s",nm[j]);j--;}snprintf(nm[j+1],96,"%s",t);}
    for(int i=0;i<n&&hl<(1<<18)-512;i++){char r2[P];snprintf(r2,P,"%s/%s",rel,nm[i]);
        char fp[P];snprintf(fp,P,"%s/%s",SROOT,r2);struct stat st;
        if(!stat(fp,&st)&&S_ISDIR(st.st_mode)){if(!strcmp(nm[i],"archive"))continue; /* archived: reachable via /doc?f= + fs only */
            hl+=snprintf(h+hl,(size_t)((1<<18)-hl),"<div style=color:#778;padding:4px 16px>%s/</div>",r2+off);hl=_docls(h,hl,r2,(int)strlen(r2)+1);}
        else if(strstr(r2,"/archive/"))hl+=snprintf(h+hl,(size_t)((1<<18)-hl),"<a href=\"/doc?f=%s\">%s</a>",r2,r2+off);
        else hl+=snprintf(h+hl,(size_t)((1<<18)-hl),"<div style=\"display:flex\"><a style=\"flex:1\" href=\"/doc?f=%s\">%s</a><a href=\"#\" style=\"color:#556\" onclick=\"fetch('/doc-arch?f=%s').then(function(){location.reload()});return false\">arch</a></div>",r2,r2+off,r2);}
    return hl;}
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
static void _ws_send(int c,const char*d,int n,int op){ /* 0x82 binary for term (split UTF-8 must not kill the socket), 0x81 text for ext reload */
    unsigned char h[10];int hl=2;h[0]=(unsigned char)op;
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
        if(target&&!strncmp(target,"ssh:",4)){char d2[160];snprintf(d2,160,"%s",target+4);char*cl=strchr(d2,':');
            if(cl){*cl=0;char ses[192];snprintf(ses,192,"a:%s",cl+1);setenv("A_TMUX_SESSION",ses,1);}
            execlp("a","a","ssh",d2,(char*)0);}
        if(target&&target[0])execlp("a","a","tmux",target,(char*)0);
        else execlp("a","a","tmux",(char*)0);
        char*b[]={"bash","-l",NULL};execvp("bash",b);
        char*cc[]={"sh","-l",NULL};execvp("sh",cc);execl("/system/bin/sh","sh",(char*)0);_exit(1);}
    close(s);
    struct pollfd pf[2]={{c,POLLIN,0},{m,POLLIN,0}};char buf[4096];
    while(poll(pf,2,-1)>0){
        if(pf[1].revents&POLLIN){int n=(int)read(m,buf,4096);if(n<=0)break;_ws_send(c,buf,n,0x82);}
        if(pf[0].revents&POLLIN){int n=_ws_recv(c,buf,4096);if(n<0)break;
            if(buf[0]=='{'){char*co=strstr(buf,"\"cols\":");char*ro=strstr(buf,"\"rows\":");
                if(co&&ro){struct winsize w={.ws_row=(unsigned short)atoi(ro+7),.ws_col=(unsigned short)atoi(co+7)};ioctl(m,TIOCSWINSZ,&w);continue;}}
            (void)!write(m,buf,(size_t)n);}
        if(pf[0].revents&(POLLHUP|POLLERR)||pf[1].revents&(POLLHUP|POLLERR))break;
    }
    kill(p,SIGHUP);close(m);waitpid(p,NULL,0);
}
static void _ws_reload(int c){ /* dev hot-reload: relay a FIFO byte -> WS "reload"; 25s heartbeat keeps the MV3 worker alive */
    int f=open("/tmp/a_extreload.fifo",O_RDWR|O_NONBLOCK);if(f<0)return;
    struct pollfd pf[2]={{c,POLLIN,0},{f,POLLIN,0}};char b[64];
    while(poll(pf,2,25000)>=0){
        if(pf[0].revents&(POLLHUP|POLLERR))break;
        if(pf[1].revents&POLLIN){if(read(f,b,64)>0)_ws_send(c,"reload",6,0x81);}
        else if(pf[0].revents&POLLIN){if(_ws_recv(c,b,64)<0)break;}
        else _ws_send(c,"ping",4,0x81);
    }
    close(f);
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
static char*_phtml;static int _phlen;static time_t _pgen_t;
static void _prompt_gen(void){ /* cached; GET /prompt regens when sources newer than cache (mtime check) */
        int pp[2];if(pipe(pp))return;pid_t ch=fork();
        if(!ch){dup2(pp[1],1);close(pp[0]);close(pp[1]);(void)!chdir(SDIR);execlp("a","a","prompt","show",(char*)0);_exit(1);}
        close(pp[1]);size_t cap=1<<19,ol=0;char*o=malloc(cap);
        if(o)for(int r;(r=(int)read(pp[0],o+ol,cap-1-ol))>0;){ol+=(size_t)r;
            if(ol+8192>cap){char*t=realloc(o,cap*=2);if(!t){free(o);o=NULL;break;}o=t;}}
        close(pp[0]);waitpid(ch,NULL,0);
        if(!o)return;
        long cboff=-1;{char ap[P];snprintf(ap,P,"%s/local/a_cat.txt",AROOT);size_t al=0;char*ac=readf(ap,&al);
            if(ac&&al){char*t=realloc(o,ol+al+1);if(t){o=t;cboff=(long)ol;memcpy(o+ol,ac,al);ol+=al;}free(ac);}}
        o[ol]=0;
        char*h=malloc(ol*6+2048);if(!h){free(o);return;}
        char cm[8192];int cl;size_t HL=strlen(HOME);
        load_cfg();const char*pap=cfget("prompt");if(!*pap)pap="default";  /* active prompt file feeds the unified prompt; CP[0] + selector reflect it */
        char pfmt[P],plbl[80];snprintf(pfmt,P,"%%s/common/prompts/%s.txt",pap);snprintf(plbl,80,"%s.txt (active)",pap);
        struct{const char*lbl,*fmt,*root,*mk;long off;}CP[]={{plbl,pfmt,SROOT,0,-1},{"AGENTS.md","%s/AGENTS.md",SDIR,0,-1},{"mem index","%s/mem/index.txt",SROOT,"==> mem index <==",-1},{"installed tools","",0,"Installed tools on this device:",-1},{"codebase (a cat 3)","%s/local/a_cat.txt",AROOT,0,cboff}};
        int N=5;
        for(int i=0;i<N;i++){if(CP[i].off>=0)continue;char key[160]={0};
            if(CP[i].mk)snprintf(key,160,"%s",CP[i].mk);
            else if(CP[i].fmt[0]){char fp[P];snprintf(fp,P,CP[i].fmt,CP[i].root);FILE*f=fopen(fp,"r");
                if(f){char ln[160];while(fgets(ln,160,f)){ln[strcspn(ln,"\n")]=0;if((int)strlen(ln)>8){snprintf(key,160,"%s",ln);break;}}fclose(f);}}
            if(key[0]){char*pq=strstr(o,key);if(pq)CP[i].off=(long)(pq-o);}}
        char pfs[64][P];int np2=0;{char pdir[P];snprintf(pdir,P,"%s/common/prompts",SROOT);np2=listdir(pdir,pfs,64);}
        cl=snprintf(cm,8192,"<div class=c><b>prompt files</b> <span class=g>· view/edit · ★ active · load: a prompt &lt;name&gt;</span><br>");
        for(int i=0;i<np2;i++){const char*b=bname(pfs[i]),*dot=strrchr(b,'.');char nm[64];snprintf(nm,64,"%.*s",(int)(dot?dot-b:(long)strlen(b)),b);
            int act=!strcmp(nm,pap);cl+=snprintf(cm+cl,(size_t)(8192-cl),"<a class=k href=\"/doc?f=common/prompts/%s\"%s>%s%s</a>&nbsp; ",b,act?" style=color:#6f6":"",act?"★":"",nm);}
        cl+=snprintf(cm+cl,(size_t)(8192-cl),"</div>");
        cl+=snprintf(cm+cl,(size_t)(8192-cl),"<div class=c><b>unified prompt</b> <span class=g>= active prompt file + AGENTS.md + mem index + tools + codebase · click to jump · edit → saves to git</span><br>");
        for(int i=0;i<N;i++){char fp[P]="";if(CP[i].fmt[0])snprintf(fp,P,CP[i].fmt,CP[i].root);
            struct stat st;long sz=fp[0]&&!stat(fp,&st)?(long)st.st_size:-1;
            const char*d=fp[0]?fp:"(generated)";if(fp[0]&&!strncmp(d,HOME,HL)&&d[HL]=='/')d+=HL+1;
            char ed[160]="";int ec=CP[i].root==SROOT||CP[i].root==SDIR;  /* editable: backed by a git file (fmt+3 skips "%s/") */
            if(ec)snprintf(ed,160," <a class=k href=\"/doc?f=%s%s\" style=color:#9f9>edit</a>",CP[i].fmt+3,CP[i].root==SDIR?"&d=code":"");
            if(CP[i].off>=0)cl+=snprintf(cm+cl,(size_t)(8192-cl),"<a class=k href=\"#c%d\">%s</a> <span class=p>%s</span> %ld tok%s<br>",i,CP[i].lbl,d,sz/4,ed);
            else cl+=snprintf(cm+cl,(size_t)(8192-cl),"<span class=k style=color:#777>%s</span> <span class=p>%s</span> %ld tok%s<br>",CP[i].lbl,d,sz/4,ed);}
        cl+=snprintf(cm+cl,(size_t)(8192-cl),"</div>");
        int hl=snprintf(h,(size_t)(ol*6+2048),"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>unified prompt</title><style>body{background:#0b0b0b;color:#ddd;margin:0;font:13px/1.5 ui-monospace,monospace}header{position:sticky;top:0;background:#000;color:#6cf;padding:8px 16px;border-bottom:1px solid #222;z-index:2}.c{padding:10px 16px;border-bottom:1px solid #222;background:#0d0d0d;font-size:12px;line-height:1.8}.g{color:#888}.k{color:#6cf;text-decoration:none}.k:hover{text-decoration:underline}.p{color:#9c9}b{color:#fff}pre{white-space:pre-wrap;word-break:break-word;padding:16px;margin:0}pre span{scroll-margin-top:46px}</style><header><b>unified prompt</b> — every agent (claude·codex·gemini·m) · %zu tok</header>%s<pre>",ol/4,cm);
        for(size_t i=0;i<ol;i++){
            for(int z=0;z<N;z++)if(CP[z].off==(long)i)hl+=snprintf(h+hl,40,"<span id=c%d></span>",z);
            char k=o[i];
            if(k=='<'){memcpy(h+hl,"&lt;",4);hl+=4;}
            else if(k=='>'){memcpy(h+hl,"&gt;",4);hl+=4;}
            else if(k=='&'){memcpy(h+hl,"&amp;",5);hl+=5;}
            else h[hl++]=k;}
        memcpy(h+hl,"</pre>",6);hl+=6;
        free(o);_phtml=h;_phlen=hl;_pgen_t=time(0);
}
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
    if(_sdir[0]){ /* static site mode: files only, no UI routes */
        if(!strncmp(req,"POST /",6)){ /* hook: executable site/.post/<name> gets body as $1, stdout back */
            char nm[64];int i=0;const char*q=req+6;
            for(;*q&&*q!=' '&&*q!='/'&&*q!='?'&&i<63;q++)nm[i++]=*q;nm[i]=0;
            char hp[P*2];snprintf(hp,P*2,"%s/.post/%s",_sdir,nm);
            if(i&&!strchr(nm,'.')&&!access(hp,X_OK)){
                char*b=strstr(req,"\r\n\r\n");b=b?b+4:(char*)"";
                int pp[2];if(pipe(pp)){_sresp(c,500,"text/plain","x",1);return;}
                pid_t ch=fork();
                if(!ch){dup2(pp[1],1);dup2(pp[1],2);close(pp[0]);close(pp[1]);signal(SIGCHLD,SIG_DFL);signal(SIGPIPE,SIG_DFL);execl(hp,hp,b,(char*)0);_exit(1);}
                close(pp[1]);
                {static const char SH[]="HTTP/1.1 200 OK\r\nContent-Type:text/plain; charset=utf-8\r\nCache-Control:no-store\r\nAccess-Control-Allow-Origin:*\r\nConnection:close\r\n\r\n";(void)!write(c,SH,sizeof SH-1);}
                char sb[4096];int r;while((r=(int)read(pp[0],sb,4096))>0)if(write(c,sb,(size_t)r)<0)break; /* stream as produced; client gone -> child SIGPIPEs */
                close(pp[0]);waitpid(ch,0,0);return;}}
        if(strncmp(req,"GET /",5)){_sresp(c,404,"text/plain","x",1);return;}
        char rel[P];int i=0;const char*q=req+5;
        for(;*q&&*q!=' '&&*q!='?'&&i<P-12;q++)rel[i++]=*q;
        rel[i]=0;
        if(strstr(rel,"..")||rel[0]=='.'||strstr(rel,"/.")){_sresp(c,400,"text/plain","x",1);return;}
        char fp[P*2];snprintf(fp,P*2,"%s/%s%s",_sdir,rel,(!i||rel[i-1]=='/')?"index.html":"");
        size_t fl=0;char*fd2=readf(fp,&fl);
        if(!fd2){snprintf(fp,P*2,"%s/%s/index.html",_sdir,rel);fd2=readf(fp,&fl);} /* /x -> /x/index.html */
        if(!fd2){_sresp(c,404,"text/plain","not found",9);return;}
        _sresph(c,200,_mime(fp),fd2,(int)fl,"no-cache");free(fd2);return;}
    /* new full-page route? add a GET handler below + one nav link in ui_full.html line 9 (<div id=wm>). docs auto-list via /docs. */
    if(!strncmp(req,"GET / ",6)||!strncmp(req,"GET /jobs",9)||!strncmp(req,"GET /note ",10)||!strncmp(req,"GET /tasks",10)||!strncmp(req,"GET /term",9)){
        char uf[P];struct stat us;snprintf(uf,P,"%s/lib/ui_full.html",SDIR);   /* regen when page file newer than cache (boot-frozen bug, cf /prompt) */
        if(_shtml&&!stat(uf,&us)&&us.st_mtime>=_sgen_t){free(_shtml);_shtml=0;_html_gen();}
        if(_shtml)_sresp(c,200,"text/html",_shtml,_shlen);else _sresp(c,503,"text/plain","starting",8);return;}
    if(!strncmp(req,"GET /ws",7)&&(strstr(req,"Upgrade: websocket")||strstr(req,"upgrade: websocket"))){
        char tgt[64]={0};const char*qw=strstr(req,"?w=");
        if(qw){qw+=3;int j=0;for(int i=0;qw[i]&&qw[i]!=' '&&qw[i]!='&'&&qw[i]!='\r'&&j<63;i++){
            if(qw[i]=='%'&&qw[i+1]&&qw[i+2]){char x[3]={qw[i+1],qw[i+2],0};tgt[j++]=(char)strtol(x,NULL,16);i+=2;}
            else tgt[j++]=qw[i]=='+'?' ':qw[i];}tgt[j]=0;}
        if(_ws_upgrade(c,req))_ws_term(c,tgt);return;}
    if(!strncmp(req,"GET /extreload",14)&&(strstr(req,"Upgrade: websocket")||strstr(req,"upgrade: websocket"))){
        if(_ws_upgrade(c,req))_ws_reload(c);return;}
    if(!strncmp(req,"GET /api/u-status",17)){_sresp(c,200,"application/json","{\"ok\":true}",11);return;}
    if(!strncmp(req,"GET /bm",7)&&(req[7]==' '||req[7]=='?')){const char*q=req+7;int js=!strncmp(q,"?js",3),tx=!strncmp(q,"?txt",4),cr=!strncmp(q,"?chrome",7);char fp[P];
        snprintf(fp,P,cr?"%s/local/bm_chrome.json":tx?"%s/bookmarks.txt":js?"%s/common/bm.js":"%s/common/bm.html",cr?AROOT:SROOT);
        size_t fl=0;char*d=readf(fp,&fl);if(!d){_sresp(c,404,"text/plain","x",1);return;}
        _sresph(c,200,js?"application/javascript":tx||cr?"text/plain; charset=utf-8":"text/html",d,(int)fl,"no-cache");free(d);return;}
    if(!strncmp(req,"GET /doc",8)&&(req[8]=='?'||req[8]==' ')){
        char rel[P];const char*m="? GET /doc?f=<path under adata/git> [&d=code for the a repo]";
        if(!_docrel(req,rel)){_sresp(c,400,"text/plain",m,(int)strlen(m));return;}
        char*dc=strstr(req,"d=code"),*eol=strstr(req,"\r\n");const char*ds=(dc&&eol&&dc<eol)?"&d=code":"";const char*base=*ds?SDIR:SROOT;
        char fp[P];snprintf(fp,P,"%s/%s",base,rel);size_t fl=0;char*fd=readf(fp,&fl);
        _docpage(c,rel,fd?fd:"",fl,NULL,ds);free(fd);return;}   /* missing path -> blank editor; save creates it */
    if(!strncmp(req,"POST /doc",9)){
        char rel[P];if(!_docrel(req,rel)){_sresp(c,400,"text/plain","bad path",8);return;}
        char*dc=strstr(req,"d=code"),*eol=strstr(req,"\r\n");const char*ds=(dc&&eol&&dc<eol)?"&d=code":"";const char*base=*ds?SDIR:SROOT;
        char*bd=strstr(req,"\r\n\r\n"),*clh=strstr(req,"Content-Length:");
        if(!bd||!clh){_sresp(c,400,"text/plain","no body",7);return;}
        bd+=4;int blen=atoi(clh+15);
        if(blen>250000){_sresp(c,413,"text/plain","too big (>250KB) for editor save",32);return;}
        char*ct=bd;if(blen>=2&&!strncmp(bd,"b=",2)){ct+=2;blen-=2;}              /* strip enctype=text/plain field name */
        while(blen>0&&(ct[blen-1]=='\n'||ct[blen-1]=='\r'))blen--;              /* drop the trailing CRLF the form appends */
        int w=0;for(int i=0;i<blen;i++)if(ct[i]!='\r')ct[w++]=ct[i];            /* CRLF -> LF */
        char fp[P];snprintf(fp,P,"%s/%s",base,rel);
        {char*sl=strrchr(fp,'/');if(sl){*sl=0;mkdirp(fp);*sl='/';}}   /* create parent folders */
        char bf[64];snprintf(bf,64,"/tmp/_b%d",(int)getpid());   /* merge base = pre-save file (what the editor loaded), not stale HEAD */
        {size_t o=0;char*d=readf(fp,&o);FILE*b=fopen(bf,"w");if(b){if(d)(void)!fwrite(d,1,o,b);fclose(b);}free(d);}
        FILE*wf=fopen(fp,"w");int ok=0;if(wf){fwrite(ct,1,(size_t)w,wf);ok=!ferror(wf);fclose(wf);}
        char saved[512],gurl[B]="";
        if(ok){  /* 3-way merge onto origin/main's latest, push just this file via plumbing (survives local divergence); url or ERR */
            char gc[B*3];snprintf(gc,B*3,"cd '%s'&&F='%s';T=/tmp/_t$$;M=/tmp/_m$$;I=/tmp/_i$$;git fetch origin -q;git show origin/main:\"$F\">$T 2>/dev/null||cp '%s' $T;"
                "if git merge-file -p \"$F\" '%s' $T>$M;then cp $M \"$F\";GIT_INDEX_FILE=$I git read-tree origin/main;GIT_INDEX_FILE=$I git update-index --add --cacheinfo 100644,$(git hash-object -w $M),\"$F\";"
                "n=$(git commit-tree $(GIT_INDEX_FILE=$I git write-tree) -p origin/main -m \"doc: $F\");e=$(git push origin $n:main 2>&1)&&{ u=$(git config remote.origin.url);u=${u#*github.com[:/]};echo \"https://github.com/${u%%.git}/commit/$(git rev-parse --short $n)\";}||printf 'ERR %%s' \"$e\";"
                "else echo 'ERR overlapping edit on origin — reopen & redo on latest';fi;rm -f $T $M $I '%s'",base,rel,bf,bf,bf);
            pcmd(gc,gurl,B);gurl[strcspn(gurl,"\n")]=0;
            char ts[16];time_t t=time(0);strftime(ts,16,"%H:%M:%S",localtime(&t));
            if(!strncmp(gurl,"https",5)){const char*h=strrchr(gurl,'/')+1;snprintf(saved,512,"✓ %s pushed · <a href=\"%s\" style=color:#6cf>%s</a>",ts,gurl,h);}
            else snprintf(saved,512,"✓ %s saved locally · ✗ not pushed — %s",ts,gurl[0]?gurl:"no git output");
        }else snprintf(saved,512,"✗ SAVE FAILED");
        {char lg[P];snprintf(lg,P,"%s/local/serve.log",AROOT);FILE*l=fopen(lg,"a");if(l){fprintf(l,"save %s %s\n",rel,ok?gurl:"WRITEFAIL");fclose(l);}}
        size_t nl=0;char*nf=readf(fp,&nl);
        _docpage(c,rel,nf?nf:ct,nf?nl:(size_t)w,saved,ds);free(nf);return;}
    if(!strncmp(req,"POST /book",10)){char nm[128];_qn(req,nm);
        char po[24]="0";char*bd=strstr(req,"\r\n\r\n"),*pp=bd?strstr(bd+4,"pos="):0;
        if(pp){pp+=4;int i=0;for(;pp[i]>='0'&&pp[i]<='9'&&i<23;i++)po[i]=pp[i];po[i]=0;}
        if(nm[0]&&!strchr(nm,'/')&&!strstr(nm,"..")&&!fork()){int n=open("/dev/null",O_WRONLY);if(n>=0)dup2(n,1);execlp("a","a","book","pos",nm,po,(char*)0);_exit(1);}
        _sresp(c,200,"text/plain","ok",2);return;}
    if(!strncmp(req,"GET /bookarchive",16)){char nm[128];_qn(req,nm);  /* dot-prefix rename = archive; restore: a book archive <substr> */
        if(!nm[0]||nm[0]=='.'||strchr(nm,'/')||strstr(nm,"..")){_sresp(c,400,"text/plain","bad book",8);return;}
        char fr[P],to[P];snprintf(fr,P,"%s/books/%s",AROOT,nm);snprintf(to,P,"%s/books/.%s",AROOT,nm);
        if(rename(fr,to)){_sresp(c,404,"text/plain","x",1);return;}_sresp(c,200,"text/plain","ok",2);return;}
    if(!strncmp(req,"GET /book",9)&&(req[9]=='?'||req[9]==' ')){char nm[128];_qn(req,nm);
        if(!nm[0]){
            int au=!!strstr(req,"sort=author");   /* /book?sort=author → group by author */
            char bd[P];snprintf(bd,P,"%s/books",AROOT);
            static char names[4096][128];int n=0;DIR*d=opendir(bd);struct dirent*e;
            if(d){while((e=readdir(d))&&n<4096){if(e->d_name[0]=='.'||!strcmp(e->d_name,"book.py"))continue;
                char dp[P];snprintf(dp,P,"%s/%s",bd,e->d_name);struct stat st;if(!stat(dp,&st)&&S_ISDIR(st.st_mode))snprintf(names[n++],128,"%s",e->d_name);}closedir(d);}
            {char ip[P];snprintf(ip,P,"%s/git/books/index.txt",AROOT);size_t il=0;char*ix=readf(ip,&il);  /* merge synced index: registered-elsewhere books appear, pull on open */
             if(ix){for(char*l=ix;l<ix+il&&n<4096;){char*e2=memchr(l,'\n',(size_t)(ix+il-l)),*lim=e2?e2:ix+il;
                char*t1=memchr(l,'\t',(size_t)(lim-l)),*t2=t1?memchr(t1+1,'\t',(size_t)(lim-t1-1)):0;
                if(t1&&t2&&t2>t1+1&&(size_t)(t2-t1-1)<127){char bn[128];snprintf(bn,128,"%.*s",(int)(t2-t1-1),t1+1);
                    int dup=0;for(int i=0;i<n;i++)if(!strcmp(names[i],bn)){dup=1;break;}
                    if(!dup&&bn[0]&&bn[0]!='.')snprintf(names[n++],128,"%s",bn);}
                if(e2)l=e2+1;else break;}free(ix);}}
            static int idx[4096];   /* author mode sorts an index by resolved-author key (book.c) */
            if(au){bk_resolve(names,n);g_ak=bk_ak;for(int i=0;i<n;i++)idx[i]=i;qsort(idx,(size_t)n,sizeof(int),g_akcmp);}
            else{qsort(names,(size_t)n,128,_scmp);for(int i=0;i<n;i++)idx[i]=i;}
            int cap=1<<20;char*h=malloc((size_t)cap);int hl=snprintf(h,(size_t)cap,
                "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
                "<style>body{background:#0b0b0b;color:#ddd;margin:0;font:15px/1.3 system-ui}h3{color:#6cf;padding:14px 16px 6px;margin:0}"
                ".r{display:flex;align-items:center;gap:12px;padding:11px 16px;border-bottom:1px solid #1a1a1a}.r:hover{background:#161616}"
                ".t{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#9cf;text-decoration:none}.r.x .t{color:#5a6b7a}"
                ".c{flex:none;color:#6a9;text-decoration:none;font-size:15px}.s{flex:none;min-width:48px;text-align:right;color:#667;font:11px ui-monospace,monospace;text-transform:uppercase}.s a{color:#667;text-decoration:none}.s a:hover{color:#6cf}"
                ".h{position:sticky;top:0;background:#0b0b0b;color:#5af;font-weight:700;font-size:12px;letter-spacing:.09em;text-transform:uppercase;padding:16px 16px 5px;border-bottom:1px solid #1a1a1a}"
                ".r.in{padding-left:30px}.nav{padding:4px 16px 10px;font-size:14px}.nav a{color:#789;text-decoration:none;margin-right:14px}.nav a.on{color:#6cf;font-weight:600}</style>"
                "<script>function _ax(e){var a=e.target.closest('a.x');if(!a)return;e.preventDefault();e.stopImmediatePropagation();"
                "if(e.type!='pointerdown')return;fetch(a.href).then(function(r){if(r.ok){a.closest('.r').style.opacity=.35;a.outerHTML='<span class=c>\xe2\x9c\x93</span>'}else a.textContent='\xe2\x9c\x97'},function(){a.textContent='\xe2\x9c\x97'})}"
                "addEventListener('pointerdown',_ax,true);addEventListener('click',_ax,true)</script>" TAPJS
                "<h3>books (%d)</h3><div class=nav><a%s href=\"/book\">by name</a><a%s href=\"/book?sort=author\">by author</a></div>",
                n,au?"":" class=on",au?" class=on":"");
            const char*ex[]={"txt","pdf","epub","azw3","mobi","docx",0};char pk[96]="";
            for(int ii=0;ii<n&&hl<cap-2048;ii++){int i=idx[ii];
                if(au&&strcmp(bk_ak[i],pk)){strcpy(pk,bk_ak[i]);   /* sticky author header per run */
                    char ah[128];const char*a=bk_ad[i];int j=0;for(;a[j]&&j<120;j++)ah[j]=a[j]=='-'?' ':a[j];ah[j]=0;
                    hl+=snprintf(h+hl,(size_t)(cap-hl),"<div class=h>%s</div>",ah);}
                char tf[P];snprintf(tf,P,"%s/%s/output/explained.txt",bd,names[i]);int has=!access(tf,R_OK);
                if(!has){snprintf(tf,P,"%s/%s/output/%s.txt",bd,names[i],names[i]);has=!access(tf,R_OK);}
                if(!has){snprintf(tf,P,"%s/%s/output/transcript.txt",bd,names[i]);has=!access(tf,R_OK);}
                if(!has){snprintf(tf,P,"%s/%s/source.txt",bd,names[i]);has=!access(tf,R_OK);}
                char xt[512]="";int xl=0;   /* every source.* becomes a clickable badge (was: first ext, dead text) */
                for(int k=0;ex[k];k++){snprintf(tf,P,"%s/%s/source.%s",bd,names[i],ex[k]);
                    if(!access(tf,R_OK))xl+=snprintf(xt+xl,(size_t)(512-xl),"<a href=\"/bookfile?n=%s&f=source.%s\">%s</a> ",names[i],ex[k],ex[k]);}
                char lb[360];snprintf(lb,360,"%s",names[i]);bk_mid(lb,96);
                hl+=snprintf(h+hl,(size_t)(cap-hl),"<div class=\"r %s %s\"><a class=t href=\"/book?n=%s\">%s</a><a class=c href=\"/bookdir?n=%s\" title=\"all versions (file manager)\">\xf0\x9f\x97\x82</a><a class=c href=\"/bookcloud?n=%s\" title=\"open in cloud\">\xe2\x98\x81</a><a class=\"c x\" href=\"/bookarchive?n=%s\" title=\"archive (hide)\">\xe2\x9c\x95</a><span class=s>%s</span></div>",has?"":"x",au?"in":"",names[i],lb,names[i],names[i],names[i],xt);}
            _sdoc(c,h,hl);free(h);return;}
        if(strchr(nm,'/')||strstr(nm,"..")){_sresp(c,400,"text/plain","bad book",8);return;}
        char tf[P];snprintf(tf,P,"%s/books/%s/output/explained.txt",AROOT,nm);
        if(access(tf,R_OK))snprintf(tf,P,"%s/books/%s/output/%s.txt",AROOT,nm,nm);
        if(access(tf,R_OK))snprintf(tf,P,"%s/books/%s/output/transcript.txt",AROOT,nm);  /* canonical elsewhere: help.c chat context, sync filter */
        if(access(tf,R_OK))snprintf(tf,P,"%s/books/%s/source.txt",AROOT,nm);
        size_t tl=0;char*txt=readf(tf,&tl);
        if(!txt){char ip[P];snprintf(ip,P,"%s/git/books/index.txt",AROOT);size_t il=0;char*ix=readf(ip,&il);int reg=0;  /* in synced index but not local: pull in bg, page retries */
            if(ix){char pat[140];snprintf(pat,140,"\t%s\t",nm);reg=!!strstr(ix,pat);free(ix);}
            if(reg){if(!fork()){signal(SIGCHLD,SIG_DFL);int z=open("/dev/null",O_WRONLY);if(z>=0){dup2(z,1);dup2(z,2);}execlp("a","a","book","pull",nm,(char*)0);_exit(1);}
                char b[512];int bl=snprintf(b,512,"<!doctype html><meta charset=utf-8><meta http-equiv=refresh content=5><body style=\"background:#0b0b0b;color:#6cf;font:16px ui-monospace,monospace;padding:40px\">syncing %s from cloud\xe2\x80\xa6 auto-retrying</body>",nm);
                _sdoc(c,b,bl);return;}
            _sresp(c,404,"text/plain","no text — a book transcribe first",34);return;}
        long pos=0;{char ip[P];snprintf(ip,P,"%s/git/books/index.txt",AROOT);size_t il=0;char*ix=readf(ip,&il);  /* saved offset = index.txt col5 */
            if(ix){char*l=ix;while(l<ix+il){char*e=memchr(l,'\n',(size_t)(ix+il-l)),*lim=e?e:ix+il;
                char*t1=memchr(l,'\t',(size_t)(lim-l)),*t2=t1?memchr(t1+1,'\t',(size_t)(lim-t1-1)):0;
                if(t1&&t2&&(size_t)(t2-t1-1)==strlen(nm)&&!strncmp(t1+1,nm,strlen(nm))){
                    char*t3=memchr(t2+1,'\t',(size_t)(lim-t2-1)),*t4=t3?memchr(t3+1,'\t',(size_t)(lim-t3-1)):0;
                    if(t4&&t4+1<lim)pos=atol(t4+1);break;}
                if(e)l=e+1;else break;}free(ix);}}
        char*esc=malloc(tl*5+1);size_t el=0;  /* escape &<> so text node==exact file chars → caret offset==file offset */
        for(size_t i=0;i<tl;i++){char ch=txt[i];
            if(ch=='&'){memcpy(esc+el,"&amp;",5);el+=5;}
            else if(ch=='<'){memcpy(esc+el,"&lt;",4);el+=4;}
            else if(ch=='>'){memcpy(esc+el,"&gt;",4);el+=4;}
            else esc[el++]=ch;}
        free(txt);
        size_t cap=el+4096;char*pg=malloc(cap);int hl=snprintf(pg,cap,
            "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<style>html,body{margin:0;background:#0b0b0b;overflow:hidden;height:100%%;touch-action:none;overscroll-behavior:none}::-webkit-scrollbar{display:none}"
            /* true pagination: bk is a fixed full-screen clipped box; flipping sets bk.scrollTop by whole screens, body never scrolls */
            "#bk{position:fixed;top:0;bottom:0;left:0;right:0;max-width:760px;margin:0 auto;overflow:hidden;scrollbar-width:none;white-space:pre-wrap;overflow-wrap:break-word;color:#ddd;font:18px/1.75 Georgia,serif;padding:0 18px;box-sizing:border-box}"
            "#hud{position:fixed;top:0;right:0;background:#000;color:#6cf;font:12px ui-monospace,monospace;padding:4px 8px;opacity:.75;z-index:9}</style>"
            "<div id=hud></div><pre id=bk>");
        memcpy(pg+hl,esc,el);hl+=(int)el;free(esc);
        hl+=snprintf(pg+hl,cap-(size_t)hl,  /* browsers split big text into 64K chunk nodes — map (chunk,local)<->global offset */
            "</pre><script>var N=\"%s\",P=%ld,K=bk,H=hud,ns=[].slice.call(K.childNodes),T=0,bs=[],co=P;"
            "for(var i=0;i<ns.length;i++){bs.push(T);T+=ns[i].length||0;}"
            "function C(x,y){var n,o,r;if(document.caretRangeFromPoint){r=document.caretRangeFromPoint(x,y);if(!r)return null;n=r.startContainer;o=r.startOffset;}else if(document.caretPositionFromPoint){r=document.caretPositionFromPoint(x,y);if(!r)return null;n=r.offsetNode;o=r.offset;}else return null;for(var j=0;j<ns.length;j++)if(ns[j]===n)return bs[j]+o;return null;}"
            "function O(){var r=K.getBoundingClientRect(),x=r.left+18,o;for(var y=2;y<120;y+=8){o=C(x,r.top+y);if(o!=null)return o;}return co;}"
            "function R(f){var i=ns.length-1;while(i>0&&f<bs[i])i--;var g=document.createRange();g.setStart(ns[i],Math.min(f-bs[i],ns[i].length));g.collapse(true);var c=g.getClientRects()[0]||g.getBoundingClientRect();K.scrollTop+=c.top-K.getBoundingClientRect().top;pg=Math.round(K.scrollTop/ph());}"
            "function S(){co=O();var b='pos='+co,u='/book?n='+encodeURIComponent(N);if(navigator.sendBeacon)navigator.sendBeacon(u,new Blob([b],{type:'application/x-www-form-urlencoded'}));else fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});}"
            /* true pagination: whole-screen pages via bk.scrollTop (body can't scroll); flip on pointer DOWN (depress, not lift)/wheel/keys — scrollTop is sync = sub-ms, paints next vsync */
            "var lh=parseFloat(getComputedStyle(K).lineHeight),pg=0,fm=0,st;"
            "function ph(){return Math.max(lh,Math.floor(K.clientHeight/lh)*lh);}"
            "function NP(){return Math.max(0,Math.ceil((K.scrollHeight-K.clientHeight)/ph()));}"
            "function G(p){p=Math.max(0,Math.min(p,NP()));var t=performance.now();K.scrollTop=p*ph();pg=p;fm=performance.now()-t;H.textContent='pg '+(pg+1)+'/'+(NP()+1)+' · '+fm.toFixed(2)+'ms';clearTimeout(st);st=setTimeout(S,400);}"
            "addEventListener('pointerdown',function(e){G(pg+(e.clientX<innerWidth/3?-1:1));e.preventDefault();});"
            "addEventListener('wheel',function(e){G(pg+(e.deltaY>0?1:-1));e.preventDefault();},{passive:false});"
            "addEventListener('keydown',function(e){var k=e.key;if(k===' '||k==='PageDown'||k==='ArrowRight'||k==='ArrowDown')G(pg+1);else if(k==='b'||k==='PageUp'||k==='ArrowLeft'||k==='ArrowUp')G(pg-1);else return;e.preventDefault();});"
            "addEventListener('resize',function(){G(pg);});"
            "requestAnimationFrame(function(){if(P>0){R(P);K.scrollTop=pg*ph();}H.textContent='pg '+(pg+1)+'/'+(NP()+1);});"
            "addEventListener('pagehide',S);addEventListener('visibilitychange',function(){if(document.hidden)S();});"
            "</script>",nm,pos);
        _sdoc(c,pg,hl);free(pg);return;}
    if(!strncmp(req,"GET /bookfile",13)){char nm[128];_qn(req,nm);  /* raw book asset with real mime → pdf opens in the browser's viewer */
        char rel[P];if(!nm[0]||strchr(nm,'/')||strstr(nm,"..")||!_docrel(req,rel)){_sresp(c,400,"text/plain","bad book",8);return;}
        char fp[P];snprintf(fp,P,"%s/books/%s/%s",AROOT,nm,rel);
        size_t bl=0;char*b=readf(fp,&bl);if(!b){_sresp(c,404,"text/plain","x",1);return;}
        const char*dot=strrchr(rel,'.'),*ct="application/octet-stream";
        if(dot){if(!strcmp(dot,".pdf"))ct="application/pdf";else if(!strcmp(dot,".txt"))ct="text/plain; charset=utf-8";
            else if(!strcmp(dot,".png"))ct="image/png";else if(!strcmp(dot,".jpg")||!strcmp(dot,".jpeg"))ct="image/jpeg";
            else if(!strcmp(dot,".html"))ct="text/html; charset=utf-8";else if(!strcmp(dot,".epub"))ct="application/epub+zip";}
        char h[224];int hl=snprintf(h,224,"HTTP/1.1 200 OK\r\nContent-Type:%s\r\nContent-Length:%zu\r\nConnection:close\r\nCache-Control:max-age=300\r\n\r\n",ct,bl);
        (void)!write(c,h,(size_t)hl);   /* body looped: source.pdf can be tens of MB, one write() may be short */
        for(size_t o=0;o<bl;){ssize_t w=write(c,b+o,bl-o);if(w<=0)break;o+=(size_t)w;}
        free(b);return;}
    if(!strncmp(req,"GET /bookcloud",14)){char nm[128];_qn(req,nm);  /* → exact Drive file URL for a-gdrive:books/<name>/source.* (else Drive search) */
        if(!nm[0]||strchr(nm,'/')||strstr(nm,"..")){_sresp(c,400,"text/plain","bad book",8);return;}
        char path[256];snprintf(path,256,"a-gdrive:books/%s/",nm);char id[128]="";int pp[2];
        if(!pipe(pp)){pid_t ch=fork();
            if(!ch){dup2(pp[1],1);close(pp[0]);close(pp[1]);int z=open("/dev/null",O_WRONLY);if(z>=0)dup2(z,2);
                execlp("rclone","rclone","lsf","--files-only","--format","ip","--separator",";",path,(char*)0);_exit(1);}
            close(pp[1]);char o[8192];int ol=0,r;while(ol<8191&&(r=(int)read(pp[0],o+ol,(size_t)(8191-ol)))>0)ol+=r;close(pp[0]);waitpid(ch,NULL,0);o[ol]=0;
            for(char*l=o;l&&*l;){char*e=strchr(l,'\n');if(e)*e=0;char*s=strchr(l,';');
                if(s&&!strncmp(s+1,"source.",7)){*s=0;snprintf(id,128,"%s",l);break;}
                if(e)l=e+1;else break;}}
        char url[600];
        if(id[0])snprintf(url,600,"https://drive.google.com/file/d/%s/view",id);
        else{char q[256];int j=0;for(int i=0;nm[i]&&j<250;i++){char d=((nm[i]>='a'&&nm[i]<='z')||(nm[i]>='0'&&nm[i]<='9'))?nm[i]:'+';if(d=='+'&&j&&q[j-1]=='+')continue;q[j++]=d;}q[j]=0;
            snprintf(url,600,"https://drive.google.com/drive/search?q=%s",q);}
        _redir(c,url);return;}
    if(!strncmp(req,"GET /bookdir",12)){char nm[128];_qn(req,nm);  /* open the book's folder in the OS file manager — shows every version (epub/txt/…) */
        if(!nm[0]||strchr(nm,'/')||strstr(nm,"..")){_sresp(c,400,"text/plain","bad book",8);return;}
        char dir[P];snprintf(dir,P,"%s/books/%s",AROOT,nm);
        if(access(dir,X_OK)){_sresp(c,404,"text/plain","no such book",12);return;}
        if(!fork()){setsid();
            char rd[64];snprintf(rd,64,"/run/user/%d",(int)getuid());setenv("XDG_RUNTIME_DIR",rd,1);
            DIR*wd=opendir(rd);struct dirent*we;char wl[64]="";
            if(wd){while((we=readdir(wd)))if(!strncmp(we->d_name,"wayland-",8)&&!strstr(we->d_name,".lock")){snprintf(wl,64,"%s",we->d_name);break;}closedir(wd);}
            if(wl[0])setenv("WAYLAND_DISPLAY",wl,1);
            int z=open("/dev/null",O_RDWR);if(z>=0){dup2(z,0);dup2(z,1);dup2(z,2);}
            execlp("nautilus","nautilus",dir,(char*)0);_exit(1);}
        _sresp(c,204,"text/plain","",0);return;}  /* 204 → browser stays put; the file-manager window pops up */
    if(!strncmp(req,"GET /docs",9)){
        /* auto-list: every file under these dirs links to /doc?f= — drop a file in, it appears, no menu upkeep */
        char*h=malloc(1<<18);if(!h){_sresp(c,500,"text/plain","oom",3);return;}
        int hl=snprintf(h,1<<18,"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>docs</title><style>body{background:#0b0b0b;color:#ddd;margin:0;font:14px/1.6 ui-monospace,monospace}h3{color:#6cf;padding:12px 16px 4px;margin:0}a{display:block;color:#9cf;text-decoration:none;padding:4px 16px}a:hover{background:#161616}</style><a href=# onclick=\"var n=prompt('new adoc filename');if(n)location='/doc?f=adocs/'+n;return false\" style=color:#9f9>+ new adoc</a> <a href=# onclick=\"var n=prompt('new folder name');if(n)fetch('/api/omni',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'q=docs mkdir '+encodeURIComponent(n)}).then(function(){location.reload()});return false\" style=color:#9cf>+ new folder</a>" TAPJS);
        char*ar=strstr(req,"arch=1"),*eol=strstr(req,"\r\n");int arch=ar&&eol&&ar<eol;
        const char*dn[]={"mem","adocs"},*da[]={"mem/archive","adocs/archive"};const char**dirs=arch?da:dn;
        hl+=snprintf(h+hl,(size_t)((1<<18)-hl),arch?"<a href=\"/docs\" style=color:#9cf>&#9666; back to docs</a>":"<a href=\"/docs?arch=1\" style=color:#557>&#9656; archived</a>");
        for(int k=0;k<2;k++){hl+=snprintf(h+hl,(size_t)((1<<18)-hl),"<h3>%s/</h3>",dirs[k]);
            hl=_docls(h,hl,dirs[k],(int)strlen(dirs[k])+1);}
        _sdoc(c,h,hl);free(h);return;}
    if(!strncmp(req,"GET /doc-arch",13)){ /* move doc into sibling archive/ folder; /docs hides those */
        char rel[P];if(!_docrel(req,rel)){_sresp(c,400,"text/plain","bad path",8);return;}
        char fp[P];snprintf(fp,P,"%s/%s",SROOT,rel);
        char*b=strrchr(rel,'/');if(!b){_sresp(c,400,"text/plain","no dir",6);return;}
        *b=0;char ad[P];snprintf(ad,P,"%s/%s/archive",SROOT,rel);mkdirp(ad);
        char np[P];snprintf(np,P,"%s/%s",ad,b+1);
        if(rename(fp,np)){_sresp(c,500,"text/plain","rename failed",13);return;}
        _sresp(c,200,"text/plain","ok",2);return;}
    if(!strncmp(req,"GET /prompt",11)){ /* regen when active prompt file or mem index newer than cache */
        struct stat st;char fp[P];load_cfg();const char*pap=cfget("prompt");if(!*pap)pap="default";
        snprintf(fp,P,"%s/common/prompts/%s.txt",SROOT,pap);int stale=!_phtml||(!stat(fp,&st)&&st.st_mtime>=_pgen_t);
        if(!stale){snprintf(fp,P,"%s/mem/index.txt",SROOT);if(!stat(fp,&st)&&st.st_mtime>=_pgen_t)stale=1;}
        if(stale){free(_phtml);_phtml=0;_prompt_gen();}
        if(_phtml)_sdoc(c,_phtml,_phlen);return;}
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
        q+=2;char*cmd=q,*w=q;   /* in-place: decoded ≤ encoded */
        for(;*q&&*q!='&';q++){
            if(*q=='+')*w++=' ';
            else if(*q=='%'&&q[1]&&q[2]){char h[3]={q[1],q[2],0};*w++=(char)strtol(h,NULL,16);q+=2;}
            else *w++=*q;}*w=0;
        if(isnote){char nd[P];snprintf(nd,P,"%s/notes",SROOT);mkdirp(nd);char*nf=note_save(nd,cmd);
            char m[256]="";note_url(nf,"note",m); /* gh PUT → real url (works even when local trails); no fork, no 30s freeze */
            _sresp(c,200,"text/plain",m,(int)strlen(m));return;}
        int pp[2];pipe(pp);pid_t ch=fork();
        if(!ch){close(pp[0]);dup2(pp[1],1);dup2(pp[1],2);close(pp[1]);
            signal(SIGALRM,SIG_DFL);signal(SIGPIPE,SIG_DFL);signal(SIGCHLD,SIG_DFL); /* SIG_DFL so child's git can waitpid (serve sets SIG_IGN) */
            char*args[32]={"a"};int ac=1;char*p2=cmd;
            while(*p2&&ac<31){while(*p2==' ')p2++;if(!*p2)break;args[ac++]=p2;while(*p2&&*p2!=' ')p2++;if(*p2)*p2++=0;}
            args[ac]=NULL;execvp("a",args);
            _exit(1);}
        close(pp[1]);char out[8192];int ol=0;
        {int r;while((r=(int)read(pp[0],out+ol,(size_t)(8191-ol)))>0)ol+=r;}
        close(pp[0]);waitpid(ch,NULL,0);out[ol]=0;
        {char resp[16384];int rl=ol?snprintf(resp,16384,"<pre style=\"color:#fff\">%.*s</pre>",ol,out):0;
            _sresp(c,200,"text/html",resp,rl);}
        return;}
    if(!strncmp(req,"POST /api/savep",15)){char buf[1024];buf[0]=0;
        char*body=strstr(req,"\r\n\r\n");if(body){char*q=strstr(body+4,"q=");if(q){q+=2;int i=0;
            for(;q[i]&&q[i]!='&'&&i<1023;i++){if(q[i]=='+')buf[i]=' ';
                else if(q[i]=='%'&&q[i+1]&&q[i+2]){char x[3]={q[i+1],q[i+2],0};buf[i]=(char)strtol(x,NULL,16);q+=2;}
                else buf[i]=q[i];}buf[i]=0;}}
        char pf[P];snprintf(pf,P,"%s/prompts.log",SROOT);FILE*f=fopen(pf,"a");
        if(f){time_t t=time(NULL);char ts[32];strftime(ts,32,"%Y-%m-%d %H:%M",localtime(&t));fprintf(f,"%s\t%s\n",ts,buf);fclose(f);}
        _sresp(c,200,"text/plain",pf,(int)strlen(pf));return;}
    if(!strncmp(req,"POST /api/sync",14)){sync_bg();_sresp(c,200,"text/plain","ok",2);return;}
    if(!strncmp(req,"GET /api/sync-status",20)){int fd=open("/tmp/.a_git.lock",O_RDONLY);
        int busy=fd>=0&&flock(fd,LOCK_EX|LOCK_NB)<0;if(fd>=0){if(!busy)flock(fd,LOCK_UN);close(fd);}
        const char*r=busy?"syncing":sync_age();_sresp(c,200,"text/plain",r,(int)strlen(r));return;}
    if(!strncmp(req,"GET /note-list",14)){
        int cap=524288;char*html=malloc((size_t)cap);if(!html)return;
        int hl=_notes_build(html,cap,"notes");_sresp(c,200,"text/html",html,hl);free(html);return;}
    if(!strncmp(req,"GET /api/tasks",14)){
        int cap=524288;char*html=malloc((size_t)cap);if(!html)return;
        const char*sort="pri";char*q=strstr(req,"sort="),*eol=strchr(req,'\n');
        if(q&&(!eol||q<eol)){q+=5;if(!strncmp(q,"new",3))sort="new";else if(!strncmp(q,"due",3))sort="due";}
        int hl=_tasks_build(html,cap,sort);_sresp(c,200,"text/html",html,hl);free(html);return;}
    if(!strncmp(req,"GET /flow",9)&&(req[9]==' '||req[9]=='?'||req[9]=='\r')){   /* structured review surface: notes + tasks + prompts, full text, per-row archive/edit, add + suggest */
        int cap=1<<19;char*buf=malloc((size_t)cap);if(!buf){_sresp(c,500,"text/plain","oom",3);return;}
        const char*sort="pri";char*q=strstr(req,"sort="),*eol=strchr(req,'\n');
        if(q&&(!eol||q<eol)){q+=5;if(!strncmp(q,"new",3))sort="new";else if(!strncmp(q,"due",3))sort="due";}
        int bl=snprintf(buf,3200,"<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>flow</title><style>body{background:#111;color:#ddd;font:14px/1.5 ui-monospace,monospace;margin:0;padding:8px 12px 9em}h3{color:#6cf;margin:16px 0 4px;font-size:15px}.ni{display:flex;align-items:flex-start;padding:6px 0;border-bottom:1px solid #1c1c1c;word-break:break-word}.nx{background:none;border:1px solid #555;color:#999;padding:5px 10px;margin-right:9px;border-radius:4px;cursor:pointer;flex:none}button{background:#0b0f1a;color:#9cf;border:1px solid #2a3a5a;border-radius:6px;padding:6px 11px;margin:0 2px;cursor:pointer;font:13px monospace}a{color:#9cf}</style><body><h3>NOTES <button onclick=\"fadd('note','note')\">+</button></h3>");
        bl+=_notes_build(buf+bl,cap-bl,"notes");
        bl+=snprintf(buf+bl,(size_t)(cap-bl),"<h3>TASKS <button onclick=\"fadd('task add -u','task')\">+</button> <span style=\"color:#456;font-size:12px\">sort: <a href=/flow?sort=pri>pri</a> <a href=/flow?sort=new>created</a> <a href=/flow?sort=due>deadline</a></span></h3>");
        bl+=_tasks_build(buf+bl,cap-bl,sort);
        bl+=snprintf(buf+bl,(size_t)(cap-bl),"<h3>PROMPT CANDIDATES <button onclick=\"fadd('prompt c','prompt')\">+</button></h3>");
        bl+=_notes_build(buf+bl,cap-bl,"prompts");
        bl+=snprintf(buf+bl,(size_t)(cap-bl),"<div style=\"position:fixed;left:0;right:0;bottom:0;background:#111;border-top:1px solid #333;padding:.6em;text-align:center\"><button onclick=fgen()>\342\234\246 suggest (book \342\206\222 claude)</button> <a href=\"/doc?f=common/prompts/propose.txt\">edit lens</a> <span id=fs style=color:#6a6></span></div><script>function omni(c){fs.textContent='\342\200\246';fetch('/api/omni',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'q='+encodeURIComponent(c)}).then(function(r){return r.text()}).then(function(){location.reload()})}function fadd(c,k){var v=prompt('new '+k);if(v)omni(c+' '+v)}function arc(u,key,f){var b={};b[key]=f;fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(function(){location.reload()})}function arcn(f){arc('/api/note/archive','f',f)}function arct(d){arc('/api/task/archive','d',d)}function arcp(f){arc('/api/prompt/archive','f',f)}function fgen(){var s=prompt('seed idea');if(!s)return;var b=prompt('book name as full context (blank=none)')||'-';omni('flow gen '+b+' '+s)}</script>");
        _sdoc(c,buf,bl);free(buf);return;}
    if(!strncmp(req,"POST /flowsplit",15)){   /* body=raw note text -> a split json -> {"segs":[..],"lossless":bool} */
        char*body=strstr(req,"\r\n\r\n");if(!body){_sresp(c,400,"text/plain","bad",3);return;}body+=4;
        int pp[2];if(pipe(pp)){_sresp(c,500,"text/plain","pipe",4);return;}pid_t ch=fork();
        if(!ch){close(pp[0]);dup2(pp[1],1);close(pp[1]);signal(SIGCHLD,SIG_DFL);execlp("a","a","split","json",body,(char*)0);_exit(1);}
        close(pp[1]);char out[65536];int ol=0,r;while(ol<65535&&(r=(int)read(pp[0],out+ol,(size_t)(65535-ol)))>0)ol+=r;
        close(pp[0]);waitpid(ch,NULL,0);out[ol]=0;_sresp(c,200,"application/json",out,ol);return;}
    if(!strncmp(req,"POST /flowsave",14)){   /* body=JSON ["seg",..] -> one `a task` each */
        char*body=strstr(req,"\r\n\r\n");if(!body){_sresp(c,400,"text/plain","bad",3);return;}body+=4;
        int saved=0;char seg[1024];
        for(char*p=strchr(body,'"');p;p=strchr(p,'"')){p++;int i=0;
            for(;*p&&*p!='"'&&i<1023;i++){if(*p=='\\'&&p[1])p++;seg[i]=*p++;}seg[i]=0;if(*p=='"')p++;
            if(seg[0]){pid_t ch=fork();if(!ch){int z=open("/dev/null",O_RDWR);if(z>=0){dup2(z,0);dup2(z,1);dup2(z,2);}signal(SIGCHLD,SIG_DFL);execlp("a","a","task",seg,(char*)0);_exit(1);}waitpid(ch,NULL,0);saved++;}}
        char m[128];int ml=snprintf(m,128,"✓ saved %d tasks — open the task page to verify",saved);_sresp(c,200,"text/plain",m,ml);return;}
    if(!strncmp(req,"POST /api/note/archive",22)||!strncmp(req,"POST /api/task/archive",22)||!strncmp(req,"POST /api/prompt/archive",24)){
        char kc=req[10];const char*kind=kc=='t'?"tasks":kc=='p'?"prompts":"notes";  /* n=notes t=tasks p=prompts */
        char*body=strstr(req,"\r\n\r\n");if(!body){_sresp(c,400,"text/plain","bad",3);return;}
        char*k=strstr(body+4,kc=='t'?"\"d\":\"":"\"f\":\"");if(!k){_sresp(c,400,"text/plain","no name",7);return;}
        k+=5;char name[256];int ni=0;while(k[ni]&&k[ni]!='"'&&ni<255){name[ni]=k[ni];ni++;}name[ni]=0;
        for(char*p=name;*p;p++)if(*p=='/'){_sresp(c,400,"text/plain","bad name",8);return;}
        char src[P],dst[P],ad[P];
        snprintf(ad,P,"%s/git/%s/.archive",AROOT,kind);mkdir(ad,0755);
        snprintf(src,P,"%s/git/%s/%s",AROOT,kind,name);
        snprintf(dst,P,"%s/%s",ad,name);rename(src,dst);
        _sresp(c,200,"text/plain","ok",2);return;}
    if(!strncmp(req,"GET /feed",9)&&(req[9]==' '||req[9]=='?'||req[9]=='\r')){   /* terminal as API: page = live `a feed` output, streamed (shell paints instantly, content lands on fleet-scan drain) */
        static const char FH[]="HTTP/1.1 200 OK\r\nContent-Type:text/html; charset=utf-8\r\nCache-Control:no-store\r\nConnection:close\r\n\r\n"
            "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>feed</title>"
            "<style>body{margin:0;background:#0b0b0b;color:#ddd;font:14px/1.7 ui-monospace,monospace}h3{color:#6cf;margin:0;padding:12px 14px 4px}#ms{color:#6c6;font-size:12px}pre{margin:0;padding:2px 14px 14px;white-space:pre-wrap}</style>"
            "<h3>a feed <span id=ms>⟳ scanning fleet…</span> <span style=\"color:#456;font-size:12px\">j/k \xe2\x86\x91\xe2\x86\x93 move · \xe2\x86\xb5 open box</span></h3><pre>";
        (void)!write(c,FH,sizeof FH-1);
        struct timespec t0,t1;clock_gettime(CLOCK_MONOTONIC,&t0);
        int pp[2];if(pipe(pp))return;pid_t ch=fork();
        if(!ch){dup2(pp[1],1);close(pp[0]);close(pp[1]);execlp("a","a","feed",(char*)0);_exit(1);}
        close(pp[1]);char b[2048],eb[10240];int r;
        while((r=(int)read(pp[0],b,sizeof b))>0){int o=0;
            for(int i=0;i<r;i++){char k=b[i];if(k=='<'){memcpy(eb+o,"&lt;",4);o+=4;}else if(k=='&'){memcpy(eb+o,"&amp;",5);o+=5;}else eb[o++]=k;}
            if(write(c,eb,(size_t)o)<0)break;}
        close(pp[0]);clock_gettime(CLOCK_MONOTONIC,&t1);
        char ft[1400];int fl=snprintf(ft,1400,"</pre><script>ms.textContent='%.4fms';"   /* rows -> divs; j/k/arrows move, Enter opens the box's tmux via /op (DEVICE col = chars 2..14) */
            "var Pr=document.querySelector('pre'),ls=Pr.textContent.split('\\n');"
            "Pr.innerHTML=ls.map(function(l){return '<div>'+l.replace(/&/g,'&amp;').replace(/</g,'&lt;')+'</div>'}).join('');"
            "var rs=[].slice.call(Pr.children).filter(function(d,i){return i>1&&(d.textContent[0]=='\xe2\x97\x8f'||d.textContent[0]=='\xe2\x8f\xb8')}),s=0;"
            "function H(){rs.forEach(function(d,i){d.style.background=i==s?'#1c2f45':''});rs[s]&&rs[s].scrollIntoView({block:'nearest'})}"
            "onkeydown=function(e){var k=e.key;if(k=='j'||k=='ArrowDown')s=Math.min(s+1,rs.length-1);else if(k=='k'||k=='ArrowUp')s=Math.max(s-1,0);"
            "else if(k=='Enter'&&rs[s]){location='/op?w=ssh:'+encodeURIComponent(rs[s].textContent.slice(2,16).trim());return}else return;e.preventDefault();H()};"
            "if(rs.length)H()</script>",(double)(t1.tv_sec-t0.tv_sec)*1e3+(double)(t1.tv_nsec-t0.tv_nsec)/1e6);
        (void)!write(c,ft,(size_t)fl);return;}
    if(!strncmp(req,"GET /dash",9)&&(req[9]==' '||req[9]=='?'||req[9]=='\r')){
        static const char H[]="<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><style>body{background:#000;color:#fff;font:16px system-ui;margin:16px}a{color:#4af;text-decoration:none;display:block;padding:10px;border-bottom:1px solid #222}h3{color:#888;font-weight:400;font-size:14px;margin:16px 0 4px}</style><div id=d>...</div><script>new EventSource('/dash/s').onmessage=m=>{var g={},o=[];m.data.split('|').filter(l=>l).forEach(w=>{var p=w.split('\\t');if(!g[p[0]])o.push(p[0]),g[p[0]]='';g[p[0]]+='<a href=\"/op?w='+encodeURIComponent(p[1])+'\">'+(p[2]||p[1])+'</a>'});d.innerHTML=o.map(x=>'<h3>'+x+'</h3>'+g[x]).join('')}</script>" TAPJS;
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
    if(!strncmp(req,"GET /dev/open",13)){   /* tunnel to a device's own `a serve` (:1111), then 302 there */
        char nm[64]="";{const char*q=strstr(req,"?h=");if(q){q+=3;int i=0;for(;q[i]&&q[i]!=' '&&q[i]!='&'&&i<63&&(isalnum((unsigned char)q[i])||q[i]=='-'||q[i]=='_'||q[i]=='.');i++)nm[i]=q[i];nm[i]=0;}}
        if(!nm[0]){_sresp(c,400,"text/plain","no host",7);return;}
        unsigned hh=5381;for(const char*s=nm;*s;s++)hh=hh*33u+(unsigned char)*s;int lp=8100+(int)(hh%400);   /* deterministic local port per host */
        struct sockaddr_in la={.sin_family=AF_INET,.sin_port=htons((uint16_t)lp),.sin_addr={htonl(INADDR_LOOPBACK)}};
        int up=0;{int s=socket(AF_INET,SOCK_STREAM,0);if(s>=0){up=connect(s,(void*)&la,sizeof la)==0;close(s);}}   /* already tunneled? reuse */
        if(!up){
            char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int n=listdir(ddir,paths,64);
            char host[256]="",pw[256]="";
            for(int i=0;i<n;i++){kvs_t kv=kvfile(paths[i]);const char*k=kvget(&kv,"Name");
                if(k&&!strcmp(k,nm)){const char*h2=kvget(&kv,"Host"),*p=kvget(&kv,"Password");if(h2)snprintf(host,256,"%s",h2);if(p)snprintf(pw,256,"%s",p);break;}}
            if(!host[0]){_sresp(c,404,"text/plain","no such device",14);return;}
            char hp[256],rp[8];ssh_parse(host,hp,rp);
            char opts[200];snprintf(opts,200,"-N -oStrictHostKeyChecking=accept-new -oConnectTimeout=8 -oExitOnForwardFailure=yes -L %d:127.0.0.1:1111",lp);
            char cmd[B];ssh_pre(cmd,B,pw[0]?pw:NULL,opts,rp,hp);
            if(!fork()){setsid();int z=open("/dev/null",O_RDWR);if(z>=0){dup2(z,0);dup2(z,1);dup2(z,2);}execl("/bin/sh","sh","-c",cmd,(char*)NULL);_exit(127);}
            for(int t=0;t<60&&!up;t++){int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)break;up=connect(s,(void*)&la,sizeof la)==0;close(s);if(!up)usleep(100000);}}
        if(!up){_sresp(c,504,"text/plain","offline",7);return;}
        {int s=socket(AF_INET,SOCK_STREAM,0),good=0;   /* ssh -L opens the local port even if remote :1111 is dead — verify it actually answers */
         if(s>=0){struct timeval tv={3,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
            if(connect(s,(void*)&la,sizeof la)==0){const char g[]="GET / HTTP/1.0\r\nHost:x\r\n\r\n";(void)!write(s,g,sizeof g-1);char b[16];good=read(s,b,sizeof b)>0;}close(s);}
         if(!good){static const char NS[]="<body style='background:#000;color:#f88;font:15px ui-monospace,monospace;padding:20px'>\xe2\x9c\x97 not serving \xe2\x80\x94 no <code>a serve</code> on :1111 of this device.<br><br><a style=color:#6cf href=/dev>\xe2\x86\x90 devices</a>";_sresp(c,503,"text/html",NS,sizeof NS-1);return;}}
        char url[64];snprintf(url,64,"http://127.0.0.1:%d/",lp);_redir(c,url);return;}
/* timeout-bounded ssh serve-check → "serving"|"noserve"|"offline" (timeout 7 = hard cap so a stalled ssh handshake can't hang the row) */
#define DEV_RCHECK " 'bash -c \"exec 3<>/dev/tcp/127.0.0.1/1111\" >/dev/null 2>&1 && echo SERVING || echo NOSERVE' 2>/dev/null"
#define DEV_OPTS "-oStrictHostKeyChecking=accept-new -oConnectTimeout=4 -oNumberOfPasswordPrompts=1"
    if(!strncmp(req,"GET /dev/probeall",17)){   /* ALL hosts in parallel server-side (one request → bypasses the browser's ~6-conn/origin limit) */
        char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int n=listdir(ddir,paths,64);
        signal(SIGCHLD,SIG_DFL);   /* serve runs SIGCHLD=IGN; restore so waitpid() works for these children */
        struct{int fd;pid_t pid;char nm[128];}S[64];int ns=0;
        for(int i=0;i<n&&ns<64;i++){kvs_t kv=kvfile(paths[i]);const char*nm=kvget(&kv,"Name");if(!nm)continue;
            const char*ho=kvget(&kv,"Host"),*pw=kvget(&kv,"Password");char host[256],pwd[256];
            snprintf(host,256,"%s",ho?ho:"");snprintf(pwd,256,"%s",pw?pw:"");if(!host[0])continue;
            int pfd[2];if(pipe(pfd))continue;pid_t p=fork();
            if(p==0){close(pfd[0]);char hp[256],rp[8];ssh_parse(host,hp,rp);
                char cmd[B];int l=snprintf(cmd,B,"timeout 7 ");l+=ssh_pre(cmd+l,B-l,pwd[0]?pwd:NULL,DEV_OPTS,rp,hp);
                snprintf(cmd+l,(size_t)(B-l),DEV_RCHECK);
                char out[64]="";FILE*pp=popen(cmd,"r");if(pp){(void)!fgets(out,64,pp);pclose(pp);}
                const char*r=strstr(out,"SERVING")?"serving":strstr(out,"NOSERVE")?"noserve":"offline";
                (void)!write(pfd[1],r,strlen(r));close(pfd[1]);_exit(0);}
            close(pfd[1]);S[ns].fd=pfd[0];S[ns].pid=p;snprintf(S[ns].nm,128,"%s",nm);ns++;}
        char*h=malloc(1<<15);int hl=snprintf(h,1<<15,"{");
        for(int i=0;i<ns;i++){char o[32]="";int r=(int)read(S[i].fd,o,31);o[r>0?r:0]=0;close(S[i].fd);waitpid(S[i].pid,NULL,0);
            hl+=snprintf(h+hl,(size_t)((1<<15)-hl),"%s\"%s\":\"%s\"",i?",":"",S[i].nm,o[0]?o:"offline");}
        hl+=snprintf(h+hl,(size_t)((1<<15)-hl),"}");
        _sresp(c,200,"application/json",h,hl);free(h);return;}
    if(!strncmp(req,"GET /dev/probe",14)){   /* single host (no tunnel): serving | noserve | offline — for on-click */
        char nm[64]="";{const char*q=strstr(req,"?h=");if(q){q+=3;int i=0;for(;q[i]&&q[i]!=' '&&q[i]!='&'&&i<63&&(isalnum((unsigned char)q[i])||q[i]=='-'||q[i]=='_'||q[i]=='.');i++)nm[i]=q[i];nm[i]=0;}}
        char host[256]="",pw[256]="";
        if(nm[0]){char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int n=listdir(ddir,paths,64);
            for(int i=0;i<n;i++){kvs_t kv=kvfile(paths[i]);const char*k=kvget(&kv,"Name");
                if(k&&!strcmp(k,nm)){const char*h2=kvget(&kv,"Host"),*p=kvget(&kv,"Password");if(h2)snprintf(host,256,"%s",h2);if(p)snprintf(pw,256,"%s",p);break;}}}
        if(!host[0]){_sresp(c,200,"text/plain","offline",7);return;}
        char hp[256],rp[8];ssh_parse(host,hp,rp);
        char cmd[B];int l=snprintf(cmd,B,"timeout 7 ");l+=ssh_pre(cmd+l,B-l,pw[0]?pw:NULL,DEV_OPTS,rp,hp);
        snprintf(cmd+l,(size_t)(B-l),DEV_RCHECK);
        char out[64]="";FILE*pp=popen(cmd,"r");if(pp){(void)!fgets(out,64,pp);pclose(pp);}   /* SIGCHLD=IGN: gate on output, not exit code */
        const char*r=strstr(out,"SERVING")?"serving":strstr(out,"NOSERVE")?"noserve":"offline";
        _sresp(c,200,"text/plain",r,(int)strlen(r));return;}
    if(!strncmp(req,"GET /dev",8)&&(req[8]==' '||req[8]=='?'||req[8]=='\r')){   /* device list — each opens its own served html over ssh; status dots probe on click / check-all */
        char*h=malloc(1<<16);if(!h){_sresp(c,500,"text/plain","oom",3);return;}
        int hl=snprintf(h,1<<16,"<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>devices</title>"
            "<style>body{margin:0;background:#000;color:#ddd;font:15px ui-monospace,monospace;padding:14px}h1{font-size:15px;color:#6cf;margin:2px 0 8px;font-weight:normal}"
            "button{background:#0b0f1a;color:#9cf;border:1px solid #2a3a5a;border-radius:6px;padding:7px 14px;font:13px ui-monospace,monospace;cursor:pointer}#cs{color:#666;font-size:12px;margin-left:8px}"
            "a.d{display:flex;align-items:center;gap:10px;color:#9cf;text-decoration:none;padding:12px 14px;margin:7px 0;border:1px solid #233;border-radius:7px;background:#0a0a0a}a.d:active{background:#13320f}a.d.dead{opacity:.45}"
            ".st{flex:none;width:11px;height:11px;border-radius:50%%;background:#3a3a3a}.st.wait{background:#46c}.st.ok{background:#3c3}.st.no{background:#c93}.st.off{background:#333}"
            ".nm{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}small{color:#666;font-size:12px}.msg{flex:none;color:#888;font-size:12px}</style>"
            "<h1>devices &mdash; open each one's own <code>a serve</code> over ssh</h1>"
            "<div><button onclick=checkAll()>\xe2\x9f\xb3 check all</button><span id=cs></span></div>");
        char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int n=listdir(ddir,paths,64);
        for(int i=0;i<n&&hl<(1<<16)-1024;i++){kvs_t kv=kvfile(paths[i]);const char*nm=kvget(&kv,"Name"),*ho=kvget(&kv,"Host");
            if(!nm)continue;hl+=snprintf(h+hl,(size_t)((1<<16)-hl),"<a class=d data-h=\"%s\" href=# onclick=\"op(this);return false\"><span class=st></span><span class=nm>%s <small>%s</small></span><span class=msg></span></a>",nm,nm,ho?ho:"");}
        if(n<=0)hl+=snprintf(h+hl,(size_t)((1<<16)-hl),"<p><small>no devices &mdash; add with <code>a ssh add</code></small></p>");
        hl+=snprintf(h+hl,(size_t)((1<<16)-hl),"%s",
            "<script>function set(el,s,t){el.querySelector('.st').className='st '+s;el.querySelector('.msg').textContent=t||'';el.classList.toggle('dead',s=='no'||s=='off')}"
            "function paint(el,t){set(el,t=='serving'?'ok':t=='noserve'?'no':'off',t=='serving'?'\\u25cf serving':t=='noserve'?'\\u2717 not serving':'\\u00b7 offline')}"
            "function op(el){set(el,'wait','\\u27f3');fetch('/dev/probe?h='+encodeURIComponent(el.dataset.h)).then(function(r){return r.text()}).then(function(t){"
            "if(t=='serving'){set(el,'ok','opening\\u2026');location.href='/dev/open?h='+encodeURIComponent(el.dataset.h)}else paint(el,t)},function(){set(el,'off','\\u00b7 error')})}"
            "function checkAll(){var rows=[].slice.call(document.querySelectorAll('a.d')),cs=document.getElementById('cs');rows.forEach(function(el){set(el,'wait','\\u27f3')});cs.textContent=' checking '+rows.length+'\\u2026';"
            "fetch('/dev/probeall').then(function(r){return r.json()}).then(function(m){rows.forEach(function(el){paint(el,m[el.dataset.h]||'offline')});cs.textContent=' done'},function(){cs.textContent=' error'})}</script>");
        _sresp(c,200,"text/html",h,hl);free(h);return;}
    if(!strncmp(req,"GET /stream/s",13)){
        char dev[64]="";{const char*q=strstr(req,"?dev=");if(q){q+=5;int i=0;for(;q[i]&&q[i]!=' '&&q[i]!='&'&&i<63&&(isalnum((unsigned char)q[i])||q[i]=='-'||q[i]=='_'||q[i]=='.');i++)dev[i]=q[i];dev[i]=0;}}
        char out[64]="";{const char*q=strstr(req,"&o=");if(q){q+=3;int i=0;for(;q[i]&&q[i]!=' '&&q[i]!='&'&&i<63&&(isalnum((unsigned char)q[i])||q[i]=='-'||q[i]=='_'||q[i]=='.');i++)out[i]=q[i];out[i]=0;}}  /* local output: name, "all"=whole layout, or empty=focused */
        pid_t fp=fork();if(fp<0){_sresp(c,500,"text/plain","fork",4);return;}
        if(fp>0)return;signal(SIGCHLD,SIG_DFL);
        char gc[1024];int remote=0;
        if(dev[0]&&strcmp(dev,"local")&&strcmp(dev,DEV)){       /* remote device: ssh in, capture to stdout */
            char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int n=listdir(ddir,paths,64);
            char host[256]="",pw[256]="";
            for(int i=0;i<n;i++){kvs_t kv=kvfile(paths[i]);const char*nm=kvget(&kv,"Name");
                if(nm&&!strcmp(nm,dev)){const char*h=kvget(&kv,"Host"),*p=kvget(&kv,"Password");if(h)snprintf(host,256,"%s",h);if(p)snprintf(pw,256,"%s",p);break;}}
            if(!host[0])_exit(0);
            char hp[256],port[8];ssh_parse(host,hp,port);char pre[600];
            ssh_pre(pre,600,pw[0]?pw:NULL,"-oStrictHostKeyChecking=accept-new -oConnectTimeout=6",port,hp);
            char ro[64]="";                     /* remote output: &o= wins else auto-pick first (else grim grabs the whole composite) */
            if(out[0]&&strcmp(out,"all"))snprintf(ro,64,"%s",out);
            else if(!out[0]){char dc[800];snprintf(dc,800,"%s 'SWAYSOCK=$(ls ${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/sway-ipc.*.sock 2>/dev/null|head -1) swaymsg -t get_outputs 2>/dev/null'",pre);
                FILE*dp=popen(dc,"r");char js[2048]="";if(dp){size_t jn=fread(js,1,2047,dp);js[jn]=0;pclose(dp);}
                char*q=strstr(js,"\"name\"");if(q&&(q=strchr(q+6,':'))&&(q=strchr(q,'"'))){q++;int i=0;while(*q&&*q!='"'&&i<63&&(isalnum((unsigned char)*q)||*q=='-'||*q=='_'))ro[i++]=*q++;ro[i]=0;}}
            char rg[80]="";if(ro[0])snprintf(rg,80,"-o %s ",ro);
            snprintf(gc,sizeof gc,"%s 'R=${XDG_RUNTIME_DIR:-/run/user/$(id -u)};W=$(ls $R/wayland-* 2>/dev/null|grep -v lock|head -1);export XDG_RUNTIME_DIR=$R WAYLAND_DISPLAY=$(basename \"$W\");while :;do grim %s-t ppm - 2>/dev/null||break;done|ffmpeg -loglevel error -f image2pipe -i - -vf scale=1024:-2 -q:v 6 -f mjpeg - 2>/dev/null'",pre,rg);remote=1;}  /* rg=one output; persistent ssh+ffmpeg->MJPEG */
        if(!remote){                                            /* local sway: derive env + focused output */
            const char*rt=getenv("XDG_RUNTIME_DIR");char rtb[64];
            if(!rt||!rt[0]){snprintf(rtb,64,"/run/user/%d",(int)getuid());rt=rtb;}
            setenv("XDG_RUNTIME_DIR",rt,1);
            {char ic[160];snprintf(ic,160,"ls %s/wayland-* 2>/dev/null|grep -v lock|head -1",rt);
             FILE*p=popen(ic,"r");char wl[128]="";if(p){if(fgets(wl,128,p))wl[strcspn(wl,"\n")]=0;pclose(p);}
             if(wl[0]){const char*b=strrchr(wl,'/');setenv("WAYLAND_DISPLAY",b?b+1:wl,1);}}
            char ob[64]="";if(out[0]&&strcmp(out,"all"))snprintf(ob,64,"%s",out);   /* monitor name -> that one; empty/all -> whole layout */
            char og[80]="";if(ob[0])snprintf(og,80,"-o '%s' ",ob);
            snprintf(gc,sizeof gc,"while :;do grim %s-t ppm - 2>/dev/null||break;done|ffmpeg -loglevel error -f image2pipe -i - -vf scale=1024:-2 -q:v 6 -f mjpeg - 2>/dev/null",og);}  /* raw grab -> persistent ffmpeg mjpeg; per-frame ffmpeg startup was the 16fps ceiling */
        static const char SH[]="HTTP/1.1 200 OK\r\nContent-Type:multipart/x-mixed-replace;boundary=f\r\nCache-Control:no-store\r\n\r\n";
        if(write(c,SH,sizeof SH-1)<0)_exit(0);
        char lg[P];snprintf(lg,P,"%s/local/serve.log",AROOT);struct timeval t0;gettimeofday(&t0,NULL);int fn=0;   /* status -> tail adata/local/serve.log */
        FILE*g=popen(gc,"r");if(!g)_exit(0);                       /* ONE persistent capture; split its MJPEG byte-stream into frames */
        size_t cap=1<<21,len=0,sc=0,r;unsigned char*buf=malloc(cap);char tb[65536];
        while(buf&&(r=fread(tb,1,sizeof tb,g))>0){
            if(len+r>cap){cap=len+r+(1<<20);unsigned char*nb=realloc(buf,cap);if(!nb)break;buf=nb;}
            memcpy(buf+len,tb,r);len+=r;
            for(size_t e=sc;e+1<len;e++)if(buf[e]==0xff&&buf[e+1]==0xd9){       /* FFD9=EOI: one complete frame */
                size_t fl=e+2;char hd[96];int hl=snprintf(hd,sizeof hd,"--f\r\nContent-Type:image/jpeg\r\nContent-Length:%zu\r\n\r\n",fl);
                if(write(c,hd,(size_t)hl)<0||write(c,buf,fl)<0||write(c,"\r\n",2)<0)_exit(0);   /* client gone -> exit -> SIGPIPE ends the pipeline (||break) */
                if(++fn==1||fn%15==0){struct timeval w;gettimeofday(&w,NULL);double el=(double)(w.tv_sec-t0.tv_sec)+(w.tv_usec-t0.tv_usec)/1e6;
                    FILE*lf=fopen(lg,"a");if(lf){fprintf(lf,"stream %s/%s %df %.1ffps %zuKB\n",dev[0]?dev:"local",out[0]?out:"all",fn,el>0?fn/el:0,fl/1024);fclose(lf);}}
                memmove(buf,buf+fl,len-fl);len-=fl;e=(size_t)-1;sc=0;}
            sc=len>1?len-1:0;}
        close(c);_exit(0);}
    if(!strncmp(req,"GET /stream",11)&&(req[11]==' '||req[11]=='?'||req[11]=='\r')){
        char nav[4096];int nl=snprintf(nav,sizeof nav,"<a href=# onclick=\"if(cur)sel(cur);return false\" style=\"color:#f99;border-color:#5a2a2a\">\xe2\x96\xa0 stop</a><a data-d=local href=# onclick=\"sel('local');return false\" title=\"this machine \xc2\xb7 all monitors\">\xe2\x97\x89 %s \xc2\xb7 local</a>",DEV);
        {FILE*p=popen("R=${XDG_RUNTIME_DIR:-/run/user/$(id -u)};S=$(ls $R/sway-ipc.*.sock 2>/dev/null|head -1);SWAYSOCK=$S swaymsg -t get_outputs 2>/dev/null|python3 -c 'import sys,json;[print(o[\"name\"]) for o in json.load(sys.stdin)]' 2>/dev/null","r");   /* enumerate local monitors; device click streams them all stacked (composited whole-layout = 100Mpx/frame, unusable) */
         char on[64];if(p){while(fgets(on,64,p)&&nl<3100){on[strcspn(on,"\n")]=0;if(!on[0])continue;
             nl+=snprintf(nav+nl,(size_t)(sizeof nav-(size_t)nl),"<a class=sub data-d=\"local:%s\" href=# onclick=\"sel('local:%s');return false\">\xe2\x96\xa1 %s</a>",on,on,on);}pclose(p);}}
        {char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int n=listdir(ddir,paths,64);
         for(int i=0;i<n&&nl<3500;i++){kvs_t kv=kvfile(paths[i]);const char*nm=kvget(&kv,"Name");
            if(nm)nl+=snprintf(nav+nl,(size_t)(sizeof nav-(size_t)nl),"<a data-d=\"%s\" href=# onclick=\"sel('%s');return false\">%s</a>",nm,nm,nm);}}
        char h[8192];int hl=snprintf(h,sizeof h,"<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>stream</title><style>html,body{margin:0;height:100%%;background:#000;font:13px ui-monospace,monospace}#b{position:fixed;top:0;left:0;bottom:0;width:150px;background:#0a0a0a;border-right:1px solid #222;padding:6px;display:flex;flex-direction:column;gap:4px;overflow-y:auto;z-index:9}#b a{color:#9cf;text-decoration:none;padding:7px 9px;border:1px solid #233;border-radius:5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;cursor:pointer}#b a.on{background:#13320f;color:#9f9;border-color:#2a5a2a}#b a.sub{margin-left:14px;font-size:11px;border-left:3px solid #2a5a2a;border-radius:0 5px 5px 0;color:#8be}#st{position:fixed;bottom:10px;right:14px;display:none;color:#7d9;font:12px ui-monospace,monospace;background:#000a;padding:4px 10px;border-radius:6px;z-index:6}#st.ld{color:#9cf;animation:pl 1s infinite}@keyframes pl{50%%{opacity:.4}}#w{position:fixed;inset:0 0 0 160px;display:flex;flex-direction:column}#w img{flex:1;min-height:0;object-fit:contain}#w p{margin:auto;color:#888}</style><div id=b>%s</div><div id=w><p>\xe2\x86\x90 pick a device</p></div><div id=st></div><script>var w=document.getElementById('w'),st=document.getElementById('st'),cur='',n=0;function P(){document.querySelectorAll('#b a').forEach(function(a){a.classList.toggle('on',a.getAttribute('data-d')===cur)})}function sel(d){w.innerHTML='';if(cur===d){cur='';st.style.display='none';P();return}cur=d;n=0;st.style.display='block';st.className='ld';st.textContent='\xe2\x9f\xb3 starting '+d+'\xe2\x80\xa6';var s=[].slice.call(document.querySelectorAll('#b a.sub')).map(function(a){return a.getAttribute('data-d')}).filter(function(x){return x.indexOf(d+':')==0});(s.length>1?s:[d]).forEach(function(m){var i=new Image();i.onload=function(){st.className='';st.textContent='\xe2\x97\x8f '+d+' \xc2\xb7 '+(++n)+' frames'};i.onerror=function(){st.className='';st.textContent='\xe2\x9c\x97 failed \xc2\xb7 '+d};var q=m.split(':');i.src='/stream/s?dev='+encodeURIComponent(q[0])+(q[1]?'&o='+encodeURIComponent(q[1]):'');w.appendChild(i)});P()}</script>",nav);
        _sresp(c,200,"text/html",h,hl);return;}
    if(!strncmp(req,"GET /op",7)&&(req[7]==' '||req[7]=='?'||req[7]=='\r')){
        const char*qw=strstr(req,"?w=");int idx=(qw&&isdigit((unsigned char)qw[3]))?atoi(qw+3):-1;
        if(idx>=0){char tc[256];
            snprintf(tc,256,"p=$(tmux display-message -t a:%d -p '#{pane_pid}' 2>/dev/null);c=$(cat /proc/$p/task/$p/children 2>/dev/null|cut -d' ' -f1);[ -n \"$c\" ]&&cat /proc/$c/comm 2>/dev/null",idx);
            FILE*pp=popen(tc,"r");char nm[64]={0};
            if(pp){if(fgets(nm,64,pp))nm[strcspn(nm,"\n")]=0;pclose(pp);}
            if(strcmp(nm,"claude")&&strcmp(nm,"codex")&&strcmp(nm,"gemini")&&strcmp(nm,"aider")){
                static const char NO[]="<!doctype html><style>body{background:#000;color:#fff;font:16px system-ui;text-align:center;padding-top:40vh}a{color:#4af}</style>no agent<br><br><a href=/dash>← dash</a>";
                _sresp(c,200,"text/html",NO,sizeof NO-1);return;}}
        char tf[P];snprintf(tf,P,"%s/lib/term.html",SDIR);size_t tl=0;char*th=readf(tf,&tl); /* direct-DOM terminal page (replaced xterm.js CDN 7/10) */
        if(th){_sresp(c,200,"text/html",th,(int)tl);free(th);}
        else _sresp(c,404,"text/plain","no term.html",12);
        return;}
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
    unlink("/tmp/a_extreload.fifo");mkfifo("/tmp/a_extreload.fifo",0644); /* ext hot-reload channel; left unopened so writes only land when a worker is reading */
    /* run-shell in a hook forks sh inside the tmux server = +0.55ms per window switch (measured); wait-for -S is ~us, bridge proc converts to fifo poke */
    (void)!system("for h in after-new-window after-rename-window after-kill-pane session-window-changed;do tmux set-hook -g $h 'wait-for -S a_dash' 2>/dev/null;done;"
        "p=/tmp/.a_dashbr.pid;kill -0 $(cat $p 2>/dev/null) 2>/dev/null||{ (while :;do tmux wait-for a_dash 2>/dev/null||sleep 1;echo x>/tmp/a_dash.fifo 2>&-;done)& echo $!>$p; }");
    {const char*op=getenv("PATH")?:"";char np[P];snprintf(np,P,"%s/.local/bin:/opt/homebrew/bin:/usr/local/bin:%s",HOME,op);setenv("PATH",np,1);}
    int port=argc>2?atoi(argv[2]):1111;
    if(argc>3){if(!realpath(argv[3],_sdir)||!dexists(_sdir)){printf("x no dir %s\n",argv[3]);return 1;}
        printf("+ site %s\n",_sdir);}
    else{printf("> generating HTML...\n");_html_gen();_prompt_gen();
        if(!_shtml){puts("x HTML generation failed");return 1;}
        printf("+ %d bytes cached\n",_shlen);}
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
