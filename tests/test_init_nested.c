/* The spec lets shmem_init and shmem_init_thread be called more than once, each matched by a
 * shmem_finalize, and the library stays usable until the last one.  (It cannot be started again
 * afterward: MPI cannot be restarted.) */
#include <shmem.h>
#include <stdio.h>

static int errs, me;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

int main(void)
{
    shmem_init();
    int provided = -1;
    CHECK(shmem_init_thread(SHMEM_THREAD_MULTIPLE, &provided) == 0, "a nested init_thread succeeds");
    shmem_init();
    me = shmem_my_pe();
    int np = shmem_n_pes(), next = (me + 1) % np;
    int *x = shmem_calloc(1, sizeof(int));

    for (int depth = 3; depth >= 1; depth--) {
        shmem_barrier_all();
        shmem_int_p(x, depth * 10 + me, next);
        shmem_barrier_all();
        CHECK(*x == depth * 10 + (me + np - 1) % np, "the library works while init calls are outstanding");
        CHECK(shmem_my_pe() == me && shmem_n_pes() == np, "pe and n_pes are stable");
        shmem_barrier_all();
        if (depth > 1) shmem_finalize();                 /* matches a nested init */
    }
    shmem_free(x);
    printf("[%d] test_init_nested: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();                                    /* matches the first init */
    return errs != 0;
}
