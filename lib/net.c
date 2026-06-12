static void bg_backup_jsonl(void) {
    char c[B]; snprintf(c, B, "nohup sh -c 'D=%s/backup/%s;mkdir -p $D&&"
        "find ~/.claude/projects -name \"*.jsonl\" 2>/dev/null|while read f;do cp -u \"$f\" $D/ 2>/dev/null;done;"  /* -u: growing sessions must re-stage; -n froze first-sync prefix */
        "r=$(rclone listremotes 2>/dev/null|grep \"^a-gdrive\"|head -1|tr -d \":\");"
        "[ -n \"$r\" ]&&rclone copy $D \"$r:adata/backup/%s/\" --include \"*.jsonl\" --include \"*.log\" -q"
        "' </dev/null >/dev/null 2>&1 &", AROOT, DEV, DEV);
    (void)!system(c);
}

static int cmd_email(int argc, char **argv) { AB;
    char bp[P]; snprintf(bp,P,"%s/scan/base.py",SROOT);
    char **na=malloc(((unsigned)argc+2)*sizeof(char*));
    na[0]="python3"; na[1]=bp;
    for(int i=2;i<argc;i++) na[i]=argv[i];
    na[argc]=NULL;
    execvp("python3",na); perror("python3"); return 1;
}

static int cmd_log(int argc, char **argv) {
    const char *sub = argc > 2 ? argv[2] : NULL;
    if (sub && !strcmp(sub, "backup")) { perf_disarm();
        char c[B], cnt[64], bdir[P]; snprintf(bdir, P, "%s/backup", AROOT);
        char jdir[P]; snprintf(jdir, P, "%s/git/jobs", AROOT);
        snprintf(c, B, "ls '%s'/*.log 2>/dev/null | wc -l", jdir);
        pcmd(c, cnt, 64); printf("Tmux logs: %d (git-synced in adata/git/jobs/)\n\n", atoi(cnt));
        snprintf(c, B, "ls -d '%s'/*/ 2>/dev/null", bdir);
        char dirs[B]; pcmd(c, dirs, B); char *dp = dirs;
        printf("%-16s %6s %6s  %s\n", "DEVICE", "LOCAL", "JSONL", "STATUS");
        while (*dp) {
            char *nl = strchr(dp, '\n'); if (nl) *nl = 0;
            char dp2[P]; snprintf(dp2, P, "%s", dp); { int l=(int)strlen(dp2); if(l>1&&dp2[l-1]=='/')dp2[l-1]=0; }
            char dn[128]; snprintf(dn, 128, "%s", bname(dp2));
            if (!dn[0]||!strcmp(dn,".")||!strcmp(dn,"..")) { if(nl)dp=nl+1;else break;continue; }
            snprintf(c, B, "ls '%s/%s' 2>/dev/null | wc -l", bdir, dn);
            pcmd(c, cnt, 64); int loc = atoi(cnt);
            if (!strcmp(dn, DEV)) { printf("%-16s %6d %6s  local (this device)\n", dn, loc, "-"); }
            else {
                snprintf(c, B, "'%s/a' ssh '%s' 'ls ~/a/adata/backup/%s/*.jsonl 2>/dev/null | wc -l' 2>/dev/null", DDIR, dn, dn);
                pcmd(c, cnt, 64); int rn = atoi(cnt);
                printf("%-16s %6d %6d  %s\n", dn, loc, rn, rn ? "remote ✓" : "remote (no JSONL)");
            }
            if (nl) dp = nl + 1; else break;
        }
        return 0;
    }

    char adir[P]; snprintf(adir, P, "%s/git/activity", AROOT);

    if (sub && !strcmp(sub, "all")) { perf_disarm();
        char c[B]; snprintf(c, B, "cat '%s'/*.txt 2>/dev/null", adir);
        (void)!system(c); return 0; }

    /* Default: recent activity — opendir+awk, 1 fork vs 5 */
    char c[B];
    printf("%-5s %-8s %-12s %-40s %s\n","DATE","TIME","DEVICE","CMD","DIR");fflush(stdout);
    { DIR*d=opendir(adir);struct dirent*e;char*fn[512];int nf=0;
    if(d){while((e=readdir(d))&&nf<512)if(strstr(e->d_name,".txt"))fn[nf++]=strdup(e->d_name);closedir(d);}
    for(int i=1;i<nf;i++){char*t=fn[i];int j=i;while(j&&strcmp(fn[j-1],t)>0){fn[j]=fn[j-1];j--;}fn[j]=t;}
    int o=snprintf(c,B,"awk '/^[0-9][0-9]\\//{split($2,t,\":\");h=int(t[1]);m=t[2];ap=\"AM\";"
        "if(h>=12){ap=\"PM\";if(h>12)h-=12}if(h==0)h=12;"
        "c=\"\";for(i=4;i<NF;i++){if(i>4)c=c\" \";c=c$i}"
        "if(length(c)>40)c=substr(c,1,18)\"...\"substr(c,length(c)-14);"
        "n=split($NF,p,\"/\");d=p[n];printf \"%%5s %%2d:%%s%%s  %%-12s %%-40s %%s\\n\",$1,h,m,ap,$3,c,d}'");
    for(int i=nf>30?nf-30:0;i<nf;i++)o+=snprintf(c+o,(size_t)(B-o)," '%s/%s'",adir,fn[i]);
    (void)!system(c);for(int i=0;i<nf;i++)free(fn[i]);}

    /* Status footer — pure C, no shell-outs */
    #define AGO(buf,sz,sec) do { int _s=(int)(sec); if(_s<60)snprintf(buf,sz,"%ds ago",_s); \
        else if(_s<3600)snprintf(buf,sz,"%dm ago",_s/60); \
        else if(_s<86400)snprintf(buf,sz,"%dh ago",_s/3600); \
        else snprintf(buf,sz,"%dd ago",_s/86400); } while(0)
    /* count .ext files in dir, track newest mtime */
    #define DCOUNT(dir,ext,cnt,newest) do { DIR*_d=opendir(dir); struct dirent*_e; struct stat _s; char _p[P]; \
        cnt=0; newest=0; if(_d){while((_e=readdir(_d))){int _l=(int)strlen(_e->d_name); int _el=(int)strlen(ext); \
        if(_l>_el&&!strcmp(_e->d_name+_l-_el,ext)){cnt++;snprintf(_p,P,"%s/%s",dir,_e->d_name); \
        if(!stat(_p,&_s)&&_s.st_mtime>newest)newest=_s.st_mtime;}}closedir(_d);} } while(0)
    mkdirp(LOGDIR);
    int nlogs; time_t llm_new; DCOUNT(LOGDIR,".log",nlogs,llm_new);
    /* count backup subdirs + newest .jsonl across them */
    int nbak=0; time_t bak_new=0; { char bd[P]; snprintf(bd,P,"%s/backup",AROOT);
        DIR*d=opendir(bd); struct dirent*e; if(d){while((e=readdir(d))){if(e->d_name[0]=='.')continue;
        char sd[P]; snprintf(sd,P,"%s/%s",bd,e->d_name); struct stat ss; if(!stat(sd,&ss)&&S_ISDIR(ss.st_mode)){
        nbak++; int _n; time_t _t; DCOUNT(sd,".jsonl",_n,_t); (void)_n; if(_t>bak_new)bak_new=_t;}}closedir(d);} }
    time_t now=time(NULL); char ago[32];
    /* gdrive folder ID: try local, then git-synced from any device */
    char gdid[64]=""; { char gp[P];
        snprintf(gp,P,"%s/backup/%s/.gdrive_id",AROOT,DEV);
        FILE *gf=fopen(gp,"r");
        if(!gf){snprintf(gp,P,"%s/git/gdrive.id",AROOT);gf=fopen(gp,"r");}
        if(gf){if(fgets(gdid,64,gf))gdid[strcspn(gdid,"\n")]=0;fclose(gf);} }
    char bpath[P]; snprintf(bpath,P,"adata/backup/%s/",DEV);
    char gdu[256]=""; if(gdid[0])snprintf(gdu,256,"https://drive.google.com/drive/folders/%s",gdid);
    #define ROW(t,n,l,p,u) { int a=t?(int)(now-t):-1; if(a>=0)AGO(ago,32,a);else snprintf(ago,32,"never"); \
        printf("%s %-18s %3d  %-28s last: %s\n",n?"✓":"x",l,n,p,ago); if(u)printf("  → %s\n",u); }
    putchar('\n');
    ROW(llm_new,nlogs,"LLM transcripts",bpath,gdu[0]?gdu:NULL)
    ROW(bak_new,nbak,"JSONL backup",bpath,gdu[0]?gdu:NULL)
    #undef ROW
    #undef DCOUNT
    #undef AGO
    return 0;
}

static int cmd_login(int argc, char **argv) {
    char ld[P];snprintf(ld,P,"%s/login",SROOT);mkdirp(ld);
    const char*sub=argc>2?argv[2]:NULL;
    char cc[P],hf[P];snprintf(cc,P,"%s/claude.json",ld);snprintf(hf,P,"%s/.claude/.credentials.json",HOME);
    if(sub&&!strcmp(sub,"save")){char tk[256];pcmd("gh auth token 2>/dev/null",tk,256);tk[strcspn(tk,"\n")]=0;
        if(*tk){char fp[P],buf[B];snprintf(fp,P,"%s/gh_%s.txt",ld,DEV);
            snprintf(buf,B,"Token: %s\nDevice: %s\n",tk,DEV);writef(fp,buf);puts("+ gh");}
        if(fexists(hf)){char cp[B];snprintf(cp,B,"cp '%s' '%s'",hf,cc);(void)!system(cp);puts("+ claude");}
        return 0;}
    if(sub&&!strcmp(sub,"apply")){char fp[P*2],tk[256]="";
        DIR*d=opendir(ld);struct dirent*e;if(d){while((e=readdir(d))){if(!strncmp(e->d_name,"gh_",3)){
            snprintf(fp,P,"%s/%s",ld,e->d_name);kvs_t kv=kvfile(fp);const char*t=kvget(&kv,"Token");
            if(t&&*t){snprintf(tk,256,"%s",t);break;}}}closedir(d);}
        if(*tk){snprintf(fp,P*2,"echo '%s'|gh auth login --with-token 2>&1",tk);(void)!system(fp);
            (void)!system("gh auth setup-git 2>/dev/null");puts("+ gh");}
        if(fexists(cc)){char cmd[B];snprintf(cmd,B,"mkdir -p '%s/.claude'&&cp '%s' '%s'&&chmod 600 '%s'",HOME,cc,hf,hf);(void)!system(cmd);puts("+ claude");}
        return 0;}
    if(sub&&!strcmp(sub,"show")){char ak[P],c[B];snprintf(ak,P,"%s/api_keys.env",ld);
        if(fexists(ak)){printf("\n\033[1;36m━━━ %s ━━━\033[0m\n",ak);snprintf(c,B,"cat '%s'",ak);(void)!system(c);}
        if(fexists(cc)){printf("\n\033[1;36m━━━ %s ━━━\033[0m\n",cc);snprintf(c,B,"jq . '%s' 2>/dev/null||cat '%s';echo",cc,cc);(void)!system(c);}
        return 0;}
    if(sub&&!strcmp(sub,"slot")){char c[B*2];snprintf(c,B*2,
        "H='%s';cd %s;A=$(cat active 2>/dev/null);N='%s';"
        "[ \"$N\" ]||{ for f in claude_*.json;do [ -e \"$f\" ]||continue;n=${f#claude_};n=${n%%.json};[ \"$n\" = \"$A\" ]&&p='*'||p=' ';printf '%%s %%-10s %%s\\n' \"$p\" \"$n\" \"$(cat claude_$n.email 2>/dev/null)\";done;exit;};"
        "F=claude_$N.json;if [ -f \"$F\" ];then cp \"$F\" \"$H\";chmod 600 \"$H\";echo $N>active;echo active: $N \"($(cat claude_$N.email 2>/dev/null))\";exit;fi;"
        "[ -f \"$H\" ]||{ echo x no creds;exit 1;};cp \"$H\" \"$F\";echo $N>active;"
        "T=$(sed -n 's/.*\"accessToken\":\"\\([^\"]*\\)\".*/\\1/p' \"$H\");E=$(curl -sSm 5 -H \"Authorization: Bearer $T\" https://api.anthropic.com/api/oauth/profile 2>/dev/null|sed -n 's/.*\"email\":\"\\([^\"]*\\)\".*/\\1/p');"
        "[ \"$E\" ]&&echo $E>claude_$N.email;echo saved: $N \"${E:+($E)}\"",hf,ld,argc<4?"":argv[3]);
        return system(c)?1:0;}
    puts("a login save|apply|show|slot [name]");return 0;}

static int cmd_sync(int argc, char **argv) { AB;
    printf("%s\n", SROOT);
    ensure_adata();
    sync_repo();
    char c[B], out[256];
    snprintf(c, B, "git -C '%s' remote get-url origin 2>/dev/null", SROOT);
    pcmd(c, out, 256); out[strcspn(out,"\n")] = 0;
    char t[256]; snprintf(c, B, "git -C '%s' log -1 --format='%%cd %%s' --date=format:'%%Y-%%m-%%d %%I:%%M:%%S %%p' 2>/dev/null", SROOT);
    pcmd(c, t, 256); t[strcspn(t,"\n")] = 0;
    const char *status = "synced";
    if (!out[0]) status = "no remote (run: gh auth login, then: a sync)";
    else if (!t[0]) status = "empty (no commits yet)";
    printf("  %s\n  Last: %s\n  Status: %s\n", out[0] ? out : "(no remote)", t[0] ? t : "(none)", status);
    /* Count files per folder */
    const char *folders[] = {"common","ssh","login","scan","notes","workspace","adocs","tasks","cal"};
    for (int i = 0; i < 9; i++) {
        char d[P]; snprintf(d, P, "%s/%s", SROOT, folders[i]);
        if (!dexists(d)) continue;
        char cnt_cmd[P]; snprintf(cnt_cmd, P, "find '%s' -name '*.txt' -maxdepth 2 2>/dev/null | wc -l", d);
        char cnt[16]; pcmd(cnt_cmd, cnt, 16); cnt[strcspn(cnt,"\n")] = 0;
        printf("  %s: %s files\n", folders[i], cnt);
    }
    bg_backup_jsonl();
    /* rclone sync context/ + books/ output */
    {char rc[64];pcmd("rclone listremotes 2>/dev/null|grep a-gdrive|head -1|tr -d ':'",rc,64);rc[strcspn(rc,"\n")]=0;
    if(rc[0]){char cd[P],bd[P];snprintf(cd,P,"%s/context",AROOT);snprintf(bd,P,"%s/books",AROOT);mkdirp(cd);mkdirp(bd);
        snprintf(c,B,"rclone copy '%s' '%s:adata/context/' -q -L 2>/dev/null;rclone copy '%s:adata/context/' '%s' -q 2>/dev/null",cd,rc,rc,cd);(void)!system(c);
        snprintf(c,B,"rclone copy '%s' '%s:adata/books/' --include '*/output/*.txt' -q -L 2>/dev/null;rclone copy '%s:adata/books/' '%s' --include '*/output/*.txt' -q 2>/dev/null",bd,rc,rc,bd);(void)!system(c);
        puts("✓ context + books");}}
    if (argc > 2 && !strcmp(argv[2], "all")) {
        puts("\n--- Broadcasting to SSH hosts ---");
        char bc[B]; snprintf(bc, B, "%s/lib/a.py", SDIR);
        char cmd[B]; snprintf(cmd, B, "python3 '%s' ssh all 'a sync'", bc); (void)!system(cmd);
    }
    return 0;
}

static int cmd_update(int argc, char **argv) { AB;
    const char *sub = argc > 2 ? argv[2] : NULL;
    if (sub && (!strcmp(sub,"help")||!strcmp(sub,"-h"))) {
        puts("a update - Update a from git + refresh caches\n  a update        Pull latest\n  a update shell  Refresh shell config\n  a update cache  Refresh caches");
        return 0;
    }
    if (sub && (!strcmp(sub,"bash")||!strcmp(sub,"zsh")||!strcmp(sub,"shell")||!strcmp(sub,"cache"))) {
        init_db(); load_cfg(); list_all(1, 1);
        gen_icache(); tm_ensure_conf();
        puts("✓ Cache"); return 0;
    }
    {char g[P];snprintf(g,P,"%s/.git",SDIR);if(access(g,F_OK)!=0){puts("x Not in git repo");init_db();load_cfg();list_all(1,1);gen_icache();return 0;}}
    char c[B],oh[64]={0},nh[64]={0};
    snprintf(c,B,"git -C '%s' rev-parse HEAD 2>/dev/null",SDIR);pcmd(c,oh,64);oh[strcspn(oh,"\n")]=0;
    snprintf(c,B,"git -C '%s' pull --ff-only 2>/dev/null",SDIR);
    if(system(c)!=0){printf("Diverged — destroy local changes & hard-reset to origin? [y/N] ");fflush(stdout);if(getchar()!='y'){puts("Aborted");return 1;}snprintf(c,B,"cd '%s'&&git fetch -q&&git reset --hard @{u}&&git clean -fd",SDIR);(void)!system(c);}
    snprintf(c,B,"git -C '%s' rev-parse HEAD 2>/dev/null",SDIR);pcmd(c,nh,64);nh[strcspn(nh,"\n")]=0;
    int ch=strcmp(oh,nh)!=0;
    /* no-op: up to date + binary exists */
    {char b[P];snprintf(b,P,"%s/a",DDIR);if(!ch&&!access(b,X_OK)){puts("✓ Up to date");return 0;}}
    /* detect dep change: a.c modified → pip/shell/node */
    int dc=0;
    if(ch&&oh[0]){snprintf(c,B,"git -C '%s' diff --name-only '%s' HEAD 2>/dev/null",SDIR,oh);char df[B];pcmd(c,df,B);dc=!!strstr(df,"a.c");}
    init_db();load_cfg();
    puts("✓ Updated (bg)");
    /* background: build, deps, cache, sync, rclone, backup */
    {pid_t p=fork();if(p==0){setsid();int n=open("/dev/null",O_WRONLY);dup2(n,1);dup2(n,2);close(n);
        snprintf(c,B,"sh '%s/a.c'",SDIR);if(!system(c))init_migrate();
        if(dc){char vp[P];snprintf(vp,P,"%s/venv/bin/pip",AROOT);
            if(!access(vp,X_OK)){snprintf(c,B,"'%s' install -q pexpect prompt_toolkit aiohttp 2>/dev/null",vp);(void)!system(c);}
            snprintf(c,B,"bash '%s/a.c' shell 2>&-;bash '%s/a.c' node 2>&-",SDIR,SDIR);(void)!system(c);
            if(!access("/data/data/com.termux",F_OK)){char td[P];snprintf(td,P,"%s/.tmp",HOME);mkdirp(td);
                snprintf(c,B,"tmux set-environment -g CLAUDE_CODE_TMPDIR '%s' 2>/dev/null",td);(void)!system(c);}}
        snprintf(c,B,"'%s/a' update cache",DDIR);(void)!system(c);
        ensure_adata();sync_repo();
        {char ld[P];snprintf(ld,P,"%s/git/login",AROOT);mkdirp(ld);
         char t[64];pcmd("rclone listremotes 2>/dev/null|grep a-gdrive|head -1",t,64);
         if(t[0]&&t[0]!='\n'){struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);struct tm*tm=localtime(&ts.tv_sec);char tf[32];strftime(tf,32,"%Y%m%dT%H%M%S",tm);
          snprintf(c,B,"cp ~/.config/rclone/rclone.conf '%s/rclone_%s.%09ld.conf'",ld,tf,ts.tv_nsec);(void)!system(c);
         }else{char ps[16][P];int np=listdir(ld,ps,16);char*lp=NULL;
          for(int i=np-1;i>=0;i--)if(strstr(ps[i],"rclone_")&&strstr(ps[i],".conf")){lp=ps[i];break;}
          if(lp){snprintf(c,B,"mkdir -p ~/.config/rclone&&cp '%s' ~/.config/rclone/rclone.conf",lp);(void)!system(c);}}}
        bg_backup_jsonl();_exit(0);}}
    hub_load();if(!hub_find("sync")){hub_t j={.en=1,.n="sync",.s="6:00",.p="a sync"};strcpy(j.d,DEV);hub_save(&j);hub_timer(&j,1);}
    if(sub&&!strcmp(sub,"all")){puts("\n--- Broadcasting to SSH hosts ---");snprintf(c,B,"'%s/a' ssh all 'a update'",DDIR);(void)!system(c);}
    return 0;
}
