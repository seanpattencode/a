/* hub — thin wrapper over systemd user timers (termux-job-scheduler on Android).
 * Job defs in adata/git/hub/<name>.txt; runtime, logs, status owned by systemd. */
#define MJ 256
typedef struct { char n[64],s[32],p[512],d[64]; int en; } hub_t;
static hub_t HJ[MJ]; static int NJ;
#define DFL(a,b) ((a)?(a):(b))
static char HD[P];

static void hub_load(void) {
    char fp[P];snprintf(HD,P,"%s/hub",SROOT);mkdirp(HD);NJ=0;
    DIR*d=opendir(HD);if(!d)return;struct dirent*e;
    while((e=readdir(d))&&NJ<MJ){if(e->d_name[0]=='.'||e->d_name[0]=='_')continue;
        const char*x=strrchr(e->d_name,'.');if(!x||strcmp(x,".txt"))continue;
        snprintf(fp,P,"%s/%s",HD,e->d_name);
        kvs_t kv=kvfile(fp);const char*nm=kvget(&kv,"Name");if(!nm)continue;
        hub_t*j=&HJ[NJ++];const char*en=kvget(&kv,"Enabled");
        snprintf(j->n,64,"%s",nm);
        snprintf(j->s,32,"%s",DFL(kvget(&kv,"Schedule"),""));
        snprintf(j->p,512,"%s",DFL(kvget(&kv,"Prompt"),""));
        snprintf(j->d,64,"%s",DFL(kvget(&kv,"Device"),DEV));
        j->en=!en||en[0]=='t'||en[0]=='T';}
    closedir(d);
}

static void hub_save(hub_t *j) {
    char fn[P],buf[B];
    snprintf(fn,P,"%s/%s.txt",HD,j->n);
    snprintf(buf,B,"Name: %s\nSchedule: %s\nPrompt: %s\nDevice: %s\nEnabled: %s\n",
        j->n,j->s,j->p,j->d,j->en?"true":"false");
    writef(fn,buf);
}

#ifdef __ANDROID__
static unsigned hub_jid(const char*s){unsigned h=5381;for(;*s;s++)h=h*33+(unsigned)(unsigned char)*s;return(h%90000)+10000;}
static long hub_period(const char*s){
    if(!s||!*s||!strcmp(s,"daily"))return 86400000L;
    if(strchr(s,'/')){int m=atoi(strchr(s,'/')+1);return m>0?(long)m*60000L:1800000L;}
    return 86400000L;}
#endif

static void hub_timer(hub_t *j, int on) {
    char buf[B*2];
#ifdef __ANDROID__
    unsigned jid=hub_jid(j->n);
    char sd[P],sf[P];snprintf(sd,P,"%s/.local/share/a/jobs",HOME);mkdirp(sd);
    snprintf(sf,P,"%s/%s.sh",sd,j->n);
    if(on){
        snprintf(buf,B*2,"#!/data/data/com.termux/files/usr/bin/sh\nPATH=%s\n%s\n",getenv("PATH"),j->p);
        writef(sf,buf);chmod(sf,0700);
        snprintf(buf,B*2,"termux-job-scheduler --script '%s' --job-id %u --period-ms %ld --persisted true 2>/dev/null",sf,jid,hub_period(j->s));
    } else {
        snprintf(buf,B*2,"termux-job-scheduler --cancel --job-id %u 2>/dev/null;rm -f '%s'",jid,sf);
    }
#elif defined(__APPLE__)
    char la[P];snprintf(la,P,"%s/Library/LaunchAgents",HOME);mkdirp(la);
    char pl[P];snprintf(pl,P,"%s/a-%s.plist",la,j->n);
    if(on){
        char when[256];int h,m;
        if(strchr(j->s,'/')){int iv=atoi(strchr(j->s,'/')+1);if(iv<=0)iv=60;
            snprintf(when,256,"<key>StartInterval</key><integer>%d</integer>",iv*60);}
        else if(sscanf(j->s,"%d:%d",&h,&m)==2)
            snprintf(when,256,"<key>StartCalendarInterval</key><dict><key>Hour</key><integer>%d</integer><key>Minute</key><integer>%d</integer></dict>",h,m);
        else snprintf(when,256,"<key>StartInterval</key><integer>3600</integer>");
        snprintf(buf,B*2,"<?xml version=\"1.0\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict><key>Label</key><string>a-%s</string><key>ProgramArguments</key><array><string>/bin/bash</string><string>-lc</string><string>%s</string></array>%s<key>StandardOutPath</key><string>/tmp/a-%s.log</string><key>StandardErrorPath</key><string>/tmp/a-%s.log</string></dict></plist>",j->n,j->p,when,j->n,j->n);
        writef(pl,buf);
        snprintf(buf,B*2,"launchctl unload '%s' 2>/dev/null;launchctl load -w '%s' 2>&1",pl,pl);
    } else {
        snprintf(buf,B*2,"launchctl unload '%s' 2>/dev/null;rm -f '%s'",pl,pl);
    }
#else
    char sd[P]; snprintf(sd,P,"%s/.config/systemd/user",HOME); mkdirp(sd);
    if(on){
        snprintf(buf,B*2,"[Unit]\nDescription=a:%s\n[Service]\nType=oneshot\nEnvironment=PATH=%s\nExecStart=/bin/bash -lc '%s'\n",j->n,getenv("PATH"),j->p);
        char svc[P]; snprintf(svc,P,"%s/a-%s.service",sd,j->n); writef(svc,buf);
        snprintf(buf,B*2,"[Unit]\nDescription=a:%s\n[Timer]\nOnCalendar=%s\nPersistent=true\n[Install]\nWantedBy=timers.target\n",j->n,j->s);
        char tmr[P]; snprintf(tmr,P,"%s/a-%s.timer",sd,j->n); writef(tmr,buf);
        /* stop+stamp=now, else Persistent catch-up fires a rescheduled live timer instantly */
        snprintf(buf,B*2,"systemctl --user stop a-%s.timer 2>/dev/null;touch %s/.local/share/systemd/timers/stamp-a-%s.timer 2>/dev/null;systemctl --user daemon-reload;systemctl --user enable --now a-%s.timer >/dev/null 2>&1",j->n,HOME,j->n,j->n);
    } else {
        snprintf(buf,B*2,"systemctl --user disable --now a-%s.timer >/dev/null 2>&1;rm -f '%s/a-%s.timer' '%s/a-%s.service';systemctl --user daemon-reload",j->n,sd,j->n,sd,j->n);
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
#elif defined(__APPLE__)
    pcmd("launchctl list 2>/dev/null",HUB_TL,sizeof(HUB_TL));
#else
    pcmd("systemctl --user list-timers --all 2>/dev/null",HUB_TL,sizeof(HUB_TL));
#endif
}
static int hub_on(hub_t*j){char p[96];
#ifdef __ANDROID__
    snprintf(p,96,"Job %u:",hub_jid(j->n));
#elif defined(__APPLE__)
    snprintf(p,96,"a-%s",j->n);
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

/* MM-DD HH:MM of last trigger, local jobs only (systemctl is local) */
static void hub_last(hub_t*j,char*out,int sz){out[0]=0;
    if(strcmp(j->d,DEV))return;
#if !defined(__ANDROID__) && !defined(__APPLE__)
    char c[B],r[256];snprintf(c,B,"systemctl --user show a-%s.timer -p LastTriggerUSec --value 2>/dev/null",j->n);
    pcmd(c,r,256);r[strcspn(r,"\n")]=0;
    int yr,mo,da,hr,mn;char wk[16];
    if(sscanf(r,"%15s %d-%d-%d %d:%d",wk,&yr,&mo,&da,&hr,&mn)==6)
        snprintf(out,(size_t)sz,"%02d-%02d %02d:%02d",mo,da,hr,mn);
#endif
}

static int hub_list(int all,const char*q){
    hub_timers();int tw=80,sh=0;struct winsize ws;if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==0)tw=ws.ws_col;
    int m=tw<60,cw=tw-(m?32:48);
    printf(m?"# %-8s %-9s On Cmd\n":"# %-10s %-6s %-12s %-8s On Cmd\n","Name",m?"Last":"Sched","Last","Dev");
    for(int i=0;i<NJ;i++){hub_t*j=&HJ[i];
        if(!all&&!q&&!j->en)continue;
        if(q&&!strcasestr(j->n,q)&&!strcasestr(j->p,q)&&!strcasestr(j->d,q))continue;
        int on=hub_on(j);char cp[512],lr[32];hub_trunc(cp,512,j->p,cw);hub_last(j,lr,32);
        sh++;
        if(m)printf("%-2d%-9s%-10s%s %s\n",i,j->n,lr[0]?lr:"-",on?"✓":" ",cp);
        else printf("%-2d%-11s%-7s%-13s%-8s%s %s\n",i,j->n,j->s,lr[0]?lr:"-",j->d,on?"✓":" ",cp);}
    if(q)printf("\n%d/%d match '%s'\n",sh,NJ,q);
    else printf(NJ-sh?"\n%d jobs (+%d disabled, a hub all)\n":"\n%d jobs\n",sh,NJ-sh);
    printf("a hub <#>       run now\na hub on/off #  toggle\na hub add|rm    create/delete\na hub log [#]   journalctl\na hub sync      re-register this device\na hub all       show disabled\n");
    return 0;}

static int cmd_hub(int argc, char **argv) {
    init_db(); hub_load();
    const char *sub=argc>2?argv[2]:NULL;

    if(!sub||!strcmp(sub,"all"))return hub_list(sub&&!strcmp(sub,"all"),NULL);

    if(!strcmp(sub,"add")) {
        if(argc<6) { fprintf(stderr,"Usage: a hub add <name> <sched> <cmd...>\n"); return 1; }
        hub_t j={.en=1}; snprintf(j.n,64,"%s",argv[3]); snprintf(j.s,32,"%s",argv[4]);
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
            if(!j->en)return 0;
            char c[B];snprintf(c,B,"/bin/bash -lc '%s'",j->p);
            return system(c)?1:0;
        }
        if(!strcmp(sub,"on")||!strcmp(sub,"off")) {
            j->en=sub[1]=='n'; hub_save(j); hub_timer(j,j->en);
            printf("✓ %s %s\n",j->n,sub); return 0;
        }
        hub_timer(j,0);
        char f[P];snprintf(f,P,"%s/%s.txt",HD,j->n);unlink(f);
        printf("✓ rm %s\n",j->n); return 0;
    }

    if(!strcmp(sub,"sync")) {
#ifdef __ANDROID__
        (void)!system("termux-job-scheduler --cancel-all 2>/dev/null");
#elif defined(__APPLE__)
        {char c[B];snprintf(c,B,"for p in %s/Library/LaunchAgents/a-*.plist;do [ -f \"$p\" ]&&{ launchctl unload \"$p\" 2>/dev/null;rm -f \"$p\";};done",HOME);(void)!system(c);}
#else
        {char c[B];snprintf(c,B,
            "systemctl --user list-unit-files --type=timer --no-legend 2>/dev/null|awk '/^(a|aio)-/{print $1}'|xargs -r systemctl --user disable --now >/dev/null 2>&1;"
            "rm -f %s/.config/systemd/user/a-*.timer %s/.config/systemd/user/a-*.service "
            "%s/.config/systemd/user/aio-*.timer %s/.config/systemd/user/aio-*.service;"
            "systemctl --user daemon-reload",HOME,HOME,HOME,HOME);(void)!system(c);}
#endif
        int m=0; for(int i=0;i<NJ;i++) if(!strcmp(HJ[i].d,DEV)&&HJ[i].en) { hub_timer(&HJ[i],1); m++; }
        printf("✓ synced %d jobs\n",m); return 0;
    }

    if(!strcmp(sub,"log")) {
        char c[B];
        if(argc>3){hub_t*j=hub_find(argv[3]);if(!j){fprintf(stderr,"x %s?\n",argv[3]);return 1;}
            snprintf(c,B,"journalctl --user -u a-%s.service -n 60 --no-pager",j->n);}
        else snprintf(c,B,"journalctl --user -n 100 --no-pager");
        return system(c);
    }

    return hub_list(1,sub); /* unknown sub = search */
}
