/* hub — concurrent jobs flock'd in git.c; warn on schedule overlap */
#define MJ 64
typedef struct { char n[64],s[16],p[512],d[64],lr[24]; int en; } hub_t;
static hub_t HJ[MJ]; static int NJ;

#define DFL(a,b) ((a)?(a):(b))
static char HD[P];
static void hub_load(void) {
    char fp[P];snprintf(HD,P,"%s/hub",SROOT);mkdirp(HD);NJ=0;
    snprintf(fp,P,"ls -t %s/*.txt 2>/dev/null",HD);FILE*f=popen(fp,"r");if(!f)return;
    while(fgets(fp,P,f)&&NJ<MJ){fp[strcspn(fp,"\n")]=0;
        kvs_t kv=kvfile(fp);const char*nm=kvget(&kv,"Name");if(!nm)continue;
        int di=0;for(;di<NJ&&strcmp(HJ[di].n,nm);di++){} if(di<NJ)continue;
        hub_t*j=&HJ[NJ++];const char*en=kvget(&kv,"Enabled");
        snprintf(j->n,64,"%s",nm);snprintf(j->s,16,"%s",DFL(kvget(&kv,"Schedule"),""));snprintf(j->p,512,"%s",DFL(kvget(&kv,"Prompt"),""));snprintf(j->d,64,"%s",DFL(kvget(&kv,"Device"),DEV));
        j->en=!en||en[0]=='t'||en[0]=='T';snprintf(j->lr,24,"%s",DFL(kvget(&kv,"Last-Run"),""));}pclose(f);
}

static void hub_save(hub_t *j) {
    char fn[P],buf[B];
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);struct tm*tm=localtime(&t.tv_sec);
    char ts[32];strftime(ts,32,"%Y%m%dT%H%M%S",tm);snprintf(fn,P,"%s/%s_%s.%09ld.txt",HD,j->n,ts,t.tv_nsec);
    int l=snprintf(buf,B,"Name: %s\nSchedule: %s\nPrompt: %s\nDevice: %s\nEnabled: %s\n",
        j->n,j->s,j->p,j->d,j->en?"true":"false");
    if(j->lr[0]) snprintf(buf+l,(size_t)(B-l),"Last-Run: %s\n",j->lr);
    writef(fn,buf);
}

#ifdef __ANDROID__
static unsigned hub_jid(const char*s){unsigned h=5381;for(;*s;s++)h=h*33+((unsigned)(unsigned char)*s);return(h%90000)+10000;}
static long hub_period(const char*s){
    if(!s||!*s)return 86400000L;
    if(!strcmp(s,"daily"))return 86400000L;
    if(strchr(s,'/')){int m=0;const char*p=strchr(s,'/');if(p)m=atoi(p+1);return m>0?(long)m*60000L:1800000L;}
    return 86400000L;/* H:MM = daily */
}
#endif
static void hub_timer(hub_t *j, int on) {
    char buf[B];
#ifdef __ANDROID__
    unsigned jid=hub_jid(j->n);
    char sd[P],sf[P];snprintf(sd,P,"%s/.local/share/a/jobs",HOME);mkdirp(sd);
    snprintf(sf,P,"%s/%s.sh",sd,j->n);
    if(on){
        snprintf(buf,B,"#!/data/data/com.termux/files/usr/bin/sh\nexec %s/.local/bin/a hub run %s\n",HOME,j->n);
        writef(sf,buf);chmod(sf,0700);
        long ms=hub_period(j->s);
        snprintf(buf,B,"termux-job-scheduler --script '%s' --job-id %u --period-ms %ld --persisted true 2>/dev/null",sf,jid,ms);
    } else {
        snprintf(buf,B,"termux-job-scheduler --cancel --job-id %u 2>/dev/null;rm -f '%s'",jid,sf);
    }
#else
    char sd[P]; snprintf(sd,P,"%s/.config/systemd/user",HOME); mkdirp(sd);
    if(on) {
        snprintf(buf,B,"[Unit]\nDescription=%s\n[Service]\nType=oneshot\nKillMode=process\nEnvironment=PATH=%s\nExecStart=/bin/bash -c '%s/.local/bin/a hub run %s'\n",j->n,getenv("PATH"),HOME,j->n);
        char svc[P]; snprintf(svc,P,"%s/a-%s.service",sd,j->n); writef(svc,buf);
        snprintf(buf,B,"[Unit]\nDescription=%s\n[Timer]\nOnCalendar=%s\nAccuracySec=1s\nPersistent=true\n[Install]\nWantedBy=timers.target\n",j->n,j->s);
        char tmr[P]; snprintf(tmr,P,"%s/a-%s.timer",sd,j->n); writef(tmr,buf);
        snprintf(buf,B,"systemctl --user daemon-reload && systemctl --user enable --now a-%s.timer 2>/dev/null",j->n);
    } else {
        snprintf(buf,B,"systemctl --user disable --now a-%s.timer 2>/dev/null;"
            "rm -f '%s/a-%s.timer' '%s/a-%s.service'",j->n,sd,j->n,sd,j->n);
    }
#endif
    (void)!system(buf);
}

static int hub_smin(const char*s){int h=0,m=0;if(!s||!*s)return 9999;
    if(strchr(s,'/'))return 0;sscanf(s,"%d:%d",&h,&m);return h*60+m;}
static int hub_cmp(const void*a,const void*b){return hub_smin(((const hub_t*)a)->s)-hub_smin(((const hub_t*)b)->s);}
static void hub_sort(void){qsort(HJ,(size_t)NJ,sizeof(hub_t),hub_cmp);}
static char HUB_TL[B*4];
static void hub_timers(void){
#ifdef __ANDROID__
    pcmd("termux-job-scheduler -p 2>/dev/null",HUB_TL,sizeof(HUB_TL));
#else
    pcmd("systemctl --user list-timers 2>/dev/null",HUB_TL,sizeof(HUB_TL));
#endif
}
static int hub_on(hub_t*j){char p[96];
#ifdef __ANDROID__
    snprintf(p,96,"Job %u:",hub_jid(j->n));
#else
    snprintf(p,96,"a-%s.timer",j->n);
#endif
    return(!strcmp(j->d,DEV))?j->en&&strstr(HUB_TL,p)!=NULL:j->en;}
static void hub_trunc(char*o,int sz,const char*s,int cw){snprintf(o,(size_t)sz,"%.*s",cw,s);}
static hub_t *hub_find(const char *s) {
    if(s[0]>='0'&&s[0]<='9') { int i=atoi(s); return i<NJ?&HJ[i]:NULL; }
    for(int i=0;i<NJ;i++) if(!strcmp(HJ[i].n,s)) return &HJ[i];
    return NULL;
}
static int hub_list(int all,const char*q){
    hub_timers();int tw=80,sh=0;struct winsize ws;if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==0)tw=ws.ws_col;
    int m=tw<60,cw=tw-(m?32:48);
    printf(m?"# %-8s %-9s On Cmd\n":"# %-10s %-6s %-12s %-8s On Cmd\n","Name",m?"Last":"Sched","Last","Dev");
    for(int i=0;i<NJ;i++){hub_t*j=&HJ[i];
        if(!all&&!q&&!j->en)continue;
        if(q&&!strcasestr(j->n,q)&&!strcasestr(j->p,q)&&!strcasestr(j->d,q))continue;
        int on=hub_on(j);char cp[512];hub_trunc(cp,512,j->p,cw);
        const char*lr=j->lr[0]?j->lr+5:"-";sh++;
        if(m)printf("%-2d%-9s%-10s%s %s\n",i,j->n,lr,on?"✓":" ",cp);
        else printf("%-2d%-11s%-7s%-13s%-8s%s %s\n",i,j->n,j->s,lr,j->d,on?"✓":" ",cp);}
    if(q)printf("\n%d/%d match '%s'\n",sh,NJ,q);
    else printf(NJ-sh?"\n%d jobs (+%d disabled, a hub all)\n":"\n%d jobs\n",sh,NJ-sh);
    printf("a hub <#>       run job\na hub on/off #  toggle\na hub add|rm    create/delete\na hub all       show disabled\na hub <query>   search\n");
    return 0;}

static int cmd_hub(int argc, char **argv) {
    init_db(); hub_load();
    const char *sub=argc>2?argv[2]:NULL;

    if(!sub||!strcmp(sub,"all"))return hub_list(sub&&!strcmp(sub,"all"),NULL);

    if(!strcmp(sub,"add")) {
        if(argc<6) { fprintf(stderr,"Usage: a hub add <name> <sched> <cmd...>\n"); return 1; }
        hub_t j={.en=1}; snprintf(j.n,64,"%s",argv[3]); snprintf(j.s,16,"%s",argv[4]);
        char cmd[B]=""; ajoin(cmd,B,argc,argv,5);
        snprintf(j.p,512,"%s",cmd); snprintf(j.d,64,"%s",DEV);
        for(int i=0;i<NJ;i++)if(strcmp(HJ[i].n,j.n)&&!strcmp(HJ[i].s,j.s)&&HJ[i].en)
            fprintf(stderr,"! %s also at %s\n",HJ[i].n,j.s);
        hub_save(&j); hub_timer(&j,1);
        printf("✓ %s @ %s\n",j.n,j.s); return 0;
    }

    if(sub[0]>='0'&&sub[0]<='9'){argv[3]=argv[2];argv[2]=(char*)"run";sub="run";argc=4;}
    if(!strcmp(sub,"run")||!strcmp(sub,"on")||!strcmp(sub,"off")||!strcmp(sub,"rm")) {
        hub_t *j=argc>3?hub_find(argv[3]):NULL;
        if(!j) { fprintf(stderr,"x %s?\n",argc>3?argv[3]:"(missing)"); return 1; }
        if(!strcmp(sub,"run")) {
            char cmd[B]; const char*jp=j->p; if(!strncmp(jp,"aio ",4))jp+=2;
            if(!strncmp(jp,"a ",2)) snprintf(cmd,B,"%s %s",G_argv[0],jp+2);
            else snprintf(cmd,B,"%s",jp);
            printf("Running %s...\n",j->n);fflush(stdout);
            char lf[P];snprintf(lf,P,"%s/hub.log",DDIR);
            char out[B*4]="";int ol=0,fail=1,sch=j->s[0]!=0;int bk[]={0,10,100,1000,10000};
            for(int try=0;try<(sch?5:2)&&fail;try++){if(try)sleep(sch?(unsigned)bk[try]:2);
                FILE*fp=popen(cmd,"r");ol=0;if(!fp)continue;char b[B];
                while(fgets(b,B,fp)&&ol<(int)sizeof(out)-B){fputs(b,stdout);ol+=sprintf(out+ol,"%s",b);}
                fail=pclose(fp)!=0;}
            time_t now=time(NULL); struct tm *t=localtime(&now); char ts[32];
            strftime(ts,32,"%Y-%m-%d %I:%M:%S%p",t); strftime(j->lr,24,"%Y-%m-%d %H:%M",t);
            hub_save(j);
            FILE *lp=fopen(lf,"a"); if(lp) { fprintf(lp,"\n[%s] %s%s\n%s",ts,j->n,fail?" FAILED":"",out); fclose(lp); }
            char sn[128]; snprintf(sn,128,"hub:%s",j->n); alog(sn,"");
            if(fail&&j->s[0]) { char ec[B];snprintf(ec,B,"%s email 'hub: %s failed' '%s failed on %s — see hub.log'",G_argv[0],j->n,j->n,DEV);(void)!system(ec); }
            if(fail) { printf("✗ %s failed\n",j->n); return 1; }
            printf("✓\n"); return 0;
        }
        if(!strcmp(sub,"on")||!strcmp(sub,"off")) {
            j->en=sub[1]=='n'; hub_save(j); hub_timer(j,j->en);
            printf("✓ %s %s\n",j->n,sub); return 0;
        }
        hub_timer(j,0);
        {char c[B];snprintf(c,B,"git -C '%s' rm -qf %s*.txt %s_*.txt 2>/dev/null",HD,j->n,j->n);(void)!system(c);}
        printf("✓ rm %s\n",j->n); return 0;
    }

    if(!strcmp(sub,"sync")) {
#ifdef __ANDROID__
        (void)!system("termux-job-scheduler --cancel-all 2>/dev/null");
        {char c[B];snprintf(c,B,"(crontab -l 2>/dev/null|grep -v '# a:\\|# aio:')|crontab - 2>/dev/null");(void)!system(c);}
#else
        {char c[B];snprintf(c,B,"systemctl --user disable --now aio-*.timer 2>/dev/null;rm -f %s/.config/systemd/user/aio-*.{timer,service} 2>/dev/null",HOME);(void)!system(c);}
        for(int i=0;i<NJ;i++) hub_timer(&HJ[i],0);
#endif
        int m=0; for(int i=0;i<NJ;i++) if(!strcmp(HJ[i].d,DEV)&&HJ[i].en) { hub_timer(&HJ[i],1); m++; }
        printf("✓ synced %d jobs\n",m); return 0;
    }

    if(!strcmp(sub,"log")) {
        char c[P];snprintf(c,P,"tail -40 '%s/hub.log'",DDIR);(void)!system(c);return 0;
    }

    return hub_list(1,sub); /* unknown sub = search */
}
