/* A data segment larger than 2 GiB (the size of the segment's window was once an int).  Only
 * three bytes of it are touched, but registering it with the network may cost real memory, so
 * the test is skipped unless OSHMPI_TEST_LARGE is set. */
#include <shmem.h>
#include <stdio.h>
#include <stdlib.h>

static char big[(size_t) 3 << 30];

int main(void)
{
    if (!getenv("OSHMPI_TEST_LARGE")) { printf("test_bigbss: skipped (set OSHMPI_TEST_LARGE=1 to run)\n"); return 77; }
    shmem_init();
    int me = shmem_my_pe(), np = shmem_n_pes(), next = (me + 1) % np, prev = (me + np - 1) % np, errs = 0;
    size_t offs[3] = { 0, ((size_t) 1 << 31) + 17, ((size_t) 3 << 30) - 1 };
    for (int k = 0; k < 3; k++) {
        char v = (char) (me + 1 + k), g;
        shmem_barrier_all();
        shmem_putmem(&big[offs[k]], &v, 1, next);
        shmem_barrier_all();
        if (big[offs[k]] != (char) (prev + 1 + k)) { printf("[%d] FAIL: put at offset %zu\n", me, offs[k]); errs++; }
        shmem_getmem(&g, &big[offs[k]], 1, next);
        if (g != (char) (me + 1 + k)) { printf("[%d] FAIL: get at offset %zu\n", me, offs[k]); errs++; }
    }
    shmem_barrier_all();
    printf("[%d] test_bigbss: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
