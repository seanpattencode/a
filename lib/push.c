#define PUSHCMD "{ git push -u origin HEAD 2>&1||{ git pull --rebase --autostash origin HEAD 2>&1&&git push -u origin HEAD 2>&1||{ git rebase --abort 2>/dev/null;echo PUSH_CONFLICT;};}; }"
static int cmd_push(int argc, char **argv) { AB;
    char cwd[P]; if(!getcwd(cwd,P)) snprintf(cwd,P,".");
    if(argc>2&&!strcmp(argv[2],"-f")){
        char cp[P];commit_path(cp);char*cs=readf(cp,NULL),*nl=cs?strchr(cs,'\n'):0;
        if(!nl){puts("x no .commit (after a done)");free(cs);return 1;}
        *nl=0;char*f=nl+1;f[strcspn(f,"\n")]=0;char c[B*2],vo[B];
        snprintf(c,B*2,"cd '%s'&&git add -- %s&&git commit -m \"%s\" -- %s&&" PUSHCMD,cwd,f,cs,f);pcmd(c,vo,B);
        if(strstr(vo,"PUSH_CONFLICT")){printf("✗ %s: rebase conflict with origin — aborted, tree restored (commit kept local).\n  Same lines changed by another agent. Merge by hand: git pull --rebase, resolve, a push -f\n",cs);free(cs);return 1;}
        snprintf(c,B*2,"cd '%s'&&git fetch origin -q 2>/dev/null;git branch -r --contains HEAD 2>/dev/null|grep -q origin&&{ u=$(git config remote.origin.url);u=${u#https://github.com/};u=${u#git@github.com:};u=${u%%.git};echo https://github.com/$u/commit/$(git rev-parse --short HEAD);}",cwd);
        pcmd(c,vo,B);vo[strcspn(vo,"\n")]=0;
        if(vo[0]){unlink(cp);printf("✓ %s\n  github: %s\n",cs,vo);}else printf("✗ %s NOT on origin; re-push\n",cs);
        free(cs);return vo[0]?0:1;
    }
    char msg[B]="";
    if(argc>2)ajoin(msg,B,argc,argv,2);
    else snprintf(msg, B, "Update %s", bname(cwd));

    if (!git_in_repo(cwd)) {
        /* Fork without .git: init, commit, push branch to upstream */
        if(in_fork(cwd)){char c[B],br[128],rf[P];
            snprintf(rf,P,"%s/.fork_remote",cwd);char*remote=readf(rf,NULL);
            if(!remote||!*remote){free(remote);puts("x No .fork_remote");return 1;}
            remote[strcspn(remote,"\n")]=0;
            snprintf(br,128,"fork-%s",bname(cwd));
            snprintf(c,B,"cd '%s'&&git init -q&&git remote add origin '%s'&&git fetch -q --depth=1 origin main&&git update-ref refs/heads/'%s' origin/main&&git symbolic-ref HEAD refs/heads/'%s'&&git add -A&&git commit -qm '%s'&&git push -u origin '%s' 2>&1",cwd,remote,br,br,msg,br);
            char out[B];pcmd(c,out,B);
            if(strstr(out,"->")||strstr(out,"new branch"))printf("✓ pushed branch %s\n",br);
            else printf("x %s\n",out);
            return 0;}
        /* Check for sub-repos */
        DIR *d = opendir(cwd); struct dirent *e; int nsub = 0;
        char subs[32][256];
        if (d) { while ((e = readdir(d)) && nsub < 32) { if(e->d_name[0]=='.')continue; char gp[P]; snprintf(gp,P,"%s/%s/.git",cwd,e->d_name); if (dexists(gp)) snprintf(subs[nsub++],256,"%s",e->d_name); } closedir(d); }
        if (nsub) {
            printf("Push %d repos? ", nsub);
            for (int i = 0; i < nsub; i++) printf("%s%s", subs[i], i<nsub-1?", ":"");
            printf(" [y/n]: "); char buf[8]; if (!fgets(buf,8,stdin) || buf[0]!='y') return 0;
            for (int i = 0; i < nsub; i++) {
                char c[B]; snprintf(c, B, "cd '%s/%s' && git add -A && git commit -m '%s' --allow-empty 2>/dev/null && git push 2>/dev/null", cwd, subs[i], msg);
                int r = system(c); printf("%s %s\n", r==0?"✓":"x", subs[i]);
            }
            return 0;
        }
        printf("Not a git repo. Set up as private GitHub repo? [y/n]: ");
        char buf2[8]; if (fgets(buf2,8,stdin) && buf2[0]=='y') return cmd_setup(argc, argv);
        return 0;
    }
    char dirty[64]="";pcmd("git status --porcelain 2>/dev/null",dirty,64);
    const char*tag=dirty[0]?"✓":"○";
    char ok[P],ef[P],c[B*2];struct stat st;
    snprintf(ok,P,"%s/logs/push.ok",DDIR);snprintf(ef,P,"%s/logs/push.err",DDIR);
    {char*e=readf(ef,NULL);if(e){unlink(ef);unlink(ok);printf("✗ %s\n",e);free(e);}}
    if (stat(ok, &st) == 0 && time(NULL) - st.st_mtime < 600) {
        snprintf(c,sizeof(c),"cd '%s'&&git add -A&&git commit -m \"%s\" --allow-empty 2>/dev/null&&"
            "{ " PUSHCMD "&&touch '%s'; }||echo fail>'%s'",cwd,msg,ok,ef);
        if(!fork()){setsid();int n=open("/dev/null",O_RDWR);if(n>=0){dup2(n,0);dup2(n,1);dup2(n,2);close(n);}
            execl("/bin/sh","sh","-c",c,(char*)NULL);_exit(1);}
        printf("%s %s (bg push)\n",tag,msg);return 0;
    }
    snprintf(c,B,"git -C '%s' config remote.origin.url 2>/dev/null",cwd);
    if(system(c)){snprintf(c,B,"cd '%s'&&gh repo create --private --source . --push",cwd);(void)!system(c);}
    snprintf(c,sizeof(c),"cd '%s'&&git add -A&&git commit -m '%s' --allow-empty 2>/dev/null&&" PUSHCMD,cwd,msg);
    char out[B];pcmd(c,out,B);
    #undef PUSHCMD
    if(!strstr(out,"->")&&!strstr(out,"up-to-date")&&!strstr(out,"Everything")){
        printf("✗ push failed\n%s\n",out);
        if(strstr(out,"PUSH_CONFLICT"))printf("rebase conflict — aborted, tree restored. Merge by hand: git pull --rebase, resolve, a push\n");
        else if(strstr(out,"rejected"))printf("fix: git pull --rebase origin main, then a push\n");
        return 1;}
    mkdirp(DDIR);snprintf(c,B,"%s/logs",DDIR);mkdirp(c);
    {int fd=open(ok,O_CREAT|O_WRONLY|O_TRUNC,0644);if(fd>=0)close(fd);}
    printf("%s %s%s\n",tag,msg,strstr(out,"rebase")?" (rebased)":"");
    /* verify push: check our diff actually landed on origin */
    {snprintf(c,B,"cd '%s'&&git fetch origin -q 2>/dev/null&&git diff HEAD origin/HEAD --name-only 2>/dev/null",cwd);
    char vf[B];pcmd(c,vf,B);if(vf[0]){printf("✗ WARN: push succeeded but origin differs:\n%s  Another agent may have overwritten. Re-run: a push\n",vf);
    system("a done 'push verify FAILED — changes lost on origin, re-push needed'");}}
    return 0;
}

static void sq(const char *s, char *o, int sz) { int j=0; o[j++]='\'';
    for(;*s&&j<sz-4;s++){if(*s=='\''){o[j++]='\'';o[j++]='\\';o[j++]='\'';o[j++]='\'';}else o[j++]=*s;} o[j++]='\'';o[j]=0; }
static int cmd_pr(int argc, char **argv) {
    char cwd[P]; if(!getcwd(cwd,P)) snprintf(cwd,P,".");
    if (!git_in_repo(cwd)) { puts("x Not a git repo"); return 1; }
    char br[128]; pcmd("git rev-parse --abbrev-ref HEAD 2>/dev/null",br,128); br[strcspn(br,"\n")]=0;
    if (!strcmp(br,"main")||!strcmp(br,"master")) { puts("x On main — create a branch first"); return 1; }
    char title[256]=""; if(argc>2)ajoin(title,256,argc,argv,2);
    else snprintf(title,256,"%s",br);
    char qt[512],qb[512]; sq(title,qt,512); sq(br,qb,512);
    /* commit + push if needed */
    char dirty[64]=""; pcmd("git status --porcelain 2>/dev/null",dirty,64);
    if(dirty[0]){char c[B];snprintf(c,B,"git add -A && git commit -m %s",qt);(void)!system(c);}
    char c[B]; snprintf(c,B,"git push -u origin %s 2>&1",br);
    char out[B]; pcmd(c,out,B);
    if(!strstr(out,"->")&&!strstr(out,"up-to-date")&&!strstr(out,"Everything")){printf("x Push: %s\n",out);return 1;}
    snprintf(c,B,"gh pr create --title %s --body '' --head %s 2>&1",qt,qb); pcmd(c,out,B);
    out[strcspn(out,"\n")]=0;
    if(strstr(out,"github.com")&&strstr(out,"/pull/")) printf("+ %s\n",out);
    else if(strstr(out,"already exists")) printf("+ PR exists for %s\n",br);
    else printf("x %s\n",out);
    return 0;
}

static int cmd_pull(int argc, char **argv) { AB;
    char cwd[P]; if(!getcwd(cwd,P)) strcpy(cwd,".");
    if(!git_in_repo(cwd)){ puts("x Not a git repo"); return 1; }
    char c[B*2],o[B];
    snprintf(c,B*2,"cd '%s'&&git config remote.origin.fetch '+refs/heads/*:refs/remotes/origin/*';git fetch origin -q;git log -1 --format='%%h %%s' origin/main 2>/dev/null||git log -1 --format='%%h %%s' origin/master",cwd);
    pcmd(c,o,B); o[strcspn(o,"\n")]=0;
    if(!o[0]){ puts("x no origin ref"); return 1; }
    printf("! DELETE local -> %s\n",o);
    if(argc<3||(strcmp(argv[2],"--yes")&&strcmp(argv[2],"-y"))){
        printf("Continue? y/n: "); char b[8]; if(!fgets(b,8,stdin)||b[0]!='y'){ puts("x"); return 1; } }
    snprintf(c,B*2,"cd '%s'&&git reset --hard origin/main 2>/dev/null||git reset --hard origin/master;git clean -f -d",cwd); (void)!system(c);
    printf("✓ %s\n",o); return 0;
}

static int cmd_diff(int argc, char **argv) { AB;
    const char *sel = (argc>2&&strcmp(argv[2],"--"))?argv[2]:NULL;
    char ps[B]="";for(int i=2;i<argc;i++)if(!strcmp(argv[i],"--")){int pl=snprintf(ps,B," --");for(int j=i+1;j<argc;j++)pl+=snprintf(ps+pl,(size_t)(B-pl)," '%s'",argv[j]);break;}
    int filt=ps[0]!=0;
    /* Token history mode */
    if (sel && sel[0] >= '0' && sel[0] <= '9') {
        int n = atoi(sel); char c[256]; snprintf(c, 256, "git log -%d --pretty=%%H\\ %%cd\\ %%s --date=format:%%I:%%M%%p", n);
        FILE *fp = popen(c, "r"); if (!fp) return 1;
        char line[512]; int total = 0, i = 0;
        while (fgets(line, 512, fp)) {
            line[strcspn(line,"\n")] = 0;
            char *sp = strchr(line, ' '); if (!sp) continue;
            *sp = 0; char *hash = line, *ts = sp + 1;
            sp = strchr(ts, ' '); if (!sp) continue;
            *sp = 0; char *msg = sp + 1;
            char dc[256]; snprintf(dc, 256, "git show %.40s --pretty=", hash);
            FILE *dp = popen(dc, "r"); int ab = 0, db_ = 0;
            if (dp) { char dl[4096]; while (fgets(dl, 4096, dp)) { int l = (int)strlen(dl);
                if (dl[0]=='+' && dl[1]!='+') ab += l-1;
                else if (dl[0]=='-' && dl[1]!='-') db_ += l-1;
            } pclose(dp); }
            int tok = (ab - db_) / 4; total += tok;
            if (strlen(msg) > 55) { msg[52]='.'; msg[53]='.'; msg[54]='.'; msg[55]=0; }
            printf("  %d  %s  %+6d  %s\n", i++, ts, tok, msg);
        }
        pclose(fp); printf("\nTotal: %+d tokens\n", total); return 0;
    }
    /* Full diff — colored + stats */
    char cwd[P]; if(!getcwd(cwd,P)) snprintf(cwd,P,".");
    struct{char name[256];int al,dl,ab,db,sb;}fs[256]; int nf=0,cf=-1; long ws=0;
    #define FS(fn) do{cf=-1;for(int _i=0;_i<nf;_i++)if(!strcmp(fs[_i].name,fn)){cf=_i;break;} \
        if(cf<0&&nf<256){cf=nf;memset(&fs[nf],0,sizeof(fs[0]));snprintf(fs[nf].name,256,"%s",fn);nf++;}}while(0)
    #define DS(cmd) do{FILE*_f=popen(cmd,"r");if(_f){char _l[4096],_oh[16]={0};while(fgets(_l,4096,_f)){_l[strcspn(_l,"\n")]=0; \
        if(!strncmp(_l,"diff --git",10)){char*_b=strstr(_l," b/");if(_b)FS(_b+3);_oh[0]=0;} \
        else if(!strncmp(_l,"diff -r",7)){char*_b=strrchr(_l,' ');if(_b&&_b[1])FS(_b+1);} \
        else if(!strncmp(_l,"index ",6)){snprintf(_oh,16,"%.*s",(int)strcspn(_l+6,"."),_l+6);} \
        else if(!strncmp(_l,"Binary files ",13)&&cf>=0&&_oh[0]){char _c[64],_o[32];snprintf(_c,64,"git cat-file -s %s 2>/dev/null",_oh);pcmd(_c,_o,32); \
            int _os=atoi(_o),_dl=strstr(_l,"and /dev/null differ")!=NULL;struct stat _st; \
            int _ns=_dl?0:(!stat(fs[cf].name,&_st)?(int)_st.st_size:0);int _d=_ns-_os; \
            if(_d>0)fs[cf].ab+=_d;else fs[cf].db+=-_d;printf("  %s: binary %+d bytes\n",fs[cf].name,_d);} \
        else if(!strncmp(_l,"+++ ",4)&&cf<0){char*_b=_l+4;while(*_b=='.'||*_b=='/')_b++;char*_t=_b;while(*_t&&*_t!='\t')_t++;*_t=0;if(*_b)FS(_b);} \
        else if(_l[0]=='@'&&_l[1]=='@'){char*_p=strchr(_l,'+');int _n=_p?(int)strtol(_p+1,NULL,10):0; \
            if(cf>=0&&_n)printf("\n%s line %d:\n",fs[cf].name,_n);} \
        else if(_l[0]=='+'&&_l[1]!='+'){printf("  \033[48;2;26;84;42m+ %s\033[0m\n",_l+1);if(cf>=0){fs[cf].al++;fs[cf].ab+=(int)strlen(_l)-1;}} \
        else if(_l[0]=='-'&&_l[1]!='-'){printf("  \033[48;2;117;34;27m- %s\033[0m\n",_l+1);if(cf>=0){fs[cf].dl++;fs[cf].db+=(int)strlen(_l)-1;}}} \
        pclose(_f);}}while(0)
    #define HR for(int _j=0;_j<60;_j++){putchar(0xe2);putchar(0x94);putchar(0x80);}putchar('\n')
    int fk=in_fork(cwd)&&!git_in_repo(cwd);
    if(fk){
        printf("%s\nfork → %s\n",cwd,SDIR);
        {char c[B];snprintf(c,B,"for f in *;do [ \"$f\" = adata ]&&continue;diff -ru '%s/'\"$f\" \"$f\" 2>/dev/null;done|grep -v '^Only in'",SDIR);DS(c);}
        {char o[32];pcmd("du -sk .|awk '{print $1*1024}'",o,32);ws=atol(o);}
    } else {
        char br[128]; pcmd("git rev-parse --abbrev-ref HEAD 2>/dev/null",br,128); br[strcspn(br,"\n")]=0;
        int wt=!sel&&(!strncmp(br,"wt-",3)||!strncmp(br,"j-",2)||!strncmp(br,"job-",4));
        char tgt[256]; snprintf(tgt,256,"origin/%s",sel?sel:wt?"main":br);
        char ts[64]; pcmd("git log -1 --format=%cd --date=format:'%Y-%m-%d %I:%M:%S %p' 2>/dev/null",ts,64); ts[strcspn(ts,"\n")]=0;
        if(sel)printf("%s -> origin/%s\n",br,sel);else printf("%s\n%s -> %s\n%s\n",cwd,br,tgt,ts);
        {char c[B];snprintf(c,B,"git diff '%s..HEAD' 2>/dev/null%s",tgt,ps);DS(c);}
        {char c[B];snprintf(c,B,"git diff HEAD 2>/dev/null%s",ps);DS(c);}
        char ut[B],uc[B];snprintf(uc,B,"git ls-files --others --exclude-standard 2>/dev/null%s",ps);pcmd(uc,ut,B);
        if(ut[0]){printf("\nUntracked:\n");char*p=ut;while(*p){char*e=strchr(p,'\n');if(e)*e=0;if(*p){
            printf("  \033[48;2;26;84;42m+ %s\033[0m\n",p);size_t sz;char*d=readf(p,&sz);
            if(d){int nl=1;for(size_t j=0;j<sz;j++)if(d[j]=='\n')nl++;FS(p);if(cf>=0){fs[cf].al=nl;fs[cf].ab=(int)sz;}free(d);}
        }if(e)p=e+1;else break;}}
        if(wt&&!nf){puts("No changes");return 0;}
        {char c[B],o[32];snprintf(c,B,"git ls-tree -r -l '%s' 2>/dev/null|awk '{s+=$4}END{print s}'",tgt);pcmd(c,o,32);ws=atol(o);}
    }
    for(int j=0;j<nf;j++){struct stat st;if(!stat(fs[j].name,&st))fs[j].sb=(int)st.st_size-fs[j].ab+fs[j].db;}
    if(!nf){puts("No changes");return 0;}
    int ti=0,td=0,ta=0,tb=0; printf("\n"); HR;
    for(int j=0;j<nf;j++){int tk=(fs[j].ab-fs[j].db)/4,st=fs[j].sb/4;
        if(st)printf("%s: +%d/-%d lines, %+d tok (%+.3f%% of file)\n",bname(fs[j].name),fs[j].al,fs[j].dl,tk,tk*100.0/st);
        else printf("%s: +%d/-%d lines, %+d tok (new)\n",bname(fs[j].name),fs[j].al,fs[j].dl,tk);
        ti+=fs[j].al;td+=fs[j].dl;ta+=fs[j].ab;tb+=fs[j].db;}
    HR; if(ws)printf("%s: %d file%s, %+d lines, %+d tok (%+.3f%% of repo)\n",fk?"fork":"net",nf,nf!=1?"s":"",ti-td,(ta-tb)/4,(ta-tb)*100.0/ws);
    else printf("%s: %d file%s, %+d lines, %+d tok\n",fk?"fork":"net",nf,nf!=1?"s":"",ti-td,(ta-tb)/4);
    if(!filt){char cp[P];commit_path(cp);char*cs=readf(cp,NULL),*nl=cs?strchr(cs,'\n'):0;
     if(nl){*nl=0;printf("\n\033[36mcommit:\033[0m %s \033[32m→ a push -f\033[0m\n",cs);}free(cs);}
    if(!fk&&!sel&&!filt) puts("\ndiff # = last #");
    return 0;
    #undef FS
    #undef DS
    #undef HR
}

static int cmd_revert(int argc, char **argv) { (void)argc; (void)argv;
    char cwd[P]; if (!getcwd(cwd, P)) strcpy(cwd, ".");
    if (!git_in_repo(cwd)) { puts("x Not a git repo"); return 1; }
    char c[B], out[B*4]; snprintf(c, B, "git -C '%s' log --format='%%h %%ad %%s' --date=format:'%%m/%%d %%H:%%M' -15", cwd);
    pcmd(c, out, sizeof(out));
    char *lines[15]; int nl = 0; char *p = out;
    while (*p && nl < 15) { lines[nl++] = p; char *e = strchr(p, '\n'); if (e) { *e = 0; p = e+1; } else break; }
    for (int i = 0; i < nl; i++) printf("  %d. %s\n", i, lines[i]);
    printf("\nRevert to #/q: "); char buf[8]; if (!fgets(buf,8,stdin) || buf[0]=='q') return 0;
    int idx = atoi(buf); if (idx < 0 || idx >= nl) { puts("x Invalid"); return 1; }
    char hash[16]; sscanf(lines[idx], "%s", hash);
    snprintf(c, B, "git -C '%s' revert --no-commit '%s..HEAD'", cwd, hash); (void)!system(c);
    snprintf(c, B, "git -C '%s' commit -m 'revert to %s'", cwd, hash); (void)!system(c);
    printf("✓ Reverted to %s\n", hash);
    printf("Push to main? (y/n): "); if (fgets(buf,8,stdin) && buf[0]=='y') {
        snprintf(c, B, "git -C '%s' push", cwd); (void)!system(c); puts("✓ Pushed");
    }
    return 0;
}
