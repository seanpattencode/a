#include <stdio.h>
#include <time.h>

#define ITERS 100000000
#define RUNS 30

int func_add(int a, int b) {
    return a + b;
}

int main(void) {
    struct timespec t0, t1;
    long long ns;
    volatile int result;

    long long func_times[RUNS];
    long long goto_times[RUNS];

    // run goto first, then func — alternate who goes first
    for (int r = 0; r < RUNS; r++) {
        if (r % 2 == 0) {
            // goto first
            result = 0;
            int i = 0;
            clock_gettime(CLOCK_MONOTONIC, &t0);
        loop_a:
            result = result + 1;
            i++;
            if (i < ITERS) goto loop_a;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            goto_times[r] = (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec);

            result = 0;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int j = 0; j < ITERS; j++) {
                result = func_add(result, 1);
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            func_times[r] = (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec);
        } else {
            // func first
            result = 0;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int j = 0; j < ITERS; j++) {
                result = func_add(result, 1);
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            func_times[r] = (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec);

            result = 0;
            int i = 0;
            clock_gettime(CLOCK_MONOTONIC, &t0);
        loop_b:
            result = result + 1;
            i++;
            if (i < ITERS) goto loop_b;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            goto_times[r] = (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec);
        }
    }

    long long func_total = 0, goto_total = 0;
    long long func_min = func_times[0], func_max = func_times[0];
    long long goto_min = goto_times[0], goto_max = goto_times[0];

    for (int r = 0; r < RUNS; r++) {
        func_total += func_times[r];
        goto_total += goto_times[r];
        if (func_times[r] < func_min) func_min = func_times[r];
        if (func_times[r] > func_max) func_max = func_times[r];
        if (goto_times[r] < goto_min) goto_min = goto_times[r];
        if (goto_times[r] > goto_max) goto_max = goto_times[r];
    }

    printf("=== %d runs x %d iters ===\n\n", RUNS, ITERS);

    printf("func call per run (ms):\n");
    for (int r = 0; r < RUNS; r++)
        printf("  [%2d] %lld.%03lld\n", r, func_times[r]/1000000, (func_times[r]%1000000)/1000);
    printf("  avg: %lld.%03lld  min: %lld.%03lld  max: %lld.%03lld\n\n",
        (func_total/RUNS)/1000000, ((func_total/RUNS)%1000000)/1000,
        func_min/1000000, (func_min%1000000)/1000,
        func_max/1000000, (func_max%1000000)/1000);

    printf("goto loop per run (ms):\n");
    for (int r = 0; r < RUNS; r++)
        printf("  [%2d] %lld.%03lld\n", r, goto_times[r]/1000000, (goto_times[r]%1000000)/1000);
    printf("  avg: %lld.%03lld  min: %lld.%03lld  max: %lld.%03lld\n\n",
        (goto_total/RUNS)/1000000, ((goto_total/RUNS)%1000000)/1000,
        goto_min/1000000, (goto_min%1000000)/1000,
        goto_max/1000000, (goto_max%1000000)/1000);

    double ratio = (double)func_total / goto_total;
    printf("func/goto ratio: %.2fx\n", ratio);

    return 0;
}
