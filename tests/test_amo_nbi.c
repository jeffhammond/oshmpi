/* OpenSHMEM 1.5 nonblocking atomics, malloc_with_hints and context sessions.
 *
 * Every family and every type it supports is run against a symmetric-heap object and a
 * data-segment object (so both the direct and the MPI path are covered when the heap is
 * directly addressable), with all PEs contending on PE 0's copy, on the default context and
 * on a created context.  Fetched values are only inspected after the matching quiet. */
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s (%s)\n", me, __LINE__, m, where); errs++; } } while (0)
#define ITER 20

static const char *where = "";
static long long *tot;                 /* accumulates fetched values on PE 0 */

#define QUIET(ctx, use_ctx) do { if (use_ctx) shmem_ctx_quiet(ctx); else shmem_quiet(); } while (0)

/* ---- fetch and swap: all fetch/swap types ------------------------------ */

#define DEFINE_FS(N, T)                                                                        \
static void fs_##N(T *p, shmem_ctx_t ctx, int use_ctx)                                         \
{                                                                                              \
    /* fetch */                                                                                \
    if (me == 0) *p = (T) 77;                                                                  \
    shmem_barrier_all();                                                                       \
    T f = 0;                                                                                   \
    if (use_ctx) shmem_ctx_##N##_atomic_fetch_nbi(ctx, &f, p, 0);                              \
    else         shmem_##N##_atomic_fetch_nbi(&f, p, 0);                                       \
    QUIET(ctx, use_ctx);                                                                       \
    CHECK(f == (T) 77, "fetch_nbi value");                                                     \
    shmem_barrier_all();                                                                       \
    /* swap: the old values and the final value together are the initial value and every      \
     * PE's new value */                                                                       \
    if (me == 0) { *p = (T) 5; *tot = 0; }                                                     \
    shmem_barrier_all();                                                                       \
    T old = 0;                                                                                 \
    if (use_ctx) shmem_ctx_##N##_atomic_swap_nbi(ctx, &old, p, (T) (me + 1), 0);               \
    else         shmem_##N##_atomic_swap_nbi(&old, p, (T) (me + 1), 0);                        \
    QUIET(ctx, use_ctx);                                                                       \
    shmem_longlong_atomic_add(tot, (long long) old, 0);                                        \
    shmem_barrier_all();                                                                       \
    if (me == 0) CHECK(*tot + (long long) *p == 5 + (long long) np * (np + 1) / 2, "swap_nbi old values"); \
    shmem_barrier_all();                                                                       \
}

/* ---- compare-swap, fetch-inc, fetch-add: standard AMO types ------------- */

#define DEFINE_STD(N, T)                                                                       \
static void std_##N(T *p, shmem_ctx_t ctx, int use_ctx)                                        \
{                                                                                              \
    long long M = (long long) np * ITER;                                                       \
    /* fetch_add: unique tickets */                                                            \
    if (me == 0) { *p = 0; *tot = 0; }                                                         \
    shmem_barrier_all();                                                                       \
    long long sum = 0;                                                                         \
    for (int i = 0; i < ITER; i++) {                                                           \
        T f = 0;                                                                               \
        if (use_ctx) shmem_ctx_##N##_atomic_fetch_add_nbi(ctx, &f, p, (T) 1, 0);               \
        else         shmem_##N##_atomic_fetch_add_nbi(&f, p, (T) 1, 0);                        \
        QUIET(ctx, use_ctx);                                                                   \
        sum += (long long) f;                                                                  \
    }                                                                                          \
    shmem_longlong_atomic_add(tot, sum, 0);                                                    \
    shmem_barrier_all();                                                                       \
    if (me == 0) { CHECK(*p == (T) M, "fetch_add_nbi total"); CHECK(*tot == M * (M - 1) / 2, "fetch_add_nbi tickets"); } \
    shmem_barrier_all();                                                                       \
    /* fetch_inc: unique tickets */                                                            \
    if (me == 0) { *p = 0; *tot = 0; }                                                         \
    shmem_barrier_all();                                                                       \
    sum = 0;                                                                                   \
    for (int i = 0; i < ITER; i++) {                                                           \
        T f = 0;                                                                               \
        if (use_ctx) shmem_ctx_##N##_atomic_fetch_inc_nbi(ctx, &f, p, 0);                      \
        else         shmem_##N##_atomic_fetch_inc_nbi(&f, p, 0);                               \
        QUIET(ctx, use_ctx);                                                                   \
        sum += (long long) f;                                                                  \
    }                                                                                          \
    shmem_longlong_atomic_add(tot, sum, 0);                                                    \
    shmem_barrier_all();                                                                       \
    if (me == 0) { CHECK(*p == (T) M, "fetch_inc_nbi total"); CHECK(*tot == M * (M - 1) / 2, "fetch_inc_nbi tickets"); } \
    shmem_barrier_all();                                                                       \
    /* compare_swap: a lock-free increment loop; the swap succeeds when the returned old       \
     * value equals the expected one */                                                        \
    if (me == 0) *p = 0;                                                                       \
    shmem_barrier_all();                                                                       \
    for (int i = 0; i < ITER; i++) {                                                           \
        T cur = 0, old;                                                                        \
        if (use_ctx) shmem_ctx_##N##_atomic_fetch_nbi(ctx, &cur, p, 0);                        \
        else         shmem_##N##_atomic_fetch_nbi(&cur, p, 0);                                 \
        QUIET(ctx, use_ctx);                                                                   \
        for (;;) {                                                                             \
            old = 0;                                                                           \
            if (use_ctx) shmem_ctx_##N##_atomic_compare_swap_nbi(ctx, &old, p, cur, (T) (cur + 1), 0); \
            else         shmem_##N##_atomic_compare_swap_nbi(&old, p, cur, (T) (cur + 1), 0);  \
            QUIET(ctx, use_ctx);                                                               \
            if (old == cur) break;                                                             \
            cur = old;                                                                         \
        }                                                                                      \
    }                                                                                          \
    shmem_barrier_all();                                                                       \
    if (me == 0) CHECK(*p == (T) M, "compare_swap_nbi total");                                 \
    shmem_barrier_all();                                                                       \
}

/* ---- fetch-and/or/xor: bitwise AMO types -------------------------------- */

#define DEFINE_BIT(N, T)                                                                       \
static void bit_##N(T *p, shmem_ctx_t ctx, int use_ctx)                                        \
{                                                                                              \
    T bit = (T) ((T) 1 << (me % 8));                                                           \
    T all_bits = 0;                                                                            \
    for (int q = 0; q < np; q++) all_bits = (T) (all_bits | (T) ((T) 1 << (q % 8)));           \
    T f = 0;                                                                                   \
    /* or */                                                                                   \
    if (me == 0) *p = 0;                                                                       \
    shmem_barrier_all();                                                                       \
    if (use_ctx) shmem_ctx_##N##_atomic_fetch_or_nbi(ctx, &f, p, bit, 0);                      \
    else         shmem_##N##_atomic_fetch_or_nbi(&f, p, bit, 0);                               \
    QUIET(ctx, use_ctx);                                                                       \
    CHECK((f & bit) == 0 || np > 8, "fetch_or_nbi old value lacks our bit");                   \
    shmem_barrier_all();                                                                       \
    if (me == 0) CHECK(*p == all_bits, "fetch_or_nbi result");                                 \
    shmem_barrier_all();                                                                       \
    /* and: clear every PE's bit from an all-ones word */                                      \
    if (me == 0) *p = (T) ~(T) 0;                                                              \
    shmem_barrier_all();                                                                       \
    if (use_ctx) shmem_ctx_##N##_atomic_fetch_and_nbi(ctx, &f, p, (T) ~bit, 0);                \
    else         shmem_##N##_atomic_fetch_and_nbi(&f, p, (T) ~bit, 0);                         \
    QUIET(ctx, use_ctx);                                                                       \
    shmem_barrier_all();                                                                       \
    if (me == 0) CHECK(*p == (T) ~all_bits, "fetch_and_nbi result");                           \
    shmem_barrier_all();                                                                       \
    /* xor twice restores the word */                                                          \
    if (me == 0) *p = (T) 0x55;                                                                \
    shmem_barrier_all();                                                                       \
    for (int k = 0; k < 2; k++) {                                                              \
        if (use_ctx) shmem_ctx_##N##_atomic_fetch_xor_nbi(ctx, &f, p, bit, 0);                 \
        else         shmem_##N##_atomic_fetch_xor_nbi(&f, p, bit, 0);                          \
        QUIET(ctx, use_ctx);                                                                   \
    }                                                                                          \
    shmem_barrier_all();                                                                       \
    if (me == 0) CHECK(*p == (T) 0x55, "fetch_xor_nbi twice restores");                        \
    shmem_barrier_all();                                                                       \
}

OSHMPI_AMO_TYPES(DEFINE_FS)
OSHMPI_EXT_AMO_ONLY_TYPES(DEFINE_FS)
OSHMPI_AMO_TYPES(DEFINE_STD)
OSHMPI_BITWISE_AMO_TYPES(DEFINE_BIT)

/* each test is run on a heap object and a data-segment object, with and without a context */
#define SEG_OBJ(N, T) static T seg_##N;

OSHMPI_AMO_TYPES(SEG_OBJ)
OSHMPI_EXT_AMO_ONLY_TYPES(SEG_OBJ)   /* the bitwise types are a subset of the AMO types */

#define RUN(FN, N, T, ctx)                                                                     \
    do {                                                                                       \
        T *heap = shmem_calloc(1, sizeof(T));                                                  \
        for (int use_ctx = 0; use_ctx < 2; use_ctx++) {                                        \
            where = use_ctx ? "heap, ctx " #N : "heap " #N;                                    \
            FN##_##N(heap, ctx, use_ctx);                                                      \
            where = use_ctx ? "data segment, ctx " #N : "data segment " #N;                    \
            FN##_##N(&seg_##N, ctx, use_ctx);                                                  \
        }                                                                                      \
        shmem_free(heap);                                                                      \
    } while (0)

#define RUN_FS(N, T)   RUN(fs, N, T, ctx);
#define RUN_STD(N, T)  RUN(std, N, T, ctx);
#define RUN_BIT(N, T)  RUN(bit, N, T, ctx);

/* ---- C11 type-generic bindings ------------------------------------------ */

static void test_generics(shmem_ctx_t ctx)
{
    where = "generics";
    int32_t   *i32 = shmem_calloc(1, sizeof(int32_t));
    uint64_t  *u64 = shmem_calloc(1, sizeof(uint64_t));
    size_t    *sz  = shmem_calloc(1, sizeof(size_t));
    double    *dd  = shmem_calloc(1, sizeof(double));
    float     *ff  = shmem_calloc(1, sizeof(float));
    long      *lg  = shmem_calloc(1, sizeof(long));
    long long *ll  = shmem_calloc(1, sizeof(long long));
    int64_t   *i64 = shmem_calloc(1, sizeof(int64_t));
    int32_t f32 = -1; uint64_t f64 = 0; size_t fsz = 0; double fd = 0; float ff1 = 0;
    long fl = 0; long long fll = 0; int64_t f64s = 0;

    if (me == 0) { *i32 = 11; *u64 = 0xF0; *sz = 3; *dd = 2.5; *ff = 1.5f; *lg = 100; *ll = 200; }
    shmem_barrier_all();

    shmem_atomic_fetch_nbi(&f32, i32, 0);                       /* int32_t, default context */
    shmem_atomic_fetch_nbi(ctx, &fsz, sz, 0);                   /* size_t, context */
    shmem_atomic_fetch_nbi(&fd, dd, 0);                         /* double */
    shmem_atomic_fetch_nbi(ctx, &ff1, ff, 0);                   /* float, context */
    shmem_atomic_fetch_nbi(&fl, lg, 0);                         /* long */
    shmem_atomic_fetch_nbi(ctx, &fll, ll, 0);                   /* long long, context */
    shmem_quiet();
    CHECK(f32 == 11 && fsz == 3 && fd == 2.5 && ff1 == 1.5f && fl == 100 && fll == 200, "generic fetch_nbi");
    shmem_barrier_all();

    shmem_atomic_swap_nbi(&fd, dd, 9.0, 0);                     /* every PE swaps; old value is 2.5 or 9.0 */
    shmem_quiet();
    CHECK(fd == 2.5 || fd == 9.0, "generic swap_nbi");
    shmem_barrier_all();
    if (me == 0) CHECK(*dd == 9.0, "generic swap_nbi result");

    shmem_barrier_all();
    shmem_atomic_fetch_add_nbi(&fsz, sz, (size_t) 1, 0);
    shmem_atomic_fetch_inc_nbi(ctx, &fl, lg, 0);
    shmem_atomic_compare_swap_nbi(&fll, ll, 200LL, 201LL, 0);   /* only one PE can win */
    shmem_atomic_compare_swap_nbi(ctx, &f32, i32, 11, 12, 0);   /* int32_t, only one PE can win */
    shmem_ctx_quiet(ctx);
    shmem_quiet();
    shmem_barrier_all();
    if (me == 0) {
        CHECK(*sz == (size_t) (3 + np), "generic fetch_add_nbi size_t");
        CHECK(*lg == 100 + np, "generic fetch_inc_nbi long");
        CHECK(*ll == 201, "generic compare_swap_nbi long long");
        CHECK(*i32 == 12, "generic compare_swap_nbi int32_t");
    }
    shmem_barrier_all();

    int ones = 0;
    if (me == 0) *u64 = ~0ULL;
    shmem_barrier_all();
    shmem_atomic_fetch_and_nbi(&f64, u64, ~(1ULL << (me % 8)), 0);
    shmem_atomic_fetch_or_nbi(ctx, &f64, u64, 1ULL << (32 + me % 8), 0);
    shmem_atomic_fetch_xor_nbi(&f32, i32, 0, 0);                /* int32_t: xor with 0 leaves it unchanged */
    shmem_atomic_fetch_and_nbi(&f32, i32, -1, 0);               /* int32_t, and with all ones */
    shmem_atomic_fetch_or_nbi(ctx, &fl, lg, 0, 0);              /* long: int64 function */
    shmem_atomic_fetch_xor_nbi(&f64s, i64, (int64_t) 0, 0);    /* int64_t */
    shmem_ctx_quiet(ctx);
    shmem_quiet();
    shmem_barrier_all();
    if (me == 0) { ones = (*u64 >> 32) != 0; CHECK(ones, "generic bitwise_nbi on uint64_t"); CHECK(*i32 == 12, "generic bitwise_nbi on int32_t"); }
    shmem_barrier_all();

    shmem_free(i64); shmem_free(ll); shmem_free(lg); shmem_free(ff); shmem_free(dd); shmem_free(sz); shmem_free(u64); shmem_free(i32);
}

/* ---- malloc with hints and context sessions ------------------------------ */

static void test_hints_and_sessions(shmem_ctx_t ctx)
{
    where = "hints/sessions";
    CHECK(shmem_malloc_with_hints(0, SHMEM_MALLOC_ATOMICS_REMOTE) == NULL, "zero size returns NULL");

    long all_hints[] = { 0, SHMEM_MALLOC_ATOMICS_REMOTE, SHMEM_MALLOC_SIGNAL_REMOTE,
                         SHMEM_MALLOC_ATOMICS_REMOTE | SHMEM_MALLOC_SIGNAL_REMOTE };
    for (int h = 0; h < 4; h++) {
        long *p = shmem_malloc_with_hints(64 * sizeof(long), all_hints[h]);
        CHECK(p != NULL, "malloc_with_hints returns memory");
        if (p == NULL) continue;
        for (int i = 0; i < 64; i++) p[i] = me * 1000 + i;
        shmem_barrier_all();
        long v = shmem_long_g(&p[63], (me + 1) % np);        /* it is symmetric, remotely accessible */
        CHECK(v == ((me + 1) % np) * 1000 + 63, "memory from malloc_with_hints is remotely accessible");
        shmem_barrier_all();
        shmem_free(p);
    }

    shmem_ctx_session_config_t cfg = { .total_ops = 100 };
    shmem_ctx_session_start(ctx, SHMEM_CTX_SESSION_BATCH, &cfg, SHMEM_CTX_SESSION_TOTAL_OPS);
    int *x = shmem_calloc(1, sizeof(int));
    int f;
    for (int i = 0; i < 10; i++) shmem_ctx_int_atomic_fetch_add_nbi(ctx, &f, x, 1, 0);
    shmem_ctx_session_start(ctx, 0, NULL, 0);                /* a second start combines with the first */
    shmem_ctx_quiet(ctx);
    shmem_ctx_session_stop(ctx);
    shmem_ctx_session_stop(ctx);                             /* stopping a stopped session has no effect */
    shmem_ctx_session_start(SHMEM_CTX_INVALID, SHMEM_CTX_SESSION_BATCH, &cfg, SHMEM_CTX_SESSION_TOTAL_OPS);
    shmem_ctx_session_start(SHMEM_CTX_DEFAULT, 0, NULL, 0);
    shmem_ctx_session_stop(SHMEM_CTX_DEFAULT);
    shmem_barrier_all();
    if (me == 0) CHECK(*x == 10 * np, "atomics issued inside a session");
    shmem_barrier_all();
    shmem_free(x);
}

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();

    shmem_ctx_t ctx;
    if (shmem_ctx_create(0, &ctx) != 0) ctx = SHMEM_CTX_DEFAULT;
    tot = shmem_calloc(1, sizeof(long long));

    OSHMPI_AMO_TYPES(RUN_FS)
    OSHMPI_EXT_AMO_ONLY_TYPES(RUN_FS)
    OSHMPI_AMO_TYPES(RUN_STD)
    OSHMPI_BITWISE_AMO_TYPES(RUN_BIT)

    test_generics(ctx);
    test_hints_and_sessions(ctx);

    shmem_free(tot);
    if (ctx != SHMEM_CTX_DEFAULT) shmem_ctx_destroy(ctx);
    printf("[%d] test_amo_nbi: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
