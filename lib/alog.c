/* alog */
static void alog(const char *cmd, const char *cwd) {
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    char dir[P]; snprintf(dir, P, "%s/local/activity/%04d-%02d", AROOT, tm->tm_year+1900, tm->tm_mon+1);
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
    /* device-local, never in git: a 141k-entry activity tree cost 8.8MB per sync commit (fedora disk full, 2026-08-30) */
}
