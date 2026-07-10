/* git */
static int git_in_repo(const char *p) {
    char c[P]; snprintf(c, P, "%s/.git", p); return dexists(c)||fexists(c);
}

/* adata setup */
static void ensure_adata(void) {
    char c[B];
    if(!git_in_repo(SROOT)){
        snprintf(c,B,"gh repo clone seanpattencode/a-git '%s' 2>/dev/null",SROOT);
        if(!system(c))puts("✓ Cloned adata/git");
        else{mkdirp(SROOT);snprintf(c,B,"git -C '%s' init -q&&git -C '%s' checkout -b main 2>/dev/null",SROOT,SROOT);
            (void)!system(c);puts("✓ Init adata/git (gh auth login for sync)");}
    }
    char d[P];snprintf(d,P,"%s/my",SROOT);mkdir(d,0755);
}

static void ensure_git_id(void) {
    char n[128];pcmd("git config user.name 2>/dev/null",n,128);
    if(n[0]&&n[0]!='\n')return;
    pcmd("gh api user -q .login 2>/dev/null",n,128);n[strcspn(n,"\n")]=0;
    char e[128],c[B];
    if(n[0]){pcmd("gh api user -q .email 2>/dev/null",e,128);e[strcspn(e,"\n")]=0;
        if(!e[0]||!strcmp(e,"null"))snprintf(e,128,"%s@users.noreply.github.com",n);
    }else{gethostname(n,128);snprintf(e,128,"%s@local",n);}
    snprintf(c,B,"git config --global user.name '%s'&&git config --global user.email '%s'",n,e);
    (void)!system(c);printf("✓ git id: %s <%s>\n",n,e);
}
/* sync — flock serializes concurrent git ops */
static void sync_repo(void) {
    ensure_git_id();
    int fd=open("/tmp/.a_git.lock",O_CREAT|O_WRONLY,0644);
    if(fd>=0&&flock(fd,LOCK_EX|LOCK_NB)){close(fd);return;}
    char c[B];
    /* self-heal (7/8 wedge): a sync orphan died mid `pull --rebase`; the stateful rebase-merge dir then blocked every sync silently for a day.
       heal each run: pin HEAD to rescue-*, abort dead state, commit churn, merge rescue back (-X ours: state files last-writer-wins), atomic merge-pull (never rebase). */
    snprintf(c,B,"{ D='%s';g(){ git -C \"$D\" \"$@\";};g rev-parse --abbrev-ref HEAD >/dev/null||exit;"
        "[ -d \"$D/.git/rebase-merge\" ]&&{ g branch -f rescue-$(date +%%s) HEAD;g rebase --abort;};"
        "[ -f \"$D/.git/MERGE_HEAD\" ]&&g merge --abort;"
        "[ -s \"$D/.git/index\" ]||g read-tree HEAD;g add --sparse -A;g commit -qm sync;"
        "for r in $(g branch --list 'rescue-*'|tr -d ' *');do g merge -X ours --no-edit -q \"$r\"&&g branch -D \"$r\";done;"
        "g pull --no-rebase --no-edit -q origin main||g merge --abort;g push -q origin main;} >/dev/null 2>&1",SROOT);
    (void)!system(c);if(fd>=0)close(fd);
}
static void sync_bg(void) {
    fflush(NULL);   /* flush before fork, else children re-emit buffered stdout */
    pid_t p=fork();if(p<0)return;if(p>0){waitpid(p,NULL,WNOHANG);return;}
    if(fork()>0)_exit(0);setsid();freopen("/dev/null","w",stdout);freopen("/dev/null","w",stderr);sync_repo();_exit(0);
}
static void sync_pane(const char *text){(void)text;sync_bg();}
/* save proof. gh contents API commits to the REMOTE head directly → real url even when local trails the high-churn repo (a bare push would be rejected). then a local commit (NO push) so later pulls don't trip on the new file and the next note's PUT can't 422. no gh (e.g. phone) → bg sync + "saved ✓ syncing", never the old ~30s blocking pull/push. flock serializes; out!=0 writes line else prints. */
static void note_url(const char*fn,const char*msg,char*out){
    const char*rel=fn;size_t sl=strlen(SROOT);if(fn&&!strncmp(fn,SROOT,sl)&&fn[sl]=='/')rel=fn+sl+1;
    int fd=open("/tmp/.a_git.lock",O_CREAT|O_WRONLY,0644);if(fd>=0)flock(fd,LOCK_EX);
    char c[B*2],o[512]="";
    if(fn){snprintf(c,B*2,"command -v gh>/dev/null||exit 1;D='%s';"
        "r=$(git -C $D remote get-url origin 2>/dev/null|sed 's#.*github.com[:/]##;s#\\.git$##');[ -n \"$r\" ]||exit 1;"
        "d=$(base64 -w0 <'%s' 2>/dev/null||base64 <'%s'|tr -d '\\n');"
        "u=$(gh api --method PUT \"repos/$r/contents/%s\" -f message=%s -f content=\"$d\" --jq .content.html_url 2>/dev/null);[ -n \"$u\" ]||exit 1;"
        "git -C $D add --sparse -A;git -C $D commit -qm %s >/dev/null 2>&1;echo \"$u\"",SROOT,fn,fn,rel,msg,msg);
        pcmd(c,o,512);o[strcspn(o,"\n")]=0;}  /* trust output, not pcmd's exit: serve sets SIGCHLD=IGN so pclose returns -1 even on success */
    if(fd>=0)close(fd);
    char l[300];if(!strncmp(o,"https",5))snprintf(l,300,"saved \342\206\222 %s",o);else{sync_bg();snprintf(l,300,"saved \342\234\223 syncing");}
    if(out)snprintf(out,256,"%s",l);else puts(l);}
static const char*sync_age(void){static char b[16];char p[P];
    snprintf(p,P,"%s/.git/FETCH_HEAD",SROOT);struct stat st;
    if(stat(p,&st))return"never";
    strftime(b,16,"%H:%M:%S",localtime(&st.st_mtime));return b;}
