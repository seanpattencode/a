/* alog */
static void alog(const char *cmd, const char *cwd) {
    char dir[P]; snprintf(dir, P, "%s/git/activity", AROOT);
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    char lf[P]; snprintf(lf, P, "%s/%04d%02d%02dT%02d%02d%02d.%03ld_%s.txt", dir,
        tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
        ts.tv_nsec / 1000000, DEV);
    FILE *f = fopen(lf, "w"); if (!f) return;
    const char *sid = getenv("ASID");
    fprintf(f, "%02d/%02d %02d:%02d %s %s %s%s%s\n",
        tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min,
        DEV, cmd, cwd, sid?" sid:":"", sid?sid:"");
    fclose(f);
    /* throttled bg prune: skip-worktree+rm activity older than 30d (filename ts). keeps git history. */
    char mk[P]; snprintf(mk,P,"%s/.last_prune",dir);
    struct stat st; if(stat(mk,&st)==0 && t-st.st_mtime<86400) return;
    fclose(fopen(mk,"w"));
    if(fork()==0){ setsid(); time_t co=t-30*86400; struct tm *ctm=localtime(&co);
        char c[P*2]; snprintf(c,sizeof(c),
          "cd '%s'&&ls|awk -vc=%04d%02d%02d 'match($0,/2[0-9]{7}T/){if(substr($0,RSTART,8)<c)print}'"
          "|xargs -n500 sh -c 'git update-index --skip-worktree \"$@\" 2>/dev/null;rm -f \"$@\"' _ 2>/dev/null",
          dir,ctm->tm_year+1900,ctm->tm_mon+1,ctm->tm_mday);
        system(c); _exit(0);}
}
