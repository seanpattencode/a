static int git_in_repo(const char *p) {
    char c[P]; snprintf(c, P, "%s/.git", p); return dexists(c)||fexists(c);
}

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
static void sync_repo(void) {
    ensure_git_id();
    int fd=open("/tmp/.a_git.lock",O_CREAT|O_WRONLY|O_CLOEXEC,0644);
    if(fd>=0&&flock(fd,LOCK_EX|LOCK_NB)){close(fd);return;}
    char c[B];
    snprintf(c,B,"{ D='%s';g(){ git -C \"$D\" \"$@\";};g rev-parse --abbrev-ref HEAD >/dev/null||exit;"
        "[ -f \"$D/.git/index.lock\" ]&&! pgrep -x git >/dev/null&&rm -f \"$D/.git/index.lock\";"
        "[ -d \"$D/.git/rebase-merge\" ]&&{ g branch -f rescue-$(date +%%s) HEAD;g rebase --abort;};"
        "[ -f \"$D/.git/MERGE_HEAD\" ]&&g merge --abort;"
        "g add -A -- . ':!activity';g ls-files -z --others --exclude-standard activity|g add --sparse --pathspec-from-file=- --pathspec-file-nul;g commit -qm sync;"
        "g fetch -q origin main 2>/dev/null;b=$(g rev-list --count HEAD..origin/main 2>/dev/null);"
        "[ \"${b:-0}\" -gt 0 ]&&g branch -f rescue-$(date +%%s) HEAD;"
        "n=$(g rev-parse origin/main);o=$n;for r in $(g for-each-ref --sort=refname --format='%%(refname:short)' 'refs/heads/rescue-*');do t=$(g merge-tree --write-tree --no-messages -X ours -X no-renames $r $n)||exit;n=$(printf sync|g commit-tree $t -p $n -p $r)||exit;g branch -D $r;done;[ \"$n\" = \"$o\" ]||{ h=$(g rev-parse HEAD);g diff --no-renames --name-only -z $h $n -- . ':!activity'|g restore --source=$n --worktree --pathspec-from-file=- --pathspec-file-nul;g reset -q --soft $n;g read-tree $n;g ls-files -z activity|g update-index --skip-worktree -z --stdin;};"
        "g pull --no-rebase --no-edit -q origin main||g merge --abort;g push -q origin main;g gc --auto -q;} >/dev/null 2>&1",SROOT);  /* gated gc inside the flock (agit-gc.md) */
    (void)!system(c);if(fd>=0)close(fd);
}
static void sync_bg(void) {
    fflush(NULL);
    pid_t p=fork();if(p<0)return;if(p>0){waitpid(p,NULL,WNOHANG);return;}
    if(fork()>0)_exit(0);setsid();freopen("/dev/null","w",stdout);freopen("/dev/null","w",stderr);sync_repo();_exit(0);
}
/* Note uploads never commit the worktree. */
static void note_url(const char*fn,const char*msg,char*out){
    if(!fn){if(out)snprintf(out,256,"save failed");return;}
    const char*rel=fn;size_t sl=strlen(SROOT);if(!strncmp(fn,SROOT,sl)&&fn[sl]=='/')rel=fn+sl+1;
    int fd=open("/tmp/.a_note.lock",O_CREAT|O_WRONLY|O_CLOEXEC,0644);if(fd>=0)flock(fd,LOCK_EX);
    char c[B*2],o[512]="";
    snprintf(c,B*2,"D='%s';"
        "r=$(git -C $D remote get-url origin 2>/dev/null|sed 's#.*github.com[:/]##;s#\\.git$##');[ -n \"$r\" ]||exit 1;"
        "d=$(base64 -w0 <'%s' 2>/dev/null||base64 <'%s'|tr -d '\\n');"
        "gh api --method PUT \"repos/$r/contents/%s\" -f message=%s -f content=\"$d\" --jq .content.html_url",SROOT,fn,fn,rel,msg);
    pcmd(c,o,512);o[strcspn(o,"\n")]=0;
    if(fd>=0)close(fd);
    char l[300];if(!strncmp(o,"https",5))snprintf(l,300,"saved \342\206\222 %s",o);else snprintf(l,300,"saved locally · sync FAILED · retry: a sync");
    if(out)snprintf(out,256,"%s",l);else puts(l);}
static const char*sync_age(void){static char b[16];char p[P];
    snprintf(p,P,"%s/.git/FETCH_HEAD",SROOT);struct stat st;
    if(stat(p,&st))return"never";
    strftime(b,16,"%H:%M:%S",localtime(&st.st_mtime));return b;}
