/* Regression tests: bitwise AND reduction, broadcast root as an ordinal in the
 * active set, shmem_align argument checking. */
#include <stdio.h>
#include <shmem.h>

static long psync[SHMEM_SYNC_SIZE];
static int  wrk[SHMEM_REDUCE_MIN_WRKDATA_SIZE + 1];
static int  src, dst;
static long bsrc[2], bdst[2];

#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

int main(void)
{
    shmem_init();
    int me = shmem_my_pe(), np = shmem_n_pes(), errs = 0;
    for (int i = 0; i < SHMEM_SYNC_SIZE; i++) psync[i] = SHMEM_SYNC_VALUE;

    /* and_to_all must be a bitwise AND: 0x0f & 0x3c... */
    src = (me % 2) ? 0x0f : 0x3c;
    dst = -1;
    shmem_barrier_all();
    shmem_int_and_to_all(&dst, &src, 1, 0, 0, np, wrk, psync);
    shmem_barrier_all();
    CHECK(dst == (np > 1 ? (0x0f & 0x3c) : 0x3c), "and_to_all is bitwise");

    /* broadcast: root is an ordinal within the active set, which starts at PE 1 */
    for (int i = 0; i < SHMEM_SYNC_SIZE; i++) psync[i] = SHMEM_SYNC_VALUE;
    bsrc[0] = 100 + me; bsrc[1] = 200 + me;
    bdst[0] = bdst[1] = -1;
    shmem_barrier_all();
    if (me >= 1) {
        /* active set {1..np-1}; root ordinal 0 is PE 1 */
        shmem_broadcast64(bdst, bsrc, 2, 0, 1, 0, np - 1, psync);
        if (me != 1) CHECK(bdst[0] == 101 && bdst[1] == 201, "broadcast from the first PE of the active set");
    }
    shmem_barrier_all();
    if (np > 2 && me >= 1) {
        /* root ordinal 1 in the set starting at PE 1 is PE 2 */
        bdst[0] = bdst[1] = -1;
        for (int i = 0; i < SHMEM_SYNC_SIZE; i++) psync[i] = SHMEM_SYNC_VALUE;
        shmem_broadcast64(bdst, bsrc, 2, 1, 1, 0, np - 1, psync);
        if (me != 2) CHECK(bdst[0] == 102 && bdst[1] == 202, "broadcast from ordinal 1");
    }
    shmem_barrier_all();

    /* alltoalls: block l of PE i's source (nelems strided elements, stride sst) goes to
     * block i of PE l's dest (stride dst) */
    {
        enum { NE = 2, SST = 2, DST = 3 };
        static long as_src[8 * SST * NE], as_dst[8 * DST * NE];
        static long as_sync[SHMEM_ALLTOALLS_SYNC_SIZE];
        for (int i = 0; i < SHMEM_ALLTOALLS_SYNC_SIZE; i++) as_sync[i] = SHMEM_SYNC_VALUE;
        for (int i = 0; i < 8 * SST * NE; i++) as_src[i] = -1;
        for (int i = 0; i < 8 * DST * NE; i++) as_dst[i] = -1;
        for (int l = 0; l < np; l++)
            for (int e = 0; e < NE; e++) as_src[(l * NE + e) * SST] = 1000 * me + 10 * l + e;
        shmem_barrier_all();
        shmem_alltoalls64(as_dst, as_src, DST, SST, NE, 0, 0, np, as_sync);
        shmem_barrier_all();
        for (int k = 0; k < np; k++)
            for (int e = 0; e < NE; e++)
                CHECK(as_dst[(k * NE + e) * DST] == 1000 * k + 10 * me + e, "alltoalls64");
    }

    {   /* the 32-bit variant moves 4-byte elements */
        enum { NE = 2, SST = 1, DST = 2 };
        static int a_src[8 * SST * NE], a_dst[8 * DST * NE];
        static long a_sync[SHMEM_ALLTOALLS_SYNC_SIZE];
        for (int i = 0; i < SHMEM_ALLTOALLS_SYNC_SIZE; i++) a_sync[i] = SHMEM_SYNC_VALUE;
        for (int i = 0; i < 8 * DST * NE; i++) a_dst[i] = -1;
        for (int l = 0; l < np; l++)
            for (int e = 0; e < NE; e++) a_src[(l * NE + e) * SST] = 1000 * me + 10 * l + e;
        shmem_barrier_all();
        shmem_alltoalls32(a_dst, a_src, DST, SST, NE, 0, 0, np, a_sync);
        shmem_barrier_all();
        for (int k = 0; k < np; k++)
            for (int e = 0; e < NE; e++)
                CHECK(a_dst[(k * NE + e) * DST] == 1000 * k + 10 * me + e, "alltoalls32");
    }

    /* alignment checks */
    void *p = shmem_align(64, 10);
    CHECK(p != NULL && ((unsigned long)p % 64) == 0, "align 64");
    shmem_free(p);
    CHECK(shmem_align(0, 10) == NULL, "align 0 returns NULL");

    printf("[%d] test_coll_semantics: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
