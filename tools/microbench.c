/* OSHMPI microbenchmark: put/get latency, atomic rate, put bandwidth, barrier. Run with OSHMPI_DIRECT=0/auto. */
#include <shmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + 1e-9 * t.tv_nsec; }

int main(void)
{
    shmem_init();
    int me = shmem_my_pe(), np = shmem_n_pes(), peer = (me + 1) % np;
    size_t big = 1 << 20;
    char *buf = shmem_malloc(big), *loc = malloc(big);
    long *ctr = shmem_calloc(1, sizeof(long));
    memset(loc, 1, big);
    int iters = 20000;
    double t0, t1;

    shmem_barrier_all();
    t0 = now();
    for (int i = 0; i < iters; i++) { shmem_putmem(buf, loc, 8, peer); shmem_quiet(); }
    t1 = now();
    double put_lat = (t1 - t0) / iters * 1e6;

    shmem_barrier_all();
    t0 = now();
    for (int i = 0; i < iters; i++) shmem_getmem(loc, buf, 8, peer);
    t1 = now();
    double get_lat = (t1 - t0) / iters * 1e6;

    shmem_barrier_all();
    t0 = now();
    for (int i = 0; i < iters; i++) shmem_long_atomic_add(ctr, 1, peer);
    shmem_quiet();
    t1 = now();
    double amo_rate = iters / (t1 - t0) / 1e6;

    shmem_barrier_all();
    t0 = now();
    for (int i = 0; i < iters; i++) (void) shmem_long_atomic_fetch_add(ctr, 1, peer);
    t1 = now();
    double fadd_lat = (t1 - t0) / iters * 1e6;

    int reps = 200;
    shmem_barrier_all();
    t0 = now();
    for (int i = 0; i < reps; i++) shmem_putmem(buf, loc, big, peer);
    shmem_quiet();
    t1 = now();
    double bw = (double) big * reps / (t1 - t0) / 1e9;

    int bi = 5000;
    shmem_barrier_all();
    t0 = now();
    for (int i = 0; i < bi; i++) shmem_barrier_all();
    t1 = now();
    double bar = (t1 - t0) / bi * 1e6;

    if (me == 0)
        printf("np=%d  put+quiet 8B %8.3f us | get 8B %8.3f us | atomic_add %7.3f Mop/s | fetch_add %8.3f us | put 1MB %7.2f GB/s | barrier %8.3f us\n",
               np, put_lat, get_lat, amo_rate, fadd_lat, bw, bar);
    shmem_finalize();
    return 0;
}
