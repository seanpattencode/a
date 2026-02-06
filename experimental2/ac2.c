/*
 * ac2 - full C rewrite of 'a' command dispatcher + all commands
 * Every command runs in <1ms except network ops (git push/pull/fetch)
 *
 * Build: cc -O2 -o ac2 ac2.c -lsqlite3
 * Link against system sqlite3 for DB operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sqlite3.h>
#include <glob.h>
#include <libgen.h>

/* ── paths ── */
static char HOME[512], DATA[600], SCRIPT[512], SYNC[600], PY[700];
static char PROJ_DIR[700], CMDS_DIR[700], LOG_DIR_[700];

static void init_paths(void) {
    const char *h = getenv("HOME"); if (!h) h = "/tmp";
    snprintf(HOME, sizeof(HOME), "%s", h);
    snprintf(DATA, sizeof(DATA), "%s/.local/share/a", h);
    snprintf(LOG_DIR_, sizeof(LOG_DIR_), "%s/.local/share/a/logs", h);
    char self[512]; ssize_t n = readlink("/proc/self/exe", self, sizeof(self)-1);
    if (n > 0) { self[n] = '\0'; char *sl = strrchr(self, '/'); if (sl) *sl = '\0'; snprintf(SCRIPT, sizeof(SCRIPT), "%s", self); }
    snprintf(PY, sizeof(PY), "%s/../a.py", SCRIPT);
    /* SYNC_ROOT = parent of SCRIPT_DIR's parent (projects/a-sync) */
    char script_parent[600];
    snprintf(script_parent, sizeof(script_parent), "%s/..", SCRIPT);
    char *rp = realpath(script_parent, NULL);
    if (rp) {
        char *pp = strrchr(rp, '/'); /* go up one more to projects/ */
        if (pp) { *pp = '\0'; snprintf(SYNC, sizeof(SYNC), "%s/a-sync", rp); }
        else snprintf(SYNC, sizeof(SYNC), "%s/../a-sync", SCRIPT);
        free(rp);
    } else snprintf(SYNC, sizeof(SYNC), "%s/../a-sync", SCRIPT);
    snprintf(PROJ_DIR, sizeof(PROJ_DIR), "%s/workspace/projects", SYNC);
    snprintf(CMDS_DIR, sizeof(CMDS_DIR), "%s/workspace/cmds", SYNC);
}

/* ── tiny helpers ── */
static int fexists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static int isdir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int isfile(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }

static int cat_file(const char *path) {
    int fd = open(path, O_RDONLY); if (fd < 0) return -1;
    char buf[8192]; ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) write(STDOUT_FILENO, buf, n);
    close(fd); return 0;
}

static char *read_file(const char *path, size_t *len) {
    int fd = open(path, O_RDONLY); if (fd < 0) return NULL;
    struct stat st; if (fstat(fd, &st) < 0) { close(fd); return NULL; }
    char *buf = malloc(st.st_size + 1); if (!buf) { close(fd); return NULL; }
    ssize_t n = read(fd, buf, st.st_size); close(fd);
    if (n < 0) { free(buf); return NULL; }
    buf[n] = '\0'; if (len) *len = n; return buf;
}

/* Run command, capture stdout into buf. Returns exit code. */
static int run_cap(const char *cmd, char *buf, int bufsz) {
    FILE *fp = popen(cmd, "r"); if (!fp) return -1;
    int off = 0; char line[4096];
    while (fgets(line, sizeof(line), fp) && off < bufsz - 1) {
        int l = strlen(line); if (off + l >= bufsz) break;
        memcpy(buf + off, line, l); off += l;
    }
    buf[off] = '\0'; return pclose(fp);
}

/* Run command silently, return exit code */
static int run_quiet(const char *cmd) {
    char buf[64]; return run_cap(cmd, buf, sizeof(buf));
}

/* chomp trailing newline */
static void chomp(char *s) { int l = strlen(s); while (l > 0 && s[l-1] == '\n') s[--l] = '\0'; }

/* ── SQLite helpers ── */
static sqlite3 *dbopen(void) {
    char p[700]; snprintf(p, sizeof(p), "%s/aio.db", DATA);
    sqlite3 *db; if (sqlite3_open(p, &db) != SQLITE_OK) return NULL;
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    return db;
}

static char *db_get(sqlite3 *db, const char *key) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, "SELECT value FROM config WHERE key=?", -1, &s, NULL)) return NULL;
    sqlite3_bind_text(s, 1, key, -1, NULL);
    char *r = NULL;
    if (sqlite3_step(s) == SQLITE_ROW) r = strdup((const char*)sqlite3_column_text(s, 0));
    sqlite3_finalize(s); return r;
}

static void db_set(sqlite3 *db, const char *key, const char *val) {
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO config VALUES(?,?)", -1, &s, NULL);
    sqlite3_bind_text(s, 1, key, -1, NULL);
    sqlite3_bind_text(s, 2, val, -1, NULL);
    sqlite3_step(s); sqlite3_finalize(s);
}

/* ── Project/App loading from sync workspace files ── */
typedef struct { char path[512]; char repo[512]; char name[128]; } proj_t;
typedef struct { char name[128]; char command[512]; } app_t;

static int load_projects(proj_t *out, int max) {
    glob_t g; char pat[800]; int n = 0;
    snprintf(pat, sizeof(pat), "%s/*.txt", PROJ_DIR);
    if (glob(pat, 0, NULL, &g)) return 0;
    for (size_t i = 0; i < g.gl_pathc && n < max; i++) {
        char *data = read_file(g.gl_pathv[i], NULL); if (!data) continue;
        char *name = NULL, *path = NULL, *repo = NULL;
        for (char *line = strtok(data, "\n"); line; line = strtok(NULL, "\n")) {
            char *col = strchr(line, ':'); if (!col) continue;
            *col = '\0'; char *v = col + 1; while (*v == ' ') v++;
            if (!strcmp(line, "Name")) name = v;
            else if (!strcmp(line, "Path")) path = v;
            else if (!strcmp(line, "Repo")) repo = v;
        }
        if (name) {
            proj_t *p = &out[n];
            if (path) { char tmp[512]; snprintf(tmp, sizeof(tmp), "%s", path);
                if (tmp[0] == '~') snprintf(p->path, sizeof(p->path), "%s%s", HOME, tmp+1);
                else snprintf(p->path, sizeof(p->path), "%s", tmp);
            } else snprintf(p->path, sizeof(p->path), "%s/projects/%s", HOME, name);
            snprintf(p->repo, sizeof(p->repo), "%s", repo ? repo : "");
            snprintf(p->name, sizeof(p->name), "%s", name);
            n++;
        }
        free(data);
    }
    globfree(&g);
    /* sort by name */
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(out[i].name, out[j].name) > 0) { proj_t t = out[i]; out[i] = out[j]; out[j] = t; }
    return n;
}

static int load_apps(app_t *out, int max) {
    glob_t g; char pat[800]; int n = 0;
    snprintf(pat, sizeof(pat), "%s/*.txt", CMDS_DIR);
    if (glob(pat, 0, NULL, &g)) return 0;
    for (size_t i = 0; i < g.gl_pathc && n < max; i++) {
        char *data = read_file(g.gl_pathv[i], NULL); if (!data) continue;
        char *name = NULL, *cmd = NULL;
        for (char *line = strtok(data, "\n"); line; line = strtok(NULL, "\n")) {
            char *col = strchr(line, ':'); if (!col) continue;
            *col = '\0'; char *v = col + 1; while (*v == ' ') v++;
            if (!strcmp(line, "Name")) name = v;
            else if (!strcmp(line, "Command")) cmd = v;
        }
        if (name && cmd) {
            snprintf(out[n].name, sizeof(out[n].name), "%s", name);
            snprintf(out[n].command, sizeof(out[n].command), "%s", cmd);
            n++;
        }
        free(data);
    }
    globfree(&g);
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(out[i].name, out[j].name) > 0) { app_t t = out[i]; out[i] = out[j]; out[j] = t; }
    return n;
}

/* Write projects.txt cache + help_cache.txt */
static void refresh_cache(proj_t *P, int np, app_t *A, int na) {
    char path[700]; FILE *fp;
    snprintf(path, sizeof(path), "%s/projects.txt", DATA);
    fp = fopen(path, "w"); if (fp) { for (int i = 0; i < np; i++) fprintf(fp, "%s\n", P[i].path); fclose(fp); }
    snprintf(path, sizeof(path), "%s/help_cache.txt", DATA);
    fp = fopen(path, "w"); if (!fp) return;
    fprintf(fp, "a c|co|g|ai     Start claude/codex/gemini/aider\n"
               "a <#>           Open project by number\n"
               "a prompt        Manage default prompt\n"
               "a help          All commands\n");
    if (np) { fprintf(fp, "PROJECTS:\n"); for (int i = 0; i < np; i++) fprintf(fp, "  %d. %c %s\n", i, isdir(P[i].path) ? '+' : (P[i].repo[0] ? '~' : 'x'), P[i].path); }
    if (na) { fprintf(fp, "COMMANDS:\n"); for (int i = 0; i < na; i++) fprintf(fp, "  %d. %s -> %.60s\n", np+i, A[i].name, A[i].command); }
    fclose(fp);
}

/* ── fallback to python ── */
__attribute__((noreturn))
static void fallback(int argc, char **argv) {
    char **a = malloc((argc + 3) * sizeof(char*));
    a[0] = "python3"; a[1] = PY;
    for (int i = 1; i < argc; i++) a[i+1] = argv[i];
    a[argc+1] = NULL; execvp("python3", a);
    perror("ac2: exec python3"); _exit(127);
}

/* ═══════════════════════════════════════════
 * COMMANDS - each returns 0 on success
 * ═══════════════════════════════════════════ */

/* --- help (no args) --- */
static int cmd_help(void) {
    char p[700]; snprintf(p, sizeof(p), "%s/help_cache.txt", DATA);
    if (cat_file(p) < 0) {
        proj_t P[64]; app_t A[32];
        int np = load_projects(P, 64), na = load_apps(A, 32);
        refresh_cache(P, np, A, na);
        cat_file(p);
    }
    return 0;
}

/* --- help full --- */
static int cmd_help_full(void) {
    puts("a - AI agent session manager\n\n"
         "AGENTS          c=claude  co=codex  g=gemini  ai=aider\n"
         "  a <key>             Start agent in current dir\n"
         "  a <key> <#>         Start agent in project #\n"
         "  a <key>++           Start agent in new worktree\n\n"
         "PROJECTS\n"
         "  a <#>               cd to project #\n"
         "  a add               Add current dir as project\n"
         "  a remove <#>        Remove project\n"
         "  a move <#> <#>      Reorder project\n"
         "  a scan              Add your repos fast\n\n"
         "GIT\n"
         "  a push [msg]        Commit and push\n"
         "  a pull              Sync with remote\n"
         "  a diff              Show changes\n"
         "  a revert            Select commit to revert to\n\n"
         "REMOTE\n"
         "  a ssh               List hosts\n"
         "  a ssh <#>           Connect to host\n"
         "  a run <#> \"task\"    Run task on remote\n\n"
         "OTHER\n"
         "  a jobs              Active sessions\n"
         "  a ls                List tmux sessions\n"
         "  a attach            Reconnect to session\n"
         "  a kill              Kill all sessions\n"
         "  a n \"text\"          Quick note\n"
         "  a log               View agent logs\n"
         "  a config            View/set settings\n"
         "  a update [shell|cache]  Update a (or just shell/cache)\n"
         "  a mono              Generate monolith for reading\n\n"
         "EXPERIMENTAL\n"
         "  a agent \"task\"      Spawn autonomous subagent\n"
         "  a hub               Scheduled jobs (systemd)\n"
         "  a all               Multi-agent parallel runs\n"
         "  a tree              Create git worktree\n"
         "  a gdrive            Cloud sync (Google Drive)");
    proj_t P[64]; app_t A[32];
    int np = load_projects(P, 64), na = load_apps(A, 32);
    refresh_cache(P, np, A, na);
    if (np) { puts("PROJECTS:"); for (int i = 0; i < np; i++) printf("  %d. %c %s\n", i, isdir(P[i].path)?'+':'x', P[i].path); }
    if (na) { puts("COMMANDS:"); for (int i = 0; i < na; i++) printf("  %d. %s -> %.60s\n", np+i, A[i].name, A[i].command); }
    return 0;
}

/* --- done --- */
static int cmd_done(void) {
    char p[700]; snprintf(p, sizeof(p), "%s/.done", DATA);
    int fd = open(p, O_WRONLY|O_CREAT|O_TRUNC, 0644); if (fd >= 0) close(fd);
    puts("\xe2\x9c\x93 done"); return 0;
}

/* --- hi --- */
static int cmd_hi(void) { for (int i = 1; i <= 10; i++) printf("%d\n", i); puts("hi"); return 0; }

/* --- dir --- */
static int cmd_dir(void) {
    char cwd[1024]; if (getcwd(cwd, sizeof(cwd))) puts(cwd);
    execlp("ls", "ls", NULL); return 1;
}

/* --- set --- */
static int cmd_set(int argc, char **argv) {
    char *f = argc > 2 ? argv[2] : NULL;
    char *v = argc > 3 ? argv[3] : NULL;
    if (!f) {
        char p[700]; snprintf(p, sizeof(p), "%s/n", DATA);
        printf("1. n [%s] commands without aio prefix\n   a set n %s\n", fexists(p)?"on":"off", fexists(p)?"off":"on");
        return 0;
    }
    char p[700]; snprintf(p, sizeof(p), "%s/%s", DATA, f);
    if (v && !strcmp(v, "on")) { int fd = open(p, O_WRONLY|O_CREAT|O_TRUNC, 0644); if (fd>=0) close(fd); puts("\xe2\x9c\x93 on"); }
    else if (v && !strcmp(v, "off")) { unlink(p); puts("\xe2\x9c\x93 off"); }
    else puts(fexists(p) ? "on" : "off");
    return 0;
}

/* --- config --- */
static int cmd_config(int argc, char **argv) {
    sqlite3 *db = dbopen(); if (!db) return 1;
    char *key = argc > 2 ? argv[2] : NULL;
    char *val = NULL; char vbuf[2048] = {0};
    if (argc > 3) { for (int i = 3; i < argc; i++) { if (i > 3) strcat(vbuf, " "); strncat(vbuf, argv[i], sizeof(vbuf)-strlen(vbuf)-2); } val = vbuf; }
    if (!key) {
        sqlite3_stmt *s;
        sqlite3_prepare_v2(db, "SELECT key, value FROM config ORDER BY key", -1, &s, NULL);
        while (sqlite3_step(s) == SQLITE_ROW) {
            const char *k = (const char*)sqlite3_column_text(s, 0);
            const char *v2 = (const char*)sqlite3_column_text(s, 1);
            printf("  %s: %.50s%s\n", k, v2, strlen(v2) > 50 ? "..." : "");
        }
        sqlite3_finalize(s);
    } else if (val) {
        if (!strcmp(val,"off")||!strcmp(val,"none")||!strcmp(val,"\"\"")||!strcmp(val,"''")) val = "";
        db_set(db, key, val);
        proj_t P[64]; app_t A[32]; int np = load_projects(P, 64), na = load_apps(A, 32); refresh_cache(P, np, A, na);
        printf("\xe2\x9c\x93 %s=%s\n", key, val[0] ? val : "(cleared)");
    } else {
        char *v2 = db_get(db, key);
        printf("%s: %s\n", key, v2 ? v2 : "(not set)");
        free(v2);
    }
    sqlite3_close(db); return 0;
}

/* --- prompt --- */
static int cmd_prompt(int argc, char **argv) {
    sqlite3 *db = dbopen(); if (!db) return 1;
    if (argc > 2) {
        char vbuf[2048] = {0};
        for (int i = 2; i < argc; i++) { if (i > 2) strcat(vbuf, " "); strncat(vbuf, argv[i], sizeof(vbuf)-strlen(vbuf)-2); }
        char *val = vbuf;
        if (!strcmp(val,"off")||!strcmp(val,"none")||!strcmp(val,"\"\"")||!strcmp(val,"''")) val = "";
        db_set(db, "default_prompt", val);
        proj_t P[64]; app_t A[32]; int np = load_projects(P, 64), na = load_apps(A, 32); refresh_cache(P, np, A, na);
        printf("\xe2\x9c\x93 %s\n", val[0] ? val : "(cleared)");
    } else {
        char *cur = db_get(db, "default_prompt");
        printf("Current: %s\n", cur && cur[0] ? cur : "(none)");
        printf("New: "); char line[1024]; if (fgets(line, sizeof(line), stdin)) {
            chomp(line); if (line[0]) { db_set(db, "default_prompt", line); printf("\xe2\x9c\x93 %s\n", line); }
        }
        free(cur);
    }
    sqlite3_close(db); return 0;
}

/* --- ls --- */
static int cmd_ls(int argc, char **argv) {
    if (argc > 2 && argv[2][0] >= '0' && argv[2][0] <= '9') {
        /* attach by index */
        char buf[4096]; run_cap("tmux list-sessions -F '#{session_name}' 2>/dev/null", buf, sizeof(buf));
        char *sessions[64]; int n = 0;
        for (char *l = strtok(buf, "\n"); l && n < 64; l = strtok(NULL, "\n")) if (l[0]) sessions[n++] = l;
        int idx = atoi(argv[2]);
        if (idx < n) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "tmux %s -t '%s'", getenv("TMUX") ? "switch-client" : "attach", sessions[idx]);
            execlp("sh", "sh", "-c", cmd, NULL);
        }
        return 0;
    }
    char buf[4096]; int rc = run_cap("tmux list-sessions -F '#{session_name}' 2>/dev/null", buf, sizeof(buf));
    if (rc || !buf[0]) { puts("No sessions"); return 0; }
    char *sessions[64]; int n = 0;
    for (char *l = strtok(buf, "\n"); l && n < 64; l = strtok(NULL, "\n")) if (l[0]) sessions[n++] = l;
    if (!n) { puts("No sessions"); return 0; }
    for (int i = 0; i < n; i++) {
        char cmd[512], path[512];
        snprintf(cmd, sizeof(cmd), "tmux display-message -p -t '%s' '#{pane_current_path}' 2>/dev/null", sessions[i]);
        run_cap(cmd, path, sizeof(path)); chomp(path);
        printf("  %d  %s: %s\n", i, sessions[i], path);
    }
    puts("\nSelect:\n  a ls 0");
    return 0;
}

/* --- kill --- */
static int cmd_kill(int argc, char **argv) {
    char *sel = argc > 2 ? argv[2] : NULL;
    if ((sel && !strcmp(sel, "all")) || !strcmp(argv[1], "killall")) {
        system("pkill -9 -f tmux 2>/dev/null"); system("clear"); puts("\xe2\x9c\x93"); return 0;
    }
    char buf[4096]; run_cap("tmux list-sessions -F '#{session_name}' 2>/dev/null", buf, sizeof(buf));
    char *sessions[64]; int n = 0;
    for (char *l = strtok(buf, "\n"); l && n < 64; l = strtok(NULL, "\n")) if (l[0]) sessions[n++] = l;
    if (!n) { puts("No sessions"); return 0; }
    if (sel && sel[0] >= '0' && sel[0] <= '9') {
        int idx = atoi(sel);
        if (idx < n) { char cmd[256]; snprintf(cmd, sizeof(cmd), "tmux kill-session -t '%s'", sessions[idx]); system(cmd); printf("\xe2\x9c\x93 %s\n", sessions[idx]); }
        return 0;
    }
    for (int i = 0; i < n; i++) printf("  %d  %s\n", i, sessions[i]);
    puts("\nSelect:\n  a kill 0\n  a kill all");
    return 0;
}

/* --- copy --- */
static int cmd_copy(void) {
    if (!getenv("TMUX")) { puts("x Not in tmux"); return 1; }
    /* Detect clipboard command */
    const char *clip = NULL;
    if (getenv("TERMUX_VERSION")) clip = "termux-clipboard-set";
    else if (access("/usr/bin/wl-copy", X_OK) == 0) clip = "wl-copy";
    else if (access("/usr/bin/xclip", X_OK) == 0) clip = "xclip -selection clipboard -i";
    if (!clip) { puts("x No clipboard"); return 1; }
    char buf[32768]; run_cap("tmux capture-pane -pJ -S -99", buf, sizeof(buf));
    /* Find last two prompts (lines with $ and @) */
    char *lines[512]; int nl = 0;
    char *copy = strdup(buf);
    for (char *l = strtok(copy, "\n"); l && nl < 512; l = strtok(NULL, "\n")) lines[nl++] = l;
    int prompts[64], np = 0;
    for (int i = 0; i < nl; i++) if (strchr(lines[i], '$') && strchr(lines[i], '@')) prompts[np++] = i;
    int u = nl, p = -1;
    for (int i = np-1; i >= 0; i--) if (strstr(lines[prompts[i]], "copy")) { u = prompts[i]; break; }
    for (int i = np-1; i >= 0; i--) if (prompts[i] < u) { p = prompts[i]; break; }
    char output[32768] = {0}; int off = 0;
    for (int i = p+1; i < u; i++) { if (off) { output[off++] = '\n'; } int l = strlen(lines[i]); memcpy(output+off, lines[i], l); off += l; }
    output[off] = '\0';
    char cmd[256]; snprintf(cmd, sizeof(cmd), "%s", clip);
    FILE *fp = popen(cmd, "w"); if (fp) { fputs(output, fp); pclose(fp); }
    /* Truncate display */
    char flat[256]; int fl = 0;
    for (int i = 0; output[i] && fl < 250; i++) flat[fl++] = output[i] == '\n' ? ' ' : output[i];
    flat[fl] = '\0';
    if (fl > 50) printf("\xe2\x9c\x93 %.23s...%.24s\n", flat, flat+fl-24);
    else printf("\xe2\x9c\x93 %s\n", flat);
    free(copy); return 0;
}

/* --- push --- */
static int cmd_push(int argc, char **argv) {
    char cwd[1024]; getcwd(cwd, sizeof(cwd));
    char msg[1024] = {0};
    if (argc > 2) { for (int i = 2; i < argc; i++) { if (i > 2) strcat(msg, " "); strncat(msg, argv[i], sizeof(msg)-strlen(msg)-2); } }
    else { char *b = strrchr(cwd, '/'); snprintf(msg, sizeof(msg), "Update %s", b ? b+1 : cwd); }

    /* Multi-repo push if not a git repo */
    char gdir[1200]; snprintf(gdir, sizeof(gdir), "%s/.git", cwd);
    if (!isdir(gdir)) {
        /* Check for sub-repos */
        DIR *d = opendir(cwd); struct dirent *e; int subs = 0;
        char subd[64][256]; if (d) { while ((e = readdir(d)) && subs < 64) { if (e->d_name[0] == '.') continue; char sub[1300]; snprintf(sub, sizeof(sub), "%s/%s/.git", cwd, e->d_name); if (isdir(sub)) snprintf(subd[subs++], 256, "%s", e->d_name); } closedir(d); }
        if (subs) {
            printf("Push %d repos? [y/n]: ", subs); char c; if (scanf(" %c", &c) != 1 || (c != 'y' && c != 'Y')) return 0;
            for (int i = 0; i < subs; i++) {
                char cmd[2048]; snprintf(cmd, sizeof(cmd), "cd '%s/%s' && git add -A && git commit -m '%s' --allow-empty 2>/dev/null && git push -u origin HEAD 2>&1", cwd, subd[i], msg);
                printf("%s: ", subd[i]); fflush(stdout); system(cmd); puts("");
            }
            return 0;
        }
        fallback(argc, argv);
    }

    char ok[700]; snprintf(ok, sizeof(ok), "%s/logs/push.ok", DATA);
    { char ld[700]; snprintf(ld, sizeof(ld), "%s/logs", DATA); mkdir(ld, 0755); }

    char chk[128]; run_cap("git status --porcelain 2>/dev/null", chk, sizeof(chk));
    const char *tag = chk[0] ? "\xe2\x9c\x93" : "\xe2\x97\x8b";

    /* Instant mode if recent success */
    struct stat st;
    if (stat(ok, &st) == 0 && time(NULL) - st.st_mtime < 600) {
        char cmd[2048]; snprintf(cmd, sizeof(cmd), "cd '%s' && git add -A && git commit -m '%s' --allow-empty 2>/dev/null; git push 2>/dev/null; touch '%s'", cwd, msg, ok);
        pid_t pid = fork();
        if (pid == 0) { setsid(); int null = open("/dev/null", O_RDWR); dup2(null,0); dup2(null,1); dup2(null,2); if (null>2) close(null); execl("/bin/sh","sh","-c",cmd,NULL); _exit(1); }
        printf("%s %s\n", tag, msg); return 0;
    }

    /* Real push */
    char remote_chk[128]; run_cap("git config remote.origin.url 2>/dev/null", remote_chk, sizeof(remote_chk));
    if (!remote_chk[0]) system("gh repo create --private --source . --push 2>/dev/null");
    system("git add -A");
    char commit[1200]; snprintf(commit, sizeof(commit), "git commit -m '%s' --allow-empty 2>/dev/null", msg); system(commit);
    char pushbuf[4096]; int rc = run_cap("git push -u origin HEAD 2>&1", pushbuf, sizeof(pushbuf));
    if (rc == 0 || strstr(pushbuf, "up-to-date")) {
        int fd = open(ok, O_WRONLY|O_CREAT|O_TRUNC, 0644); if (fd >= 0) close(fd);
        printf("%s %s\n", tag, msg);
    } else { chomp(pushbuf); printf("\xe2\x9c\x97 %s\n", pushbuf); }
    return 0;
}

/* --- pull --- */
static int cmd_pull(int argc, char **argv) {
    char chk[64]; if (run_cap("git rev-parse --git-dir 2>/dev/null", chk, sizeof(chk))) { puts("x Not a git repo"); return 1; }
    system("git fetch origin 2>/dev/null");
    char ref[64] = "origin/main";
    { char t[64]; if (run_cap("git rev-parse --verify origin/main 2>/dev/null", t, sizeof(t))) strcpy(ref, "origin/master"); }
    char info[256]; { char cmd[128]; snprintf(cmd, sizeof(cmd), "git log -1 --format='%%h %%s' %s 2>/dev/null", ref); run_cap(cmd, info, sizeof(info)); chomp(info); }
    printf("! DELETE local changes -> %s\n", info);
    int yes = 0; for (int i = 2; i < argc; i++) if (!strcmp(argv[i],"--yes")||!strcmp(argv[i],"-y")) yes = 1;
    if (!yes) { printf("Continue? (y/n): "); char c; if (scanf(" %c", &c) != 1 || (c != 'y' && c != 'Y')) { puts("x Cancelled"); return 1; } }
    char cmd[256]; snprintf(cmd, sizeof(cmd), "git reset --hard %s 2>/dev/null && git clean -f -d 2>/dev/null", ref); system(cmd);
    printf("\xe2\x9c\x93 Synced: %s\n", info); return 0;
}

/* --- diff --- */
static int cmd_diff(int argc, char **argv) {
    char *sel = argc > 2 ? argv[2] : NULL;
    /* Token history mode */
    if (sel && sel[0] >= '0' && sel[0] <= '9') {
        int n = atoi(sel); char cmd[256]; snprintf(cmd, sizeof(cmd), "git log -%d --pretty=%%H\\ %%s", n);
        FILE *fp = popen(cmd, "r"); if (!fp) return 1;
        char line[512]; int total = 0, i = 0;
        while (fgets(line, sizeof(line), fp)) {
            chomp(line); char *sp = strchr(line, ' '); if (!sp) continue;
            *sp = '\0'; char *hash = line, *m = sp + 1;
            char dcmd[256]; snprintf(dcmd, sizeof(dcmd), "git show %.40s --pretty=", hash);
            FILE *dp = popen(dcmd, "r"); int ab = 0, db2 = 0;
            if (dp) { char dl[4096]; while (fgets(dl, sizeof(dl), dp)) { int l = strlen(dl); if (dl[0]=='+' && dl[1]!='+') ab += l-1; else if (dl[0]=='-' && dl[1]!='-') db2 += l-1; } pclose(dp); }
            int tok = (ab - db2) / 4; total += tok;
            if (strlen(m) > 55) { m[52] = '.'; m[53] = '.'; m[54] = '.'; m[55] = '\0'; }
            printf("  %d  %+6d  %s\n", i++, tok, m);
        }
        pclose(fp); printf("\nTotal: %+d tokens\n", total); return 0;
    }
    /* Full diff mode - delegate to python for color output */
    fallback(argc, argv);
}

/* --- web --- */
static int cmd_web(int argc, char **argv) {
    char url[2048] = "https://google.com";
    if (argc > 2) {
        strcpy(url, "https://google.com/search?q=");
        for (int i = 2; i < argc; i++) { if (i > 2) strcat(url, "+"); strncat(url, argv[i], sizeof(url)-strlen(url)-2); }
    }
    pid_t pid = fork();
    if (pid == 0) { int null = open("/dev/null", O_RDWR); dup2(null,1); dup2(null,2); execlp("xdg-open","xdg-open",url,NULL); _exit(1); }
    return 0;
}

/* --- install --- */
static int cmd_install(void) {
    char p[700]; snprintf(p, sizeof(p), "%s/../install.sh", SCRIPT);
    if (fexists(p)) execl("/bin/bash", "bash", p, NULL);
    else execl("/bin/bash", "bash", "-c", "curl -fsSL https://raw.githubusercontent.com/seanpattencode/aio/main/install.sh | bash", NULL);
    return 1;
}

/* --- uninstall --- */
static int cmd_uninstall(void) {
    printf("Uninstall? (y/n): "); char c; if (scanf(" %c", &c) != 1 || (c != 'y' && c != 'Y')) return 0;
    char p[700];
    snprintf(p, sizeof(p), "%s/.local/bin/aio", HOME); unlink(p);
    snprintf(p, sizeof(p), "%s/.local/bin/aioUI.py", HOME); unlink(p);
    puts("\xe2\x9c\x93 Uninstalled"); return 0;
}

/* --- repo --- */
static int cmd_repo(int argc, char **argv) {
    if (argc < 3) { puts("Usage: a repo <name>"); return 1; }
    mkdir(argv[2], 0755); chdir(argv[2]);
    system("git init -q");
    char cmd[512]; snprintf(cmd, sizeof(cmd), "gh repo create '%s' --public --source=.", argv[2]); system(cmd);
    return 0;
}

/* --- dash --- */
static int cmd_dash(void) {
    char cwd[1024]; getcwd(cwd, sizeof(cwd));
    char chk[64]; if (run_cap("tmux has-session -t dash 2>&1", chk, sizeof(chk))) {
        char cmd[1200]; snprintf(cmd, sizeof(cmd), "tmux new-session -d -s dash -c '%s' && tmux split-window -h -t dash -c '%s' 'sh -c \"a jobs; exec $SHELL\"'", cwd, cwd); system(cmd);
    }
    char cmd[128]; snprintf(cmd, sizeof(cmd), "tmux %s -t dash", getenv("TMUX") ? "switch-client" : "attach");
    execlp("sh", "sh", "-c", cmd, NULL); return 1;
}

/* --- rebuild / backup (placeholder) --- */
static int cmd_rebuild(void) { puts("rebuild: sync system removed, rewrite pending"); return 0; }
static int cmd_backup(void) { puts("backup: sync system removed, rewrite pending"); return 0; }

/* --- project_num --- */
static int cmd_project_num(int argc, char **argv, int idx) {
    proj_t P[64]; app_t A[32];
    int np = load_projects(P, 64), na = load_apps(A, 32);
    if (idx >= 0 && idx < np) {
        if (!isdir(P[idx].path) && P[idx].repo[0]) {
            /* Clone */
            char parent[512]; snprintf(parent, sizeof(parent), "%s", P[idx].path);
            char *sl = strrchr(parent, '/'); if (sl) { *sl = '\0'; char cmd[600]; snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", parent); system(cmd); }
            printf("Cloning %s...\n", P[idx].repo);
            char cmd[1024]; snprintf(cmd, sizeof(cmd), "git clone '%s' '%s'", P[idx].repo, P[idx].path);
            if (system(cmd)) return 1;
        }
        if (!isdir(P[idx].path)) { printf("x %s\n", P[idx].path); return 1; }
        printf("Opening project %d: %s\n", idx, P[idx].path);
        /* Background check for push.ok */
        char ok[700]; snprintf(ok, sizeof(ok), "%s/logs/push.ok", DATA);
        pid_t pid = fork();
        if (pid == 0) {
            setsid(); int null = open("/dev/null", O_RDWR); dup2(null,0); dup2(null,1); dup2(null,2);
            char cmd[1024]; snprintf(cmd, sizeof(cmd), "git -C '%s' ls-remote --exit-code origin HEAD>/dev/null 2>&1 && touch '%s'", P[idx].path, ok);
            execl("/bin/sh", "sh", "-c", cmd, NULL); _exit(1);
        }
        chdir(P[idx].path);
        char *sh = getenv("SHELL"); if (!sh) sh = "/bin/bash";
        execvp(sh, (char*[]){sh, NULL});
    } else if (idx >= np && idx < np + na) {
        int ai = idx - np;
        printf("> Running: %s\n   Command: %.60s\n", A[ai].name, A[ai].command);
        char *sh = getenv("SHELL"); if (!sh) sh = "/bin/bash";
        execvp(sh, (char*[]){sh, "-c", A[ai].command, NULL});
    } else { printf("x Invalid index: %d\n", idx); return 1; }
    return 0;
}

/* --- dir_file --- */
static int cmd_dir_file(int argc, char **argv) {
    char *arg = argv[1];
    char expanded[1024];
    if (arg[0] == '~') snprintf(expanded, sizeof(expanded), "%s%s", HOME, arg+1);
    else if (!strncmp(arg, "/projects/", 10)) snprintf(expanded, sizeof(expanded), "%s%s", HOME, arg);
    else snprintf(expanded, sizeof(expanded), "%s", arg);
    if (isdir(expanded)) { puts(expanded); execlp("ls", "ls", expanded, NULL); }
    else if (isfile(expanded)) {
        int l = strlen(expanded);
        if (l > 3 && !strcmp(expanded+l-3, ".py")) { execvp("python3", (char*[]){"python3", expanded, NULL}); }
        else if (l > 3 && !strcmp(expanded+l-3, ".md")) { char *ed = getenv("EDITOR"); if (!ed) ed = "e"; execvp(ed, (char*[]){ed, expanded, NULL}); }
    }
    return 0;
}

/* --- send --- */
static int cmd_send(int argc, char **argv) {
    if (argc < 4) { puts("Usage: a send <session> <prompt> [--wait] [--no-enter]"); return 1; }
    char *sess = argv[2];
    int wait = 0, enter = 1;
    char prompt[4096] = {0};
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--wait")) { wait = 1; continue; }
        if (!strcmp(argv[i], "--no-enter")) { enter = 0; continue; }
        if (prompt[0]) strcat(prompt, " "); strncat(prompt, argv[i], sizeof(prompt)-strlen(prompt)-2);
    }
    /* Send keys */
    char cmd[8192]; snprintf(cmd, sizeof(cmd), "tmux send-keys -l -t '%s' '%s'", sess, prompt);
    system(cmd);
    if (enter) { usleep(100000); char ecmd[256]; snprintf(ecmd, sizeof(ecmd), "tmux send-keys -t '%s' Enter", sess); system(ecmd); printf("\xe2\x9c\x93 Sent to '%s'\n", sess); }
    else printf("\xe2\x9c\x93 Inserted into '%s'\n", sess);
    if (wait) {
        printf("Waiting..."); fflush(stdout); time_t last = time(NULL);
        while (1) {
            char act[64], acmd[256]; snprintf(acmd, sizeof(acmd), "tmux display-message -p -t '%s' '#{window_activity}' 2>/dev/null", sess);
            run_cap(acmd, act, sizeof(act)); chomp(act);
            if (act[0]) { time_t a = atol(act); if (time(NULL) - a < 2) { last = time(NULL); printf("."); fflush(stdout); }
            else if (time(NULL) - last > 3) { puts("\n+ Done"); break; } }
            usleep(500000);
        }
    }
    return 0;
}

/* --- watch --- */
static int cmd_watch(int argc, char **argv) {
    if (argc < 3) { puts("Usage: a watch <session> [duration]"); return 1; }
    char *sess = argv[2]; int dur = argc > 3 ? atoi(argv[3]) : 0;
    printf("Watching '%s'%s\n", sess, dur ? "" : " (once)");
    time_t start = time(NULL); char last[32768] = {0};
    struct { const char *pat; const char *resp; } patterns[] = {
        {"Are you sure?", "y"}, {"Continue?", "yes"}, {"[y/N]", "y"}, {"[Y/n]", "y"}, {NULL, NULL}
    };
    while (1) {
        if (dur && time(NULL) - start > dur) break;
        char buf[32768], cmd[256]; snprintf(cmd, sizeof(cmd), "tmux capture-pane -t '%s' -p 2>/dev/null", sess);
        if (run_cap(cmd, buf, sizeof(buf))) { printf("x Session %s not found\n", sess); return 1; }
        if (strcmp(buf, last)) {
            for (int i = 0; patterns[i].pat; i++) {
                if (strstr(buf, patterns[i].pat)) {
                    char rcmd[512]; snprintf(rcmd, sizeof(rcmd), "tmux send-keys -t '%s' '%s' Enter", sess, patterns[i].resp);
                    system(rcmd); puts("\xe2\x9c\x93 Auto-responded"); break;
                }
            }
            strcpy(last, buf);
        }
        usleep(100000);
    }
    return 0;
}

/* --- revert --- */
static int cmd_revert(void) {
    char chk[64]; if (run_cap("git rev-parse --git-dir 2>/dev/null", chk, sizeof(chk))) { puts("x Not a git repo"); return 1; }
    char buf[4096]; run_cap("git log --format='%h %ad %s' --date=format:'%m/%d %H:%M' -15", buf, sizeof(buf));
    char *lines[16]; int n = 0;
    for (char *l = strtok(buf, "\n"); l && n < 15; l = strtok(NULL, "\n")) { lines[n] = strdup(l); printf("  %d. %s\n", n, l); n++; }
    printf("\nRevert to #/q: "); char in[32]; if (!fgets(in, sizeof(in), stdin)) return 0; chomp(in);
    if (!in[0] || in[0] == 'q') return 0;
    int idx = atoi(in); if (idx >= n) { puts("x Invalid"); return 1; }
    char hash[16]; sscanf(lines[idx], "%12s", hash);
    char cmd[256]; snprintf(cmd, sizeof(cmd), "git revert --no-commit %s..HEAD && git commit -m 'revert to %s'", hash, hash);
    if (system(cmd)) { puts("x Failed"); return 1; }
    printf("\xe2\x9c\x93 Reverted to %s\n", hash);
    printf("Push? (y/n): "); char c; if (scanf(" %c", &c) == 1 && (c == 'y' || c == 'Y')) { system("git push"); puts("\xe2\x9c\x93 Pushed"); }
    for (int i = 0; i < n; i++) free(lines[i]);
    return 0;
}

/* --- attach --- */
static int cmd_attach(int argc, char **argv) {
    char *sel = argc > 2 ? argv[2] : NULL;
    sqlite3 *db = dbopen(); if (!db) return 1;
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db, "SELECT id, repo FROM multi_runs ORDER BY created_at DESC LIMIT 10", -1, &s, NULL);
    char ids[10][64], repos[10][256]; int n = 0;
    while (sqlite3_step(s) == SQLITE_ROW && n < 10) {
        snprintf(ids[n], 64, "%s", (const char*)sqlite3_column_text(s, 0));
        snprintf(repos[n], 256, "%s", (const char*)sqlite3_column_text(s, 1));
        n++;
    }
    sqlite3_finalize(s); sqlite3_close(db);
    if (!n) { puts("No sessions"); return 0; }
    if (sel && sel[0] >= '0' && sel[0] <= '9') {
        int idx = atoi(sel); if (idx < n) {
            char *bn = strrchr(repos[idx], '/'); bn = bn ? bn+1 : repos[idx];
            char sn[256]; snprintf(sn, sizeof(sn), "%s-%s", bn, ids[idx]);
            char cmd[300]; snprintf(cmd, sizeof(cmd), "tmux %s -t '%s'", getenv("TMUX")?"switch-client":"attach", sn);
            execlp("sh","sh","-c",cmd,NULL);
        }
    }
    for (int i = 0; i < n; i++) {
        char *bn = strrchr(repos[i], '/'); bn = bn ? bn+1 : repos[i];
        char sn[256]; snprintf(sn, sizeof(sn), "%s-%s", bn, ids[i]);
        char chk[64], cmd[300]; snprintf(cmd, sizeof(cmd), "tmux has-session -t '%s' 2>/dev/null", sn);
        int active = !run_cap(cmd, chk, sizeof(chk));
        printf("  %d  %s %s\n", i, active ? "\xe2\x97\x8f" : "\xe2\x97\x8b", sn);
    }
    puts("\nSelect:\n  a attach 0");
    return 0;
}

/* --- tree --- */
static int cmd_tree(int argc, char **argv) {
    char chk[64]; if (run_cap("git rev-parse --git-dir 2>/dev/null", chk, sizeof(chk))) { puts("x Not a git repo"); return 1; }
    char cwd[1024]; getcwd(cwd, sizeof(cwd));
    char *proj = cwd;
    if (argc > 2 && argv[2][0] >= '0' && argv[2][0] <= '9') {
        proj_t P[64]; int np = load_projects(P, 64); int idx = atoi(argv[2]);
        if (idx < np) proj = P[idx].path;
    }
    /* Get worktrees_dir from config */
    sqlite3 *db = dbopen(); char *wt_dir = db ? db_get(db, "worktrees_dir") : NULL;
    if (db) sqlite3_close(db);
    if (!wt_dir) { wt_dir = malloc(512); snprintf(wt_dir, 512, "%s/projects/aWorktrees", HOME); }
    mkdir(wt_dir, 0755);
    char ts[32]; time_t t = time(NULL); strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", localtime(&t));
    char *bn = strrchr(proj, '/'); bn = bn ? bn+1 : proj;
    char wt[1024], branch[256];
    snprintf(wt, sizeof(wt), "%s/%s-%s", wt_dir, bn, ts);
    snprintf(branch, sizeof(branch), "wt-%s-%s", bn, ts);
    char cmd[2048]; snprintf(cmd, sizeof(cmd), "git -C '%s' worktree add -b '%s' '%s' HEAD", proj, branch, wt);
    if (system(cmd)) { puts("x Failed"); free(wt_dir); return 1; }
    printf("\xe2\x9c\x93 %s\n", wt);
    chdir(wt); char *sh = getenv("SHELL"); if (!sh) sh = "/bin/bash";
    free(wt_dir); execvp(sh, (char*[]){sh, NULL});
    return 1;
}

/* --- move --- */
static int cmd_move(int argc, char **argv) {
    if (argc < 4 || !(argv[2][0]>='0'&&argv[2][0]<='9') || !(argv[3][0]>='0'&&argv[3][0]<='9')) { puts("Usage: a move <from> <to>"); return 1; }
    /* Move is complex with workspace files - delegate to python for now */
    fallback(argc, argv);
}

/* --- jobs --- */
static int cmd_jobs(int argc, char **argv) {
    /* Complex tmux + worktree queries, delegate for now */
    fallback(argc, argv);
}

/* --- cleanup --- */
static int cmd_cleanup(int argc, char **argv) {
    sqlite3 *db = dbopen(); if (!db) return 1;
    char *wt_dir_s = db_get(db, "worktrees_dir");
    if (!wt_dir_s) { wt_dir_s = malloc(512); snprintf(wt_dir_s, 512, "%s/projects/aWorktrees", HOME); }
    /* Count items */
    int nwt = 0; DIR *d = opendir(wt_dir_s); struct dirent *e;
    if (d) { while ((e = readdir(d))) { if (e->d_name[0] != '.' && e->d_type == DT_DIR) nwt++; } closedir(d); }
    sqlite3_stmt *s; int cnt = 0;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM multi_runs", -1, &s, NULL);
    if (sqlite3_step(s) == SQLITE_ROW) cnt = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    if (!nwt && !cnt) { puts("Nothing to clean"); sqlite3_close(db); free(wt_dir_s); return 0; }
    printf("Will delete: %d dirs, %d db entries\n", nwt, cnt);
    int yes = 0; for (int i = 2; i < argc; i++) if (!strcmp(argv[i],"--yes")||!strcmp(argv[i],"-y")) yes = 1;
    if (!yes) { printf("Continue? (y/n): "); char c; if (scanf(" %c", &c) != 1 || (c!='y'&&c!='Y')) { puts("x"); sqlite3_close(db); free(wt_dir_s); return 1; } }
    /* rm worktrees */
    d = opendir(wt_dir_s);
    if (d) { while ((e = readdir(d))) { if (e->d_name[0] == '.' || e->d_type != DT_DIR) continue;
        char p[1024]; snprintf(p, sizeof(p), "rm -rf '%s/%s'", wt_dir_s, e->d_name); system(p);
        printf("\xe2\x9c\x93 %s\n", e->d_name); } closedir(d); }
    /* Prune git worktrees */
    proj_t P[64]; int np = load_projects(P, 64);
    for (int i = 0; i < np; i++) if (isdir(P[i].path)) { char cmd[600]; snprintf(cmd, sizeof(cmd), "git -C '%s' worktree prune 2>/dev/null", P[i].path); system(cmd); }
    sqlite3_exec(db, "DELETE FROM multi_runs", NULL, NULL, NULL);
    sqlite3_close(db); free(wt_dir_s); puts("\xe2\x9c\x93 Cleaned"); return 0;
}

/* --- log --- */
static int cmd_log(int argc, char **argv) {
    /* Complex with rclone + cloud sync - delegate to python */
    fallback(argc, argv);
}

/* --- update --- */
static int cmd_update(int argc, char **argv) {
    char *sub = argc > 2 ? argv[2] : NULL;
    if (sub && (!strcmp(sub,"help")||!strcmp(sub,"-h")||!strcmp(sub,"--help"))) {
        puts("a update - Update a from git + refresh caches\n  a update        Pull latest\n  a update shell  Refresh shell config\n  a update cache  Refresh caches only"); return 0;
    }
    if (sub && !strcmp(sub, "cache")) {
        proj_t P[64]; app_t A[32]; int np = load_projects(P,64), na = load_apps(A,32);
        refresh_cache(P, np, A, na); puts("\xe2\x9c\x93 Cache"); return 0;
    }
    /* For full update, delegate to python (needs shell refresh, git pull, etc) */
    fallback(argc, argv);
}

/* --- note --- */
static int cmd_note(int argc, char **argv) {
    /* Complex interactive + sync - delegate to python */
    fallback(argc, argv);
}

/* ═══════════════════════════════════════════
 * DISPATCHER
 * ═══════════════════════════════════════════ */
int main(int argc, char **argv) {
    init_paths();
    mkdir(DATA, 0755);

    if (argc < 2) return cmd_help();
    const char *arg = argv[1];

    /* Numeric = project number */
    { const char *p = arg; while (*p >= '0' && *p <= '9') p++;
      if (*p == '\0' && p != arg) return cmd_project_num(argc, argv, atoi(arg)); }

    /* Switch dispatch on first char */
    switch (arg[0]) {
    case 'a':
        if (!strcmp(arg,"attach") || !strcmp(arg,"att")) return cmd_attach(argc, argv);
        if (!strcmp(arg,"add")) fallback(argc, argv);
        if (!strcmp(arg,"agent")) fallback(argc, argv);
        if (!strcmp(arg,"all") || !strcmp(arg,"ai") || !strcmp(arg,"aio") || !strcmp(arg,"a")) fallback(argc, argv);
        if (!strcmp(arg,"ask")) fallback(argc, argv);
        break;
    case 'b':
        if (!strcmp(arg,"backup") || !strcmp(arg,"bak")) return cmd_backup();
        break;
    case 'c':
        if (!strcmp(arg,"config") || !strcmp(arg,"con")) return cmd_config(argc, argv);
        if (!strcmp(arg,"cleanup") || !strcmp(arg,"cle")) return cmd_cleanup(argc, argv);
        if (!strcmp(arg,"copy") || !strcmp(arg,"cop")) return cmd_copy();
        break;
    case 'd':
        if (!strcmp(arg,"done")) return cmd_done();
        if (!strcmp(arg,"dir")) return cmd_dir();
        if (!strcmp(arg,"diff") || !strcmp(arg,"dif")) return cmd_diff(argc, argv);
        if (!strcmp(arg,"dash") || !strcmp(arg,"das")) return cmd_dash();
        if (!strcmp(arg,"deps") || !strcmp(arg,"dep")) fallback(argc, argv);
        if (!strcmp(arg,"docs") || !strcmp(arg,"doc")) fallback(argc, argv);
        break;
    case 'e':
        if (!strcmp(arg,"e")) fallback(argc, argv);
        break;
    case 'g':
        if (!strcmp(arg,"gdrive") || !strcmp(arg,"gdr")) fallback(argc, argv);
        break;
    case 'h':
        if (!strcmp(arg,"help") || !strcmp(arg,"hel")) return cmd_help_full();
        if (!strcmp(arg,"hi")) return cmd_hi();
        if (!strcmp(arg,"hub")) fallback(argc, argv);
        break;
    case 'i':
        if (!strcmp(arg,"install") || !strcmp(arg,"ins")) return cmd_install();
        if (!strcmp(arg,"i")) fallback(argc, argv);
        break;
    case 'j':
        if (!strcmp(arg,"jobs") || !strcmp(arg,"job")) return cmd_jobs(argc, argv);
        break;
    case 'k':
        if (!strcmp(arg,"kill") || !strcmp(arg,"kil") || !strcmp(arg,"killall")) return cmd_kill(argc, argv);
        break;
    case 'l':
        if (!strcmp(arg,"ls")) return cmd_ls(argc, argv);
        if (!strcmp(arg,"log") || !strcmp(arg,"logs")) return cmd_log(argc, argv);
        if (!strcmp(arg,"login")) fallback(argc, argv);
        break;
    case 'm':
        if (!strcmp(arg,"move") || !strcmp(arg,"mov")) return cmd_move(argc, argv);
        if (!strcmp(arg,"mono") || !strcmp(arg,"monolith")) fallback(argc, argv);
        break;
    case 'n':
        if (!strcmp(arg,"note") || !strcmp(arg,"n")) return cmd_note(argc, argv);
        break;
    case 'p':
        if (!strcmp(arg,"push") || !strcmp(arg,"pus") || !strcmp(arg,"p")) return cmd_push(argc, argv);
        if (!strcmp(arg,"pull") || !strcmp(arg,"pul")) return cmd_pull(argc, argv);
        if (!strcmp(arg,"prompt") || !strcmp(arg,"pro")) return cmd_prompt(argc, argv);
        break;
    case 'r':
        if (!strcmp(arg,"revert") || !strcmp(arg,"rev")) return cmd_revert();
        if (!strcmp(arg,"rebuild")) return cmd_rebuild();
        if (!strcmp(arg,"remove") || !strcmp(arg,"rem") || !strcmp(arg,"rm")) fallback(argc, argv);
        if (!strcmp(arg,"repo")) return cmd_repo(argc, argv);
        if (!strcmp(arg,"review")) fallback(argc, argv);
        if (!strcmp(arg,"run")) fallback(argc, argv);
        break;
    case 's':
        if (!strcmp(arg,"set") || !strcmp(arg,"settings")) return cmd_set(argc, argv);
        if (!strcmp(arg,"send") || !strcmp(arg,"sen")) return cmd_send(argc, argv);
        if (!strcmp(arg,"scan") || !strcmp(arg,"sca")) fallback(argc, argv);
        if (!strcmp(arg,"ssh")) fallback(argc, argv);
        if (!strcmp(arg,"sync") || !strcmp(arg,"syn")) fallback(argc, argv);
        break;
    case 't':
        if (!strcmp(arg,"tree") || !strcmp(arg,"tre")) return cmd_tree(argc, argv);
        if (!strcmp(arg,"task") || !strcmp(arg,"tas") || !strcmp(arg,"t")) fallback(argc, argv);
        break;
    case 'u':
        if (!strcmp(arg,"update") || !strcmp(arg,"upd")) return cmd_update(argc, argv);
        if (!strcmp(arg,"uninstall") || !strcmp(arg,"uni")) return cmd_uninstall();
        if (!strcmp(arg,"ui")) fallback(argc, argv);
        break;
    case 'w':
        if (!strcmp(arg,"watch") || !strcmp(arg,"wat")) return cmd_watch(argc, argv);
        if (!strcmp(arg,"web")) return cmd_web(argc, argv);
        break;
    case 'x':
        if (arg[1] == '.') fallback(argc, argv);
        if (!strcmp(arg,"x")) fallback(argc, argv);
        break;
    case '-':
        if (!strcmp(arg,"--help") || !strcmp(arg,"-h")) return cmd_help_full();
        break;
    }

    /* Directory/file handling */
    if (isdir(arg) || isfile(arg)) return cmd_dir_file(argc, argv);
    if (arg[0] == '/') { char exp[1024]; snprintf(exp, sizeof(exp), "%s%s", HOME, arg); if (isdir(exp)) return cmd_dir_file(argc, argv); }

    /* Worktree patterns */
    { size_t len = strlen(arg);
      if (len >= 3 && arg[len-1] == '+' && arg[len-2] == '+') fallback(argc, argv);
      if (arg[0] == 'w' && strcmp(arg,"watch") && strcmp(arg,"web")) fallback(argc, argv);
    }

    /* Short session keys (1-3 char) */
    if (strlen(arg) <= 3 && arg[0] >= 'a' && arg[0] <= 'z') fallback(argc, argv);

    /* Unknown - try as session */
    fallback(argc, argv);
}
