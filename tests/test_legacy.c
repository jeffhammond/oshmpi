/* The pre-1.5 names and rarely used routines: the legacy atomic aliases (swap, cswap, fadd,
 * finc, add, inc, fetch, set) for each type that has them, put8/get8/iput8/iget8, complex
 * put/get and the complex reductions, shmem_nodename, the accessibility queries, version and
 * name, shmem_wtime, the cache stubs and the lock routines. */
#include <complex.h>
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

static long psync[SHMEM_REDUCE_SYNC_SIZE + SHMEM_BCAST_SYNC_SIZE];
static long lock;                                       /* data-segment lock */

/* every PE runs the same battery on the next PE's copy of one variable */
#define LEGACY_AMO(N, T)                                                                        \
    do {                                                                                        \
        T *v = shmem_calloc(2, sizeof(T));                                                      \
        shmem_barrier_all();                                                                    \
        shmem_##N##_set(&v[0], (T) (10 + me), next);                                            \
        shmem_barrier_all();                                                                    \
        CHECK(v[0] == (T) (10 + prev), #N "_set");                                              \
        CHECK(shmem_##N##_fetch(&v[0], next) == (T) (10 + me), #N "_fetch");                    \
        shmem_barrier_all();                                                                    \
        shmem_##N##_set(&v[0], 0, me);                                                          \
        shmem_barrier_all();                                                                    \
        shmem_##N##_inc(&v[0], 0);                                                              \
        shmem_##N##_add(&v[0], 2, 0);                                                           \
        T f = shmem_##N##_finc(&v[1], 0);                                                       \
        T g = shmem_##N##_fadd(&v[1], 5, 0);                                                    \
        (void) f; (void) g;                                                                     \
        shmem_barrier_all();                                                                    \
        if (me == 0) {                                                                          \
            CHECK(v[0] == (T) (3 * np), #N "_inc/_add");                                        \
            CHECK(v[1] == (T) (6 * np), #N "_finc/_fadd");                                      \
        }                                                                                       \
        shmem_barrier_all();                                                                    \
        shmem_##N##_set(&v[0], 50, me);                                                         \
        T old = shmem_##N##_swap(&v[0], 51, me);                                                \
        CHECK(old == 50 && v[0] == 51, #N "_swap");                                             \
        old = shmem_##N##_cswap(&v[0], 99, 7, me);                                              \
        CHECK(old == 51 && v[0] == 51, #N "_cswap that fails");                                 \
        old = shmem_##N##_cswap(&v[0], 51, 7, me);                                              \
        CHECK(old == 51 && v[0] == 7, #N "_cswap that succeeds");                               \
        shmem_barrier_all();                                                                    \
        shmem_free(v);                                                                          \
    } while (0)


/* ---- the legacy routines on every type they exist for ---------------------------------- */

#define LVAL(who, i) ((((who) * 13 + (i) * 5) % 90) + 1)

/* active-set reductions over all PEs; pWrk and pSync are static, so they are symmetric */
#define LBIT(OP, N, T, FOLD)                                                                       \
    do {                                                                                           \
        static T s[4], d[4], w[16];                                                                \
        static long ps[SHMEM_REDUCE_SYNC_SIZE + 8];                                                \
        for (int i = 0; i < SHMEM_REDUCE_SYNC_SIZE + 8; i++) ps[i] = SHMEM_SYNC_VALUE;             \
        for (int i = 0; i < 4; i++) s[i] = (T) ((~(1 << ((me + i) % 7))) & 0x7F);                  \
        shmem_barrier_all();                                                                       \
        shmem_##N##_##OP##_to_all(d, s, 4, 0, 0, np, w, ps);                                       \
        for (int i = 0; i < 4; i++) {                                                              \
            T e = (T) ((~(1 << (i % 7))) & 0x7F);                                                  \
            for (int q = 1; q < np; q++) { T x = (T) ((~(1 << ((q + i) % 7))) & 0x7F); e = FOLD; } \
            CHECK(d[i] == e, #N " " #OP "_to_all");                                                \
        }                                                                                          \
        shmem_barrier_all();                                                                       \
    } while (0)

#define LRED(OP, N, T, INIT, FOLD)                                                                 \
    do {                                                                                           \
        static T s[4], d[4], w[16];                                                                \
        static long ps[SHMEM_REDUCE_SYNC_SIZE + 8];                                                \
        for (int i = 0; i < SHMEM_REDUCE_SYNC_SIZE + 8; i++) ps[i] = SHMEM_SYNC_VALUE;             \
        for (int i = 0; i < 4; i++) s[i] = (T) INIT(me, i);                                        \
        shmem_barrier_all();                                                                       \
        shmem_##N##_##OP##_to_all(d, s, 4, 0, 0, np, w, ps);                                       \
        for (int i = 0; i < 4; i++) {                                                              \
            T e = (T) INIT(0, i);                                                                  \
            for (int q = 1; q < np; q++) { T x = (T) INIT(q, i); e = FOLD; }                       \
            CHECK(d[i] == e, #N " " #OP "_to_all");                                                \
        }                                                                                          \
        shmem_barrier_all();                                                                       \
    } while (0)

#define I_MM(who, i)    ((((who) * 7 + (i) * 3) % 11) + 1)
#define I_SUM(who, i)   ((who) + (i) + 1)
#define I_PROD(who, i)  ((who) == 0 ? (i) + 2 : (who) == 1 ? 3 : 1)

#define LEG_INT(N, T)                                                                              \
    LBIT(and, N, T, (T) (e & x)); LBIT(or, N, T, (T) (e | x)); LBIT(xor, N, T, (T) (e ^ x));       \
    LRED(max, N, T, I_MM, (x > e ? x : e)); LRED(min, N, T, I_MM, (x < e ? x : e));                \
    LRED(sum, N, T, I_SUM, (T) (e + x)); LRED(prod, N, T, I_PROD, (T) (e * x));
#define LEG_REAL(N, T)                                                                             \
    LRED(max, N, T, I_MM, (x > e ? x : e)); LRED(min, N, T, I_MM, (x < e ? x : e));                \
    LRED(sum, N, T, I_SUM, (T) (e + x)); LRED(prod, N, T, I_PROD, (T) (e * x));

/* 32- and 64-bit collectives; a separate pSync each */
#define LCOLL(B, T)                                                                                \
    do {                                                                                           \
        static T s[16], d[8 * 16], b[8];                                                            \
        static long p1[64], p2[64], p3[64], p4[64];                                                \
        for (int i = 0; i < 64; i++) p1[i] = p2[i] = p3[i] = p4[i] = SHMEM_SYNC_VALUE;             \
        for (int i = 0; i < 8; i++) { s[i] = (T) LVAL(me, i); b[i] = (T) (me == 0 ? LVAL(0, i) : 0); } \
        for (int i = 0; i < 8 * 16; i++) d[i] = 0;                                                 \
        shmem_barrier_all();                                                                       \
        shmem_broadcast##B(d, b, 8, 0, 0, 0, np, p1);                                              \
        if (me != 0) for (int i = 0; i < 8; i++) CHECK(d[i] == (T) LVAL(0, i), "broadcast" #B);    \
        shmem_barrier_all();                                                                       \
        for (int i = 0; i < 8 * 16; i++) d[i] = 0;                                                 \
        shmem_fcollect##B(d, s, 3, 0, 0, np, p2);                                                  \
        for (int q = 0; q < np; q++) for (int i = 0; i < 3; i++)                                   \
            CHECK(d[q * 3 + i] == (T) LVAL(q, i), "fcollect" #B);                                  \
        shmem_barrier_all();                                                                       \
        for (int i = 0; i < 8 * 16; i++) d[i] = 0;                                                 \
        shmem_collect##B(d, s, (size_t) (me % 3 + 1), 0, 0, np, p3);                               \
        { int at = 0;                                                                              \
          for (int q = 0; q < np; q++) for (int i = 0; i < q % 3 + 1; i++)                         \
              CHECK(d[at++] == (T) LVAL(q, i), "collect" #B); }                                    \
        shmem_barrier_all();                                                                       \
        for (int l = 0; l < np; l++) s[l] = (T) LVAL(me, l);                              \
        for (int i = 0; i < 16; i++) d[i] = 0;                                                     \
        shmem_alltoall##B(d, s, 1, 0, 0, np, p4);                                                  \
        for (int k = 0; k < np; k++) CHECK(d[k] == (T) LVAL(k, me), "alltoall" #B);       \
        shmem_barrier_all();                                                                       \
    } while (0)

#define LWAIT(N, T)                                                                                \
    do {                                                                                           \
        static T v, u;                                                                             \
        v = 0; u = 0;                                                                              \
        shmem_barrier_all();                                                                       \
        shmem_##N##_p(&v, 1, next);                                                                \
        shmem_##N##_wait(&v, 0);                    /* returns once v != 0 */                      \
        CHECK(v == 1, #N "_wait");                                                                 \
        shmem_##N##_p(&u, 5, next);                                                                \
        shmem_##N##_wait_until(&u, SHMEM_CMP_GE, 5);                                               \
        CHECK(u == 5, #N "_wait_until");                                                           \
        shmem_barrier_all();                                                                       \
    } while (0)

#define LSIZED(S, T)                                                                               \
    do {                                                                                           \
        static T d[16];                                                                            \
        T loc[16], back[16];                                                                       \
        for (int i = 0; i < 16; i++) { loc[i] = (T) LVAL(me, i); d[i] = 0; back[i] = 0; }          \
        shmem_barrier_all();                                                                       \
        shmem_put##S(d, loc, 4, next);                                                             \
        shmem_put##S##_nbi(d + 4, loc, 4, next);                                                   \
        shmem_quiet();                                                                             \
        shmem_barrier_all();                                                                       \
        for (int i = 0; i < 4; i++)                                                                \
            CHECK(d[i] == (T) LVAL(prev, i) && d[4 + i] == (T) LVAL(prev, i), "put" #S);           \
        shmem_get##S(back, d, 4, next);                                                            \
        shmem_get##S##_nbi(back + 4, d + 4, 4, next);                                              \
        shmem_quiet();                                                                             \
        for (int i = 0; i < 4; i++)                                                                \
            CHECK(back[i] == (T) LVAL(me, i) && back[4 + i] == (T) LVAL(me, i), "get" #S);         \
        shmem_barrier_all();                                                                       \
    } while (0)

#define LSIZED_STRIDED(S, T)                                                                       \
    do {                                                                                           \
        static T d[16];                                                                            \
        T loc[16], back[16];                                                                       \
        for (int i = 0; i < 16; i++) { loc[i] = (T) LVAL(me, i); d[i] = 0; back[i] = 0; }          \
        shmem_barrier_all();                                                                       \
        shmem_iput##S(d, loc, 2, 1, 4, next);                                                      \
        shmem_barrier_all();                                                                       \
        for (int k = 0; k < 4; k++) CHECK(d[2 * k] == (T) LVAL(prev, k), "iput" #S);               \
        shmem_iget##S(back, d, 1, 2, 4, next);                                                     \
        for (int k = 0; k < 4; k++) CHECK(back[k] == (T) LVAL(me, k), "iget" #S);                  \
        shmem_barrier_all();                                                                       \
    } while (0)

static void legacy_types(void)
{
    int next = (me + 1) % np, prev = (me + np - 1) % np;
    LEG_INT(short, short) LEG_INT(int, int) LEG_INT(long, long) LEG_INT(longlong, long long)
    LEG_REAL(float, float) LEG_REAL(double, double) LEG_REAL(longdouble, long double)
    LCOLL(32, uint32_t);
    LCOLL(64, uint64_t);
    LWAIT(short, short); LWAIT(int, int); LWAIT(long, long); LWAIT(longlong, long long);
    LSIZED(8, uint8_t); LSIZED(16, uint16_t); LSIZED(32, uint32_t); LSIZED(64, uint64_t);
    LSIZED(128, long double);
    LSIZED_STRIDED(8, uint8_t); LSIZED_STRIDED(16, uint16_t); LSIZED_STRIDED(32, uint32_t);
    LSIZED_STRIDED(64, uint64_t); LSIZED_STRIDED(128, long double);

    /* the untyped shmem_wait and shmem_wait_until (long) */
    {
        static long v, u;
        v = 0; u = 0;
        shmem_barrier_all();
        shmem_long_p(&v, 1, next);
        shmem_wait(&v, 0);
        CHECK(v == 1, "shmem_wait");
        shmem_long_p(&u, 4, next);
        shmem_wait_until(&u, SHMEM_CMP_EQ, 4);
        CHECK(u == 4, "shmem_wait_until");
        shmem_barrier_all();
    }

    /* aligned and resized allocations, the cache stubs, pcontrol, shmem_ptr */
    {
        void *a = shmemalign(256, 1000);
        CHECK(a != NULL && ((uintptr_t) a % 256) == 0, "shmemalign alignment");
        char *r = shrealloc(a, 5000);
        CHECK(r != NULL, "shrealloc");
        shmem_free(r);
        shmem_set_cache_inv(); shmem_clear_cache_inv();
        int tgt = 0; shmem_set_cache_line_inv(&tgt); shmem_clear_cache_line_inv(&tgt);
        shmem_pcontrol(0);
        shmem_pcontrol(1);
        shmem_barrier_all();
    }
}

/* shmem_<T>_wait_until and shmem_<T>_test on every type that has them */
#define LSYNC(N, T)                                                                                \
    do {                                                                                           \
        static T v;                                                                                \
        v = 0;                                                                                     \
        shmem_barrier_all();                                                                       \
        CHECK(shmem_##N##_test(&v, SHMEM_CMP_EQ, (T) 1) == 0, #N "_test of an unset variable");    \
        shmem_barrier_all();                                                                       \
        shmem_##N##_p(&v, (T) 1, next);                                                            \
        shmem_##N##_wait_until(&v, SHMEM_CMP_EQ, (T) 1);                                           \
        CHECK(shmem_##N##_test(&v, SHMEM_CMP_GE, (T) 1) != 0, #N "_test of a set variable");       \
        shmem_barrier_all();                                                                       \
    } while (0)

static void sync_types(void)
{
    int next = (me + 1) % np;
    LSYNC(short, short); LSYNC(ushort, unsigned short); LSYNC(int, int); LSYNC(uint, unsigned int);
    LSYNC(long, long); LSYNC(ulong, unsigned long); LSYNC(longlong, long long);
    LSYNC(ulonglong, unsigned long long); LSYNC(int32, int32_t); LSYNC(int64, int64_t);
    LSYNC(uint32, uint32_t); LSYNC(uint64, uint64_t); LSYNC(size, size_t); LSYNC(ptrdiff, ptrdiff_t);
}

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();
    int next = (me + 1) % np, prev = (me + np - 1) % np;
    for (int i = 0; i < SHMEM_REDUCE_SYNC_SIZE + SHMEM_BCAST_SYNC_SIZE; i++) psync[i] = SHMEM_SYNC_VALUE;

    /* ---- legacy atomics */
    LEGACY_AMO(int, int);
    LEGACY_AMO(long, long);
    LEGACY_AMO(longlong, long long);

    /* swap, set and fetch also exist for the floating-point types */
    {
        double *d = shmem_calloc(1, sizeof(double));
        float *f = shmem_calloc(1, sizeof(float));
        shmem_barrier_all();
        shmem_double_set(d, 2.5 + me, next);
        shmem_float_set(f, 1.5f + me, next);
        shmem_barrier_all();
        CHECK(*d == 2.5 + prev && *f == 1.5f + prev, "double/float set");
        CHECK(shmem_double_fetch(d, next) == 2.5 + me, "double_fetch");
        CHECK(shmem_float_fetch(f, next) == 1.5f + me, "float_fetch");
        shmem_barrier_all();
        CHECK(shmem_double_swap(d, 9.0, me) == 2.5 + prev && *d == 9.0, "double_swap");
        CHECK(shmem_float_swap(f, 8.0f, me) == 1.5f + prev && *f == 8.0f, "float_swap");
        shmem_barrier_all();
        shmem_free(f);
        shmem_free(d);
    }

    /* ---- put8 / get8 / iput8 / iget8: 8-bit elements */
    {
        unsigned char *b = shmem_calloc(64, 1);
        unsigned char src[64], out[64];
        for (int i = 0; i < 64; i++) src[i] = (unsigned char) (me * 5 + i);
        shmem_barrier_all();
        shmem_put8(b, src, 20, next);
        shmem_barrier_all();
        int bad = 0;
        for (int i = 0; i < 20; i++) if (b[i] != (unsigned char) (prev * 5 + i)) bad = 1;
        for (int i = 20; i < 64; i++) if (b[i] != 0) bad = 1;
        CHECK(!bad, "put8 moves 20 bytes and no more");
        memset(out, 0, sizeof out);
        shmem_get8(out, b, 20, next);
        bad = 0;
        for (int i = 0; i < 20; i++) if (out[i] != (unsigned char) (me * 5 + i)) bad = 1;
        CHECK(!bad, "get8");
        shmem_barrier_all();
        memset(b, 0, 64);
        shmem_barrier_all();
        shmem_iput8(b, src, 3, 2, 10, next);
        shmem_barrier_all();
        bad = 0;
        for (int k = 0; k < 10; k++) if (b[3 * k] != (unsigned char) (prev * 5 + 2 * k)) bad = 1;
        for (int k = 0; k < 10; k++) if (b[3 * k + 1] != 0 || b[3 * k + 2] != 0) bad = 1;
        CHECK(!bad, "iput8");
        memset(out, 0, sizeof out);
        shmem_iget8(out, b, 2, 3, 10, next);
        bad = 0;
        for (int k = 0; k < 10; k++) if (out[2 * k] != (unsigned char) (me * 5 + 2 * k)) bad = 1;
        CHECK(!bad, "iget8");
        shmem_barrier_all();
        shmem_free(b);
    }

    /* ---- complex put / get and reductions */
    {
        float _Complex *cf = shmem_calloc(8, sizeof(float _Complex));
        double _Complex *cd = shmem_calloc(8, sizeof(double _Complex));
        float _Complex sf[8];
        double _Complex sd[8];
        for (int i = 0; i < 8; i++) { sf[i] = (float) (me + i) + I * (float) (2 * i); sd[i] = (me + 0.5 * i) - I * (3.0 * i); }
        shmem_barrier_all();
        shmem_complexf_put(cf, sf, 8, next);
        shmem_complexd_put(cd, sd, 8, next);
        shmem_barrier_all();
        int bad = 0;
        for (int i = 0; i < 8; i++) {
            if (cf[i] != (float) (prev + i) + I * (float) (2 * i)) bad = 1;
            if (cd[i] != (prev + 0.5 * i) - I * (3.0 * i)) bad = 1;
        }
        CHECK(!bad, "complexf/complexd put");
        float _Complex gf[8]; double _Complex gd[8];
        shmem_complexf_get(gf, cf, 8, next);
        shmem_complexd_get(gd, cd, 8, next);
        bad = 0;
        for (int i = 0; i < 8; i++) { if (gf[i] != sf[i]) bad = 1; if (gd[i] != sd[i]) bad = 1; }
        CHECK(!bad, "complexf/complexd get");
        shmem_barrier_all();

        /* sum and product over all PEs */
        float _Complex *rf = shmem_calloc(4, sizeof(float _Complex));
        double _Complex *rd = shmem_calloc(4, sizeof(double _Complex));
        float _Complex *pf = shmem_calloc(4, sizeof(float _Complex));
        double _Complex *pd = shmem_calloc(4, sizeof(double _Complex));
        float _Complex wf[4]; double _Complex wd[4];
        float _Complex ef[4]; double _Complex ed[4];
        for (int i = 0; i < 4; i++) {
            cf[i] = (float) (me + 1) + I * (float) i;
            cd[i] = (double) (me + 1) - I * (double) i;
            ef[i] = 1; ed[i] = 0;
        }
        for (int i = 0; i < 4; i++) {
            float _Complex ps = 0, pp = 1; double _Complex ds = 0, dp = 1;
            for (int p = 0; p < np; p++) {
                float _Complex x = (float) (p + 1) + I * (float) i;
                double _Complex y = (double) (p + 1) - I * (double) i;
                ps += x; pp *= x; ds += y; dp *= y;
            }
            wf[i] = ps; ef[i] = pp; wd[i] = ds; ed[i] = dp;
        }
        shmem_barrier_all();
        shmem_complexf_sum_to_all(rf, cf, 4, 0, 0, np, NULL, psync);
        shmem_barrier_all();
        shmem_complexd_sum_to_all(rd, cd, 4, 0, 0, np, NULL, psync);
        shmem_barrier_all();
        shmem_complexf_prod_to_all(pf, cf, 4, 0, 0, np, NULL, psync);
        shmem_barrier_all();
        shmem_complexd_prod_to_all(pd, cd, 4, 0, 0, np, NULL, psync);
        shmem_barrier_all();
        bad = 0;
        for (int i = 0; i < 4; i++) {
            if (cabsf(rf[i] - wf[i]) > 1e-3f) bad = 1;
            if (cabs(rd[i] - wd[i]) > 1e-9) bad = 1;
            if (cabsf(pf[i] - ef[i]) > 1e-2f * (1 + cabsf(ef[i]))) bad = 1;
            if (cabs(pd[i] - ed[i]) > 1e-9 * (1 + cabs(ed[i]))) bad = 1;
        }
        CHECK(!bad, "complexf/complexd sum_to_all and prod_to_all");
        shmem_free(pd); shmem_free(pf); shmem_free(rd); shmem_free(rf);
        shmem_free(cd); shmem_free(cf);
    }

    /* ---- queries */
    {
        char *nn = shmem_nodename();
        CHECK(nn != NULL && nn[0] != '\0', "shmem_nodename returns a name");
        CHECK(shmem_pe_accessible(next) == 1 && shmem_pe_accessible(me) == 1, "pe_accessible");
        CHECK(shmem_pe_accessible(np) == 0 && shmem_pe_accessible(-1) == 0, "pe_accessible out of range");
        int *h = shmem_malloc(sizeof(int));
        static int seg;
        int local_only;
        CHECK(shmem_addr_accessible(h, next) == 1, "addr_accessible of a heap object");
        CHECK(shmem_addr_accessible(&seg, next) == 1, "addr_accessible of a data-segment object");
        CHECK(shmem_addr_accessible(&local_only, next) == 0, "addr_accessible of a stack object");
        CHECK(shmem_addr_accessible(h, np) == 0 && shmem_addr_accessible(h, -1) == 0, "addr_accessible out of range");
        shmem_free(h);

        int major = 0, minor = -1;
        shmem_info_get_version(&major, &minor);
        CHECK(major >= 1 && minor >= 0, "info_get_version");
        char name[SHMEM_MAX_NAME_LEN];
        memset(name, 0, sizeof name);
        shmem_info_get_name(name);
        CHECK(name[0] != '\0', "info_get_name");

        double t0 = shmem_wtime();
        shmem_barrier_all();
        double t1 = shmem_wtime();
        CHECK(t1 >= t0 && t0 >= 0, "shmem_wtime does not go backward");
        int tl = -1;
        shmem_query_thread(&tl);
        CHECK(tl >= SHMEM_THREAD_SINGLE && tl <= SHMEM_THREAD_MULTIPLE, "query_thread range");

        /* the cache routines are no-ops */
        int x = 5;
        shmem_set_cache_inv();
        shmem_set_cache_line_inv(&x);
        shmem_udcflush();
        shmem_udcflush_line(&x);
        CHECK(x == 5, "cache stubs");
    }

    /* ---- locks on a data-segment word and on a heap word */
    {
        long *hl = shmem_calloc(1, sizeof(long));
        int *counter = shmem_calloc(1, sizeof(int));
        shmem_barrier_all();
        for (int k = 0; k < 20; k++) {
            shmem_set_lock(&lock);
            int v = shmem_int_g(counter, 0);
            shmem_int_p(counter, v + 1, 0);
            shmem_quiet();
            shmem_clear_lock(&lock);
        }
        shmem_barrier_all();
        if (me == 0) CHECK(*counter == 20 * np, "data-segment lock gives mutual exclusion");
        shmem_barrier_all();
        if (me == 0) {
            CHECK(shmem_test_lock(hl) == 0, "test_lock acquires a free lock");
            CHECK(shmem_test_lock(hl) != 0, "test_lock on a held lock");
            shmem_clear_lock(hl);
            CHECK(shmem_test_lock(hl) == 0, "test_lock after clear");
            shmem_clear_lock(hl);
        }
        shmem_barrier_all();
        shmem_free(counter);
        shmem_free(hl);
    }

    legacy_types();
    sync_types();

    shmem_barrier_all();
    printf("[%d] test_legacy: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
