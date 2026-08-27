/* alog */
static void alog(const char *cmd, const char *cwd) {
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    char dir[P]; snprintf(dir, P, "%s/git/activity/%04d-%02d", AROOT, tm->tm_year+1900, tm->tm_mon+1);
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    char lf[P]; snprintf(lf, P, "%s/%04d%02d%02dT%02d%02d%02d.%03ld_%s.txt", dir,
        tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
        ts.tv_nsec / 1000000, DEV);
    FILE *f = fopen(lf, "w");
    if (!f) { mkdirp(dir); f = fopen(lf, "w"); if (!f) return; }  /* mkdir only on month rollover */
    const char *sid = getenv("ASID");
    fprintf(f, "%02d/%02d %02d:%02d %s %s %s%s%s\n",
        tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min,
        DEV, cmd, cwd, sid?" sid:":"", sid?sid:"");
    fclose(f);
    /* monthly shards (2026-08): flat 313k-file tree = 18.6MB tree object per sync commit, walks took minutes.
       bg prune 1/day: skip-worktree+rm months older than prev (*T* = pre-shard flat files, skip). */
    char mk[P]; snprintf(mk,P,"%s/git/activity/.last_prune",AROOT);
    struct stat st; if(stat(mk,&st)==0 && t-st.st_mtime<86400) return;
    fclose(fopen(mk,"w"));
    if(fork()==0){ setsid(); time_t co=t-32*86400; struct tm pv=*localtime(&co), cm=*localtime(&t);
        char c[P*2]; snprintf(c,sizeof(c),
          "cd '%s/git/activity'&&for m in 2*;do case $m in %04d-%02d|%04d-%02d|*T*);;*)"
          "git ls-files -z -- \"$m\"|xargs -0 -n500 git update-index --skip-worktree 2>/dev/null;rm -rf \"$m\";;esac;done 2>/dev/null",
          AROOT, pv.tm_year+1900,pv.tm_mon+1, cm.tm_year+1900,cm.tm_mon+1);
        system(c); _exit(0);}
}
