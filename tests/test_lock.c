/* shmem_set_lock/clear_lock/test_lock: mutual exclusion over repeated acquisitions. */
#include <stdio.h>
#include <shmem.h>

static long lock = 0;
static long counter = 0;

int main(void)
{
    int errs = 0;
    shmem_init();
    int me = shmem_my_pe(), np = shmem_n_pes();
    shmem_barrier_all();

    for (int i = 0; i < 50; i++) {
        shmem_set_lock(&lock);
        long v = shmem_long_g(&counter, 0);
        shmem_long_p(&counter, v + 1, 0);
        shmem_quiet();
        shmem_clear_lock(&lock);
    }
    shmem_barrier_all();

    if (me == 0 && counter != 50L * np) {
        printf("test_lock: counter=%ld expected %ld\n", counter, 50L * np);
        errs++;
    }

    /* test_lock returns 0 when it acquires the lock */
    shmem_barrier_all();
    if (me == 0) {
        if (shmem_test_lock(&lock) != 0) { printf("test_lock: could not take free lock\n"); errs++; }
        else shmem_clear_lock(&lock);
    }
    shmem_barrier_all();

    printf("[%d] test_lock: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
