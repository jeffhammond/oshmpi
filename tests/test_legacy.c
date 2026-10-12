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

    shmem_barrier_all();
    printf("[%d] test_legacy: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
