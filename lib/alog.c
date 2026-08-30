/* alog — one append-file per device-month, adata/local only (never git).
   Per-command files made freq/log/gen_icache open 391k files: 4.7min on HSU HDD (2026-08-30). */
static void alog(const char *cmd, const char *cwd) {
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    char lf[P]; snprintf(lf, P, "%s/local/activity/%04d-%02d_%s.txt", AROOT, tm->tm_year+1900, tm->tm_mon+1, DEV);
    FILE *f = fopen(lf, "a");
    if (!f) { char d[P]; snprintf(d, P, "%s/local/activity", AROOT); mkdirp(d); f = fopen(lf, "a"); if (!f) return; }
    const char *sid = getenv("ASID");
    fprintf(f, "%02d/%02d %02d:%02d %s %s %s%s%s\n",
        tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min,
        DEV, cmd, cwd, sid?" sid:":"", sid?sid:"");
    fclose(f);
}
