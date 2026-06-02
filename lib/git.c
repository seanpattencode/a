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
    snprintf(c,B,"{ D='%s';g(){ git -C \"$D\" \"$@\";};g rev-parse --abbrev-ref HEAD >/dev/null||exit;"
        "[ -s \"$D/.git/index\" ]||g read-tree HEAD;g add --sparse -A;g commit -qm sync;"
        "g pull --no-rebase --no-edit -q origin main;g push -q origin main;} >/dev/null 2>&1",SROOT);
    (void)!system(c);if(fd>=0)close(fd);
}
static void sync_bg(void) {
    pid_t p=fork();if(p<0)return;if(p>0){waitpid(p,NULL,WNOHANG);return;}
    if(fork()>0)_exit(0);setsid();freopen("/dev/null","w",stdout);freopen("/dev/null","w",stderr);sync_repo();_exit(0);
}
static void sync_pane(const char *text){(void)text;sync_bg();}
/* proof of save: commit locally, then a TIME-BOUNDED pull+push. Fast push (ff) → real URL.
   Slow/behind (mobile: activity/ blobs bloat the fetch) → return now with clear status + finish in bg. Never hang, never false-proof. */
static void sync_proof(void){
    int fd=open("/tmp/.a_git.lock",O_CREAT|O_WRONLY,0644);if(fd>=0)flock(fd,LOCK_EX);
    char c[B],o[256];
    snprintf(c,B,"D='%s';g(){ git -C \"$D\" \"$@\";};g add --sparse -A;g commit -qm sync >/dev/null 2>&1;"
        "u=$(g remote get-url origin 2>/dev/null|sed 's#\\.git$##');h=$(g rev-parse --short HEAD 2>/dev/null);"
        "if timeout 12 git -C \"$D\" pull --no-rebase --no-edit -q origin main >/dev/null 2>&1 && timeout 12 git -C \"$D\" push -q origin main 2>/dev/null;then echo \"$u/commit/$h\";else echo \"PENDING $h\";fi",SROOT);
    pcmd(c,o,256);o[strcspn(o,"\n")]=0;if(fd>=0)close(fd);
    if(!strncmp(o,"PENDING ",8))sync_bg(),printf("saved ✓ (commit %s) — syncing to github…\n",o+8);
    else printf("saved → %s\n",o);}
static const char*sync_age(void){static char b[16];char p[P];
    snprintf(p,P,"%s/.git/FETCH_HEAD",SROOT);struct stat st;
    if(stat(p,&st))return"never";
    strftime(b,16,"%H:%M:%S",localtime(&st.st_mtime));return b;}
