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
            hl+=snprintf(h+hl,(size_t)(cap-1-hl),"<div class=ni><button onclick=\"%s('%s',this)\" class=nx>x</button><span style=\"color:#888;display:inline-block;width:104px\">%s</span><span style=\"flex:1\">%s</span><a href=\"/doc?f=%s/%s\" style=\"color:#888;margin-left:8px;text-decoration:none\">edit</a></div>",arc,names[i],hu,ln+6,kind,names[i]);got=1;break;}}
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
    for(int i=1;i<nr;i++){__typeof__(rows[0]) k=rows[i];int j=i-1;const char*kk=k.dl[0]?k.dl:"~";
        while(j>=0){int bf=md==1?strcmp(k.name,rows[j].name)>0:md==2?strcmp(kk,rows[j].dl[0]?rows[j].dl:"~")<0:strcmp(rows[j].pri,k.pri)>0;
            if(!bf)break;rows[j+1]=rows[j];j--;}rows[j+1]=k;}
    int hl=snprintf(h,(size_t)cap,SYNC_HTML "<div class=ni style=\"color:#888;border-bottom:1px solid #444;margin-top:10px\"><span class=nx style=\"visibility:hidden\">x</span><span style=\"display:inline-block;width:124px\">WHEN</span><span style=\"display:inline-block;width:54px\">PRI</span>TASK <span style=\"color:#555\">— ⚑=deadline else created · red P≤1000</span></div>",sync_age());
    for(int i=0;i<nr&&i<4&&hl<cap-512;i++){   /* top 4 */
        const char*c=strcmp(rows[i].pri,"01000")<=0?"#fff":strcmp(rows[i].pri,"10000")<=0?"#ccc":"#aaa";
        char fr[40];{int y,mo,dd,h=0,mi=0;struct tm t={0};
            if(rows[i].dl[0]&&sscanf(rows[i].dl,"%d-%d-%d %d:%d",&y,&mo,&dd,&h,&mi)>=3){t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=dd;mktime(&t);
                char b[32];int bl2=(int)strftime(b,32,"%b %-d",&t);int h12=h%12;if(!h12)h12=12;snprintf(b+bl2,32-(size_t)bl2," %d:%02d%s",h12,mi,h>=12?"pm":"am");snprintf(fr,40,"⚑%s",b);}
            else{const char*u=strrchr(rows[i].name,'_');char ts[16]="";if(u)snprintf(ts,16,"%.15s",u+1);ts_date(u?ts:NULL,fr,40);}}
        hl+=snprintf(h+hl,(size_t)(cap-1-hl),"<div class=ni><button onclick=\"arct('%s',this)\" class=nx>x</button><span style=\"color:#fff;display:inline-block;width:124px\">%s</span><span style=\"color:%s;display:inline-block;width:54px\">P%s</span>%s</div>",rows[i].name,fr,c,rows[i].pri,rows[i].txt);}
    if(!hl)hl=snprintf(h,(size_t)cap,"<div style=\"color:#888\">No tasks</div>");
    return hl;
}
static void _html_gen(void){
    char tf[P];snprintf(tf,P,"%s/lib/ui_full.html",SDIR);_sgen_t=time(NULL);
    char*src=readf(tf,NULL);if(!src)return;
    char*s=src;
    /* substitute placeholders */
    int cap=131072;_shtml=malloc((size_t)cap);_shlen=0;
    #define EMIT(p,n) {if(_shlen+(n)>=cap){cap*=2;_shtml=realloc(_shtml,(size_t)cap);}memcpy(_shtml+_shlen,p,(size_t)(n));_shlen+=(n);}
    for(char*p=s;*p;){
        if(*p=='_'&&p[1]=='_'){
            char*end=strstr(p+2,"__");
            if(end&&(end-p)<16){
                char tag[16];memcpy(tag,p+2,(size_t)(end-p-2));tag[end-p-2]=0;
                if(!strcmp(tag,"CMDS")){FILE*f=popen("a i","r");char l[16384],e[16400];   /* ["name","desc"], per `a i` line, unbounded (64K buffers cut it mid-string = blank page); " and \ neutered — one raw " kills the router script; JS ignores the trailing comma */
                    while(f&&fgets(l,16384,f)){l[strcspn(l,"\n")]=0;for(char*q=l;*q;q++)if(*q=='"')*q='\'';else if(*q=='\\')*q='/';
                        char*t=strchr(l,'\t');if(t)*t=0;if(*l){int el=snprintf(e,16400,"[\"%s\",\"%s\"],",l,t?t+1:"");EMIT(e,el);}}
                    if(f)pclose(f);}
                else if(!strcmp(tag,"PO")){EMIT("<option value=\"\">~ (home)</option>",(int)strlen("<option value=\"\">~ (home)</option>"));}
                else if(!strcmp(tag,"DO")){char h[64]="";gethostname(h,64);char o[128];int ll=snprintf(o,128,"<option value=\"\">local: %s</option>",h);EMIT(o,ll);
                    char hbr[300]="";/* homebox is a role pointer (ssh.c hb): label it with the real entry sharing its Host so the picker says which box it is */
                    {char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int m=listdir(ddir,paths,64);char hbh[512]="";
                        for(int i=0;i<m&&!hbh[0];i++){kvs_t kv=kvfile(paths[i]);const char*nm=kvget(&kv,"Name"),*ho=kvget(&kv,"Host");
                            if(nm&&ho&&!strcasecmp(nm,"homebox"))snprintf(hbh,512,"%s",ho);}
                        if(hbh[0]){snprintf(hbr,300,"%s",hbh); /* no named sibling -> show the raw user@host */
                            for(int i=0;i<m;i++){kvs_t kv=kvfile(paths[i]);const char*nm=kvget(&kv,"Name"),*ho=kvget(&kv,"Host");
                                if(nm&&ho&&strcasecmp(nm,"homebox")&&!strcmp(ho,hbh)){snprintf(hbr,300,"%s",nm);break;}}}}
                    char gc[B];snprintf(gc,B,"grep -h '^Name:' '%s/ssh/'*.txt 2>/dev/null|sed 's/Name: //'|sort -u",SROOT);
                    FILE*df=popen(gc,"r");char dln[256];while(df&&fgets(dln,256,df)){dln[strcspn(dln,"\n")]=0;if(!dln[0])continue;
                        char o[640];int ol=!strcasecmp(dln,"homebox")&&hbr[0]
                            ?snprintf(o,640,"<option value=\"%s\">%s → %s</option>",dln,dln,hbr)
                            :snprintf(o,640,"<option>%s</option>",dln);EMIT(o,ol);}if(df)pclose(df);}
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
/* latency surfaces (/op /fw): COOP+COEP => crossOriginIsolated => performance.now() at 5us instead of 100us —
   without this the sub-ms meters quantize to .x0. Scoped here, NOT global: COEP would break pages that load
   cross-origin subresources. Both pages are fully self-contained; /op iframes stay same-origin. */
static void _siso(int c,const char*body,int bl){
    char h[320];int hl=snprintf(h,320,"HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nContent-Length:%d\r\nConnection:close\r\nCache-Control:no-store\r\nCross-Origin-Opener-Policy:same-origin\r\nCross-Origin-Embedder-Policy:require-corp\r\n\r\n",bl);
    (void)!write(c,h,(size_t)hl);if(bl)(void)!write(c,body,(size_t)bl);
}
static void _sdoc(int c,const char*body,int bl){_sresph(c,200,"text/html; charset=utf-8",body,bl,"no-cache");}
/* proxy the u server (127.0.0.1:9999) — net worth (+ any u stat) has ONE source there; we forward, never recompute (no drift). No shell → the update body can't inject. */
static char* _ufwd(const char*path,const char*body,char*buf,int cap){
    int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return 0;
    struct sockaddr_in la={.sin_family=AF_INET,.sin_port=htons(9999),.sin_addr={htonl(INADDR_LOOPBACK)}};
    struct timeval tv={2,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
    if(connect(s,(void*)&la,sizeof la)!=0){close(s);return 0;}
    char rq[1200];int rn=body
        ?snprintf(rq,1200,"POST %s HTTP/1.0\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",path,(int)strlen(body),body)
        :snprintf(rq,1200,"GET %s HTTP/1.0\r\nConnection: close\r\n\r\n",path);
    if(write(s,rq,(size_t)rn)!=rn){close(s);return 0;}
    int tot=0,r;while(tot<cap-1&&(r=(int)read(s,buf+tot,cap-1-tot))>0)tot+=r;
    close(s);buf[tot]=0;char*b=strstr(buf,"\r\n\r\n");return b?b+4:buf;}
static int _scmp(const void*a,const void*b){return strcmp((const char*)a,(const char*)b);}
static const int*g_bc;   /* /book freq sort: serve.log opens desc, tie=alpha */
static int g_bccmp(const void*a,const void*b){int x=*(const int*)a,y=*(const int*)b,d=g_bc[y]-g_bc[x];return d?d:x-y;}
static void _qn(const char*req,char*nm){nm[0]=0;const char*q=strstr(req,"?n=");if(!q)return;q+=3;int i=0;for(;q[i]&&q[i]!='&'&&q[i]!=' '&&i<127;i++)nm[i]=q[i];nm[i]=0;}
static void _qp(const char*req,const char*k,char*d,int n){d[0]=0;const char*q=strstr(req,k);if(!q)return;q+=strlen(k);int i=0;for(;q[i]&&q[i]!=' '&&q[i]!='&'&&i<n-1&&(isalnum((unsigned char)q[i])||q[i]=='-'||q[i]=='_'||q[i]=='.');i++)d[i]=q[i];d[i]=0;}
static int _sshpre(const char*dev,char*pre,int n){ /* fleet device name -> ssh cmd prefix; 1=remote 0=local -1=unknown */
    if(!dev[0]||!strcmp(dev,"local")||!strcmp(dev,DEV))return 0;
    char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int m=listdir(ddir,paths,64);
    char host[256]="",pw[256]="";
    for(int i=0;i<m;i++){kvs_t kv=kvfile(paths[i]);const char*nm=kvget(&kv,"Name");
        if(nm&&!strcmp(nm,dev)){const char*h=kvget(&kv,"Host"),*p=kvget(&kv,"Password");if(h)snprintf(host,256,"%s",h);if(p)snprintf(pw,256,"%s",p);break;}}
    if(!host[0])return -1;
    char hp[256],port[8];ssh_parse(host,hp,port);
    ssh_pre(pre,n,pw[0]?pw:NULL,"-oStrictHostKeyChecking=accept-new -oConnectTimeout=6",port,hp);return 1;}
static void _redir(int c,const char*url){char h[768];int hl=snprintf(h,768,"HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length:0\r\nConnection:close\r\n\r\n",url);(void)!write(c,h,(size_t)hl);}
/* ?f=<path> → rel (urldecoded). returns 1 if valid (non-empty, no ..) */
static int _docrel(const char*req,char*rel){rel[0]=0;const char*q=strstr(req,"?f=");if(!q)q=strstr(req,"&f=");if(!q)return 0;q+=3;
    int j=0;for(;*q&&*q!=' '&&*q!='&'&&j<P-1;q++){if(*q=='%'&&q[1]&&q[2]){char x[3]={q[1],q[2],0};rel[j++]=(char)strtol(x,0,16);q+=2;}else rel[j++]=*q=='+'?' ':*q;}rel[j]=0;
    return rel[0]&&!strstr(rel,"..");}
/* editor page: form POST + save button + escaped textarea + optional saved-banner (zero JS) */
static int _bkcol(const char*nm,int col,char*o,int osz){int r=-1;char ip[P];snprintf(ip,P,"%s/git/books/index.txt",AROOT);  /* tab-col text of row nm; -1 = no row */
    size_t il=0;char*ix=readf(ip,&il);if(!ix)return -1;size_t nl=strlen(nm);o[0]=0;
    for(char*l=ix;l<ix+il;){char*e=memchr(l,'\n',(size_t)(ix+il-l)),*lim=e?e:ix+il;
        char*t1=memchr(l,'\t',(size_t)(lim-l)),*t2=t1?memchr(t1+1,'\t',(size_t)(lim-t1-1)):0;
        if(t1&&t2&&(size_t)(t2-t1-1)==nl&&!strncmp(t1+1,nm,nl)){r=0;
            char*p=l;int cc=1;while(cc<col&&p<lim){char*nt=memchr(p,'\t',(size_t)(lim-p));if(!nt)break;p=nt+1;cc++;}
            if(cc==col){char*nt=memchr(p,'\t',(size_t)(lim-p));int L=(int)((nt&&nt<lim?nt:lim)-p);if(L>=osz)L=osz-1;memcpy(o,p,(size_t)L);o[L]=0;r=L;}
            break;}
        if(e)l=e+1;else break;}
    free(ix);return r;}
static long _bkpos(const char*nm){char b[24];return _bkcol(nm,5,b,24)<0?-1:atol(b);}  /* col5 saved offset; -1 = no row */
static void _bkfile(const char*nm,char*tf){  /* reading text resolution order, shared by reader + say */
    snprintf(tf,P,"%s/books/%s/output/explained.txt",AROOT,nm);
    if(access(tf,R_OK))snprintf(tf,P,"%s/books/%s/output/%s.txt",AROOT,nm,nm);
    if(access(tf,R_OK))snprintf(tf,P,"%s/books/%s/output/transcript.txt",AROOT,nm);  /* canonical elsewhere: help.c chat context, sync filter */
    if(access(tf,R_OK))snprintf(tf,P,"%s/books/%s/source.txt",AROOT,nm);}
static void _docpage(int c,const char*rel,const char*body,size_t bl,const char*saved,const char*ds){
    char*h=malloc(bl*6+2048);if(!h){_sresp(c,500,"text/plain","oom",3);return;}
    int hl=snprintf(h,2048,"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>%s</title><style>body{margin:0;background:#0b0b0b;color:#ddd;font:13px/1.5 ui-monospace,monospace}#bar{position:sticky;top:0;background:#000;padding:7px 12px;border-bottom:1px solid #222;display:flex;gap:12px;align-items:center}#bar b{color:#fff}button{background:#222;color:#fff;border:1px solid #555;padding:3px 14px;font:inherit;cursor:pointer}#s{color:#bbb}textarea{display:block;width:100%%;height:calc(100vh - 37px);box-sizing:border-box;background:#0b0b0b;color:#ddd;border:0;outline:none;padding:12px;font:inherit;resize:none;white-space:pre-wrap;word-break:break-word}</style><form method=POST enctype=\"text/plain\" action=\"/doc?f=%s%s\"><div id=bar><b>%s</b><button>save</button><span id=s>%s</span></div><textarea name=b spellcheck=false>",rel,rel,ds,rel,saved?saved:"");
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
            hl+=snprintf(h+hl,(size_t)((1<<18)-hl),"<div style=color:#777;padding:4px 16px>%s/</div>",r2+off);hl=_docls(h,hl,r2,(int)strlen(r2)+1);}
        else if(strstr(r2,"/archive/"))hl+=snprintf(h+hl,(size_t)((1<<18)-hl),"<a href=\"/doc?f=%s\">%s</a>",r2,r2+off);
        else hl+=snprintf(h+hl,(size_t)((1<<18)-hl),"<div style=\"display:flex\"><a style=\"flex:1\" href=\"/doc?f=%s\">%s</a><a href=\"#\" style=\"color:#555\" onclick=\"fetch('/doc-arch?f=%s').then(function(){location.reload()});return false\">arch</a></div>",r2,r2+off,r2);}
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
    char cty[64];{const char*tn=ttyname(s);snprintf(cty,64,"%s",tn?tn:"");}
    pid_t p=fork();
    if(!p){close(m);setsid();ioctl(s,TIOCSCTTY,0);dup2(s,0);dup2(s,1);dup2(s,2);close(s);
        setenv("TERM","xterm-256color",0);
        unsetenv("TMUX");unsetenv("TMUX_PANE");  /* serve may live in a pane; inherited TMUX would make `a tmux` switch the real client instead of grouped-attaching this pty */
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
                if(co&&ro){struct winsize w={.ws_row=(unsigned short)atoi(ro+7),.ws_col=(unsigned short)atoi(co+7)};ioctl(m,TIOCSWINSZ,&w);continue;}
                /* claim (/fw): switch-client onto itself re-takes window-size latest hidden — never resize-window, it manual-locks (def3b2ee); non-tmux pty no-ops */
                if(strstr(buf,"\"claim\"")){char cc[300];snprintf(cc,300,"s=$(tmux lsc -f '#{==:#{client_tty},%s}' -F '#{session_name}' 2>/dev/null);[ -n \"$s\" ]&&tmux switch-client -c %s -t \"$s\" 2>/dev/null",cty,cty);(void)!system(cc);continue;}}
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
static char*_phtml;static int _phlen;static time_t _pgen_t;
static void _prompt_gen(void){ /* cached; GET /prompt regens when sources newer than cache (mtime check) */
        int pp[2];if(pipe(pp))return;pid_t ch=fork();
        if(!ch){dup2(pp[1],1);close(pp[0]);close(pp[1]);(void)!chdir(SDIR);execlp("a","a","prompt","show",(char*)0);_exit(1);}
        close(pp[1]);size_t cap=1<<19,ol=0;char*o=malloc(cap);
        if(o)for(int r;(r=(int)read(pp[0],o+ol,cap-1-ol))>0;){ol+=(size_t)r;
            if(ol+8192>cap){char*t=realloc(o,cap*=2);if(!t){free(o);o=NULL;break;}o=t;}}
        close(pp[0]);waitpid(ch,NULL,0);
        if(!o)return;
        o[ol]=0;
        char*h=malloc(ol*6+2048);if(!h){free(o);return;}
        char cm[8192];int cl;size_t HL=strlen(HOME);
        load_cfg();const char*pap=cfget("prompt");if(!*pap)pap="default";  /* active prompt file feeds the unified prompt; CP[0] + selector reflect it */
        char pfmt[P],plbl[80];snprintf(pfmt,P,"%%s/common/prompts/%s.txt",pap);snprintf(plbl,80,"%s.txt (active)",pap);
        struct{const char*lbl,*fmt,*root,*mk;long off;}CP[]={{plbl,pfmt,SROOT,0,-1},{"AGENTS.md","%s/AGENTS.md",SDIR,0,-1},{"mem index","%s/mem/index.txt",SROOT,"==> mem index <==",-1},{"installed tools","",0,"Installed tools on this device:",-1},{"codebase (a cat)","%s/local/a_cat.txt",AROOT,"a_cat.txt (",-1}};
        int N=5;
        for(int i=0;i<N;i++){if(CP[i].off>=0)continue;char key[160]={0};
            if(CP[i].mk)snprintf(key,160,"%s",CP[i].mk);
            else if(CP[i].fmt[0]){char fp[P];snprintf(fp,P,CP[i].fmt,CP[i].root);FILE*f=fopen(fp,"r");
                if(f){char ln[160];while(fgets(ln,160,f)){ln[strcspn(ln,"\n")]=0;if((int)strlen(ln)>8){snprintf(key,160,"%s",ln);break;}}fclose(f);}}
            if(key[0]){char*pq=strstr(o,key);if(pq)CP[i].off=(long)(pq-o);}}
        char pfs[64][P];int np2=0;{char pdir[P];snprintf(pdir,P,"%s/common/prompts",SROOT);np2=listdir(pdir,pfs,64);}
        cl=snprintf(cm,8192,"<div class=c><b>prompt files</b> <span class=g>· view/edit · ★ active · load: a prompt &lt;name&gt;</span><br>");
        for(int i=0;i<np2;i++){const char*b=bname(pfs[i]),*dot=strrchr(b,'.');char nm[64];snprintf(nm,64,"%.*s",(int)(dot?dot-b:(long)strlen(b)),b);
            int act=!strcmp(nm,pap);cl+=snprintf(cm+cl,(size_t)(8192-cl),"<a class=k href=\"/doc?f=common/prompts/%s\"%s>%s%s</a>&nbsp; ",b,act?" style=color:#ddd":"",act?"★":"",nm);}
        cl+=snprintf(cm+cl,(size_t)(8192-cl),"</div>");
        cl+=snprintf(cm+cl,(size_t)(8192-cl),"<div class=c><b>unified prompt</b> <span class=g>= active prompt file + AGENTS.md + mem index + tools + codebase · click to jump · edit → saves to git</span><br>");
        for(int i=0;i<N;i++){char fp[P]="";if(CP[i].fmt[0])snprintf(fp,P,CP[i].fmt,CP[i].root);
            struct stat st;long sz=fp[0]&&!stat(fp,&st)?(long)st.st_size:-1;
            const char*d=fp[0]?fp:"(generated)";if(fp[0]&&!strncmp(d,HOME,HL)&&d[HL]=='/')d+=HL+1;
            char ed[160]="";int ec=CP[i].root==SROOT||CP[i].root==SDIR;  /* editable: backed by a git file (fmt+3 skips "%s/") */
            if(ec)snprintf(ed,160," <a class=k href=\"/doc?f=%s%s\" style=color:#fff>edit</a>",CP[i].fmt+3,CP[i].root==SDIR?"&d=code":"");
            if(CP[i].off>=0)cl+=snprintf(cm+cl,(size_t)(8192-cl),"<a class=k href=\"#c%d\">%s</a> <span class=p>%s</span> %ld tok%s<br>",i,CP[i].lbl,d,sz/4,ed);
            else cl+=snprintf(cm+cl,(size_t)(8192-cl),"<span class=k style=color:#777>%s</span> <span class=p>%s</span> %ld tok%s<br>",CP[i].lbl,d,sz/4,ed);}
        cl+=snprintf(cm+cl,(size_t)(8192-cl),"</div>");
        int hl=snprintf(h,(size_t)(ol*6+2048),"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>unified prompt</title><style>body{background:#0b0b0b;color:#ddd;margin:0;font:13px/1.5 ui-monospace,monospace}header{position:sticky;top:0;background:#000;color:#fff;padding:8px 16px;border-bottom:1px solid #222;z-index:2}.c{padding:10px 16px;border-bottom:1px solid #222;background:#0d0d0d;font-size:12px;line-height:1.8}.g{color:#888}.k{color:#fff;text-decoration:none}.k:hover{text-decoration:underline}.p{color:#bbb}b{color:#fff}pre{white-space:pre-wrap;word-break:break-word;padding:16px;margin:0}pre span{scroll-margin-top:46px}</style><header><b>unified prompt</b> — every agent (claude·codex·gemini·m) · %zu tok</header>%s<pre>",ol/4,cm);
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
typedef struct{time_t t;char*p,*w,*n,*m;size_t i;}rv_t;static int _rvcmp(const void*a,const void*b){time_t x=((const rv_t*)a)->t,y=((const rv_t*)b)->t;return y>x?1:y<x?-1:0;}   /* /review rows, newest first */
static int _rvdoc(const char*m,const char*dir,int k,char*out,int n){   /* a review: k-th path of <doc>a,b</doc> in an a done message, relative to its dir; 1 = found */
    const char*a=strstr(m,"<doc>"),*b=a?strstr(a,"</doc>"):0;if(!a||!b)return 0;a+=5;
    for(int i=0;a<b;i++){const char*e=memchr(a,',',(size_t)(b-a));if(!e)e=b;if(i==k){while(a<e&&*a==' ')a++;int l=(int)(e-a);while(l&&a[l-1]==' ')l--;if(l<=0)return 0;if(*a=='/')snprintf(out,(size_t)n,"%.*s",l,a);else snprintf(out,(size_t)n,"%s/%.*s",dir,l,a);return 1;}a=e+1;}
    return 0;}
static int _rvpath(int N,int K,char*fp,int n){   /* a review: K-th <doc> path of done.log line N; 1 = found */
    char lf[P];snprintf(lf,P,"%s/done.log",DDIR);char*rl=readf(lf,NULL);int i=0,ok=0;fp[0]=0;
    for(char*l=rl,*nl;l&&*l;l=nl?nl+1:l+strlen(l),i++){nl=strchr(l,'\n');if(nl)*nl=0;if(i!=N)continue;char*f[5]={l,0,0,0,0};int k=1;for(char*q=l;*q&&k<5;q++)if(*q=='\t'){*q=0;f[k++]=q+1;}if(k==5)ok=_rvdoc(f[4],f[3],K,fp,n);break;}
    free(rl);return ok;}
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
    if(!strncmp(req,"GET /nw",7)&&(req[7]==' '||req[7]=='\r'||req[7]=='?')){   /* net worth: single source = u server; home-page banner reads this */
        char nb[4096];char*b=_ufwd("/nw",0,nb,4096);
        if(b&&*b)_sresp(c,200,"text/plain; charset=utf-8",b,(int)strlen(b));
        else _sresp(c,200,"text/plain; charset=utf-8","x u server down \xe2\x80\x94 start it: u web run",39);return;}
    if(!strncmp(req,"POST /nw",8)){   /* update a part: body "bank 2500" | "rm bank" | "log" -> u's nw_apply (parsed in C, no shell) */
        char*bd=strstr(req,"\r\n\r\n");bd=bd?bd+4:(char*)"";char nb[4096];char*b=_ufwd("/nw",bd,nb,4096);
        _sresp(c,200,"text/plain; charset=utf-8",b?b:(char*)"x u down",b?(int)strlen(b):8);return;}
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
            if(!strncmp(gurl,"https",5)){const char*h=strrchr(gurl,'/')+1;snprintf(saved,512,"✓ %s pushed · <a href=\"%s\" style=color:#fff>%s</a>",ts,gurl,h);}
            else snprintf(saved,512,"✓ %s saved locally · ✗ not pushed — %s",ts,gurl[0]?gurl:"no git output");
        }else snprintf(saved,512,"✗ SAVE FAILED");
        {char lg[P];snprintf(lg,P,"%s/local/serve.log",AROOT);FILE*l=fopen(lg,"a");if(l){fprintf(l,"save %s %s\n",rel,ok?gurl:"WRITEFAIL");fclose(l);}}
        size_t nl=0;char*nf=readf(fp,&nl);
        _docpage(c,rel,nf?nf:ct,nf?nl:(size_t)w,saved,ds);free(nf);return;}
    if(!strncmp(req,"POST /book",10)&&(req[10]=='?'||req[10]==' ')){char nm[128];_qn(req,nm);   /* guard: /bookmark shares the prefix */
        char po[24]="0";char*bd=strstr(req,"\r\n\r\n"),*pp=bd?strstr(bd+4,"pos="):0;
        if(pp){pp+=4;int i=0;for(;pp[i]>='0'&&pp[i]<='9'&&i<23;i++)po[i]=pp[i];po[i]=0;}
        if(nm[0]&&!strchr(nm,'/')&&!strstr(nm,"..")&&!fork()){int n=open("/dev/null",O_WRONLY);if(n>=0)dup2(n,1);execlp("a","a","book","pos",nm,po,(char*)0);_exit(1);}
        _sresp(c,200,"text/plain","ok",2);return;}
    if(!strncmp(req,"GET /bookpos",12)){char nm[128];_qn(req,nm);  /* readback: reader verifies its save landed (POST ok is pre-fork) */
        if(!nm[0]||strchr(nm,'/')||strstr(nm,"..")){_sresp(c,400,"text/plain","x",1);return;}
        char b[24];int bl=snprintf(b,24,"%ld",_bkpos(nm));_sresp(c,200,"text/plain",b,bl);return;}
    if(!strncmp(req,"GET /bookmark",13)){char nm[128];_qn(req,nm);  /* csv of col6 mark offsets */
        if(!nm[0]||strchr(nm,'/')||strstr(nm,"..")){_sresp(c,400,"text/plain","x",1);return;}
        char b[512];int L=_bkcol(nm,6,b,512);_sresp(c,200,"text/plain",b,L>0?L:0);return;}
    if(!strncmp(req,"POST /bookmark",14)){char nm[128];_qn(req,nm);  /* add=|del=<off> → RMW col6 under flock, reply authoritative csv */
        char*bd2=strstr(req,"\r\n\r\n"),*p=0;char op=0;long ov=-1;
        if(bd2){if((p=strstr(bd2+4,"add=")))op='a';else if((p=strstr(bd2+4,"del=")))op='d';if(p)ov=atol(p+4);}
        if(!nm[0]||strchr(nm,'/')||strstr(nm,"..")||!op||ov<0){_sresp(c,400,"text/plain","x",1);return;}
        char ip[P];snprintf(ip,P,"%s/git/books/index.txt",AROOT);
        int lf=open(ip,O_RDWR|O_CREAT,0644);if(lf<0){_sresp(c,500,"text/plain","x",1);return;}
        flock(lf,LOCK_EX);
        long mk[64];int km=0;char cur[512];
        if(_bkcol(nm,6,cur,512)>0)for(char*q=cur;*q&&km<64;){long v=atol(q);if(v>=0)mk[km++]=v;char*cm=strchr(q,',');if(!cm)break;q=cm+1;}
        if(op=='d'){int w=0;for(int i=0;i<km;i++)if(mk[i]!=ov)mk[w++]=mk[i];km=w;}
        else{int dup=0;for(int i=0;i<km;i++)dup|=mk[i]==ov;if(!dup&&km<64)mk[km++]=ov;
            for(int i=1;i<km;i++){long x=mk[i];int j=i-1;for(;j>=0&&mk[j]>x;j--)mk[j+1]=mk[j];mk[j+1]=x;}}  /* panel = book order */
        char csv[512];int cl=0;for(int i=0;i<km;i++)cl+=snprintf(csv+cl,(size_t)(512-cl),"%s%ld",i?",":"",mk[i]);
        size_t il=0;char*ix=readf(ip,&il);size_t nl2=strlen(nm);int found=0;
        char*out=malloc(il+nl2+600);size_t ol=0;
        if(ix)for(char*l=ix;l<ix+il;){char*e=memchr(l,'\n',(size_t)(ix+il-l));size_t ll=e?(size_t)(e-l):(size_t)(ix+il-l);
            char*t1=memchr(l,'\t',ll),*t2=t1?memchr(t1+1,'\t',ll-(size_t)(t1+1-l)):0;
            if(!found&&t1&&t2&&(size_t)(t2-t1-1)==nl2&&!strncmp(t1+1,nm,nl2)){found=1;
                size_t k=0;int tabs=0;for(;k<ll&&tabs<5;k++){out[ol+k]=l[k];if(l[k]=='\t')tabs++;}ol+=k;   /* cols 1-5 verbatim */
                if(tabs<5){for(size_t z=0;z<ll-k;z++)out[ol+z]=l[k+z];ol+=ll-k;while(tabs++<5)out[ol++]='\t';}   /* short row: keep rest, pad */
                memcpy(out+ol,csv,(size_t)cl);ol+=(size_t)cl;}
            else{memcpy(out+ol,l,ll);ol+=ll;}
            out[ol++]='\n';l=e?e+1:ix+il;}
        if(!found)ol+=(size_t)snprintf(out+ol,nl2+600,"\t%s\t\t\t\t%s\n",nm,csv);
        char tp[P];snprintf(tp,P,"%s.new",ip);FILE*f=fopen(tp,"w");int ok=f&&fwrite(out,1,ol,f)==ol&&!fclose(f)&&!rename(tp,ip);
        free(ix);free(out);flock(lf,LOCK_UN);close(lf);
        if(!ok){_sresp(c,500,"text/plain","x",1);return;}
        _sresp(c,200,"text/plain",csv,cl);return;}
    if(!strncmp(req,"GET /booksay",12)){char nm[128];_qn(req,nm);  /* speak from pos via `a say` (edge ryan); one group at a time, new play or stop=1 kills it */
        if(!nm[0]||strchr(nm,'/')||strstr(nm,"..")){_sresp(c,400,"text/plain","x",1);return;}
        char sf2[P];snprintf(sf2,P,"%s/local/.booksay.pid",AROOT);   /* fork-per-conn: say-group pid must survive the request child */
        {size_t pl=0;char*ps=readf(sf2,&pl);
         if(ps){pid_t op=(pid_t)atol(ps);free(ps);
            if(op>1){char cl[64],cb[256];snprintf(cl,64,"/proc/%d/cmdline",op);   /* direct read: procfs st_size=0 so readf returns empty */
                int cf=open(cl,O_RDONLY);ssize_t cn=cf<0?-1:read(cf,cb,255);if(cf>=0)close(cf);
                if((cn>3&&memmem(cb,(size_t)cn,"say",3))||(cn<0&&!kill(-op,0)))kill(-op,SIGTERM);}   /* leader = exec-chained bash lib/say.sh (substring, reuse-guarded), or leader gone but group (uvx/ffplay) lives — ours by construction, reap */
            unlink(sf2);}}
        char*pq=strstr(req,"pos=");
        if(!pq||strstr(req,"stop=")){_sresp(c,200,"text/plain","off",3);return;}
        char tf[P];_bkfile(nm,tf);size_t tl=0;char*txt=readf(tf,&tl);
        if(!txt){_sresp(c,404,"text/plain","x",1);return;}
        size_t o=(size_t)atol(pq+4);if(o>=tl)o=tl?tl-1:0;
        size_t e2=o+1400>tl?tl:o+1400,x=e2;   /* sentence-snap the tail within +300 */
        while(x<tl&&x<e2+300&&!(strchr(".!?",txt[x-1])&&(txt[x]==' '||txt[x]=='\n')))x++;
        if(x<tl)e2=x;
        char*ch=malloc(e2-o+1);memcpy(ch,txt+o,e2-o);ch[e2-o]=0;free(txt);
        pid_t k=fork();
        if(!k){setsid();int dn=open("/dev/null",O_WRONLY);if(dn>=0){dup2(dn,1);dup2(dn,2);}
            signal(SIGCHLD,SIG_DFL);execlp("a","a","say",ch,(char*)0);_exit(127);}
        free(ch);
        if(k>0){char pb[24];int pn=snprintf(pb,24,"%d",k);int pfd=open(sf2,O_WRONLY|O_CREAT|O_TRUNC,0644);if(pfd>=0){(void)!write(pfd,pb,(size_t)pn);close(pfd);}}
        _sresp(c,200,"text/plain","on",2);return;}
    if(!strncmp(req,"GET /bookarchive",16)){char nm[128];_qn(req,nm);  /* dot-prefix rename = archive; restore: a book archive <substr> */
        if(!nm[0]||nm[0]=='.'||strchr(nm,'/')||strstr(nm,"..")){_sresp(c,400,"text/plain","bad book",8);return;}
        char fr[P],to[P];snprintf(fr,P,"%s/books/%s",AROOT,nm);snprintf(to,P,"%s/books/.%s",AROOT,nm);
        if(rename(fr,to)){_sresp(c,404,"text/plain","x",1);return;}_sresp(c,200,"text/plain","ok",2);return;}
    if(!strncmp(req,"POST /up?",9)){char nm[96];_qp(req,"&n=",nm,96);char*bp=strstr(req,"\r\n\r\n");char f2[P];snprintf(f2,P,"%s/%s",TMP,nm);  /* <=200KB slices; _qp bars / */
        int fd=*nm&&bp?open(f2,O_WRONLY|O_CREAT|(req[11]=='1'?O_TRUNC:O_APPEND),0644):-1;
        _sresp(c,fd<0||write(fd,bp+4,(size_t)(n-(bp+4-req)))<0?400:200,"text/plain","",0);return;}
    if(!strncmp(req,"GET /book",9)&&(req[9]=='?'||req[9]==' ')){char nm[128];_qn(req,nm);
        if(!nm[0]){
            int au=!!strstr(req,"sort=author"),alp=!!strstr(req,"sort=name");   /* default = most-opened first; ?sort=name | ?sort=author */
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
            else{qsort(names,(size_t)n,128,_scmp);for(int i=0;i<n;i++)idx[i]=i;
                if(!alp){static int cnt[4096];   /* fork-per-conn: fresh zeroed copy each request */
                    char lp[P];snprintf(lp,P,"%s/local/serve.log",AROOT);char*lg=readf(lp,NULL);
                    if(lg){for(char*p=lg;(p=strstr(p,"GET /book?n="));){p+=12;char bn[128];int j=0;
                        for(;*p&&*p!=' '&&*p!='&'&&j<127;p++){if(*p=='%'&&p[1]&&p[2]){char x[3]={p[1],p[2],0};bn[j++]=(char)strtol(x,0,16);p+=2;}else bn[j++]=*p;}
                        bn[j]=0;for(int i=0;i<n;i++)if(!strcmp(names[i],bn)){cnt[i]++;break;}}
                    free(lg);}g_bc=cnt;qsort(idx,(size_t)n,sizeof(int),g_bccmp);}}
            int cap=1<<20;char*h=malloc((size_t)cap);int hl=snprintf(h,(size_t)cap,
                "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
                "<style>body{background:#0b0b0b;color:#ddd;margin:0;font:18px/1.35 system-ui}h3{color:#fff;padding:14px 16px 6px;margin:0}"
                ".r{display:flex;align-items:center;gap:12px;padding:11px 16px;border-bottom:1px solid #1a1a1a}.r:hover{background:#161616}"
                ".t{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#fff;text-decoration:none}.r.x .t{color:#666}.r.x .s:before{content:\"no txt\";color:#c33;margin-right:7px}"
                ".c{flex:none;color:#999;text-decoration:none;font-size:18px}.s{flex:none;min-width:48px;text-align:right;color:#666;font:13px ui-monospace,monospace;text-transform:uppercase}.s a{color:#666;text-decoration:none}.s a:hover{color:#fff}"
                ".h{position:sticky;top:0;background:#0b0b0b;color:#fff;font-weight:700;font-size:15px;letter-spacing:.09em;text-transform:uppercase;padding:16px 16px 5px;border-bottom:1px solid #1a1a1a}"
                ".r.in{padding-left:30px}.nav{padding:4px 16px 10px;font-size:17px}.nav a{color:#888;text-decoration:none;margin-right:14px}.nav a.on{color:#fff;font-weight:600}"
                "#q,#ab{display:block;box-sizing:border-box;width:calc(100%% - 32px);margin:2px 16px 8px;padding:9px 12px;background:#161616;color:#fff;border:1px solid #2a2a2a;border-radius:8px;font:18px system-ui;outline:none}#qms{float:right;color:#555;font:11px ui-monospace,monospace}</style>"
                "<script>function _ax(e){var a=e.target.closest('a.x');if(!a)return;e.preventDefault();e.stopImmediatePropagation();"
                "if(e.type!='pointerdown')return;fetch(a.href).then(function(r){if(r.ok){a.closest('.r').style.opacity=.35;a.outerHTML='<span class=c>\xe2\x9c\x93 archived</span>'}else a.textContent='\xe2\x9c\x97'},function(){a.textContent='\xe2\x9c\x97'})}"
                "addEventListener('pointerdown',_ax,true);addEventListener('click',_ax,true)</script>" TAPJS
                "<h3>books (%d)</h3><div class=nav><a%s href=\"/book\">by freq</a><a%s href=\"/book?sort=name\">by name</a><a%s href=\"/book?sort=author\">by author</a><span id=qms></span></div>"
                "<input id=q placeholder=\"type to search\" autofocus>"
                "<script>q.oninput=function(){var t0=performance.now(),v=q.value.toLowerCase(),hd=0,vn=0,ht='';"
                "document.querySelectorAll('.h,.r').forEach(function(e){if(e.className=='h'){if(hd)hd.style.display=vn?'':'none';hd=e;ht=e.textContent.toLowerCase();vn=0}"
                "else{var m=(e.textContent+' '+ht).toLowerCase().indexOf(v)>=0;e.style.display=m?'':'none';vn+=m}});"
                "if(hd)hd.style.display=vn?'':'none';qms.textContent=(performance.now()-t0).toFixed(2)+'ms'};"
                "q.onkeydown=function(e){if(e.key=='Enter'){var r=document.querySelector('.r:not([style*=none]) a.t');if(r)location=r.href}};"
                "onkeydown=function(e){if(document.activeElement!=q&&!e.ctrlKey&&!e.metaKey&&(e.key.length==1||e.key=='Backspace'))q.focus()}</script>"
                "<button id=ab onpointerdown=af.click()>+ add book</button><input id=af type=file hidden><script>af.onchange=async()=>{var f=af.files[0],m=f.name.replace(/[^\\w.]+/g,'-');for(var o=0;o<f.size;o+=2e5)await fetch('/up?s='+ +!o+'&n='+m,{method:'POST',body:f.slice(o,o+2e5)}),ab.textContent=o;navigator.sendBeacon('/api/omni','q=cmd+a+book+add+${TMPDIR:-/tmp}/'+m);setTimeout(\"location=''\",999)}</script>",
                n,(au||alp)?"":" class=on",alp?" class=on":"",au?" class=on":"");
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
                hl+=snprintf(h+hl,(size_t)(cap-hl),"<div class=\"r %s %s\"><a class=t href=\"/book?n=%s\">%s</a><a class=c href=\"/bookdir?n=%s\" title=\"all versions (file manager)\">\xf0\x9f\x97\x82</a><a class=c href=\"/bookcloud?n=%s\" title=\"open in cloud\">\xe2\x98\x81</a><a class=\"c x\" href=\"/bookarchive?n=%s\" title=\"archive (restorable)\">\xf0\x9f\x97\x84</a><span class=s>%s</span></div>",has?"":"x",au?"in":"",names[i],lb,names[i],names[i],names[i],xt);}
            _sdoc(c,h,hl);free(h);return;}
        if(strchr(nm,'/')||strstr(nm,"..")){_sresp(c,400,"text/plain","bad book",8);return;}
        char tf[P];_bkfile(nm,tf);
        size_t tl=0;char*txt=readf(tf,&tl);
        if(!txt){char ip[P];snprintf(ip,P,"%s/git/books/index.txt",AROOT);size_t il=0;char*ix=readf(ip,&il);int reg=0;  /* in synced index but not local: pull in bg, page retries */
            if(ix){char pat[140];snprintf(pat,140,"\t%s\t",nm);reg=!!strstr(ix,pat);free(ix);}
            if(reg){if(!fork()){signal(SIGCHLD,SIG_DFL);int z=open("/dev/null",O_WRONLY);if(z>=0){dup2(z,1);dup2(z,2);}execlp("a","a","book","pull",nm,(char*)0);_exit(1);}
                char b[512];int bl=snprintf(b,512,"<!doctype html><meta charset=utf-8><meta http-equiv=refresh content=5><body style=\"background:#0b0b0b;color:#fff;font:16px ui-monospace,monospace;padding:40px\">syncing %s from cloud\xe2\x80\xa6 auto-retrying</body>",nm);
                _sdoc(c,b,bl);return;}
            _sresp(c,404,"text/plain","no text — a book transcribe first",34);return;}
        long pos=_bkpos(nm);if(pos<0)pos=0;
        char*esc=malloc(tl*5+1);size_t el=0;  /* escape &<> so text node==exact file chars → caret offset==file offset */
        for(size_t i=0;i<tl;i++){char ch=txt[i];
            if(ch=='&'){memcpy(esc+el,"&amp;",5);el+=5;}
            else if(ch=='<'){memcpy(esc+el,"&lt;",4);el+=4;}
            else if(ch=='>'){memcpy(esc+el,"&gt;",4);el+=4;}
            else esc[el++]=ch;}
        free(txt);
        size_t cap=el+16384;char*pg=malloc(cap);int hl=snprintf(pg,cap,   /* head+tail JS ~6KB and growing; 4096 truncated the script (snprintf returns would-be len → heap garbage served) */
            "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<style>html,body{margin:0;background:#0b0b0b;overflow:hidden;height:100%%;touch-action:none;overscroll-behavior:none}::-webkit-scrollbar{display:none}"
            /* true pagination: bk is a fixed full-screen clipped box; flipping sets bk.scrollTop by whole screens, body never scrolls */
            "#bk{position:fixed;top:0;bottom:0;left:0;right:0;max-width:760px;margin:0 auto;overflow:hidden;scrollbar-width:none;white-space:pre-wrap;overflow-wrap:break-word;color:#ddd;font:18px/1.75 Georgia,serif;padding:0 18px;box-sizing:border-box}"
            /* one top-right line: marks controls + hud share #tr so nothing stacks (Sean: only take one line) */
            "#tr{position:fixed;top:0;right:0;display:flex;align-items:center;background:#000;opacity:.85;z-index:9}"
            "#tr a{color:#fff;text-decoration:none;padding:5px 10px;font:14px ui-monospace,monospace}"
            "#hud{color:#fff;font:12px ui-monospace,monospace;padding:4px 8px 4px 0}"
            "#mp{display:none;position:fixed;top:28px;right:0;max-width:88vw;max-height:60vh;overflow:auto;background:#000;color:#fff;font:13px ui-monospace,monospace;z-index:9}"
            "#mp div{padding:9px 10px;border-bottom:1px solid #1a1a1a;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}#mp b{color:#ccc;font-weight:400;padding:0 8px}</style>"
            "<div id=tr><a id=ms>\xe2\x96\xb6</a><a id=ma>+\xe2\x9a\x91</a><a id=mt>\xe2\x9a\x91</a><div id=hud></div></div><div id=mp></div><pre id=bk>");
        memcpy(pg+hl,esc,el);hl+=(int)el;free(esc);
        hl+=snprintf(pg+hl,cap-(size_t)hl,  /* browsers split big text into 64K chunk nodes — map (chunk,local)<->global offset */
            "</pre><script>var N=\"%s\",P=%ld,K=bk,H=hud,ns=[].slice.call(K.childNodes),T=0,bs=[],co=P;"
            "for(var i=0;i<ns.length;i++){bs.push(T);T+=ns[i].length||0;}"
            "function C(x,y){var n,o,r;if(document.caretRangeFromPoint){r=document.caretRangeFromPoint(x,y);if(!r)return null;n=r.startContainer;o=r.startOffset;}else if(document.caretPositionFromPoint){r=document.caretPositionFromPoint(x,y);if(!r)return null;n=r.offsetNode;o=r.offset;}else return null;for(var j=0;j<ns.length;j++)if(ns[j]===n)return bs[j]+o;return null;}"
            "function O(){var r=K.getBoundingClientRect(),x=r.left+18,o;for(var y=2;y<120;y+=8){o=C(x,r.top+y);if(o!=null)return o;}return co;}"
            "function R(f){var i=ns.length-1;while(i>0&&f<bs[i])i--;var g=document.createRange();g.setStart(ns[i],Math.min(f-bs[i],ns[i].length));g.collapse(true);var c=g.getClientRects()[0]||g.getBoundingClientRect();K.scrollTop+=c.top-K.getBoundingClientRect().top;pg=Math.round(K.scrollTop/ph());}"
            /* sync truth by readback: POST, then after uv settles GET /bookpos and compare. Steady '✓' while verified
               (load counts: pos came from the index), '✗sync' while a save can't be confirmed (10s re-save loop) —
               absence would be ambiguous with the feature being broken, so healthy has a mark that never changes */
            /* time inline: T/D and most single letters are taken by the pager vars — a helper name here broke the whole script once */
            "var sx=' \xc2\xb7 \xe2\x9c\x93'+new Date().toTimeString().slice(0,5),sq=0,rt;function U(b){H.textContent=(b||'pg '+(pg+1)+'/'+(NP()+1))+sx;}"
            "function M(ok){clearTimeout(rt);if(!ok)rt=setTimeout(S,10000);var s=ok?' \xc2\xb7 \xe2\x9c\x93'+new Date().toTimeString().slice(0,5):' \xc2\xb7 \xe2\x9c\x97sync';if(s!=sx){sx=s;U();}}"
            "function V(v,q){fetch('/bookpos?n='+encodeURIComponent(N)).then(function(r){return r.text()}).then(function(t){if(q==sq)M(parseInt(t)===v)},function(){if(q==sq)M(0)})}"
            "function S(){co=O();var q=++sq,v=co,b='pos='+v,u='/book?n='+encodeURIComponent(N);fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(function(r){if(q!=sq)return;if(r.ok)setTimeout(function(){V(v,q)},2000);else M(0)},function(){if(q==sq)M(0)})}"
            "function B(){var b='pos='+O();if(navigator.sendBeacon)navigator.sendBeacon('/book?n='+encodeURIComponent(N),new Blob([b],{type:'application/x-www-form-urlencoded'}))}"
            /* true pagination: whole-screen pages via bk.scrollTop (body can't scroll); flip on pointer DOWN (depress, not lift)/wheel/keys — scrollTop is sync = sub-ms, paints next vsync */
            "var lh=parseFloat(getComputedStyle(K).lineHeight),pg=0,fm=0,st;"
            "function ph(){return Math.max(lh,Math.floor(K.clientHeight/lh)*lh);}"
            "function NP(){return Math.max(0,Math.ceil((K.scrollHeight-K.clientHeight)/ph()));}"
            "function G(p){p=Math.max(0,Math.min(p,NP()));var t=performance.now();K.scrollTop=p*ph();pg=p;fm=performance.now()-t;U('pg '+(pg+1)+'/'+(NP()+1)+' · '+fm.toFixed(2)+'ms');clearTimeout(st);st=setTimeout(S,400);}"
            "addEventListener('pointerdown',function(e){G(pg+(e.clientX<innerWidth/3?-1:1));e.preventDefault();});"
            "addEventListener('wheel',function(e){G(pg+(e.deltaY>0?1:-1));e.preventDefault();},{passive:false});"
            "addEventListener('keydown',function(e){var k=e.key;if(k===' '||k==='PageDown'||k==='ArrowRight'||k==='ArrowDown')G(pg+1);else if(k==='b'||k==='PageUp'||k==='ArrowLeft'||k==='ArrowUp')G(pg-1);else return;e.preventDefault();});"
            "addEventListener('resize',function(){G(pg);});"
            "requestAnimationFrame(function(){if(P>0){R(P);K.scrollTop=pg*ph();}U();});"
            "addEventListener('pagehide',B);addEventListener('visibilitychange',function(){if(document.hidden)B();});"
            /* marks: col6 csv offsets, server reply is the authoritative list; rows self-label from the text at the offset.
               controls stopPropagation so the global pointerdown pager doesn't flip; TX = unescaped text, offsets == file offsets */
            "var TX=K.textContent,mks=[];"
            "function EH(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;')}"
            "function MR(t){mks=t?t.split(',').filter(Boolean).map(Number):[];mt.textContent='\xe2\x9a\x91'+(mks.length||'');"
            "mp.innerHTML=mks.map(function(o){return '<div data-o='+o+'><b data-x='+o+'>\xe2\x9c\x95</b>'+EH(TX.substr(o,44).replace(/\\s+/g,' '))+'</div>'}).join('')||'<div>no marks \xe2\x80\x94 +\xe2\x9a\x91 adds this page</div>'}"
            "function MW(b){fetch('/bookmark?n='+encodeURIComponent(N),{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(function(r){return r.text()}).then(MR)}"
            "ma.addEventListener('pointerdown',function(e){e.stopPropagation();e.preventDefault();MW('add='+O())});"
            "mt.addEventListener('pointerdown',function(e){e.stopPropagation();e.preventDefault();mp.style.display=mp.style.display=='block'?'none':'block'});"
            "mp.addEventListener('pointerdown',function(e){e.stopPropagation();e.preventDefault();var x=e.target.getAttribute('data-x');if(x){MW('del='+x);return}"
            "var r=e.target.closest('[data-o]');if(r){R(+r.getAttribute('data-o'));K.scrollTop=pg*ph();U();clearTimeout(st);st=setTimeout(S,400);mp.style.display='none'}});"
            "fetch('/bookmark?n='+encodeURIComponent(N)).then(function(r){return r.text()}).then(MR);"
            /* ▶ = speak from here via server-side `a say`; server owns the single say-group, reply is the state */
            "var sp=0;ms.addEventListener('pointerdown',function(e){e.stopPropagation();e.preventDefault();"
            "fetch('/booksay?n='+encodeURIComponent(N)+(sp?'&stop=1':'&pos='+O())).then(function(r){return r.text()}).then(function(t){sp=t=='on'?1:0;ms.textContent=sp?'\\u25a0':'\\u25b6'},function(){sp=0;ms.textContent='\\u25b6'})});"
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
        int hl=snprintf(h,1<<18,"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>docs</title><style>body{background:#0b0b0b;color:#ddd;margin:0;font:14px/1.6 ui-monospace,monospace}h3{color:#fff;padding:12px 16px 4px;margin:0}a{display:block;color:#fff;text-decoration:none;padding:4px 16px}a:hover{background:#161616}</style><a href=# onclick=\"var n=prompt('new adoc filename');if(n)location='/doc?f=adocs/'+n;return false\" style=color:#fff>+ new adoc</a> <a href=# onclick=\"var n=prompt('new folder name');if(n)fetch('/api/omni',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'q=docs mkdir '+encodeURIComponent(n)}).then(function(){location.reload()});return false\" style=color:#fff>+ new folder</a>" TAPJS);
        char*ar=strstr(req,"arch=1"),*eol=strstr(req,"\r\n");int arch=ar&&eol&&ar<eol;
        const char*dn[]={"mem","adocs"},*da[]={"mem/archive","adocs/archive"};const char**dirs=arch?da:dn;
        hl+=snprintf(h+hl,(size_t)((1<<18)-hl),arch?"<a href=\"/docs\" style=color:#fff>&#9666; back to docs</a>":"<a href=\"/docs?arch=1\" style=color:#555>&#9656; archived</a>");
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
        char h[B*3];int hl=snprintf(h,sizeof h,"<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><style>body{background:#000;color:#fff;font:18px system-ui;margin:16px}h3{color:#888;font-weight:400}.r{padding:10px;border-bottom:1px solid #222;display:flex;align-items:center;gap:10px}.o{color:#555;min-width:1.4em;text-align:right}.r b{flex:1;font-weight:400}button{background:#1a1a1a;color:#fff;border:1px solid #333;border-radius:6px;padding:7px 13px;font:inherit;cursor:pointer}#st{color:#666;padding:4px 0 12px;min-height:1.2em;font-size:15px}</style><h3>projects</h3><div id=st>ready \xe2\x80\x94 \xe2\x86\x91\xe2\x86\x93 reorder · ✕ drop, saves via CLI</div>");
        for(char*l=out;*l;){char*nl=strchr(l,'\n');if(nl)*nl=0;char*tab=strstr(l,"\tproject");
            if(tab){*tab=0;int i=atoi(l);char*nm=strchr(l,' ');nm=nm?nm+1:l;
                hl+=snprintf(h+hl,(size_t)(sizeof h-(size_t)hl),"<div class=r><span class=o>%d</span><b>%s</b><button onclick='mv(%d,%d)'>↑</button><button onclick='mv(%d,%d)'>↓</button><button onclick='dr(%d,\"%s\")'>✕</button></div>",i,nm,i,i-1,i,i+1,i,nm);}
            l=nl?nl+1:l+strlen(l);}
        hl+=snprintf(h+hl,(size_t)(sizeof h-(size_t)hl),"<script>var st=document.getElementById('st'),m=sessionStorage.pmsg;if(m){st.innerHTML=m;sessionStorage.removeItem('pmsg')}function q(c,pk){st.textContent='… a '+c;st.style.color='#ccc';var t0=performance.now();fetch('/api/omni',{method:'POST',body:'q='+c}).then(r=>r.text()).then(x=>{x=pk(x.replace(/<[^>]*>/g,'').split('\\n').map(l=>l.trim()))||'(no output)';sessionStorage.pmsg='<span style=color:'+(x.indexOf('✓')>=0?'#ccc':'#fff')+'>'+x+' · '+(performance.now()-t0).toFixed(1)+'ms</span>';location.reload()}).catch(e=>{st.textContent='✗ CLI unreachable';st.style.color='#fff'})}function mv(f,t){var n=document.querySelectorAll('.r').length;if(t<0||t>=n)return;q('move '+f+' '+t,a=>a.filter(l=>l&&l.indexOf('tokens')<0).pop())}function dr(i,n){confirm('drop '+n+'? folder and repo stay')&&q('remove '+i,a=>a.find(l=>/Removed|Not/.test(l)))}</script>");
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
    if(!strncmp(req,"GET /fleet",10)){char fp[P],*fb;size_t fn=0;   /* device status: serve the a fleet cache instantly, refresh in bg (TUI primary, this mirrors it) */
        snprintf(fp,P,"%s/fleet.txt",DDIR);fb=readf(fp,&fn);
        (void)!system("setsid a fleet >/dev/null 2>&1 &");
        char h[8192];int hl=snprintf(h,8192,"<title>a fleet</title><body style=\"background:#000;color:#eee;margin:12px\"><pre style=\"font:14px monospace;overflow-x:auto\">%s</pre>",fb&&fn?fb:"no data yet - refreshing, reload in a few seconds");
        free(fb);_sresp(c,200,"text/html",h,hl);return;}
    if(!strncmp(req,"GET /fwins",10)){   /* fleet-wide tmux window list: serve cache instantly, refresh via lib/fwins.sh in bg (mirrors /fleet) */
        char fp[P];snprintf(fp,P,"%s/fleetwins.txt",DDIR);size_t fn=0;char*fb=readf(fp,&fn);struct stat ws;
        /* RATE LIMIT: a scan is an ssh fanout to the whole fleet + a tmux client per box. The page polls every 4s;
           refiring the scan on every poll ran 175 scans in 11min and kept N boxes + this tmux server under constant
           load. Cache age gates it — polls stay a 0.2ms file read and cost the fleet nothing. */
        if(stat(fp,&ws)||time(0)-ws.st_mtime>=20){
            if(!fork()){close(c);char sh[P];snprintf(sh,P,"%s/lib/fwins.sh",SDIR);execl("/bin/sh","sh",sh,DEV,DDIR,(char*)0);_exit(0);}}
        _sresp(c,200,"text/plain",fb&&fn?fb:"",fb&&fn?(int)fn:0);if(fb)free(fb);return;}
    if(!strncmp(req,"GET /review/armed",17)){const char*rt=getenv("XDG_RUNTIME_DIR");char af[P];snprintf(af,P,"%s/a_pick",rt&&*rt?rt:"/tmp");char*q=readf(af,NULL);char out[P]="";
        for(char*l=q,*nl;l&&*l;l=nl?nl+1:l+strlen(l)){nl=strchr(l,'\n');if(nl)*nl=0;if(*l&&access(l,R_OK)==0){snprintf(out,P,"%s",strrchr(l,'/')?strrchr(l,'/')+1:l);break;}}
        free(q);_sresp(c,200,"text/plain; charset=utf-8",out,(int)strlen(out));return;}   /* a review: what the e picker queue will attach next (i pick arm), for the banner */
    if(!strncmp(req,"GET /review/arm?n=",18)){int N=atoi(req+18);const char*kq=strstr(req,"&k=");int K=kq?atoi(kq+3):0;char fp[P]="",msg[P]="not found";
        if(_rvpath(N,K,fp,P)&&access(fp,R_OK)==0){const char*rt=getenv("XDG_RUNTIME_DIR");char af[P];snprintf(af,P,"%s/a_pick",rt&&*rt?rt:"/tmp");FILE*f=fopen(af,"w");if(f){fprintf(f,"%s\n",fp);fclose(f);snprintf(msg,P,"armed %s",strrchr(fp,'/')?strrchr(fp,'/')+1:fp);}}
        _sresp(c,200,"text/plain; charset=utf-8",msg,(int)strlen(msg));return;}   /* a review: arm the e file picker queue with this document: the next Attach/Upload click in any browser attaches it, no navigating (Sean 2026-09-03: auto arm like resume does) */
    if(!strncmp(req,"GET /review/doc?n=",18)){int N=atoi(req+18);const char*kq=strstr(req,"&k=");int K=kq?atoi(kq+3):0;char fp[P]="";_rvpath(N,K,fp,P);size_t bl=0;char*b=fp[0]?readf(fp,&bl):0;if(!b){_sresp(c,404,"text/plain","no such document",16);return;}   /* only paths an a done recorded: the server is on the LAN */
        const char*dot=strrchr(fp,'.'),*ct="text/plain; charset=utf-8";if(dot){if(!strcmp(dot,".pdf"))ct="application/pdf";else if(!strcmp(dot,".html"))ct="text/html; charset=utf-8";else if(!strcmp(dot,".png"))ct="image/png";else if(!strcmp(dot,".jpg")||!strcmp(dot,".jpeg"))ct="image/jpeg";else if(!strcmp(dot,".svg"))ct="image/svg+xml";}
        if(!strncmp(ct,"text/plain",10)&&!strstr(req,"&raw=1")){size_t oc=bl*6+512;char*o=malloc(oc);int ol=snprintf(o,oc,"<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><style>body{margin:0;padding:16px 16px 80px;background:#000;color:#fff;font:18px/1.5 ui-monospace,monospace;white-space:pre-wrap;word-break:break-word}</style>");
            for(size_t q=0;q<bl&&ol<(int)oc-8;q++){char ch=b[q];if(ch=='<')ol+=snprintf(o+ol,oc-(size_t)ol,"&lt;");else if(ch=='>')ol+=snprintf(o+ol,oc-(size_t)ol,"&gt;");else if(ch=='&')ol+=snprintf(o+ol,oc-(size_t)ol,"&amp;");else o[ol++]=ch;}
            free(b);b=o;bl=(size_t)ol;ct="text/html; charset=utf-8";}   /* a text file in the black frame has a transparent body: black on black (Sean 09-03, "literally cant see text"). Wrap it dark, white, wrapping */
        char hd[224];int hl=snprintf(hd,224,"HTTP/1.1 200 OK\r\nContent-Type:%s\r\nContent-Length:%zu\r\nConnection:close\r\nCache-Control:no-cache\r\n\r\n",ct,bl);(void)!write(c,hd,(size_t)hl);for(size_t o=0;o<bl;){ssize_t w=write(c,b+o,bl-o);if(w<=0)break;o+=(size_t)w;}free(b);return;}
    if(!strncmp(req,"GET /review/wsz?w=",18)){int w=atoi(req+18);char tc[160],sz[32]="";snprintf(tc,160,"tmux display-message -p -t a:%d '#{window_width} #{window_height}' 2>/dev/null",w);FILE*pp=popen(tc,"r");if(pp){if(fgets(sz,32,pp))sz[strcspn(sz,"\n")]=0;pclose(pp);}_sresp(c,200,"text/plain",sz,(int)strlen(sz));return;}   /* host tmux window size, for the pull-up's fit-width scaling (a review) */
    if(!strncmp(req,"GET /review",11)){   /* a review: GUI review queue (Sean 2026-09-02), DDIR/done.log, one line per `a done` (ts\tidx\tname\tdir\tmsg, written by cmd_done), newest first; per AGENT (Sean 09-03); shell = lib/review.html */
        char tf[P];snprintf(tf,P,"%s/lib/review.html",SDIR);size_t tl=0;char*th=readf(tf,&tl);if(!th){_sresp(c,404,"text/plain","no review.html",14);return;}
        char lf[P];snprintf(lf,P,"%s/done.log",DDIR);char*rl=readf(lf,NULL);size_t n=0,nc=0;rv_t*rs=NULL;
        size_t li=0;for(char*l=rl,*nl;l&&*l;l=nl?nl+1:l+strlen(l),li++){nl=strchr(l,'\n');if(nl)*nl=0;char*f[5]={l,0,0,0,0};int k=1;for(char*q=l;*q&&k<5;q++)if(*q=='\t'){*q=0;f[k++]=q+1;}if(k<5)continue;
            if(n>=nc){nc=nc?nc*2:64;rs=realloc(rs,nc*sizeof*rs);}rs[n].t=atol(f[0]);rs[n].w=f[1][0]&&strspn(f[1],"0123456789")==strlen(f[1])?f[1]:0;rs[n].n=f[2];rs[n].p=f[3];rs[n].i=li;rs[n++].m=f[4];}
        qsort(rs,n,sizeof*rs,_rvcmp);int cap=1<<18;char*h=malloc((size_t)cap);int hl=snprintf(h,(size_t)cap,"%.*s",(int)tl,th);free(th);time_t now=time(NULL);
        for(size_t i=0;i<n&&hl<cap-4096;i++){long a=(long)(now-rs[i].t);char*m=strrchr(rs[i].m,']');m=m?m+1:rs[i].m;while(*m==' ')m++;for(char*q=m;*q;q++)if(*q=='<'||*q=='>')*q=' ';
            const char*rp=strncmp(rs[i].p,HOME,strlen(HOME))?rs[i].p:rs[i].p+strlen(HOME)+1;char ag[32];if(a<3600)snprintf(ag,32,"%ldm",a/60);else if(a<86400)snprintf(ag,32,"%ldh%02ldm",a/3600,a%3600/60);else snprintf(ag,32,"%ldd%ldh%02ldm",a/86400,a%86400/3600,a%3600/60);   /* age to the minute (Sean 09-03) */
            char nm[64]="";{int k=0;for(const char*q=rs[i].n;*q&&k<63;q++)if(isalnum((unsigned char)*q)||strchr("-_.",*q))nm[k++]=*q;nm[k]=0;}
            char bt[384]="";if(rs[i].w)snprintf(bt,384,"<div style=\"margin-top:8px\"><button class=op onpointerdown=\"op(this)\" data-w=\"%s\" data-n=\"%s\">open conversation in terminal: %s</button></div>",rs[i].w,nm,nm[0]?nm:"window");   /* own line, plain words (Sean 09-03: a play icon reads as music); pull-up web terminal of the agent's tmux window */
            hl+=snprintf(h+hl,(size_t)(cap-hl),"<div style=\"padding:8px 0;border-bottom:1px solid #222\"><span style=color:#888>%s</span> <b>%s</b> <span style=color:#888>%s</span> %.300s%s",ag,nm[0]?nm:"(no window)",rp,m,bt);
            for(int k=0;k<8;k++){char dp[P];if(!_rvdoc(rs[i].m,rs[i].p,k,dp,P))break;const char*bn=strrchr(dp,'/');bn=bn?bn+1:dp;char sn[96];int z=0;for(const char*q=bn;*q&&z<95;q++)if(!strchr("<>\"&",*q))sn[z++]=*q;sn[z]=0;   /* <doc> files: view in the same pull-up */
                hl+=snprintf(h+hl,(size_t)(cap-hl),"<div style=\"margin-top:8px\"><button class=op onpointerdown=\"dv(this)\" data-u=\"/review/doc?n=%zu&amp;k=%d\" data-n=\"%s\">view document: %s</button></div>",rs[i].i,k,sn,sn);}
            hl+=snprintf(h+hl,(size_t)(cap-hl),"</div>");}
        free(rs);free(rl);_sdoc(c,h,hl);free(h);return;}
    if(!strncmp(req,"GET /music",10)){char mc[P],rel[P]="";snprintf(mc,P,"%s/music",DDIR);setenv("MC",mc,1);   /* a music web: /music page · /musics?f=q rows (empty q = cache) · /musicf?f=name stream · /musicg?f=id = a music get → stream */
        if(req[10]=='s'){_docrel(req,rel);setenv("Q",rel,1);char b[8192];   /* cache matches, then 5 YouTube hits via ONE InnerTube call (0.45s; yt-dlp ytsearch was 9s) */
            FILE*p=popen(rel[0]?"ls \"$MC\"|grep -v '\\.part$'|grep -iF -- \"$Q\";jq -cn --arg q \"$Q\" '{context:{client:{clientName:\"WEB\",clientVersion:\"2.20250101.00.00\"}},query:$q,params:\"EgIQAQ%3D%3D\"}'|curl -s -m6 -d @- -H content-type:application/json 'https://www.youtube.com/youtubei/v1/search?prettyPrint=false'|jq -r '[..|.videoRenderer?|select(.)|\"\\(.videoId)\\t\\(.title.runs[0].text) \\(.lengthText.simpleText//\"\")\"]|.[:5][]'":"ls \"$MC\"|grep -v '\\.part$'","r");
            size_t n=p?fread(b,1,8191,p):0;if(p)pclose(p);_sresp(c,200,"text/plain; charset=utf-8",b,(int)n);b[n]=0;
            char*ar[16];int an=0;ar[an++]="a";ar[an++]="music";ar[an++]="pre";   /* prefetch every hit at once (first first) so a tap plays instantly; ids come off the network → argv, never a shell, and only [A-Za-z0-9_-] */
            for(char*ln=b;*ln&&an<15;){char*e=strchr(ln,'\n'),*t=strchr(ln,'\t');
                if(t&&(!e||t<e)&&t-ln<16){int ok=1;for(char*z=ln;z<t;z++)if(!isalnum((unsigned char)*z)&&*z!='-'&&*z!='_')ok=0;
                    if(ok){*t=0;ar[an++]=ln;}}
                if(!e)break;ln=e+1;}
            ar[an]=0;if(an>3&&!fork()){close(c);execvp("a",ar);_exit(0);}
            return;}
        if(req[10]=='c'){_docrel(req,rel);setenv("K",rel,1);char b[64];   /* gear: "<cap-GB> <clip> <MB-now>"; ?f=cap-8 / trim-0 set it */
            FILE*p=popen("a music cfg \"$K\"","r");size_t n=p?fread(b,1,63,p):0;if(p)pclose(p);
            _sresp(c,200,"text/plain",b,(int)n);return;}
        if(req[10]=='t'){_docrel(req,rel);setenv("K",rel,1);char b[64];   /* "<skip-in> <stop-at>" — dead air at the two ends */
            FILE*p=popen("a music trim \"$K\"","r");size_t n=p?fread(b,1,63,p):0;if(p)pclose(p);
            _sresp(c,200,"text/plain",b,(int)n);return;}
        if(req[10]=='r'){_docrel(req,rel);setenv("K",rel,1);char b[256];   /* clear one track's local bytes; .index keeps the how-to-get */
            FILE*p=popen("a music rm \"$K\"","r");size_t n=p?fread(b,1,255,p):0;if(p)pclose(p);
            _sresp(c,200,"text/plain",b,(int)n);return;}
        if(req[10]=='g'){char id[32];_qp(req,"?f=",id,32);setenv("I",id,1);
            #define MIDX "sed -n \"s|^$I  ||p\" \"$MC/.index\" 2>&-|sed q"
            FILE*ip=popen(MIDX,"r");   /* the 10s head prefetch recorded the name */
            if(ip){if(fgets(rel,P,ip))rel[strcspn(rel,"\n")]=0;pclose(ip);}
            char fl[P+300],pt[P+308];snprintf(fl,sizeof fl,"%s/%s",mc,rel);snprintf(pt,sizeof pt,"%s.part",fl);
            if(!rel[0]||access(fl,F_OK)){   /* not complete on disk: stream as it downloads — was a blocking whole-file get when no .part (1hr track = minutes of dead air, Sean 2026-09-01) */
                pid_t sf=fork();if(sf)return;
                if(!rel[0]){if(!fork()){execlp("a","a","music","pre",id,(char*)0);_exit(0);}   /* head: resolves name -> .index row (written AFTER its 160KB curl, so get can't race the .part) */
                    for(int w=0;w<600&&!rel[0];w++){usleep(100000);FILE*p2=popen(MIDX,"r");if(p2){if(fgets(rel,P,p2))rel[strcspn(rel,"\n")]=0;pclose(p2);}}
                    if(!rel[0])_exit(0);
                    snprintf(fl,sizeof fl,"%s/%s",mc,rel);snprintf(pt,sizeof pt,"%s.part",fl);}
                if(!fork()){execlp("a","a","music","get",id,(char*)0);_exit(0);}
                char h[200];int hl=snprintf(h,200,"HTTP/1.1 200 OK\r\nContent-Type:%s\r\nConnection:close\r\nCache-Control:no-store\r\n\r\n",strstr(rel,".m4a")?"audio/mp4":strstr(rel,".opus")?"audio/ogg":"audio/webm");
                if(write(c,h,(size_t)hl)!=hl)_exit(0);
                off_t off=0;struct stat st;
                for(int idle=0;idle<600;idle++){int fd=open(access(fl,F_OK)?pt:fl,O_RDONLY);
                    if(fd>=0){char bu[65536];ssize_t r;
                        if(!fstat(fd,&st)&&st.st_size>off&&lseek(fd,off,SEEK_SET)>=0)
                            while((r=read(fd,bu,65536))>0){if(write(c,bu,(size_t)r)!=r)_exit(0);off+=r;idle=0;}
                        close(fd);}
                    if(!stat(fl,&st)&&off>=st.st_size)_exit(0);   /* renamed by yt-dlp + fully sent = done */
                    usleep(100000);}
                _exit(0);}
            #undef MIDX
            }
        else if(req[10]=='f')_docrel(req,rel);
        else{char tf[P];snprintf(tf,P,"%s/lib/music.html",SDIR);size_t tl=0;char*th=readf(tf,&tl);if(th){_sdoc(c,th,(int)tl);free(th);}else _sresp(c,404,"text/plain","x",1);return;}
        char fp[P];snprintf(fp,P,"%s/%s",mc,rel);size_t n=0;char*d=readf(fp,&n);
        if(d){const char*mt=strstr(rel,".m4a")?"audio/mp4":strstr(rel,".opus")?"audio/ogg":"audio/webm";   /* Range support: without Accept-Ranges/206 Chrome marks audio unseekable (seekable=0-0) — trim skip and the seek bar both clamp to 0 */
            char*rg=strstr(req,"Range: bytes=");size_t s0=0,e0=n?n-1:0;
            if(rg){s0=(size_t)atoll(rg+13);char*dh=strchr(rg+13,'-');if(dh&&isdigit((unsigned char)dh[1])){e0=(size_t)atoll(dh+1);if(e0>=n)e0=n?n-1:0;}}
            char h[256];int hl;
            if(rg&&s0<n){hl=snprintf(h,256,"HTTP/1.1 206 OK\r\nContent-Type:%s\r\nAccept-Ranges:bytes\r\nContent-Range:bytes %zu-%zu/%zu\r\nContent-Length:%zu\r\nConnection:close\r\n\r\n",mt,s0,e0,n,e0-s0+1);
                if(write(c,h,(size_t)hl)==hl)(void)!write(c,d+s0,e0-s0+1);}
            else{hl=snprintf(h,256,"HTTP/1.1 200 OK\r\nContent-Type:%s\r\nAccept-Ranges:bytes\r\nContent-Length:%zu\r\nConnection:close\r\n\r\n",mt,n);
                if(write(c,h,(size_t)hl)==hl)(void)!write(c,d,n);}
            free(d);}else _sresp(c,404,"text/plain","x",1);return;}
    if(!strncmp(req,"GET /fw",7)&&(req[7]==' '||req[7]=='?'||req[7]=='\r')){   /* unified fleet tmux view: all devices' windows in one list, one inline terminal that re-points */
        char tf[P];snprintf(tf,P,"%s/lib/fleetview.html",SDIR);size_t tl=0;char*th=readf(tf,&tl);
        if(th){_siso(c,th,(int)tl);free(th);}else _sresp(c,404,"text/plain","no fleetview.html",16);return;}
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
        int bl=snprintf(buf,3200,"<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>flow</title><style>body{background:#111;color:#ddd;font:14px/1.5 ui-monospace,monospace;margin:0;padding:8px 12px 9em}h3{color:#fff;margin:16px 0 4px;font-size:15px}.ni{display:flex;align-items:flex-start;padding:6px 0;border-bottom:1px solid #1c1c1c;word-break:break-word}.nx{background:none;border:1px solid #555;color:#999;padding:5px 10px;margin-right:9px;border-radius:4px;cursor:pointer;flex:none}button{background:#111;color:#fff;border:1px solid #444;border-radius:6px;padding:6px 11px;margin:0 2px;cursor:pointer;font:13px monospace}a{color:#fff}</style><body><h3>NOTES <button onclick=\"fadd('note','note')\">+</button></h3>");
        bl+=_notes_build(buf+bl,cap-bl,"notes");
        bl+=snprintf(buf+bl,(size_t)(cap-bl),"<h3>TASKS <button onclick=\"fadd('task add -u','task')\">+</button> <span style=\"color:#555;font-size:12px\">sort: <a href=/flow?sort=pri>pri</a> <a href=/flow?sort=new>created</a> <a href=/flow?sort=due>deadline</a></span></h3>");
        bl+=_tasks_build(buf+bl,cap-bl,sort);
        bl+=snprintf(buf+bl,(size_t)(cap-bl),"<h3>PROMPT CANDIDATES <button onclick=\"fadd('prompt c','prompt')\">+</button></h3>");
        bl+=_notes_build(buf+bl,cap-bl,"prompts");
        bl+=snprintf(buf+bl,(size_t)(cap-bl),"<div style=\"position:fixed;left:0;right:0;bottom:0;background:#111;border-top:1px solid #333;padding:.6em;text-align:center\"><button onclick=fgen()>\342\234\246 suggest (book \342\206\222 claude)</button> <a href=\"/doc?f=common/prompts/propose.txt\">edit lens</a> <span id=fs style=color:#bbb></span></div><script>function omni(c){fs.textContent='\342\200\246';fetch('/api/omni',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'q='+encodeURIComponent(c)}).then(function(r){return r.text()}).then(function(){location.reload()})}function fadd(c,k){var v=prompt('new '+k);if(v)omni(c+' '+v)}function arc(u,key,f){var b={};b[key]=f;fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(function(){location.reload()})}function arcn(f){arc('/api/note/archive','f',f)}function arct(d){arc('/api/task/archive','d',d)}function arcp(f){arc('/api/prompt/archive','f',f)}function fgen(){var s=prompt('seed idea');if(!s)return;var b=prompt('book name as full context (blank=none)')||'-';omni('flow gen '+b+' '+s)}</script>");
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
    if(!strncmp(req,"GET /feeddismiss",16)){   /* web confirm → a feed dismiss (scan for live truth, archive all parked); gate on output not rc (SIGCHLD) */
        FILE*fp=popen("a feed dismiss 2>/dev/null|tail -1","r");char b[192];size_t n=fp?fread(b,1,191,fp):0;if(fp)pclose(fp);b[n]=0;
        _sresp(c,200,"text/plain",n?b:"x no output",n?(int)n:11);return;}
    if(!strncmp(req,"GET /feed",9)&&(req[9]==' '||req[9]=='?'||req[9]=='\r')){   /* terminal as API: page = live `a feed` output, streamed (shell paints instantly, content lands on fleet-scan drain) */
        static const char FH[]="HTTP/1.1 200 OK\r\nContent-Type:text/html; charset=utf-8\r\nCache-Control:no-store\r\nConnection:close\r\n\r\n"
            "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>feed</title>"
            "<style>body{margin:0;background:#0b0b0b;color:#ddd;font:14px/1.7 ui-monospace,monospace}h3{color:#fff;margin:0;padding:12px 14px 4px}#ms{color:#bbb;font-size:12px}pre{margin:0;padding:2px 14px 14px;white-space:pre-wrap}</style>"
            "<h3>a feed <span id=ms>⟳ scanning fleet…</span> <span style=\"color:#555;font-size:12px\">j/k \xe2\x86\x91\xe2\x86\x93 move · \xe2\x86\xb5 open box</span>"
            "<span id=dsm style=\"float:right;color:#e66;font-size:12px;cursor:pointer;border:1px solid #444;border-radius:6px;padding:2px 8px\">\xe2\x9c\x95 dismiss parked</span></h3>"
            "<script>dsm.onclick=function(){if(confirm('dismiss ALL parked sessions from feed?')){ms.textContent='dismissing...';"   /* handler in the HEADER: armed from first paint — footer JS only lands after the fleet scan drains (dead-button window, Sean hit it 8/4) */
            "fetch('/feeddismiss').then(function(r){return r.text()}).then(function(t){ms.textContent=t;setTimeout(function(){location.reload()},900)})}}</script><pre>";
        (void)!write(c,FH,sizeof FH-1);
        struct timespec t0,t1;clock_gettime(CLOCK_MONOTONIC,&t0);
        int pp[2];if(pipe(pp))return;pid_t ch=fork();
        if(!ch){dup2(pp[1],1);close(pp[0]);close(pp[1]);execlp("a","a","feed",(char*)0);_exit(1);}
        close(pp[1]);char b[2048],eb[10240];int r;
        while((r=(int)read(pp[0],b,sizeof b))>0){int o=0;
            for(int i=0;i<r;i++){char k=b[i];if(k=='<'){memcpy(eb+o,"&lt;",4);o+=4;}else if(k=='&'){memcpy(eb+o,"&amp;",5);o+=5;}else eb[o++]=k;}
            if(write(c,eb,(size_t)o)<0)break;}
        close(pp[0]);clock_gettime(CLOCK_MONOTONIC,&t1);
        char ft[1700];int fl=snprintf(ft,1700,"</pre><script>ms.textContent='%.4fms';"   /* rows -> divs; j/k/arrows move, Enter opens the box's tmux via /op (DEVICE col = chars 2..14) */
            "var Pr=document.querySelector('pre'),ls=Pr.textContent.split('\\n');"
            "Pr.innerHTML=ls.map(function(l){var e=l.replace(/&/g,'&amp;').replace(/</g,'&lt;');"
            "if(l.lastIndexOf('\xe2\x9c\x97 not accessible',0)==0){var p=e.split(': ');return '<details style=color:#e66><summary>'+p[0]+'</summary><div style=color:#999>'+(p[1]||'')+'</div></details>'}"
            "return '<div>'+e+'</div>'}).join('');"
            "var rs=[].slice.call(Pr.children).filter(function(d,i){return i>1&&(d.textContent[0]=='\xe2\x97\x8f'||d.textContent[0]=='\xe2\x8f\xb8')}),s=0;"
            "function H(){rs.forEach(function(d,i){d.style.background=i==s?'#333':''});rs[s]&&rs[s].scrollIntoView({block:'nearest'})}"
            "onkeydown=function(e){var k=e.key;if(k=='j'||k=='ArrowDown')s=Math.min(s+1,rs.length-1);else if(k=='k'||k=='ArrowUp')s=Math.max(s-1,0);"
            "else if(k=='Enter'&&rs[s]){location='/op?w=ssh:'+encodeURIComponent(rs[s].textContent.slice(2,16).trim());return}else return;e.preventDefault();H()};"
            "if(rs.length)H()</script>",(double)(t1.tv_sec-t0.tv_sec)*1e3+(double)(t1.tv_nsec-t0.tv_nsec)/1e6);
        (void)!write(c,ft,(size_t)fl);return;}
    if(!strncmp(req,"GET /dash",9)&&(req[9]==' '||req[9]=='?'||req[9]=='\r')){
        static const char H[]="<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><style>body{background:#000;color:#fff;font:16px system-ui;margin:16px}a{color:#fff;text-decoration:none;display:block;padding:10px;border-bottom:1px solid #222}h3{color:#888;font-weight:400;font-size:14px;margin:16px 0 4px}</style><div id=d>...</div><script>new EventSource('/dash/s').onmessage=m=>{var g={},o=[];m.data.split('|').filter(l=>l).forEach(w=>{var p=w.split('\\t');if(!g[p[0]])o.push(p[0]),g[p[0]]='';g[p[0]]+='<a href=\"/op?w='+encodeURIComponent(p[1])+'\">'+(p[2]||p[1])+'</a>'});d.innerHTML=o.map(x=>'<h3>'+x+'</h3>'+g[x]).join('')}</script>" TAPJS;
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
         if(!good){static const char NS[]="<body style='background:#000;color:#ddd;font:15px ui-monospace,monospace;padding:20px'>\xe2\x9c\x97 not serving \xe2\x80\x94 no <code>a serve</code> on :1111 of this device.<br><br><a style=color:#fff href=/dev>\xe2\x86\x90 devices</a>";_sresp(c,503,"text/html",NS,sizeof NS-1);return;}}
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
            "<style>body{margin:0;background:#000;color:#ddd;font:15px ui-monospace,monospace;padding:14px}h1{font-size:15px;color:#fff;margin:2px 0 8px;font-weight:normal}"
            "button{background:#111;color:#fff;border:1px solid #444;border-radius:6px;padding:7px 14px;font:13px ui-monospace,monospace;cursor:pointer}#cs{color:#666;font-size:12px;margin-left:8px}"
            "a.d{display:flex;align-items:center;gap:10px;color:#fff;text-decoration:none;padding:12px 14px;margin:7px 0;border:1px solid #333;border-radius:7px;background:#0a0a0a}a.d:active{background:#222}a.d.dead{opacity:.45}"
            ".st{flex:none;width:11px;height:11px;border-radius:50%%;background:#3a3a3a}.st.wait{background:#888}.st.ok{background:#fff}.st.no{background:#555}.st.off{background:#333}"
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
    if(!strncmp(req,"GET /mic/s",10)){   /* device mic -> streaming WAV (ffmpeg writes the header); page's <audio> plays it live */
        char dev[64];_qp(req,"?dev=",dev,64);
        pid_t fp=fork();if(fp<0){_sresp(c,500,"text/plain","fork",4);return;}
        if(fp>0)return;signal(SIGCHLD,SIG_DFL);
        static const char MV[]="ffmpeg -loglevel error -f pulse -fragment_size 4096 -i default -ac 1 -ar 24000 -f wav - 2>/dev/null";
        char gc[1024],pre[600];int r=_sshpre(dev,pre,600);if(r<0)_exit(0);
        if(!r){const char*rt=getenv("XDG_RUNTIME_DIR");if(!rt||!rt[0]){char rb[64];snprintf(rb,64,"/run/user/%d",(int)getuid());setenv("XDG_RUNTIME_DIR",rb,1);}}
        if(r)snprintf(gc,sizeof gc,"%s '%s'",pre,MV);else snprintf(gc,sizeof gc,"%s",MV);
        static const char AH[]="HTTP/1.1 200 OK\r\nContent-Type:audio/wav\r\nCache-Control:no-store\r\n\r\n";
        if(write(c,AH,sizeof AH-1)<0)_exit(0);
        FILE*g=popen(gc,"r");if(!g)_exit(0);
        char tb[8192];size_t rr;while((rr=fread(tb,1,sizeof tb,g))>0)if(write(c,tb,rr)<0)_exit(0);
        close(c);_exit(0);}
    if(!strncmp(req,"GET /cam",8)&&(req[8]==' '||req[8]=='?'||req[8]=='\r')){   /* cam viewer: local+fleet, snap=canvas download, mic toggle */
        char nav[2048];int nl=snprintf(nav,sizeof nav,"<button data-d=\"local\">%s</button>",DEV);
        char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int n=listdir(ddir,paths,64);
        for(int i=0;i<n&&nl<1800;i++){kvs_t kv=kvfile(paths[i]);const char*nm=kvget(&kv,"Name");
            if(nm)nl+=snprintf(nav+nl,(size_t)(sizeof nav-(size_t)nl),"<button data-d=\"%s\">%s</button>",nm,nm);}
        char h[8192];int hl=snprintf(h,sizeof h,
            "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>cam</title>"
            "<style>body{margin:0;background:#000;font:13px ui-monospace,monospace}"
            "#w{position:fixed;inset:0 0 108px 0;display:flex}#w img{flex:1;min-width:0;object-fit:contain}"
            "#b{position:fixed;left:0;right:0;bottom:0;padding:5px;display:flex;flex-direction:column;gap:5px;background:#0a0a0a}"
            "#L{position:fixed;inset:0 0 108px 0;background:#000e;display:none;flex-direction:column;gap:5px;padding:8px;overflow-y:auto}"
            "#st{color:#bbb;text-align:center;height:15px}.r{display:flex;gap:5px}"
            ".r button,#L button{flex:1;padding:10px 2px;background:#111;color:#fff;border:1px solid #333;border-radius:6px;font:inherit;overflow:hidden}"
            "#d{overflow-x:auto}#d button,#L button{flex:0 0 auto;padding:10px 8px}"
            ".on{background:#222;color:#fff}</style>"
            "<div id=w></div><div id=L>%s</div><div id=b><div id=st>pick a device</div><div class=r id=d></div>"
            "<div class=r><button id=sn>&#128247; snap</button><button id=mi>&#127908; mic</button></div></div>"
            "<script>var g=function(i){return document.getElementById(i)},H='%s',img=null,au=null,cur='';"
            "function S(t){g('st').textContent=t}"
            "function J(){try{return JSON.parse(localStorage.camr)||[]}catch(e){return[]}}"
            "function R(){var h='<button data-d=local'+(cur=='local'?' class=on':'')+'>'+H+'</button>';"
            "J().forEach(function(n){h+='<button data-d='+n+(cur==n?' class=on':'')+'>'+n+'</button>'});"
            "g('d').innerHTML=h+'<button id=al>\\u22ee all</button>'}"
            "function mio(on){if(au){au.pause();au.src='';au=null}g('mi').className='';if(!on)return;"
            "au=new Audio('/mic/s?dev='+(cur||'local'));au.play();g('mi').className='on';S('\\u25cf mic '+(cur||'local'))}"
            "function sel(d){mio(0);g('w').innerHTML='';img=null;if(cur===d){cur='';R();S('stopped');return}cur=d;"
            "if(d!='local')localStorage.camr=JSON.stringify([d].concat(J().filter(function(x){return x!=d})).slice(0,3));"
            "R();img=new Image();img.onload=function(){S('\\u25cf '+d)};img.onerror=function(){S('\\u2717 '+d)};"
            "img.src='/stream/s?dev='+d+'&c=1';g('w').appendChild(img);S('\\u27f3 '+d+'\\u2026')}"
            "function T(e){var b=e.target.closest('button');if(!b||b.id=='al'){g('L').style.display=b?'flex':'none';return}"
            "g('L').style.display='none';sel(b.dataset.d)}"
            "g('d').addEventListener('pointerdown',T);g('L').addEventListener('pointerdown',T);"
            "g('mi').addEventListener('pointerdown',function(){mio(!au)});"
            "g('sn').addEventListener('pointerdown',function(){if(!cur){S('\\u2717 no stream');return}"
            "var a=document.createElement('a');a.download='cam_'+cur+'.jpg';a.href='/stream/s?dev='+cur+'&pic=1';a.click()});"
            "R()</script>",nav,DEV);
        _sresp(c,200,"text/html",h,hl);return;}
    if(!strncmp(req,"GET /stream/s",13)){
        char dev[64];_qp(req,"?dev=",dev,64);
        char out[64];_qp(req,"&o=",out,64);  /* local output: name, "all"=whole layout, or empty=focused */
        char sp[96];snprintf(sp,96,"%s/a_snap_%s.jpg",TMP,dev[0]?dev:"local");
        if(strstr(req,"&pic=1")){size_t n=0;char*j=readf(sp,&n);   /* snap = latest teed frame; no capture, no canvas */
            if(j){_sresp(c,200,"image/jpeg",j,(int)n);free(j);}else _sresp(c,404,"text/plain","no snap",7);return;}
        pid_t fp=fork();if(fp<0){_sresp(c,500,"text/plain","fork",4);return;}
        if(fp>0)return;signal(SIGCHLD,SIG_DFL);
        char gc[1024],t2[104]="";int remote=0;
        if(strstr(req,"&c=1")){                                 /* camera: ffmpeg retry-loop self-heals (EBUSY/USB blip; parser SOI-anchor eats the '.' probe + restarts), printf dies with client = no /dev/video0 orphan; F_GETLK kills the prior view (browsers keep abandoned img sockets open). native MJPG, no v4l2-ctl */
            static const char CV[]="while printf .;do ffmpeg -loglevel error -f v4l2 -input_format mjpeg -i /dev/video0 -c copy -f mjpeg -;sleep .3;done";
            char pre[600];int cr=_sshpre(dev,pre,600);if(cr<0)_exit(0);
            if(cr)snprintf(gc,sizeof gc,"%s '%s'",pre,CV);else snprintf(gc,sizeof gc,"%s",CV);remote=1;
            snprintf(t2,104,"%s.t",sp);
            char lk[80];snprintf(lk,80,"%s/a_stream_%s",TMP,dev[0]?dev:"local");int lf=open(lk,O_RDWR|O_CREAT|O_CLOEXEC,0644);
            struct flock fl={.l_type=F_WRLCK};
            for(int i=0;lf>=0&&fcntl(lf,F_SETLK,&fl)<0&&i<50;i++){struct flock q=fl;if(!fcntl(lf,F_GETLK,&q)&&q.l_type!=F_UNLCK&&q.l_pid>0)kill(q.l_pid,SIGTERM);usleep(100000);}}
        else if(dev[0]&&strcmp(dev,"local")&&strcmp(dev,DEV)){       /* remote device: ssh in, capture to stdout */
            char pre[600];if(_sshpre(dev,pre,600)<1)_exit(0);
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
            for(size_t e=sc;e+1<len;e++)if(buf[e]==0xff&&buf[e+1]==0xd9){       /* FFD9=EOI */
                size_t s=0;while(s+1<e&&!(buf[s]==0xff&&buf[s+1]==0xd8))s++;    /* anchor the part on SOI: webcam -c copy pads a 00 between frames, and a part not starting FFD8 desyncs strict JPEG/multipart parsers (froze the browser mid-stream) */
                if(buf[s]==0xff&&buf[s+1]==0xd8){size_t fl=e+2-s;char hd[96];int hl=snprintf(hd,sizeof hd,"--f\r\nContent-Type:image/jpeg\r\nContent-Length:%zu\r\n\r\n",fl);
                    if(write(c,hd,(size_t)hl)<0||write(c,buf+s,fl)<0||write(c,"\r\n",2)<0)_exit(0);   /* client gone -> exit -> SIGPIPE ends the pipeline */
                    if(t2[0]){int td=open(t2,O_WRONLY|O_CREAT|O_TRUNC,0644);if(td>=0){(void)!write(td,buf+s,fl);close(td);(void)!rename(t2,sp);}}   /* tee latest frame, atomic, for &pic=1 */
                    if(++fn==1||fn%15==0){struct timeval w;gettimeofday(&w,NULL);double el=(double)(w.tv_sec-t0.tv_sec)+(w.tv_usec-t0.tv_usec)/1e6;
                        FILE*lf=fopen(lg,"a");if(lf){fprintf(lf,"stream %s/%s %df %.1ffps %zuKB\n",dev[0]?dev:"local",out[0]?out:"all",fn,el>0?fn/el:0,fl/1024);fclose(lf);}}}
                memmove(buf,buf+e+2,len-e-2);len-=e+2;e=(size_t)-1;sc=0;}
            sc=len>1?len-1:0;}
        close(c);_exit(0);}
    if(!strncmp(req,"GET /stream",11)&&(req[11]==' '||req[11]=='?'||req[11]=='\r')){
        char nav[4096];int nl=snprintf(nav,sizeof nav,"<a href=# onclick=\"if(cur)sel(cur);return false\" style=\"color:#ddd;border-color:#444\">\xe2\x96\xa0 stop</a><a data-d=local href=# onclick=\"sel('local');return false\" title=\"this machine \xc2\xb7 all monitors\">\xe2\x97\x89 %s \xc2\xb7 local</a>",DEV);
        {FILE*p=popen("R=${XDG_RUNTIME_DIR:-/run/user/$(id -u)};S=$(ls $R/sway-ipc.*.sock 2>/dev/null|head -1);SWAYSOCK=$S swaymsg -t get_outputs 2>/dev/null|python3 -c 'import sys,json;[print(o[\"name\"]) for o in json.load(sys.stdin)]' 2>/dev/null","r");   /* enumerate local monitors; device click streams them all stacked (composited whole-layout = 100Mpx/frame, unusable) */
         char on[64];if(p){while(fgets(on,64,p)&&nl<3100){on[strcspn(on,"\n")]=0;if(!on[0])continue;
             nl+=snprintf(nav+nl,(size_t)(sizeof nav-(size_t)nl),"<a class=sub data-d=\"local:%s\" href=# onclick=\"sel('local:%s');return false\">\xe2\x96\xa1 %s</a>",on,on,on);}pclose(p);}}
        {char ddir[P];snprintf(ddir,P,"%s/ssh",SROOT);char paths[64][P];int n=listdir(ddir,paths,64);
         for(int i=0;i<n&&nl<3500;i++){kvs_t kv=kvfile(paths[i]);const char*nm=kvget(&kv,"Name");
            if(nm)nl+=snprintf(nav+nl,(size_t)(sizeof nav-(size_t)nl),"<a data-d=\"%s\" href=# onclick=\"sel('%s');return false\">%s</a>",nm,nm,nm);}}
        char h[8192];int hl=snprintf(h,sizeof h,"<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>stream</title><style>html,body{margin:0;height:100%%;background:#000;font:13px ui-monospace,monospace}#b{position:fixed;top:0;left:0;bottom:0;width:150px;background:#0a0a0a;border-right:1px solid #222;padding:6px;display:flex;flex-direction:column;gap:4px;overflow-y:auto;z-index:9}#b a{color:#fff;text-decoration:none;padding:7px 9px;border:1px solid #333;border-radius:5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;cursor:pointer}#b a.on{background:#222;color:#fff;border-color:#555}#b a.sub{margin-left:14px;font-size:11px;border-left:3px solid #555;border-radius:0 5px 5px 0;color:#ccc}#st{position:fixed;bottom:10px;right:14px;display:none;color:#bbb;font:12px ui-monospace,monospace;background:#000a;padding:4px 10px;border-radius:6px;z-index:6}#st.ld{color:#fff;animation:pl 1s infinite}@keyframes pl{50%%{opacity:.4}}#w{position:fixed;inset:0 0 0 160px;display:flex;flex-direction:column}#w img{flex:1;min-height:0;object-fit:contain}#w p{margin:auto;color:#888}</style><div id=b>%s</div><div id=w><p>\xe2\x86\x90 pick a device</p></div><div id=st></div><script>var w=document.getElementById('w'),st=document.getElementById('st'),cur='',n=0;function P(){document.querySelectorAll('#b a').forEach(function(a){a.classList.toggle('on',a.getAttribute('data-d')===cur)})}function sel(d){w.innerHTML='';if(cur===d){cur='';st.style.display='none';P();return}cur=d;n=0;st.style.display='block';st.className='ld';st.textContent='\xe2\x9f\xb3 starting '+d+'\xe2\x80\xa6';var s=[].slice.call(document.querySelectorAll('#b a.sub')).map(function(a){return a.getAttribute('data-d')}).filter(function(x){return x.indexOf(d+':')==0});(s.length>1?s:[d]).forEach(function(m){var i=new Image();i.onload=function(){st.className='';st.textContent='\xe2\x97\x8f '+d+' \xc2\xb7 '+(++n)+' frames'};i.onerror=function(){st.className='';st.textContent='\xe2\x9c\x97 failed \xc2\xb7 '+d};var q=m.split(':');i.src='/stream/s?dev='+encodeURIComponent(q[0])+(q[1]?'&o='+encodeURIComponent(q[1]):'');w.appendChild(i)});P()}</script>",nav);
        _sresp(c,200,"text/html",h,hl);return;}
    if(!strncmp(req,"GET /op",7)&&(req[7]==' '||req[7]=='?'||req[7]=='\r')){
        const char*qw=strstr(req,"?w=");int idx=(qw&&isdigit((unsigned char)qw[3])&&!strstr(req,"&all"))?atoi(qw+3):-1;   /* &all = fleet view: skip agent-only gate, show any window */
        if(idx>=0){char tc[256];
            snprintf(tc,256,"p=$(tmux display-message -t a:%d -p '#{pane_pid}' 2>/dev/null);c=$(cat /proc/$p/task/$p/children 2>/dev/null|cut -d' ' -f1);[ -n \"$c\" ]&&cat /proc/$c/comm 2>/dev/null",idx);
            FILE*pp=popen(tc,"r");char nm[64]={0};
            if(pp){if(fgets(nm,64,pp))nm[strcspn(nm,"\n")]=0;pclose(pp);}
            if(strcmp(nm,"claude")&&strcmp(nm,"codex")&&strcmp(nm,"gemini")&&strcmp(nm,"aider")){
                static const char NO[]="<!doctype html><style>body{background:#000;color:#fff;font:16px system-ui;text-align:center;padding-top:40vh}a{color:#fff}</style>no agent<br><br><a href=/dash>← dash</a>";
                _sresp(c,200,"text/html",NO,sizeof NO-1);return;}}
        char tf[P];snprintf(tf,P,"%s/lib/term.html",SDIR);size_t tl=0;char*th=readf(tf,&tl); /* direct-DOM terminal page (replaced xterm.js CDN 7/10) */
        if(th){_siso(c,th,(int)tl);free(th);}
        else _sresp(c,404,"text/plain","no term.html",12);
        return;}
    _sresp(c,404,"text/plain","not found",9);
}
static int cmd_cam(int c,char**v){(void)c;(void)v;AB;perf_disarm();
    (void)!system("a ui on >/dev/null 2>&1");bg_exec(OPENER,"http://localhost:1111/cam");puts("✓ localhost:1111/cam");return 0;}
static int cmd_serve(int argc,char**argv){perf_disarm();signal(SIGPIPE,SIG_IGN);signal(SIGCHLD,SIG_IGN);
    {const char*op=getenv("PATH");if(!op)op="";char np[P];snprintf(np,P,"%s/.local/bin:/opt/homebrew/bin:/usr/local/bin:%s",HOME,op);setenv("PATH",np,1);}
    int port=argc>2?atoi(argv[2]):1111;
    if(argc>3){if(!realpath(argv[3],_sdir)||!dexists(_sdir)){printf("x no dir %s\n",argv[3]);return 1;}
        printf("+ site %s\n",_sdir);}
    else{printf("> generating HTML...\n");_html_gen();_prompt_gen();
        if(!_shtml){puts("x HTML generation failed");return 1;}
        printf("+ %d bytes cached\n",_shlen);}
    int fd=socket(AF_INET,SOCK_STREAM,0);fcntl(fd,F_SETFD,FD_CLOEXEC);
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&(int){1},4);
    struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};
    if(bind(fd,(void*)&a,sizeof a)<0){perror("bind");free(_shtml);return 1;} /* lost the port race -> exit BEFORE any tmux touch, so N concurrent invokes can't stampede the dashboard bridge */
    listen(fd,64);printf("+ http://localhost:%d (C server, pid %d)\n",port,(int)getpid());
    /* dashboard bridge — only the serve that actually owns the port reaches here. hooks are idempotent; the bridge is a flock singleton (was a racy `kill -0 $(cat pidfile)` TOCTOU: N serves each saw "none" and spawned N bridges, each a tight `tmux wait-for` loop that under window churn floods the server with client connects and could kill it). the 50ms coalesce caps tmux client spawns at ~20/s no matter how fast the hooks fire. flock fd auto-releases on death (no stale-pid wedge); pidfile path kept as a fallback where flock is absent (e.g. mac). */
    mkfifo("/tmp/a_dash.fifo",0644);(void)!open("/tmp/a_dash.fifo",O_RDWR|O_NONBLOCK);
    unlink("/tmp/a_extreload.fifo");mkfifo("/tmp/a_extreload.fifo",0644); /* ext hot-reload channel; left unopened so writes only land when a worker is reading */
    (void)!system("for h in after-new-window after-rename-window after-kill-pane session-window-changed;do tmux set-hook -g $h 'wait-for -S a_dash' 2>/dev/null;done; "
        "if command -v flock >/dev/null 2>&1; then "
        "(flock -n 9||exit 0;while :;do tmux wait-for a_dash 2>/dev/null||sleep 1;echo x>/tmp/a_dash.fifo 2>&-;sleep 0.05;done) 9>/tmp/.a_dashbr.lock & "
        "else "
        "p=/tmp/.a_dashbr.pid;kill -0 $(cat $p 2>/dev/null) 2>/dev/null||{ (while :;do tmux wait-for a_dash 2>/dev/null||sleep 1;echo x>/tmp/a_dash.fifo 2>&-;sleep 0.05;done)& echo $!>$p;}; "
        "fi");
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
