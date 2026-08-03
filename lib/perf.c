static const char *BENCH_CMDS[] = {
    "i","help","task","ls","add","agent","copy","done","docs",
    "move","prompt","remove","repo","send","set","setup",
    "uninstall","watch",/* "x" kills tmux server, destroys active dev sessions */
    "e","kill","revert","hub",
    "jobs","mono","ssh","work","ask","login","gdrive","email","ui",
    "run","pull","diff","all","push","tree","review","log","note",
    "sync","scan","update","install",NULL
};

/* perf file format: cmd:limit[:last]. field 0=limit, 1=last */
static unsigned perf_field(const char *data, const char *cmd, int field) {
    if (!data) return 0;
    char needle[128]; snprintf(needle, 128, "\n%s:", cmd);
    const char *m = strstr(data, needle);
    size_t cl = strlen(cmd);
    if (!m && !strncmp(data, cmd, cl) && data[cl] == ':') m = data - 1;
    if (!m) return 0;
    const char *p = m + 1 + cl + 1;
    if (!field) return (unsigned)atoi(p);
    const char *c = strchr(p, ':');
    return c ? (unsigned)atoi(c + 1) : 0;
}
#define perf_limit(d,c) perf_field(d,c,0)
#define perf_last(d,c)  perf_field(d,c,1)

/* display in ms, enforce in us. 27us overhead × 5B users = 1 human lifetime/year */
#define fmt_us(us,buf,sz) snprintf(buf,sz,"%.3fms",(us)/1000.0)
#define el_us(a,b) ((unsigned)(((a).tv_sec-(b).tv_sec)*1000000+((a).tv_nsec-(b).tv_nsec)/1000))

static int cmd_perf(int argc, char **argv) {
    perf_disarm();
    init_db();
    char pf[P]; snprintf(pf, P, "%s/perf/%s.txt", SROOT, DEV);
    const char *sub = argc > 2 ? argv[2] : NULL;

    if (sub && (!strcmp(sub,"help")||!strcmp(sub,"-h"))) {
        puts("a perf - Performance regression enforcer\n"
             "  a perf          Show current limits for this device\n"
             "  a perf bench    Benchmark all commands and save tighter limits\n\n"
             "System: every command has a timeout (default 1s local, 5s network/disk).\n"
             "If exceeded, process is killed. Limits only tighten, never loosen.\n"
             "Per-device profiles live in adata/git/perf/{device}.txt and sync across devices.");
        return 0;
    }

    if (!sub || !strcmp(sub, "show")) {
        char *data = readf(pf, NULL);
        printf("PERF — device: %s\n", DEV);
        printf("Profile: %s\n", pf);
        puts("──────────────────────────────────────────────────");
        printf("%-15s %10s %10s\n", "COMMAND", "LIMIT", "LAST");
        puts("──────────────────────────────────────────────────");
        for (const char **c = BENCH_CMDS; *c; c++) {
            const char *label = *c;
            unsigned lim = perf_limit(data, label), last = perf_last(data, label);
            char fb[32], fl[32]; if(lim)fmt_us(lim,fb,32);else snprintf(fb,32,"1000ms");
            if(last)fmt_us(last,fl,32);else snprintf(fl,32,"-");
            printf("%-15s %10s %10s\n", label, fb, fl);
        }
        puts("──────────────────────────────────────────────────");
        puts("\nDefault: 1000ms. Override with per-device file.");
        puts("Run 'a perf bench' to benchmark and auto-tighten limits.");
        free(data);
        return 0;
    }

    if (!strcmp(sub, "bench")) {
        char *data = readf(pf, NULL);
        const char *only = argc > 3 ? argv[3] : NULL;
        int ncmds = 0; for (const char **c = BENCH_CMDS; *c; c++) ncmds++;
        typedef struct { const char *cmd; pid_t pid; unsigned us, old_lim, new_lim; int done, pass, skip; } res_t;
        res_t *res = calloc((size_t)ncmds, sizeof(res_t));

        struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
        int nul = open("/dev/null", O_RDWR);
        char bin[P]; snprintf(bin, P, "%s/local/a", AROOT);
        for (int i = 0; i < ncmds; i++) {
            res[i].cmd = BENCH_CMDS[i];
            res[i].old_lim = perf_limit(data, BENCH_CMDS[i]);
            if (only && strcmp(only, BENCH_CMDS[i])) { res[i].skip = 1; res[i].done = 1; continue; }
            /* 'i': read internal timer from stderr (measures real TUI startup) */
            if (!strcmp(BENCH_CMDS[i],"i")) {
                int pfd[2];pipe(pfd);pid_t p=fork();
                if(p==0){dup2(nul,0);dup2(nul,1);dup2(pfd[1],2);close(pfd[0]);putenv("A_BENCH=1");execl(bin,"a","i",(char*)NULL);_exit(127);}
                close(pfd[1]);char sb[256]={0};read(pfd[0],sb,255);close(pfd[0]);
                int st;waitpid(p,&st,0);res[i].us=(unsigned)atoi(sb);
                res[i].done=1;res[i].pass=WIFEXITED(st);continue;
            }
            pid_t p = fork();
            if (p == 0) {
                dup2(nul, STDIN_FILENO); dup2(nul, STDOUT_FILENO); dup2(nul, STDERR_FILENO);
                setpgid(0, 0);
                putenv("A_BENCH=1");
                execl(bin, "a", BENCH_CMDS[i], (char *)NULL);
                _exit(127);
            }
            if (p < 0) { res[i].skip = res[i].done = 1; continue; }  /* fork failed: pid -1 would make the timeout path kill(-1) = SIGKILL everything we own */
            res[i].pid = p;
            setpgid(p, p);
        }
        close(nul);

        /* reap with 1s hard deadline — poll each child by PID (not -1) */
        int remaining = 0; for (int i = 0; i < ncmds; i++) if (!res[i].skip) remaining++;
        while (remaining > 0) {
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            unsigned elapsed = el_us(now, t0);
            if (elapsed > 1000000) {
                for (int i = 0; i < ncmds; i++) if (!res[i].done) {
                    kill(-res[i].pid, SIGKILL); kill(res[i].pid, SIGKILL);
                    waitpid(res[i].pid, NULL, 0);
                    res[i].done = 1; res[i].us = elapsed; remaining--;
                }
                fprintf(stderr, "\033[31mx\033[0m a perf bench exceeded 1s — make slow commands faster\n");
                break;
            }
            int any = 0;
            for (int i = 0; i < ncmds; i++) {
                if (res[i].done) continue;
                int status; pid_t p = waitpid(res[i].pid, &status, WNOHANG);
                if (p <= 0) continue;
                clock_gettime(CLOCK_MONOTONIC, &now);
                res[i].us = el_us(now, t0);
                res[i].done = 1; remaining--; any = 1;
                res[i].pass = !WIFSIGNALED(status);
            }
            if (!any) usleep(500);
        }

        struct timespec tend; clock_gettime(CLOCK_MONOTONIC, &tend);
        char ft_total[32]; fmt_us(el_us(tend,t0),ft_total,32);
        printf("PERF BENCH — device: %s (%s)\n", DEV, ft_total);
        puts("─────────────────────────────────────────────────────────────");
        printf("%-12s %10s %10s %10s  %s\n", "COMMAND", "TIME", "LIMIT", "NEW", "STATUS");
        puts("─────────────────────────────────────────────────────────────");
        int passed = 0, tightened = 0, shown = 0;
        for (int i = 0; i < ncmds; i++) {
            if (res[i].skip) continue;
            shown++;
            int killed = !res[i].pass;
            unsigned t = res[i].us;
            unsigned proposed = (t * 13 + 9) / 10;
            if (proposed < 500) proposed = 500;
            unsigned old = res[i].old_lim; int tight = 0;
            res[i].new_lim = old;
            if (killed) { /* keep old */ }
            else if (!old || proposed < old) { res[i].new_lim = proposed; tight = 1; }
            if (!killed) passed++;
            if (tight) tightened++;
            const char *label = res[i].cmd;
            char ft[32], fo[32], fn[32];
            fmt_us(t, ft, 32); fmt_us(old, fo, 32); fmt_us(res[i].new_lim, fn, 32);
            const char *st = killed ? "\033[31mKILLED\033[0m" : tight ? "\033[32m↓ tight\033[0m" : "\033[32m✓\033[0m";
            printf("%-12s %10s %10s %10s  %s\n", label, ft, old ? fo : "-", fn, st);
        }
        puts("─────────────────────────────────────────────────────────────");
        printf("%d/%d passed, %d tightened\n", passed, shown, tightened);
        {char pd[P]; snprintf(pd, P, "%s/perf", SROOT); mkdirp(pd);
            FILE *f = fopen(pf, "w");
            if (f) {
                for (int i = 0; i < ncmds; i++) {
                    const char *k = BENCH_CMDS[i];
                    unsigned lim = res[i].skip ? perf_limit(data, k) : res[i].new_lim;
                    unsigned last = res[i].skip ? perf_last(data, k) : res[i].us;
                    fprintf(f, "%s:%u:%u\n", k, lim, last);
                }
                fclose(f);
            }
        }
        free(data); free(res);
        return 0;
    }

    fprintf(stderr, "a perf: unknown subcommand '%s'. Try 'a perf help'\n", sub);
    return 1;
}
