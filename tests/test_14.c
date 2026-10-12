/* OpenSHMEM 1.4: standard-named RMA/AMO routines, bitwise atomics, contexts, test and sync. */
#include <stdio.h>
#include <stdint.h>
#include <shmem.h>

static int errs = 0;
static int me, np;

#define CHECK(cond, msg) do { if (!(cond)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, msg); errs++; } } while (0)

/* symmetric (static) data, so the globals path is covered too */
static unsigned char  s_uc[4];
static int64_t        s_i64[4];
static size_t         s_size[4];
static ptrdiff_t      s_pd[4];
static unsigned short s_us[4];
static long double    s_ld[4];

#define RMA_TEST(N, T, ARR)                                                       \
    do {                                                                          \
        int right = (me + 1) % np, left = (me + np - 1) % np;                     \
        T src[4] = { (T)(me + 1), (T)(me + 2), (T)(me + 3), (T)(me + 4) };        \
        for (int i = 0; i < 4; i++) ARR[i] = 0;                                   \
        shmem_barrier_all();                                                      \
        shmem_##N##_put(ARR, src, 4, right);                                      \
        shmem_barrier_all();                                                      \
        for (int i = 0; i < 4; i++) CHECK(ARR[i] == (T)(left + 1 + i), #N " put");\
        T got[4];                                                                 \
        shmem_##N##_get(got, ARR, 4, right);                                      \
        for (int i = 0; i < 4; i++) CHECK(got[i] == (T)(me + 1 + i), #N " get");  \
        shmem_barrier_all();                                                      \
        shmem_##N##_p(&ARR[1], (T)(100 + me), right);                             \
        shmem_barrier_all();                                                      \
        CHECK(ARR[1] == (T)(100 + left), #N " p");                                \
        CHECK(shmem_##N##_g(&ARR[1], right) == (T)(100 + me), #N " g");           \
        shmem_barrier_all();                                                      \
        /* strided: every other element */                                        \
        shmem_##N##_iput(ARR, src, 2, 1, 2, right);                               \
        shmem_barrier_all();                                                      \
        CHECK(ARR[0] == (T)(left + 1) && ARR[2] == (T)(left + 2), #N " iput");    \
        shmem_barrier_all();                                                      \
        shmem_##N##_put_nbi(ARR, src, 4, right);                                  \
        shmem_quiet();                                                            \
        shmem_barrier_all();                                                      \
        for (int i = 0; i < 4; i++) CHECK(ARR[i] == (T)(left + 1 + i), #N " put_nbi"); \
        shmem_barrier_all();                                                      \
    } while (0)

static int64_t a64;           /* AMO targets on PE 0 */
static unsigned int au;
static size_t asz;
static float af;
static double ad;
static unsigned long long aull;
static int32_t a32;

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();

    int major, minor;
    shmem_info_get_version(&major, &minor);
    CHECK(major == 1 && minor == 6, "version is 1.6");

    RMA_TEST(uchar, unsigned char, s_uc);
    RMA_TEST(int64, int64_t, s_i64);
    RMA_TEST(size, size_t, s_size);
    RMA_TEST(ptrdiff, ptrdiff_t, s_pd);
    RMA_TEST(ushort, unsigned short, s_us);
    RMA_TEST(longdouble, long double, s_ld);

    /* heap-allocated target for the AMOs too */
    int64_t * h64 = shmem_calloc(1, sizeof(int64_t));
    unsigned int * hu = shmem_calloc(1, sizeof(unsigned int));
    double * hd = shmem_calloc(1, sizeof(double));
    shmem_barrier_all();

    /* add / inc / fetch variants: all PEs update PE 0 */
    shmem_int64_atomic_add(h64, 2, 0);
    shmem_int64_atomic_inc(h64, 0);
    int64_t f = shmem_int64_atomic_fetch_add(h64, 10, 0);
    CHECK(f >= 0, "fetch_add returns a prior value");
    f = shmem_int64_atomic_fetch_inc(h64, 0);
    CHECK(f >= 0, "fetch_inc returns a prior value");
    shmem_barrier_all();
    if (me == 0) CHECK(*h64 == (int64_t)np * (2 + 1 + 10 + 1), "int64 add/inc total");
    shmem_barrier_all();

    /* swap / compare_swap / set / fetch */
    shmem_int64_atomic_set(h64, 7, 0);
    shmem_barrier_all();
    CHECK(shmem_int64_atomic_fetch(h64, 0) == 7, "fetch after set");
    shmem_barrier_all();
    if (me == 0) {
        CHECK(shmem_int64_atomic_swap(h64, 9, 0) == 7, "swap returns old");
        CHECK(shmem_int64_atomic_compare_swap(h64, 9, 11, 0) == 9, "cswap match");
        CHECK(shmem_int64_atomic_compare_swap(h64, 9, 13, 0) == 11, "cswap no match returns current");
        CHECK(*h64 == 11, "cswap only swapped on match");
    }
    shmem_barrier_all();

    /* float/double extended AMOs */
    shmem_double_atomic_set(hd, 2.5, 0);
    shmem_barrier_all();
    CHECK(shmem_double_atomic_fetch(hd, 0) == 2.5, "double fetch");
    shmem_barrier_all();
    if (me == 0) CHECK(shmem_double_atomic_swap(hd, 4.0, 0) == 2.5, "double swap");
    shmem_barrier_all();

    /* bitwise: each PE clears/sets/toggles its own bit in PE 0's word */
    shmem_uint_atomic_set(hu, 0, 0);
    shmem_barrier_all();
    shmem_uint_atomic_or(hu, 1u << me, 0);
    shmem_barrier_all();
    if (me == 0) CHECK(*hu == (1u << np) - 1, "uint or sets all PE bits");
    shmem_barrier_all();
    shmem_uint_atomic_xor(hu, 1u << me, 0);   /* toggles each bit back off */
    shmem_barrier_all();
    if (me == 0) CHECK(*hu == 0, "uint xor clears all bits");
    shmem_barrier_all();
    if (me == 0) *hu = ~0u;
    shmem_barrier_all();
    shmem_uint_atomic_and(hu, ~(1u << me), 0);
    shmem_barrier_all();
    if (me == 0) CHECK(*hu == ~((1u << np) - 1), "uint and clears PE bits");
    shmem_barrier_all();
    if (me == 0) {
        *hu = 0xf0u;
        CHECK(shmem_uint_atomic_fetch_and(hu, 0x3cu, 0) == 0xf0u && *hu == 0x30u, "fetch_and");
        CHECK(shmem_uint_atomic_fetch_or(hu, 0x0fu, 0) == 0x30u && *hu == 0x3fu, "fetch_or");
        CHECK(shmem_uint_atomic_fetch_xor(hu, 0xffu, 0) == 0x3fu && *hu == 0xc0u, "fetch_xor");
    }
    shmem_barrier_all();

    /* contexts */
    shmem_ctx_t ctx;
    CHECK(shmem_ctx_create(SHMEM_CTX_SERIALIZED, &ctx) == 0 && ctx != SHMEM_CTX_INVALID, "ctx_create");
    shmem_ctx_int64_atomic_set(ctx, h64, 0, 0);
    shmem_ctx_quiet(ctx);
    shmem_barrier_all();
    shmem_ctx_int64_atomic_add(ctx, h64, 1, 0);
    shmem_ctx_quiet(ctx);
    shmem_barrier_all();
    if (me == 0) CHECK(*h64 == np, "ctx add");
    shmem_barrier_all();
    shmem_ctx_int64_p(ctx, &s_i64[3], 55 + me, (me + 1) % np);
    shmem_ctx_fence(ctx);
    shmem_ctx_quiet(ctx);
    shmem_barrier_all();
    CHECK(s_i64[3] == 55 + (me + np - 1) % np, "ctx p");
    shmem_ctx_destroy(ctx);
    shmem_ctx_int64_p(SHMEM_CTX_DEFAULT, &s_i64[3], 1, me);   /* default ctx still works */
    CHECK(s_i64[3] == 1, "default ctx p to self");

    /* test and wait_until */
    uint64_t * flag = shmem_calloc(1, sizeof(uint64_t));
    shmem_barrier_all();
    CHECK(shmem_uint64_test(flag, SHMEM_CMP_EQ, 5) == 0, "test false before set");
    shmem_barrier_all();   /* every PE has checked before anyone sets */
    shmem_uint64_atomic_set(flag, 5, (me + 1) % np);
    shmem_uint64_wait_until(flag, SHMEM_CMP_EQ, 5);
    CHECK(shmem_uint64_test(flag, SHMEM_CMP_EQ, 5) == 1, "test true after wait_until");
    CHECK(shmem_uint64_test(flag, SHMEM_CMP_GT, 5) == 0, "test GT false");
    shmem_barrier_all();

    /* sync_all and sync */
    shmem_sync_all();
    static long psync[SHMEM_BARRIER_SYNC_SIZE];
    for (int i = 0; i < SHMEM_BARRIER_SYNC_SIZE; i++) psync[i] = SHMEM_SYNC_VALUE;
    shmem_barrier_all();
    shmem_sync(0, 0, np, psync);

    shmem_free(flag);
    shmem_free(hd);
    shmem_free(hu);
    shmem_free(h64);

    printf("[%d] test_14: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
