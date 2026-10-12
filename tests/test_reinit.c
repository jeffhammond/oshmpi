/* OpenSHMEM 1.6: the library may be initialized again after the last matching finalize.  Each
 * round uses the heap, static data, atomics, a context, a team and a collective, and checks
 * that nothing from the previous round is left over. */
#include <shmem.h>
#include <stdio.h>

static int errs, me;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

static long sdata;       /* symmetric data segment */

static void round_trip(int round)
{
    me = shmem_my_pe();
    int np = shmem_n_pes(), next = (me + 1) % np, prev = (me + np - 1) % np;

    long *h = shmem_calloc(4, sizeof(long));
    CHECK(h != NULL && h[3] == 0, "fresh heap");
    sdata = 0;
    shmem_barrier_all();
    shmem_long_p(h, 100 * round + me, next);
    shmem_long_p(&sdata, 7 * round + me, next);
    shmem_long_atomic_add(h + 1, round, next);
    shmem_barrier_all();
    CHECK(h[0] == 100 * round + prev, "put to the heap");
    CHECK(sdata == 7 * round + prev, "put to static data");
    CHECK(h[1] == round, "atomic add");

    shmem_ctx_t ctx;
    if (shmem_ctx_create(0, &ctx) == 0) {
        shmem_ctx_long_p(ctx, h + 2, round, next);
        shmem_ctx_quiet(ctx);
        shmem_ctx_destroy(ctx);
    }
    shmem_barrier_all();
    CHECK(h[2] == round, "context put");

    shmem_team_t t = SHMEM_TEAM_INVALID;
    CHECK(shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 1, np, NULL, 0, &t) == 0, "split");
    long *sum = shmem_malloc(sizeof(long));
    *sum = me + 1;
    CHECK(shmem_long_sum_reduce(t, sum, sum, 1) == 0, "team reduce");
    CHECK(*sum == (long) np * (np + 1) / 2, "reduction result");
    shmem_free(sum);
    shmem_team_destroy(t);
    shmem_free(h);
}

int main(void)
{
    int init = -1;
    for (int round = 1; round <= 3; round++) {
        shmem_query_initialized(&init);
        if (init != 0) { printf("FAIL: initialized before init, round %d\n", round); errs++; }
        if (round == 2) {
            int provided = -1;
            if (shmem_init_thread(SHMEM_THREAD_MULTIPLE, &provided) != 0) { printf("FAIL: init_thread\n"); return 1; }
        } else {
            shmem_init();
        }
        shmem_query_initialized(&init);
        CHECK(init != 0, "initialized");
        round_trip(round);
        shmem_finalize();
        shmem_query_initialized(&init);
        CHECK(init == 0, "not initialized after the last finalize");
    }
    printf("[%d] test_reinit: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    return errs != 0;
}
