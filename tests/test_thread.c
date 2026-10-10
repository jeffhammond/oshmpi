/* OpenSHMEM 1.4 thread support: shmem_init_thread / shmem_query_thread. */
#include <stdio.h>
#include <shmem.h>

int main(void)
{
    int errs = 0, provided = -1, queried = -1;
    int rc = shmem_init_thread(SHMEM_THREAD_MULTIPLE, &provided);
    if (rc != 0) { printf("shmem_init_thread returned %d\n", rc); errs++; }
    shmem_query_thread(&queried);
    int me = shmem_my_pe();

    if (provided < SHMEM_THREAD_SINGLE || provided > SHMEM_THREAD_MULTIPLE) {
        printf("[%d] provided=%d out of range\n", me, provided); errs++;
    }
    if (queried != provided) {
        printf("[%d] query_thread=%d != provided=%d\n", me, queried, provided); errs++;
    }

    /* the library must work at whatever level was provided */
    long * x = shmem_calloc(1, sizeof(long));
    shmem_long_p(x, me + 1, (me + 1) % shmem_n_pes());
    shmem_barrier_all();
    if (*x != ((me + shmem_n_pes() - 1) % shmem_n_pes()) + 1) { printf("[%d] bad data\n", me); errs++; }
    shmem_free(x);

    printf("[%d] test_thread: %s (provided=%d)\n", me, errs ? "FAILED" : "OK", provided);
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
