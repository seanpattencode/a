#include <glob.h>
/* a pedal — install keyd footswitch config from adata, reload keyd, show mapping.
   Debian/Ubuntu name the binary `keyd.rvaiya`, so resolve it rather than guess — and print the real status: a hardcoded "OK" after a failed reload sends you debugging the footswitch, not the install. */
static int cmd_pedal(int c, char **v) { (void)c; (void)v; perf_disarm();
    char d[P], pat[P]; snprintf(d, P, "%s/settings/keyd", SROOT); snprintf(pat, P, "%s/*.conf", d);
    glob_t g;
    if (glob(pat, 0, NULL, &g) != 0 || g.gl_pathc == 0) { printf("x no keyd config in %s\n", d); globfree(&g); return 1; }
    const char *av[64]; int n = 0; av[n++] = "sudo"; av[n++] = "cp";
    for (size_t i = 0; i < g.gl_pathc && n < 61; i++) av[n++] = g.gl_pathv[i];
    av[n++] = "/etc/keyd/"; av[n] = NULL;
    int st = 0;
    if (fork() == 0) { execvp("sudo", (char *const *)av); _exit(127); } wait(&st);
    if (WEXITSTATUS(st)) { puts("x cp -> /etc/keyd failed"); globfree(&g); return 1; }
    char kb[64]; pcmd("command -v keyd||command -v keyd.rvaiya", kb, 64); kb[strcspn(kb, "\n")] = 0;
    if (*kb) { if (fork() == 0) { execlp("sudo", "sudo", kb, "reload", (char *)NULL); _exit(127); } wait(&st); }
    int ok = *kb && !WEXITSTATUS(st);
    printf("%s: %s\n", ok ? "OK keyd reloaded" : *kb ? "x keyd reload FAILED — mapping not live" : "x keyd not installed — mapping not live", d);
    char buf[B]; pcmd("lsusb 2>/dev/null", buf, B);
    int found = 0;
    for (char *ln = strtok(buf, "\n"); ln; ln = strtok(NULL, "\n")) if (strcasestr(ln, "footswitch")) { puts(ln); found = 1; }
    if (!found) puts("! footswitch not detected — plug it in");
    puts("--- mapping ---");
    for (size_t i = 0; i < g.gl_pathc; i++) { char *t = readf(g.gl_pathv[i], NULL); if (t) fputs(t, stdout), free(t); }
    globfree(&g);
    return !ok;
}
