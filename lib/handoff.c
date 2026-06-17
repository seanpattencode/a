/* a handoff <srcdir> <dstdir> [session-id] — move a Claude Code conversation between project dirs, then resume it.
   Same-device proven; cross-device ssh is the next step. Converted from lib/handoff.sh per IDEAS #130/#131 (no standalone .sh). */
static int cmd_handoff(int c, char **v) {
    perf_disarm();
    if (c < 4) { puts("usage: a handoff <srcdir> <dstdir> [session-id]"); return 1; }
    char src[P], dst[P];
    if (!realpath(v[2], src)) { printf("x src dir not found: %s\n", v[2]); return 1; }
    if (!realpath(v[3], dst)) { printf("x dst dir not found (mkdir it first): %s\n", v[3]); return 1; }
    char ss[P], ds[P]; snprintf(ss, P, "%s", src); snprintf(ds, P, "%s", dst);
    for (char *p = ss; *p; p++) if (*p == '/') *p = '-';
    for (char *p = ds; *p; p++) if (*p == '/') *p = '-';
    char sdir[P]; snprintf(sdir, P, "%s/.claude/projects/%s", HOME, ss);
    char sid[128] = "";
    if (c > 4) snprintf(sid, 128, "%s", v[4]);
    else {
        DIR *d = opendir(sdir); struct dirent *e; time_t best = 0; struct stat st; char fp[P];
        while (d && (e = readdir(d))) {
            size_t L = strlen(e->d_name);
            if (L < 7 || strcmp(e->d_name + L - 6, ".jsonl")) continue;
            snprintf(fp, P, "%s/%s", sdir, e->d_name);
            if (!stat(fp, &st) && st.st_mtime >= best) { best = st.st_mtime; snprintf(sid, 128, "%.*s", (int)(L - 6), e->d_name); }
        }
        if (d) closedir(d);
        if (!sid[0]) { printf("x no conversation .jsonl in %s\n", sdir); return 1; }
    }
    char jp[P]; snprintf(jp, P, "%s/%s.jsonl", sdir, sid);
    if (!fexists(jp)) { printf("x not found: %s\n", jp); return 1; }
    char ddir[P]; snprintf(ddir, P, "%s/.claude/projects/%s", HOME, ds); mkdirp(ddir);
    char dp[P]; snprintf(dp, P, "%s/%s.jsonl", ddir, sid);
    int in = open(jp, O_RDONLY), out = open(dp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (in < 0 || out < 0) { printf("x copy failed\n"); if (in >= 0) close(in); if (out >= 0) close(out); return 1; }
    char buf[65536]; ssize_t n; long tot = 0;
    while ((n = read(in, buf, sizeof buf)) > 0) { (void)!write(out, buf, (size_t)n); tot += n; }
    close(in); close(out);
    printf("1) src slug: %s\n2) dst slug: %s\n3) conversation: %s\n4) copied -> %s (%ldb)\n5) resume in %s\n", sdir, ddir, sid, dp, tot, dst);
    char rc[B]; snprintf(rc, B, "claude --dangerously-skip-permissions --resume %s", sid);
    char wn[64]; snprintf(wn, 64, "ho-%.8s", sid);
    if (getenv("TMUX")) {
        if (!fork()) { execlp("tmux", "tmux", "new-window", "-c", dst, "-n", wn, rc, (char *)0); _exit(127); }
        wait(0); printf("   tmux window %s launched: %s\n", wn, rc);
    } else printf("   (no tmux) run: cd %s && %s\n", dst, rc);
    return 0;
}
