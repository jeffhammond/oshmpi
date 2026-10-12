/* Collectives whose counts or total sizes do not fit in an MPI int.  OSHMPI sends these through
 * derived datatypes or, for reductions, in pieces.  They need several GiB per PE, so the test
 * is skipped unless OSHMPI_TEST_LARGE is set:
 *   OSHMPI_TEST_LARGE=1   team collectives whose receive side exceeds INT_MAX bytes, a strided
 *                         all-to-all of more than 2^30 elements, a reduction that crosses the
 *                         internal chunk size, and a strided put with a stride over 2^31
 *                         (about 5 GiB per PE for 2 PEs)
 *   OSHMPI_TEST_LARGE=2   also the legacy 32-bit broadcast of 2^31 elements and fcollect of 2^30
 *                         elements per PE (about 13 GiB per PE for 2 PEs) */
#include <limits.h>
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

static inline unsigned char pat(size_t i, int salt)
{
    return (unsigned char) (i ^ (i >> 8) ^ (i >> 16) ^ (i >> 24) ^ (size_t) salt * 29);
}

static void fill(unsigned char *p, size_t n, int salt) { for (size_t i = 0; i < n; i++) p[i] = pat(i, salt); }

/* index of the first mismatch, or n */
static size_t first_bad(const unsigned char *p, size_t n, int salt)
{
    for (size_t i = 0; i < n; i++) if (p[i] != pat(i, salt)) return i;
    return n;
}

#define NSYNC (SHMEM_REDUCE_SYNC_SIZE + SHMEM_BCAST_SYNC_SIZE + SHMEM_COLLECT_SYNC_SIZE)
static long psync[NSYNC];
static int pwrk[SHMEM_REDUCE_MIN_WRKDATA_SIZE];

int main(void)
{
    const char *large = getenv("OSHMPI_TEST_LARGE");
    int level = large ? atoi(large) : 0;
    if (level < 1) { printf("test_bigcoll: skipped (set OSHMPI_TEST_LARGE=1 to run)\n"); return 77; }

    /* The heap size is fixed at init, before the PE count is known to us, so take it from the
     * launcher and check it afterwards. */
    int npes = 2;
    {
        const char *s = getenv("OMPI_COMM_WORLD_SIZE");
        if (!s) s = getenv("PMI_SIZE");
        if (s && atoi(s) > 0) npes = atoi(s);
    }
    /* npes pieces of 'pc' bytes together just exceed INT_MAX */
    const size_t pc = (size_t) INT_MAX / (size_t) npes + ((size_t) 1 << 24);
    size_t heap = 2 * pc * (size_t) npes;                          /* two buffers, level 1 */
    if (level >= 2) {
        size_t l2 = ((size_t) 1 << 30) * 4 * (size_t) (npes + 1);  /* fcollect32 source + dest */
        if (l2 < ((size_t) 1 << 31) * 4) l2 = ((size_t) 1 << 31) * 4;   /* bcast32 */
        if (l2 > heap) heap = l2;
    }
    {   /* room for the strided put below */
        size_t sp = ((size_t) 1 << 32) + ((size_t) 128 << 20);
        if (heap < sp) heap = sp;
    }
    char sz[64];
    snprintf(sz, sizeof sz, "%zu", heap + ((size_t) 64 << 20));
    setenv("SHMEM_SYMMETRIC_SIZE", sz, 1);

    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();
    if (np != npes) { printf("[%d] FAIL: expected %d PEs, have %d\n", me, npes, np); shmem_global_exit(2); }
    for (int i = 0; i < NSYNC; i++) psync[i] = SHMEM_SYNC_VALUE;
    shmem_team_t world = SHMEM_TEAM_WORLD;

    size_t tot = pc * (size_t) np;
    unsigned char *A = shmem_malloc(tot);
    unsigned char *B = shmem_malloc(tot);
    if (!A || !B) { printf("[%d] FAIL: cannot allocate %zu bytes\n", me, tot); shmem_global_exit(2); }

    /* fcollect: PE p's piece lands at p * pc */
    fill(A, pc, me + 1);
    memset(B, 0, tot);
    shmem_barrier_all();
    CHECK(shmem_fcollectmem(world, B, A, pc) == 0, "fcollectmem returns 0");
    for (int p = 0; p < np; p++) CHECK(first_bad(B + p * pc, pc, p + 1) == pc, "fcollectmem data");
    shmem_barrier_all();
    if (me == 0) { printf("test_bigcoll: fcollectmem %zu bytes total done\n", tot); fflush(stdout); }

    /* alltoall: piece q of PE p's source goes to PE q, where it is piece p */
    for (int q = 0; q < np; q++) fill(A + q * pc, pc, me * 16 + q + 1);
    memset(B, 0, tot);
    shmem_barrier_all();
    CHECK(shmem_alltoallmem(world, B, A, pc) == 0, "alltoallmem returns 0");
    for (int p = 0; p < np; p++) CHECK(first_bad(B + p * pc, pc, p * 16 + me + 1) == pc, "alltoallmem data");
    shmem_barrier_all();
    if (me == 0) { printf("test_bigcoll: alltoallmem done\n"); fflush(stdout); }

    /* collect: pieces differ in length, and the total exceeds INT_MAX */
    {
        size_t mylen = pc - (size_t) me * 4096, total = 0;
        for (int p = 0; p < np; p++) total += pc - (size_t) p * 4096;
        fill(A, mylen, me + 3);
        memset(B, 0, tot);
        shmem_barrier_all();
        CHECK(shmem_collectmem(world, B, A, mylen) == 0, "collectmem returns 0");
        size_t o = 0;
        for (int p = 0; p < np; p++) {
            size_t l = pc - (size_t) p * 4096;
            CHECK(first_bad(B + o, l, p + 3) == l, "collectmem data");
            o += l;
        }
        shmem_barrier_all();
        if (me == 0) { printf("test_bigcoll: collectmem %zu bytes total done\n", total); fflush(stdout); }
    }

    /* strided all-to-all: more elements per block than one MPI call can describe */
    {
        size_t ne = ((size_t) 1 << 30) + 3;
        if (ne * (size_t) np > tot) ne = tot / (size_t) np;
        for (int q = 0; q < np; q++) fill(A + q * ne, ne, me * 16 + q + 5);
        memset(B, 0, tot);
        shmem_barrier_all();
        CHECK(shmem_alltoallsmem(world, B, A, 1, 1, ne) == 0, "alltoallsmem returns 0");
        for (int p = 0; p < np; p++) CHECK(first_bad(B + p * ne, ne, p * 16 + me + 5) == ne, "alltoallsmem data");
        shmem_barrier_all();
        if (me == 0) { printf("test_bigcoll: alltoallsmem %zu elements per block done\n", ne); fflush(stdout); }
    }
    shmem_free(B);
    shmem_free(A);

    /* a reduction that crosses the internal chunk size (2^28 elements), team and legacy */
    {
        size_t n = ((size_t) 1 << 28) + 5;
        int *R = shmem_malloc(n * sizeof(int));
        int *S = shmem_malloc(n * sizeof(int));
        if (!R || !S) { printf("[%d] FAIL: cannot allocate reduce buffers\n", me); shmem_global_exit(2); }
        for (size_t i = 0; i < n; i++) S[i] = (int) (i % 1000) + me;
        memset(R, 0, n * sizeof(int));
        shmem_barrier_all();
        CHECK(shmem_int_sum_reduce(world, R, S, n) == 0, "int_sum_reduce returns 0");
        size_t bad = n;
        for (size_t i = 0; i < n; i++)
            if (R[i] != (int) (i % 1000) * np + np * (np - 1) / 2) { bad = i; break; }
        CHECK(bad == n, "int_sum_reduce data");
        shmem_barrier_all();
        memset(R, 0, n * sizeof(int));
        shmem_int_sum_to_all(R, S, (int) n, 0, 0, np, pwrk, psync);   /* legacy interface */
        bad = n;
        for (size_t i = 0; i < n; i++)
            if (R[i] != (int) (i % 1000) * np + np * (np - 1) / 2) { bad = i; break; }
        CHECK(bad == n, "int_sum_to_all data");
        shmem_barrier_all();
        shmem_free(S);
        shmem_free(R);
        if (me == 0) { printf("test_bigcoll: reductions of %zu elements done\n", n); fflush(stdout); }
    }

    /* a stride that does not fit in an int: elements 2^31 + 5 apart, so the transfer spans
     * more than 4 GiB of address space of which only three bytes are touched */
    {
        ptrdiff_t stride = ((ptrdiff_t) 1 << 31) + 5;
        size_t span = (size_t) stride * 2 + 1;
        if (span + ((size_t) 64 << 20) > heap) span = 0;       /* the level-1 heap is too small */
        unsigned char *H = span ? shmem_malloc(span) : NULL;
        if (span && !H) { printf("[%d] FAIL: cannot allocate %zu bytes\n", me, span); shmem_global_exit(2); }
        if (H) {
            int next = (me + 1) % np, prev = (me + np - 1) % np;
            H[0] = H[stride] = H[2 * stride] = 0;
            unsigned char src[3] = { (unsigned char) (me + 1), (unsigned char) (me + 2), (unsigned char) (me + 3) };
            shmem_barrier_all();
            shmem_iput8(H, src, stride, 1, 3, next);
            shmem_barrier_all();
            CHECK(H[0] == prev + 1 && H[stride] == prev + 2 && H[2 * stride] == prev + 3, "iput8 with stride over 2^31");
            unsigned char out[3] = { 0, 0, 0 };
            shmem_iget8(out, H, 1, stride, 3, next);
            CHECK(out[0] == me + 1 && out[1] == me + 2 && out[2] == me + 3, "iget8 with stride over 2^31");
            shmem_barrier_all();
            shmem_free(H);
            if (me == 0) { printf("test_bigcoll: iput8/iget8 with stride %td done\n", stride); fflush(stdout); }
        } else if (me == 0) {
            printf("test_bigcoll: strided put with huge stride not run (heap too small)\n");
        }
    }

    if (level >= 2) {
        /* legacy 32-bit collectives with element counts that need a derived type */
        size_t n = (size_t) 1 << 31;
        unsigned char *D = shmem_malloc(n * 4);
        if (!D) { printf("[%d] FAIL: cannot allocate %zu bytes\n", me, n * 4); shmem_global_exit(2); }
        if (me == 0) fill(D, n * 4, 41); else memset(D, 0, n * 4);
        shmem_barrier_all();
        shmem_broadcast32(D, D, n, 0, 0, 0, np, psync);
        if (me != 0) CHECK(first_bad(D, n * 4, 41) == n * 4, "broadcast32 of 2^31 elements");
        shmem_barrier_all();
        shmem_free(D);
        if (me == 0) { printf("test_bigcoll: broadcast32 %zu elements done\n", n); fflush(stdout); }

        size_t m = (size_t) 1 << 30;
        unsigned char *Sx = shmem_malloc(m * 4);
        unsigned char *Dx = shmem_malloc(m * 4 * (size_t) np);
        if (!Sx || !Dx) { printf("[%d] FAIL: cannot allocate fcollect buffers\n", me); shmem_global_exit(2); }
        fill(Sx, m * 4, me + 51);
        memset(Dx, 0, m * 4 * (size_t) np);
        shmem_barrier_all();
        shmem_fcollect32(Dx, Sx, m, 0, 0, np, psync);
        for (int p = 0; p < np; p++)
            CHECK(first_bad(Dx + (size_t) p * m * 4, m * 4, p + 51) == m * 4, "fcollect32 of 2^30 elements per PE");
        shmem_barrier_all();
        shmem_free(Dx);
        shmem_free(Sx);
        if (me == 0) { printf("test_bigcoll: fcollect32 %zu elements per PE done\n", m); fflush(stdout); }
    }

    shmem_barrier_all();
    printf("[%d] test_bigcoll: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
