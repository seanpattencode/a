/* init */
static void init_paths(void) {
    const char *h = getenv("HOME"); if (!h) h = "/tmp";
    snprintf(HOME, P, "%s", h);
    {const char*t=getenv("TMPDIR");snprintf(TMP,P,"%s",t&&*t?t:"/tmp");}
#ifdef __APPLE__
    /* non-interactive ssh PATH on macOS omits Homebrew, so tmux/brew tools aren't found. prepend them once. */
    {const char*pe=getenv("PATH");if(!pe||!strstr(pe,"/opt/homebrew/bin")){char np[4096];snprintf(np,4096,"/opt/homebrew/bin:/usr/local/bin:%s",pe?pe:"/usr/bin:/bin");setenv("PATH",np,1);}}
#endif
    char self[P]; ssize_t n = -1;
#ifdef __APPLE__
    uint32_t sz = P - 1;
    if (_NSGetExecutablePath(self, &sz) == 0) { n = (ssize_t)strlen(self); char rp[P]; if (realpath(self, rp)) { snprintf(self, P, "%s", rp); n = (ssize_t)strlen(self); } }
#else
    n = readlink("/proc/self/exe", self, P - 1);
#endif
    if (n > 0) {
        self[n] = 0; char *s = strrchr(self, '/');
        if (s) { *s = 0;
            {char *al=strstr(self,"/adata/local");if(al)*al=0;}
            snprintf(SDIR, P, "%s", self);
            snprintf(AROOT, P, "%s/adata", self);
            snprintf(SROOT, P, "%s/git", AROOT);
        }
    }
    {const char*e=getenv("A_SDIR");if(e&&*e){snprintf(SDIR,P,"%s",e);snprintf(AROOT,P,"%s/adata",e);snprintf(SROOT,P,"%s/git",AROOT);}}
    if (!SROOT[0]) { snprintf(AROOT, P, "%s/a/adata", h); snprintf(SROOT, P, "%s/git", AROOT); }
    snprintf(DDIR, P, "%s/local", AROOT);
    mkdirp(DDIR);
    /* device id */
    char df[P]; snprintf(df, P, "%s/.device", DDIR);
    FILE *f = fopen(df, "r");
    if (f) { if (fgets(DEV, 128, f)) DEV[strcspn(DEV, "\n")] = 0; fclose(f); }
    if (!DEV[0] || !strcmp(DEV, "localhost")) {
        DEV[0] = 0;
        if (!access("/data/data/com.termux", F_OK)) {
            FILE *p = popen("getprop ro.product.model 2>/dev/null", "r");
            if (p) { if (fgets(DEV, 128, p)) { DEV[strcspn(DEV, "\n")] = 0; for (char *c = DEV; *c; c++) *c = (*c == ' ') ? '-' : (char)tolower((unsigned char)*c); } pclose(p); }
        }
        if (!DEV[0]) gethostname(DEV, 128);
        f = fopen(df, "w"); if (f) { fputs(DEV, f); fclose(f); }
    }
    snprintf(LOGDIR, P, "%s/backup/%s", AROOT, DEV);
}
