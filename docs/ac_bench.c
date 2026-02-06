/* Minimal benchmark: what costs time in ac startup on Termux? */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <sqlite3.h>

static double ms(struct timespec *a, struct timespec *b) {
    return (b->tv_sec - a->tv_sec) * 1000.0 + (b->tv_nsec - a->tv_nsec) / 1e6;
}

int main() {
    struct timespec t0, t1, t2, t3, t4, t5, t6, t7;
    char buf[4096];
    const char *h = getenv("HOME");

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* 1. readlink */
    char self[1024];
    readlink("/proc/self/exe", self, 1023);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    /* 2. fopen + read small file (device) */
    snprintf(buf, sizeof(buf), "%s/.local/share/a/.device", h);
    FILE *f = fopen(buf, "r"); if (f) { fgets(buf, 128, f); fclose(f); }
    clock_gettime(CLOCK_MONOTONIC, &t2);

    /* 3. catf help_cache.txt */
    snprintf(buf, sizeof(buf), "%s/.local/share/a/help_cache.txt", h);
    int fd = open(buf, O_RDONLY);
    if (fd >= 0) { char b[8192]; read(fd, b, sizeof(b)); close(fd); }
    clock_gettime(CLOCK_MONOTONIC, &t3);

    /* 4. sqlite3_open */
    sqlite3 *db;
    snprintf(buf, sizeof(buf), "%s/.local/share/a/aio.db", h);
    sqlite3_open(buf, &db);
    clock_gettime(CLOCK_MONOTONIC, &t4);

    /* 5. PRAGMA WAL */
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", 0, 0, 0);
    clock_gettime(CLOCK_MONOTONIC, &t5);

    /* 6. SELECT from config */
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db, "SELECT key,value FROM config", -1, &st, 0);
    while (sqlite3_step(st) == SQLITE_ROW) {}
    sqlite3_finalize(st);
    clock_gettime(CLOCK_MONOTONIC, &t6);

    /* 7. write to stdout */
    write(STDOUT_FILENO, "hi\n", 3);
    clock_gettime(CLOCK_MONOTONIC, &t7);

    sqlite3_close(db);

    fprintf(stderr, "readlink:     %.3f ms\n", ms(&t0, &t1));
    fprintf(stderr, "fopen device: %.3f ms\n", ms(&t1, &t2));
    fprintf(stderr, "read cache:   %.3f ms\n", ms(&t2, &t3));
    fprintf(stderr, "sqlite open:  %.3f ms\n", ms(&t3, &t4));
    fprintf(stderr, "PRAGMA WAL:   %.3f ms\n", ms(&t4, &t5));
    fprintf(stderr, "SELECT cfg:   %.3f ms\n", ms(&t5, &t6));
    fprintf(stderr, "write stdout: %.3f ms\n", ms(&t6, &t7));
    fprintf(stderr, "TOTAL:        %.3f ms\n", ms(&t0, &t7));
    return 0;
}
