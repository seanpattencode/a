/* a grep [term] — indexed all-repo code+notes+commit-log search. tty+no-args = live TUI (type/backspace, ↑↓ sel, ⏎ = e at hit line / cd for dirs+⎇log); args = one-shot; a grep index = rebuild. repos: SROOT/my/grep.repos.
   idx = [orig][\2][lowercase] byte-identical halves; "\1path\n"+content per file; "\1⎇ repo\n"+log LAST. ORDER RULE: display order = blob order — zero search-time ranking. Scan = rarest-byte memchr, threaded; TUI appends filter prev full set in µs. */
#include <pthread.h>
#include <termios.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#define FCAP (256*1024)
#define RCAP (12u<<20)
#define SHOW 40
#define HKEEP 256
#define QMAX 128
#define MAXT 24
static char gp_idx[P];
static const char*gp_def[]={"a","u","i","b","e","alab","a/adata/git","inspiration","qcompress-context","civilization","fredix",0};
static double gp_ms(struct timespec a,struct timespec b){return(b.tv_sec-a.tv_sec)*1e3+(b.tv_nsec-a.tv_nsec)/1e6;}
static const char*gp_rel(const char*p){size_t hl=strlen(HOME);return !strncmp(p,HOME,hl)&&p[hl]=='/'?p+hl+1:p;}

static int gp_nrepo;static char gp_repo[64][512];
static void gp_repos(void){
    char f[512],ln[512];snprintf(f,512,"%s/my/grep.repos",SROOT);FILE*p=fopen(f,"r");
    if(p){while(gp_nrepo<64&&fgets(ln,512,p)){ln[strcspn(ln,"\n")]=0;if(!*ln||*ln=='#')continue;
        snprintf(gp_repo[gp_nrepo++],512,"%s%s%s",*ln=='/'?"":HOME,*ln=='/'?"":"/",ln);}fclose(p);return;}
    for(int i=0;gp_def[i]&&gp_nrepo<64;i++)snprintf(gp_repo[gp_nrepo++],512,"%s/%s",HOME,gp_def[i]);
}

static int gp_build(void){
    struct timespec t0,t1;clock_gettime(CLOCK_MONOTONIC,&t0);
    char tmp[512],cmd[600],path[2048],fp[2600];snprintf(tmp,512,"%s.tmp",gp_idx);
    FILE*o=fopen(tmp,"w");if(!o){perror(tmp);return 1;}
    size_t cap=8u<<20,len=0;char*all=malloc(cap);if(!all){fclose(o);return 1;}int nf=0;
    for(int i=0;i<gp_nrepo;i++){                           /* pass 1: files first */
        snprintf(cmd,600,"git -C '%s' ls-files -z 2>/dev/null",gp_repo[i]);
        FILE*p=popen(cmd,"r");if(!p)continue;
        size_t r0=len,pl=0;int ch,capped=0;
        while((ch=fgetc(p))!=EOF){
            if(ch){if(pl<2047)path[pl++]=(char)ch;continue;}
            path[pl]=0;pl=0;
            if(capped||len-r0>RCAP){capped=1;continue;}
            struct stat st;snprintf(fp,2600,"%s/%s",gp_repo[i],path);
            if(stat(fp,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>FCAP)continue;
            size_t need=len+(size_t)st.st_size+strlen(fp)+4;
            while(need>cap){cap*=2;all=realloc(all,cap);}
            size_t hdr=len;len+=(size_t)snprintf(all+len,cap-len,"\1%s\n",fp);
            int fd=open(fp,O_RDONLY);if(fd<0){len=hdr;continue;}
            ssize_t rd=read(fd,all+len,(size_t)st.st_size);close(fd);
            if(rd<=0||memchr(all+len,0,(size_t)rd)||memchr(all+len,1,(size_t)rd)||memchr(all+len,2,(size_t)rd)){len=hdr;continue;}
            len+=(size_t)rd;nf++;
        }
        pclose(p);
        fprintf(stderr,"  %-28s %6.2fMB%s\n",gp_rel(gp_repo[i]),(len-r0)/1e6,capped?"  (CAPPED at 12MB — grep.repos to tune)":"");
    }
    for(int i=0;i<gp_nrepo;i++){                           /* pass 2: logs last */
        snprintf(cmd,600,"git -C '%s' log --format='%%h %%s' 2>/dev/null",gp_repo[i]);
        FILE*lg=popen(cmd,"r");if(!lg)continue;
        while(cap<len+(1u<<20)+4096){cap*=2;all=realloc(all,cap);}
        size_t hdr=len,rd;len+=(size_t)snprintf(all+len,cap-len,"\1⎇ %s\n",gp_rel(gp_repo[i]));
        size_t l0=len;
        while(len-l0<(1u<<20)&&(rd=fread(all+len,1,65536,lg))>0)len+=rd;
        for(size_t z=l0;z<len;z++)if((unsigned char)all[z]<3)all[z]=' ';
        if(len==l0)len=hdr;else nf++;
        pclose(lg);
    }
    fwrite(all,1,len,o);fputc(2,o);
    for(size_t i=0;i<len;i++)all[i]=(char)tolower((unsigned char)all[i]);
    fwrite(all,1,len,o);fclose(o);free(all);
    if(rename(tmp,gp_idx)){perror("rename");return 1;}
    clock_gettime(CLOCK_MONOTONIC,&t1);
    fprintf(stderr,"indexed %d files, %.1fMB in %.0fms → %s\n",nf,len/1e6,gp_ms(t0,t1),gp_idx);
    return 0;
}

static void gp_stale(void){
    struct stat si,sg,sl;if(stat(gp_idx,&si))return;
    for(int i=0;i<gp_nrepo;i++){
        char g[600];snprintf(g,600,"%s/.git/index",gp_repo[i]);
        if(!stat(g,&sg)&&sg.st_mtime>si.st_mtime){
            char lk[512];snprintf(lk,512,"%s.lock",gp_idx);
            if(!stat(lk,&sl)){if(time(0)-sl.st_mtime<120)return;unlink(lk);}
            int lf=open(lk,O_CREAT|O_EXCL|O_WRONLY,0644);if(lf<0)return;close(lf);
            fprintf(stderr,"(index stale vs %s — refreshing in background)\n",gp_rel(gp_repo[i]));
            if(!fork()){if(!fork()){freopen("/dev/null","w",stderr);gp_build();unlink(lk);}_exit(0);}
            return;
        }
    }
}

typedef struct{const char*hay,*q;size_t s,e,nl,half;size_t hit[SHOW];int n;}GPTH;
static unsigned char gp_rb;static size_t gp_rj;            /* rarest needle byte+offset */
static void*gp_scan(void*a){GPTH*t=a;
    size_t off=t->s+gp_rj,hi=t->e+gp_rj;if(hi>t->half)hi=t->half;
    while(t->n<SHOW&&off<hi){
        const char*p=memchr(t->hay+off,gp_rb,hi-off);if(!p)break;
        size_t hs=(size_t)(p-t->hay)-gp_rj;
        if(hs+t->nl<=t->half&&!memcmp(t->hay+hs,t->q,t->nl))t->hit[t->n++]=hs;
        off=(size_t)(p-t->hay)+1;}
    return 0;}
static int gp_ocmp(const void*a,const void*b){size_t x=*(const size_t*)a,y=*(const size_t*)b;return(x>y)-(x<y);}

static int gp_coll(char*m,char*mid,size_t half,const char*need0,size_t*out,int outcap,int*cappedp,int*cip){
    size_t nl=strlen(need0);char low[512];int ci=1;
    for(size_t i=0;i<=nl&&i<511;i++){if(isupper((unsigned char)need0[i]))ci=0;low[i]=(char)tolower((unsigned char)need0[i]);}
    const char*hay=ci?mid+1:m,*q=ci?low:need0;
    long T=sysconf(_SC_NPROCESSORS_ONLN);if(T>MAXT)T=MAXT;if(T<1||half<(2u<<20))T=1;
    size_t cnt[256]={0},sc=half<(64u<<10)?half:(64u<<10);
    for(size_t i=0;i<sc;i++)cnt[(unsigned char)hay[i]]++;
    gp_rb=(unsigned char)q[0];gp_rj=0;
    for(size_t j=1;j<nl;j++)if(cnt[(unsigned char)q[j]]<cnt[gp_rb]){gp_rb=(unsigned char)q[j];gp_rj=j;}
    GPTH th[MAXT];pthread_t id[MAXT];size_t chunk=half/(size_t)T;
    for(long i=0;i<T;i++){
        th[i]=(GPTH){hay,q,(size_t)i*chunk,i==T-1?half:(size_t)(i+1)*chunk,nl,half,{0},0};
        if(i<T-1)pthread_create(&id[i],0,gp_scan,&th[i]);else gp_scan(&th[i]);}
    size_t allv[MAXT*SHOW]={0};int na=0,capped=0;
    for(long i=0;i<T;i++){
        if(i<T-1)pthread_join(id[i],0);
        if(th[i].n==SHOW)capped=1;
        for(int j=0;j<th[i].n;j++)allv[na++]=th[i].hit[j];}
    qsort(allv,(size_t)na,sizeof(size_t),gp_ocmp);
    memcpy(out,allv,(size_t)(na<outcap?na:outcap)*sizeof(size_t));
    *cappedp=capped;*cip=ci;return na;
}

typedef struct{const char*pp;int ppl;const char*ls;int ll,ln,islog,isname;const char*fp;int fpl;size_t foff;}GPHIT;
static size_t gp_dec(const char*m,size_t half,size_t ho,size_t hl,GPHIT*h){
    const char*o=m+ho,*ls=o;
    while(ls>m&&ls[-1]!='\n')ls--;
    const char*le=memchr(o,'\n',half-ho);if(!le)le=m+half;
    const char*hd=0;for(const char*p=m+ho;p>m;)if(*--p==1){hd=p;break;}  /* memrchr: no BSD/macOS libc */
    const char*hp=hd?hd+1:m;
    const char*he=memchr(hp,'\n',(size_t)(m+half-hp));if(!he)he=m+half;
    h->fp=hp;h->fpl=(int)(he-hp);
    h->foff=ls>he?(size_t)(ls-(he+1)):0;
    h->pp=hp;h->ppl=h->fpl;
    if(!strncmp(hp,HOME,hl)&&hp[hl]=='/'){h->pp+=hl+1;h->ppl-=(int)hl+1;}
    h->islog=h->ppl>3&&!memcmp(h->pp,"\xe2\x8e\x87",3);
    h->isname=*ls==1;
    h->ls=ls;h->ll=(int)(le-ls);if(h->ll>200)h->ll=200;
    h->ln=0;
    if(!h->islog&&!h->isname){int ln=1;
        for(const char*z=he+1;z<ls;){const char*n=memchr(z,'\n',(size_t)(ls-z));if(!n)break;ln++;z=n+1;}
        h->ln=ln;}
    return(size_t)(le-m)+1;
}

static int gp_omap(char**mp,char**midp,size_t*halfp){
    int fd=open(gp_idx,O_RDONLY);
    if(fd<0){fprintf(stderr,"first run: building index…\n");if(gp_build())return -1;fd=open(gp_idx,O_RDONLY);if(fd<0)return -1;}
    struct stat st;fstat(fd,&st);
    char*m=mmap(0,(size_t)st.st_size,PROT_READ,MAP_PRIVATE,fd,0);if(m==MAP_FAILED)return -1;
    char*mid=m+(st.st_size-1)/2;                           /* midpoint arithmetic */
    if(*mid!=2){mid=memchr(m,2,(size_t)st.st_size);if(!mid)return -1;}
    *mp=m;*midp=mid;*halfp=(size_t)(mid-m);return fd;
}

static int gp_srch(const char*need0){
    struct timespec tw0,t0,t1,t2;clock_gettime(CLOCK_MONOTONIC,&tw0);
    char*m,*mid;size_t half;if(gp_omap(&m,&mid,&half)<0)return 1;
    size_t hl=strlen(HOME),out[MAXT*SHOW],last=0;int capped,ci,shown=0;
    clock_gettime(CLOCK_MONOTONIC,&t0);
    int na=gp_coll(m,mid,half,need0,out,MAXT*SHOW,&capped,&ci);
    clock_gettime(CLOCK_MONOTONIC,&t1);
    for(int i=0;i<na&&shown<SHOW;i++){
        if(out[i]<last)continue;
        GPHIT h;last=gp_dec(m,half,out[i],hl,&h);shown++;
        if(h.isname)printf("\033[35m%.*s\033[0m  (filename match)\n",h.ppl,h.pp);
        else if(h.islog)printf("\033[2m%.*s\033[0m %.*s\n",h.ppl,h.pp,h.ll,h.ls);
        else printf("\033[35m%.*s\033[0m:%d: %.*s\n",h.ppl,h.pp,h.ln,h.ll,h.ls);
    }
    clock_gettime(CLOCK_MONOTONIC,&t2);
    if(capped||na>shown)printf("… showing %d (more exist)\n",shown);
    fprintf(stderr,"%d hit%s  \033[32m%.4fms search\033[0m  %.4fms total  idx %.1fMB%s\n",
        shown,shown==1?"":"s",gp_ms(t0,t1),gp_ms(tw0,t2),half/1e6,ci?"":"  (case-sensitive: query has uppercase)");
    gp_stale();
    return shown?0:1;
}

/* ---- live TUI ---- */
typedef struct{int ok,n,na,capped,full,ci;size_t off[HKEEP];}GPRS;
static struct termios gp_tsav;static int gp_traw=0;
static void gp_trest(void){if(gp_traw){tcsetattr(0,TCSANOW,&gp_tsav);(void)!write(1,"\033[?1049l\033[?25h",14);gp_traw=0;}}
static void gp_cdt(const char*d){                          /* a() wrapper consumes cd_target */
    char ct[P];snprintf(ct,P,"%s/cd_target",DDIR);writef(ct,d);
    gp_trest();printf("→ %s\n",d);}
static int gp_eo(const char*p,size_t off){
    gp_trest();
    if(off){char ob[32];snprintf(ob,32,"+%zu",off);printf("e %s %s\n",ob,p);execlp("e","e",ob,p,(char*)0);}
    else{printf("e %s\n",p);execlp("e","e",p,(char*)0);}
    perror("e");return 1;}
static int gp_nls;static char gp_lsn[512][256];            /* [255] = isdir flag */
static int gp_lcmp(const void*a,const void*b){const char*x=a,*y=b;return x[255]!=y[255]?y[255]-x[255]:strcmp(x,y);}
static int gp_tui(void){
    char*m,*mid;size_t half;if(gp_omap(&m,&mid,&half)<0)return 1;
    size_t hl=strlen(HOME);
    gp_stale();
    char rcwd[1024],crumb[1100];                           /* cwd = user shell's (CMDS dispatch never chdirs) */
    if(!getcwd(rcwd,1024))snprintf(rcwd,1024,"%s",HOME);
    int uh=!strncmp(rcwd,HOME,hl)&&(rcwd[hl]=='/'||!rcwd[hl]);
    snprintf(crumb,1100,"%s%s",uh?"~":"",uh?rcwd+hl:rcwd);
    DIR*d=opendir(".");struct dirent*de;
    while(d&&(de=readdir(d))&&gp_nls<512){
        if(de->d_name[0]=='.')continue;
        snprintf(gp_lsn[gp_nls],255,"%s",de->d_name);
        if(de->d_type==DT_UNKNOWN||de->d_type==DT_LNK){struct stat sb;gp_lsn[gp_nls][255]=!stat(de->d_name,&sb)&&S_ISDIR(sb.st_mode);}
        else gp_lsn[gp_nls][255]=de->d_type==DT_DIR;
        gp_nls++;}
    if(d)closedir(d);
    qsort(gp_lsn,(size_t)gp_nls,256,gp_lcmp);
    tcgetattr(0,&gp_tsav);atexit(gp_trest);
    struct termios r=gp_tsav;r.c_lflag&=(tcflag_t)~(ICANON|ECHO|ISIG);r.c_cc[VMIN]=1;r.c_cc[VTIME]=0;
    tcsetattr(0,TCSANOW,&r);gp_traw=1;
    (void)!write(1,"\033[?1049h\033[?25l",14);
    struct winsize ws={0};ioctl(1,TIOCGWINSZ,&ws);
    int rows=ws.ws_row?ws.ws_row:40,cols=ws.ws_col?ws.ws_col:120;
    if(cols>1000)cols=1000;
    static GPRS hist[QMAX];char qb[QMAX]={0};int ql=0,sel=0;double lms=0;
    for(;;){
        struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
        GPRS*cur=&hist[ql];
        if(!cur->ok){
            qb[ql]=0;
            int newci=1;for(int i=0;i<ql;i++)if(isupper((unsigned char)qb[i]))newci=0;
            GPRS*pv=&hist[ql-1];
            if(ql>1&&pv->ok&&pv->full&&pv->ci==newci){     /* append filter */
                const char*hay=newci?mid+1:m;
                char qc=newci?(char)tolower((unsigned char)qb[ql-1]):qb[ql-1];
                cur->n=0;
                for(int i=0;i<pv->n;i++){size_t o=pv->off[i];
                    if(o+(size_t)ql<=half&&hay[o+(size_t)ql-1]==qc)cur->off[cur->n++]=o;}
                cur->na=cur->n;cur->capped=0;cur->full=1;cur->ci=newci;
            }else if(ql){
                cur->na=gp_coll(m,mid,half,qb,cur->off,HKEEP,&cur->capped,&cur->ci);
                cur->n=cur->na<HKEEP?cur->na:HKEEP;
                cur->full=!cur->capped&&cur->na<=HKEEP;
            }else{cur->n=cur->na=cur->capped=0;cur->full=1;cur->ci=1;}
            cur->ok=1;
        }
        size_t disp[128];int nd=0,lim=rows-4;
        lim=lim<1?1:lim>127?127:lim;
        if(ql){size_t last=0;
            for(int i=0;i<cur->n&&nd<lim;i++){
                if(cur->off[i]<last)continue;
                GPHIT h;last=gp_dec(m,half,cur->off[i],hl,&h);
                disp[nd++]=cur->off[i];}}
        else nd=gp_nls<lim?gp_nls:lim;
        if(sel>=nd)sel=nd?nd-1:0;
        static char fb[262144];size_t fbp=0;
        fbp+=(size_t)snprintf(fb+fbp,sizeof fb-fbp,"\033[Hgrep> %.*s█\033[K\r\n\033[2m%s\033[0m\033[K\r\n",ql,qb,crumb);
        for(int i=0;i<nd;i++){
            char ln[1200];
            if(!ql)snprintf(ln,sizeof ln,"%s%s",gp_lsn[i],gp_lsn[i][255]?"/":"");
            else{GPHIT h;gp_dec(m,half,disp[i],hl,&h);
                if(h.isname)snprintf(ln,sizeof ln,"%.*s  (file)",h.ppl,h.pp);
                else if(h.islog)snprintf(ln,sizeof ln,"%.*s %.*s",h.ppl,h.pp,h.ll,h.ls);
                else snprintf(ln,sizeof ln,"%.*s:%d: %.*s",h.ppl,h.pp,h.ln,h.ll,h.ls);}
            for(char*z=ln;*z;z++)if((unsigned char)*z<32)*z=' ';
            fbp+=(size_t)snprintf(fb+fbp,sizeof fb-fbp,"%s%.*s\033[0m\033[K\r\n",i==sel?"\033[7m":"",cols-1,ln);
        }
        clock_gettime(CLOCK_MONOTONIC,&b);lms=gp_ms(a,b);
        char st[256];
        if(!ql)snprintf(st,256," %d file%s · %s · type to search · %.4fms · ⏎ open · esc quit ",gp_nls,gp_nls==1?"":"s",crumb,lms);
        else snprintf(st,256," %s%d hit%s · \033[1m%.4fms\033[0m\033[7m · ↑↓ select · ⏎ open · esc quit ",
            cur->capped?"≥":"",cur->na,cur->na==1?"":"s",lms);
        fbp+=(size_t)snprintf(fb+fbp,sizeof fb-fbp,"\033[J\033[%d;1H\033[7m%s\033[0m",rows,st);
        (void)!write(1,fb,fbp);
        unsigned char c;if(read(0,&c,1)!=1)break;
        if(c==27){struct termios t2=r;t2.c_cc[VMIN]=0;t2.c_cc[VTIME]=1;tcsetattr(0,TCSANOW,&t2);
            unsigned char d2,fin=0;int got=0;
            while(read(0,&d2,1)==1){got=1;if(isalpha(d2)||d2=='~'){fin=d2;break;}}
            tcsetattr(0,TCSANOW,&r);
            if(!got)break;
            if(fin=='A'&&sel>0)sel--;
            if(fin=='B'&&sel<nd-1)sel++;
            continue;}
        if(c==3)break;
        if(c==16){if(sel>0)sel--;continue;}
        if(c==14){if(sel<nd-1)sel++;continue;}
        if(c=='\r'||c=='\n'){
            if(!nd)break;
            if(!ql){char abs[1400];snprintf(abs,1400,"%s/%s",rcwd,gp_lsn[sel]);
                if(gp_lsn[sel][255]){gp_cdt(abs);return 0;}
                return gp_eo(abs,0);}
            GPHIT h;gp_dec(m,half,disp[sel],hl,&h);
            if(h.islog){char dir[2200];int nml=h.fpl-4;const char*nm=h.fp+4;   /* skip "⎇ " */
                if(nml>0&&nm[0]=='/')snprintf(dir,2200,"%.*s",nml,nm);
                else snprintf(dir,2200,"%s/%.*s",HOME,nml>0?nml:1,nml>0?nm:".");
                gp_cdt(dir);return 0;}
            char path[2200];snprintf(path,2200,"%.*s",h.fpl,h.fp);
            return gp_eo(path,h.foff);}
        if(c==127||c==8){if(ql>0)ql--;sel=0;continue;}
        if(c==21){for(int i=1;i<QMAX;i++)hist[i].ok=0;ql=0;sel=0;continue;}
        if(c>=32&&c<127&&ql<QMAX-2){qb[ql++]=(char)c;hist[ql].ok=0;sel=0;}
    }
    gp_trest();return 0;
}

static int cmd_grep(int c,char**v){
    perf_disarm();
    snprintf(gp_idx,P,"%s/grep.idx",DDIR);
    gp_repos();
    if(c==3&&!strcmp(v[2],"index"))return gp_build();
    if(c<3){
        if(isatty(0)&&isatty(1))return gp_tui();
        fprintf(stderr,"usage: a grep <term…>   (tty: a grep = live TUI; a grep index = rebuild; repos: my/grep.repos)\n");
        return 1;}
    char need[512]="";size_t l=0;
    for(int i=2;i<c;i++)l+=(size_t)snprintf(need+l,512-l,"%s%s",i>2?" ":"",v[i]);
    return gp_srch(need);
}
#undef FCAP
#undef RCAP
#undef SHOW
#undef HKEEP
#undef QMAX
#undef MAXT
