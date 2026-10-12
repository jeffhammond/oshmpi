/* OpenSHMEM 1.6 additions: shmem_query_initialized, sum scans, interleaved block transfers,
 * shmem_pe_quiet, and the clarified edge cases of team split and test_all. */
#include <complex.h>
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

#define NS 5

#define SCAN_TEST(NAME, T, VAL)                                                                    \
static void scan_##NAME(shmem_team_t team, int trank, const char *what)                            \
{                                                                                                  \
    static T src[NS], in[NS], dst[NS], ex[NS], ip[NS];     /* shmem_malloc is collective on the world */ \
    for (int j = 0; j < NS; j++) { src[j] = VAL(trank, j); in[j] = src[j]; dst[j] = ex[j] = (T) 99; } \
    shmem_team_sync(team);                                                                         \
    CHECK(shmem_##NAME##_sum_inscan(team, dst, src, NS) == 0, what);                               \
    CHECK(shmem_##NAME##_sum_exscan(team, ex, src, NS) == 0, what);                                \
    shmem_##NAME##_sum_inscan(team, in, in, NS);                       /* in place */              \
    for (int j = 0; j < NS; j++) {                                                                 \
        T inc = 0, exc = 0;                                                                        \
        for (int k = 0; k <= trank; k++) { inc += VAL(k, j); if (k < trank) exc += VAL(k, j); }    \
        CHECK(dst[j] == inc, what);                                                                \
        CHECK(in[j] == inc, what);                                                                 \
        CHECK(ex[j] == exc, what);                                                                 \
    }                                                                                              \
    shmem_team_sync(team);                                                                         \
    for (int j = 0; j < NS; j++) ip[j] = VAL(trank, j);                                            \
    shmem_team_sync(team);                                                                         \
    shmem_##NAME##_sum_exscan(team, ip, ip, NS);                       /* in place, exclusive */   \
    for (int j = 0; j < NS; j++) {                                                                 \
        T exc = 0;                                                                                 \
        for (int k = 0; k < trank; k++) exc += VAL(k, j);                                          \
        CHECK(ip[j] == exc, what);                                                                 \
    }                                                                                              \
    shmem_team_sync(team);                                                                         \
}

#define V_INT(r, j)    ((int) ((r) * 7 + (j) + 1))
#define V_LONG(r, j)   ((long) ((r) + 1) * 1000000000L + (j))
#define V_UINT(r, j)   ((unsigned) ((r) + (j) + 3))
#define V_DBL(r, j)    ((double) (r) * 0.5 + (j))
#define V_FLT(r, j)    ((float) ((r) + 1) * 2 + (j))
#define V_CD(r, j)     ((double) (r) + ((double) (j)) * I)
#define V_CF(r, j)     ((float) ((r) * 2) + ((float) (j) + 1) * I)
SCAN_TEST(int, int, V_INT)
SCAN_TEST(long, long, V_LONG)
SCAN_TEST(uint, unsigned int, V_UINT)
SCAN_TEST(double, double, V_DBL)
SCAN_TEST(float, float, V_FLT)
SCAN_TEST(complexd, double _Complex, V_CD)
SCAN_TEST(complexf, float _Complex, V_CF)

static void all_scans(shmem_team_t team, int trank, const char *what)
{
    scan_int(team, trank, what);
    scan_long(team, trank, what);
    scan_uint(team, trank, what);
    scan_double(team, trank, what);
    scan_float(team, trank, what);
    scan_complexd(team, trank, what);
    scan_complexf(team, trank, what);
}

/* blocks of bsize, source stride sst, destination stride dst, all in elements */
#define IB_TEST(NAME, T)                                                                           \
static void ib_##NAME(shmem_ctx_t ctx)                                                             \
{                                                                                                  \
    const size_t bsize = 3, nb = 4;                                                                \
    const ptrdiff_t dst = 5, sst = 4;                                                              \
    size_t dn = nb * dst, sn = nb * sst;                                                       \
    T *d = shmem_malloc(dn * sizeof(T));                                                           \
    T *s = malloc(sn * sizeof(T)), *back = malloc(sn * sizeof(T));                                 \
    for (size_t i = 0; i < dn; i++) d[i] = (T) 100;                                                \
    for (size_t i = 0; i < sn; i++) { s[i] = (T) (me * 10 + i + 1); back[i] = (T) 77; }            \
    int next = (me + 1) % np, prev = (me + np - 1) % np;                                           \
    shmem_barrier_all();                                                                           \
    if (ctx == SHMEM_CTX_DEFAULT) shmem_##NAME##_ibput(d, s, dst, sst, bsize, nb, next);           \
    else shmem_ctx_##NAME##_ibput(ctx, d, s, dst, sst, bsize, nb, next);                           \
    shmem_barrier_all();                                                                           \
    for (size_t b = 0; b < nb; b++)                                                                \
        for (size_t e = 0; e < (size_t) dst; e++) {                                                \
            T want = e < bsize ? (T) (prev * 10 + b * sst + e + 1) : (T) 100;                      \
            CHECK(d[b * dst + e] == want, #NAME " ibput");                                         \
        }                                                                                          \
    if (ctx == SHMEM_CTX_DEFAULT) shmem_##NAME##_ibget(back, d, sst, dst, bsize, nb, me);          \
    else shmem_ctx_##NAME##_ibget(ctx, back, d, sst, dst, bsize, nb, me);                          \
    for (size_t b = 0; b < nb; b++)                                                                \
        for (size_t e = 0; e < (size_t) sst; e++)                                                  \
            CHECK(back[b * sst + e] == (e < bsize ? (T) (prev * 10 + b * sst + e + 1) : (T) 77),   \
                  #NAME " ibget");                                                                 \
    shmem_barrier_all();                                                                           \
    free(s); free(back); shmem_free(d);                                                            \
}
IB_TEST(int, int)
IB_TEST(double, double)
IB_TEST(char, char)
IB_TEST(uint64, uint64_t)
IB_TEST(longdouble, long double)

static void ib_sized(void)
{
    /* sized: 32-bit and 64-bit blocks, plus the contiguous and one-element special cases */
    uint32_t *d = shmem_malloc(64 * sizeof(uint32_t));
    uint32_t s[64];
    int next = (me + 1) % np, prev = (me + np - 1) % np;
    for (int i = 0; i < 64; i++) { s[i] = me * 1000 + i; d[i] = 0; }
    shmem_barrier_all();
    shmem_ibput32(d, s, 6, 4, 2, 5, next);          /* blocks of 2 */
    shmem_ibput32(d + 40, s, 3, 3, 3, 4, next);     /* contiguous: strides equal the block size */
    shmem_ibput32(d + 60, s, 1, 1, 1, 1, next);
    shmem_barrier_all();
    for (int b = 0; b < 5; b++)
        for (int e = 0; e < 2; e++) CHECK(d[b * 6 + e] == (uint32_t) (prev * 1000 + b * 4 + e), "ibput32");
    for (int i = 0; i < 12; i++) CHECK(d[40 + i] == (uint32_t) (prev * 1000 + i), "ibput32 contiguous");
    CHECK(d[60] == (uint32_t) (prev * 1000), "ibput32 single");
    uint32_t back[64] = { 0 };
    shmem_ibget32(back, d, 4, 6, 2, 5, me);
    for (int b = 0; b < 5; b++)
        for (int e = 0; e < 2; e++) CHECK(back[b * 4 + e] == (uint32_t) (prev * 1000 + b * 4 + e), "ibget32");
    /* with a ctx, 64-bit */
    uint64_t *d8 = shmem_malloc(32 * sizeof(uint64_t));
    uint64_t s8[32], b8[32] = { 0 };
    for (int i = 0; i < 32; i++) { s8[i] = ((uint64_t) 1 << 40) + me * 100 + i; d8[i] = 0; }
    shmem_barrier_all();
    shmem_ctx_ibput64(SHMEM_CTX_DEFAULT, d8, s8, 8, 4, 3, 3, next);
    shmem_barrier_all();
    shmem_ctx_ibget64(SHMEM_CTX_DEFAULT, b8, d8, 4, 8, 3, 3, me);
    for (int b = 0; b < 3; b++)
        for (int e = 0; e < 3; e++)
            CHECK(b8[b * 4 + e] == ((uint64_t) 1 << 40) + prev * 100 + b * 4 + e, "ibget64 ctx");
    /* zero-sized transfers do nothing */
    shmem_ibput32(NULL, NULL, 1, 1, 0, 3, next);
    shmem_ibput32(NULL, NULL, 1, 1, 3, 0, next);
    shmem_barrier_all();
    shmem_free(d8);
    shmem_free(d);
}


/* ibput/ibget of every size, plain and with a context */
#define IB_SIZE(S, T)                                                                              \
{                                                                                                  \
    T *d = shmem_malloc(32 * sizeof(T));                                                           \
    T loc[32], back[32];                                                                           \
    int next = (me + 1) % np, prev = (me + np - 1) % np;                                           \
    for (int round = 0; round < 2; round++) {                                                      \
        for (int i = 0; i < 32; i++) { loc[i] = (T) (me * 3 + i + 1); d[i] = 0; back[i] = 0; }     \
        shmem_barrier_all();                                                                       \
        if (round) shmem_ctx_ibput##S(SHMEM_CTX_DEFAULT, d, loc, 5, 4, 2, 3, next);                \
        else shmem_ibput##S(d, loc, 5, 4, 2, 3, next);                                             \
        shmem_barrier_all();                                                                       \
        for (int b = 0; b < 3; b++) for (int e = 0; e < 2; e++)                                    \
            CHECK(d[5 * b + e] == (T) (prev * 3 + 4 * b + e + 1), "ibput" #S);                     \
        if (round) shmem_ctx_ibget##S(SHMEM_CTX_DEFAULT, back, d, 4, 5, 2, 3, me);                 \
        else shmem_ibget##S(back, d, 4, 5, 2, 3, me);                                              \
        for (int b = 0; b < 3; b++) for (int e = 0; e < 2; e++)                                    \
            CHECK(back[4 * b + e] == (T) (prev * 3 + 4 * b + e + 1), "ibget" #S);                  \
        shmem_barrier_all();                                                                       \
    }                                                                                              \
    shmem_free(d);                                                                                 \
}

static void ib_all_sizes(void)
{
    IB_SIZE(8, uint8_t)
    IB_SIZE(16, uint16_t)
    IB_SIZE(32, uint32_t)
    IB_SIZE(64, uint64_t)
    IB_SIZE(128, long double)
}

/* the shmemx names of signal set and add */
static void x_signal(void)
{
    uint64_t *sig = shmem_calloc(2, sizeof(uint64_t));
    int next = (me + 1) % np, prev = (me + np - 1) % np;
    shmem_barrier_all();
    shmemx_signal_set(&sig[0], 40 + me, next);
    shmemx_signal_add(&sig[1], 2, 0);
    shmem_barrier_all();
    CHECK(sig[0] == (uint64_t) (40 + prev), "shmemx_signal_set");
    if (me == 0) CHECK(sig[1] == (uint64_t) (2 * np), "shmemx_signal_add");
    shmem_barrier_all();
    shmem_free(sig);
}

int main(void)
{
    int init = -1;
    shmem_query_initialized(&init);
    if (init != 0) { printf("FAIL: initialized before init\n"); errs++; }
    shmem_init();
    shmem_query_initialized(&init);
    CHECK(init != 0, "initialized after init");
    shmem_init();
    shmem_finalize();
    shmem_query_initialized(&init);
    CHECK(init != 0, "still initialized after a nested finalize");
    me = shmem_my_pe();
    np = shmem_n_pes();

    all_scans(SHMEM_TEAM_WORLD, me, "world scan");

    /* a team of the even PEs, and a team with a stride */
    shmem_team_t even = SHMEM_TEAM_INVALID;
    if (np >= 2) {
        shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 2, (np + 1) / 2, NULL, 0, &even);
        if (even != SHMEM_TEAM_INVALID) all_scans(even, shmem_team_my_pe(even), "even-team scan");
        if (even != SHMEM_TEAM_INVALID) shmem_team_destroy(even);
    }

    /* a scan over an invalid team fails without crashing the callers that check */
    CHECK(shmem_int_sum_inscan(SHMEM_TEAM_INVALID, NULL, NULL, 0) != 0, "scan on invalid team");

    /* the C11 generic names pick the type */
    {
        int *a = shmem_malloc(2 * sizeof(int)), *b = shmem_malloc(2 * sizeof(int));
        a[0] = 1; a[1] = me;
        shmem_barrier_all();
        shmem_sum_inscan(SHMEM_TEAM_WORLD, b, a, 2);
        CHECK(b[0] == me + 1, "generic sum_inscan");
        shmem_sum_exscan(SHMEM_TEAM_WORLD, b, a, 2);
        CHECK(b[0] == me, "generic sum_exscan");
        shmem_barrier_all();
        shmem_free(b); shmem_free(a);
    }

    ib_int(SHMEM_CTX_DEFAULT);
    ib_double(SHMEM_CTX_DEFAULT);
    ib_char(SHMEM_CTX_DEFAULT);
    ib_uint64(SHMEM_CTX_DEFAULT);
    ib_longdouble(SHMEM_CTX_DEFAULT);
    {
        shmem_ctx_t ctx;
        if (shmem_ctx_create(0, &ctx) == 0) { ib_int(ctx); ib_double(ctx); shmem_ctx_destroy(ctx); }
    }
    ib_sized();
    ib_all_sizes();
    x_signal();

    /* C11 generic ibput/ibget, default context and explicit context */
    {
        int *d = shmem_malloc(16 * sizeof(int));
        int s[16], back[16] = { 0 };
        for (int i = 0; i < 16; i++) { s[i] = me * 100 + i; d[i] = -1; }
        int next = (me + 1) % np, prev = (me + np - 1) % np;
        shmem_barrier_all();
        shmem_ibput(d, s, 4, 4, 2, 2, next);
        shmem_ctx_t c = SHMEM_CTX_DEFAULT;
        shmem_ibput(c, d + 8, s, 4, 4, 2, 2, next);
        shmem_barrier_all();
        CHECK(d[0] == prev * 100 && d[1] == prev * 100 + 1 && d[4] == prev * 100 + 4 && d[2] == -1, "generic ibput");
        CHECK(d[8] == prev * 100 && d[12] == prev * 100 + 4, "generic ctx ibput");
        shmem_ibget(back, d, 4, 4, 2, 2, me);
        CHECK(back[0] == prev * 100 && back[5] == prev * 100 + 5, "generic ibget");
        shmem_barrier_all();
        shmem_free(d);
    }

    /* shmem_pe_quiet orders puts to the listed PEs */
    {
        long *x = shmem_malloc(sizeof(long)), *flag = shmem_malloc(sizeof(long));
        *x = 0; *flag = 0;
        shmem_barrier_all();
        int next = (me + 1) % np;
        shmem_long_p(x, 42 + me, next);
        shmem_pe_quiet(&next, 1);
        shmem_long_atomic_set(flag, 1, next);
        shmem_pe_quiet(NULL, 0);                       /* nothing to do, must return */
        shmem_long_wait_until(flag, SHMEM_CMP_EQ, 1);
        int prev = (me + np - 1) % np;
        CHECK(*x == 42 + prev, "data visible once the flag is");
        shmem_ctx_pe_quiet(SHMEM_CTX_DEFAULT, &next, 1);
        shmem_barrier_all();
        shmem_free(flag); shmem_free(x);
    }

    /* 1.6 clarifications */
    {
        long v[2] = { 1, 2 };
        CHECK(shmem_long_test_all(v, 0, NULL, SHMEM_CMP_EQ, 5) == 1, "test_all of an empty set is 1");
        shmem_team_t t = (shmem_team_t) 0;
        int rc = shmem_team_split_strided(SHMEM_TEAM_INVALID, 0, 1, 1, NULL, 0, &t);
        CHECK(rc != 0, "split_strided of an invalid parent fails");
        CHECK(t == SHMEM_TEAM_INVALID, "split_strided of an invalid parent gives an invalid team");
        rc = shmem_team_split_2d(SHMEM_TEAM_INVALID, 1, NULL, 0, &t, NULL, 0, &t);
        CHECK(rc != 0, "split_2d of an invalid parent fails");
        rc = shmem_team_split_strided(SHMEM_TEAM_WORLD, np - 1, 0, 1, NULL, 0, &t);   /* stride 0 */
        CHECK(rc == 0, "stride 0 with size 1");
        CHECK((t != SHMEM_TEAM_INVALID) == (me == np - 1), "stride 0 team has one member");
        if (t != SHMEM_TEAM_INVALID) {
            CHECK(shmem_team_n_pes(t) == 1, "stride 0 team size");
            shmem_team_destroy(t);
        }
        rc = shmem_team_split_strided(SHMEM_TEAM_WORLD, np - 1, 1, 2, NULL, 0, &t);   /* runs off the end */
        CHECK(rc != 0 && t == SHMEM_TEAM_INVALID, "split past the end of the parent fails");
        if (np >= 2) {                                                                   /* negative stride */
            rc = shmem_team_split_strided(SHMEM_TEAM_WORLD, np - 1, -1, np, NULL, 0, &t);
            CHECK(rc == 0 && t != SHMEM_TEAM_INVALID, "negative stride");
            if (t != SHMEM_TEAM_INVALID) {
                CHECK(shmem_team_my_pe(t) == np - 1 - me, "negative stride reverses the order");
                shmem_team_destroy(t);
            }
        }
    }

    shmem_barrier_all();
    printf("[%d] test_shmem16: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    shmem_query_initialized(&init);
    if (init != 0) { printf("FAIL: initialized after finalize\n"); errs++; }
    return errs != 0;
}
