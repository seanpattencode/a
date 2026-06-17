#include <glob.h>
/* a pedal — install keyd footswitch config from adata, reload keyd, show mapping.
   Converted from lib/pedal.sh per IDEAS #130/#131 (no standalone .sh; fork/exec, no shell). NOTE: Linux+sudo+keyd — functional test pending on a Linux box. */
static int cmd_pedal(int c, char **v) { (void)c; (void)v; perf_disarm();
    char d[P], pat[P]; snprintf(d, P, "%s/settings/keyd", SROOT); snprintf(pat, P, "%s/*.conf", d);
    glob_t g;
    if (glob(pat, 0, NULL, &g) != 0 || g.gl_pathc == 0) { printf("x no keyd config in %s\n", d); globfree(&g); return 1; }
    const char *av[64]; int n = 0; av[n++] = "sudo"; av[n++] = "cp";
    for (size_t i = 0; i < g.gl_pathc && n < 61; i++) av[n++] = g.gl_pathv[i];
    av[n++] = "/etc/keyd/"; av[n] = NULL;
    int st = 0;
    if (fork() == 0) { execvp("sudo", (char *const *)av); _exit(127); } wait(&st);
    if (fork() == 0) { execlp("sudo", "sudo", "keyd", "reload", (char *)NULL); _exit(127); } wait(&st);
    if (WEXITSTATUS(st) != 0) { if (fork() == 0) { execlp("sudo", "sudo", "keyd.rvaiya", "reload", (char *)NULL); _exit(127); } wait(&st); }
    printf("OK keyd reloaded from %s\n", d);
    int p[2]; char buf[4096] = "";
    if (pipe(p) == 0) {
        if (fork() == 0) { dup2(p[1], 1); close(p[0]); close(p[1]); execlp("lsusb", "lsusb", (char *)NULL); _exit(127); }
        close(p[1]); ssize_t r = read(p[0], buf, sizeof buf - 1); if (r > 0) buf[r] = 0; close(p[0]); wait(&st);
    }
    int found = 0;
    for (char *ln = strtok(buf, "\n"); ln; ln = strtok(NULL, "\n")) if (strcasestr(ln, "footswitch")) { puts(ln); found = 1; }
    if (!found) puts("! footswitch not detected — plug it in");
    puts("--- mapping ---");
    for (size_t i = 0; i < g.gl_pathc; i++) { FILE *f = fopen(g.gl_pathv[i], "r"); if (!f) continue; char b[1024]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0) (void)!fwrite(b, 1, k, stdout); fclose(f); }
    globfree(&g);
    return 0;
}
