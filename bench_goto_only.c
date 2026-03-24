#include <stdio.h>
#include <time.h>

#define ITERS 100000000
#define RUNS 30

int main(void) {
    struct timespec t0, t1;
    volatile int result;
    long long times[RUNS];

    for (int r = 0; r < RUNS; r++) {
        result = 0;
        int i = 0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
    loop:
        result = result + 1;
        i++;
        if (i < ITERS) goto loop;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        times[r] = (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec);
    }

    long long total = 0, mn = times[0], mx = times[0];
    for (int r = 0; r < RUNS; r++) {
        total += times[r];
        if (times[r] < mn) mn = times[r];
        if (times[r] > mx) mx = times[r];
    }

    for (int r = 0; r < RUNS; r++)
        printf("  [%2d] %lld.%03lld ms\n", r, times[r]/1000000, (times[r]%1000000)/1000);
    printf("  avg: %lld.%03lld  min: %lld.%03lld  max: %lld.%03lld\n",
        (total/RUNS)/1000000, ((total/RUNS)%1000000)/1000,
        mn/1000000, (mn%1000000)/1000,
        mx/1000000, (mx%1000000)/1000);

    return 0;
}
