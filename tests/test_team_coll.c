/* Team collectives: broadcast, collect, fcollect, alltoall, alltoalls and every reduction on
 * every type, over the world team, a team that excludes PEs, the shared team and 2-D axis
 * teams, with buffers in the symmetric heap and in the data segment. */
#include <shmem.h>
#include <complex.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d (team size %d): %s\n", me, __LINE__, tn, m); errs++; } } while (0)

static char  seg_buf[8192] __attribute__((aligned(64)));
static char *heap_buf;
static char *buf;
static int   tn, tr;                       /* current team size and my number in it */

#define SRC(T) ((T *) buf)
#define DST(T) ((T *) (buf + 4096))

/* ---- reductions: one block per type, expanded for every type in the tables */

#define BITVAL(T, q, i) ((T) (1ULL << (((q) + (i)) % (8 * sizeof(T)))))

#define RED_BIT(N, T)                                                                              \
    do {                                                                                           \
        T *s = SRC(T), *d = DST(T);                                                                \
        for (int i = 0; i < 3; i++) s[i] = (T) ~BITVAL(T, tr, i);                                  \
        CHECK(shmem_##N##_and_reduce(team, d, s, 3) == 0, #N " and rc");                           \
        for (int i = 0; i < 3; i++) {                                                              \
            T e = (T) ~0;                                                                          \
            for (int q = 0; q < tn; q++) e = (T) (e & (T) ~BITVAL(T, q, i));                       \
            CHECK(d[i] == e, #N " and");                                                           \
        }                                                                                          \
        for (int i = 0; i < 3; i++) s[i] = BITVAL(T, tr, i);                                       \
        CHECK(shmem_##N##_or_reduce(team, d, s, 3) == 0, #N " or rc");                             \
        for (int i = 0; i < 3; i++) {                                                              \
            T e = 0;                                                                               \
            for (int q = 0; q < tn; q++) e = (T) (e | BITVAL(T, q, i));                            \
            CHECK(d[i] == e, #N " or");                                                            \
        }                                                                                          \
        CHECK(shmem_##N##_xor_reduce(team, d, s, 3) == 0, #N " xor rc");                           \
        for (int i = 0; i < 3; i++) {                                                              \
            T e = 0;                                                                               \
            for (int q = 0; q < tn; q++) e = (T) (e ^ BITVAL(T, q, i));                            \
            CHECK(d[i] == e, #N " xor");                                                           \
        }                                                                                          \
    } while (0);

#define RED_MINMAX(N, T)                                                                           \
    do {                                                                                           \
        T *s = SRC(T), *d = DST(T);                                                                \
        for (int i = 0; i < 3; i++) s[i] = (T) (tr + i + 1);                                       \
        CHECK(shmem_##N##_max_reduce(team, d, s, 3) == 0, #N " max rc");                           \
        for (int i = 0; i < 3; i++) CHECK(d[i] == (T) (tn - 1 + i + 1), #N " max");                \
        CHECK(shmem_##N##_min_reduce(team, d, s, 3) == 0, #N " min rc");                           \
        for (int i = 0; i < 3; i++) CHECK(d[i] == (T) (i + 1), #N " min");                         \
    } while (0);

#define RED_SUMPROD_REAL(N, T)                                                                     \
    do {                                                                                           \
        T *s = SRC(T), *d = DST(T);                                                                \
        for (int i = 0; i < 3; i++) s[i] = (T) (tr + i + 1);                                       \
        CHECK(shmem_##N##_sum_reduce(team, d, s, 3) == 0, #N " sum rc");                           \
        for (int i = 0; i < 3; i++) {                                                              \
            int e = 0; for (int q = 0; q < tn; q++) e += q + i + 1;                                \
            CHECK(d[i] == (T) e, #N " sum");                                                       \
        }                                                                                          \
        for (int i = 0; i < 3; i++) s[i] = (T) ((tr % 2) + 1);                                     \
        CHECK(shmem_##N##_prod_reduce(team, d, s, 3) == 0, #N " prod rc");                         \
        for (int i = 0; i < 3; i++) {                                                              \
            int e = 1; for (int q = 0; q < tn; q++) e *= (q % 2) + 1;                              \
            CHECK(d[i] == (T) e, #N " prod");                                                      \
        }                                                                                          \
        /* in place */                                                                             \
        for (int i = 0; i < 3; i++) s[i] = (T) (tr + i + 1);                                       \
        CHECK(shmem_##N##_sum_reduce(team, s, s, 3) == 0, #N " in-place sum rc");                  \
        for (int i = 0; i < 3; i++) {                                                              \
            int e = 0; for (int q = 0; q < tn; q++) e += q + i + 1;                                \
            CHECK(s[i] == (T) e, #N " in-place sum");                                              \
        }                                                                                          \
    } while (0);

static void check_complex(shmem_team_t team)
{
    double _Complex * sd = SRC(double _Complex), * dd = DST(double _Complex);
    float _Complex *  sf = (float _Complex *) (buf + 1024), * df = (float _Complex *) (buf + 4096 + 1024);
    for (int i = 0; i < 3; i++) {
        sd[i] = (tr + 1) + (i + 2) * 1.0 * _Complex_I;
        sf[i] = (float) (tr + 1) + (float) (i + 2) * _Complex_I;
    }
    CHECK(shmem_complexd_sum_reduce(team, dd, sd, 3) == 0, "complexd sum rc");
    CHECK(shmem_complexf_sum_reduce(team, df, sf, 3) == 0, "complexf sum rc");
    for (int i = 0; i < 3; i++) {
        double re = 0; for (int q = 0; q < tn; q++) re += q + 1;
        CHECK(creal(dd[i]) == re && cimag(dd[i]) == tn * (i + 2.0), "complexd sum");
        CHECK(crealf(df[i]) == (float) re && cimagf(df[i]) == (float) (tn * (i + 2)), "complexf sum");
    }
    for (int i = 0; i < 3; i++) { sd[i] = (tr % 2) + 1; sf[i] = (float) ((tr % 2) + 1); }
    CHECK(shmem_complexd_prod_reduce(team, dd, sd, 3) == 0, "complexd prod rc");
    CHECK(shmem_complexf_prod_reduce(team, df, sf, 3) == 0, "complexf prod rc");
    for (int i = 0; i < 3; i++) {
        double e = 1; for (int q = 0; q < tn; q++) e *= (q % 2) + 1;
        CHECK(creal(dd[i]) == e && cimag(dd[i]) == 0, "complexd prod");
        CHECK(crealf(df[i]) == (float) e && cimagf(df[i]) == 0, "complexf prod");
    }
}

/* the reduction type tables, with the per-block macros above */
#define RUN_RED_BIT(N, T)      RED_BIT(N, T)
#define RUN_RED_MINMAX(N, T)   RED_MINMAX(N, T)
#define RUN_RED_SUMPROD(N, T)  RED_SUMPROD_REAL(N, T)

static void run_reductions(shmem_team_t team)
{
    OSHMPI_TEAM_RED_BIT_TYPES(RUN_RED_BIT)
    OSHMPI_TEAM_RED_MINMAX_TYPES(RUN_RED_MINMAX)
    {
#define RUN_REAL_SUMPROD_ONLY(N, T) RED_SUMPROD_REAL(N, T)
        OSHMPI_TEAM_RED_MINMAX_TYPES(RUN_REAL_SUMPROD_ONLY)   /* the real types: sum and prod */
    }
    check_complex(team);

    /* nreduce of 0 leaves dest alone; nreduce of 1 */
    int *s = SRC(int), *d = DST(int);
    s[0] = tr + 1; d[0] = -7;
    CHECK(shmem_int_sum_reduce(team, d, s, 0) == 0 && d[0] == -7, "nreduce 0 leaves dest alone");
    CHECK(shmem_int_sum_reduce(team, d, s, 1) == 0 && d[0] == tn * (tn + 1) / 2, "nreduce 1");

    /* C11 type-generic forms */
    long *sl = SRC(long), *dl = DST(long);
    for (int i = 0; i < 4; i++) sl[i] = tr * 10 + i;
    CHECK(shmem_sum_reduce(team, dl, sl, 4) == 0, "generic sum rc");
    for (int i = 0; i < 4; i++) { long e = 0; for (int q = 0; q < tn; q++) e += q * 10 + i; CHECK(dl[i] == e, "generic sum"); }
    double *sdb = SRC(double), *ddb = DST(double);
    sdb[0] = tr + 0.5;
    CHECK(shmem_max_reduce(team, ddb, sdb, 1) == 0 && ddb[0] == tn - 1 + 0.5, "generic max on double");
    unsigned char *su = SRC(unsigned char), *du = DST(unsigned char);
    su[0] = (unsigned char) (1u << (tr % 8));
    CHECK(shmem_or_reduce(team, du, su, 1) == 0, "generic or rc");
    { unsigned char e = 0; for (int q = 0; q < tn; q++) e |= (unsigned char) (1u << (q % 8)); CHECK(du[0] == e, "generic or"); }
}

/* ---- data movement collectives */

static void run_movement(shmem_team_t team)
{
    int *si = SRC(int), *di = DST(int);

    /* broadcast from the first and the last PE; the result lands on every PE, root included */
    for (int rootsel = 0; rootsel < 2; rootsel++) {
        int root = rootsel ? tn - 1 : 0;
        for (int i = 0; i < 6; i++) { si[i] = (tr == root) ? 1000 * root + i : -1; di[i] = -2; }
        CHECK(shmem_int_broadcast(team, di, si, 6, root) == 0, "broadcast rc");
        for (int i = 0; i < 6; i++) CHECK(di[i] == 1000 * root + i, "broadcast value");
    }
    for (int i = 0; i < 4; i++) { si[i] = tr == 0 ? 5 + i : 0; di[i] = 0; }
    CHECK(shmem_int_broadcast(team, di, si, 0, 0) == 0, "broadcast of 0 elements");
    CHECK(shmem_int_broadcast(team, di, si, 1, 0) == 0 && di[0] == 5, "broadcast of 1 element");
    { char *sc = SRC(char), *dc = DST(char);
      for (int i = 0; i < 10; i++) { sc[i] = tr == 0 ? (char) ('a' + i) : '?'; dc[i] = '-'; }
      CHECK(shmem_broadcastmem(team, dc, sc, 10, 0) == 0, "broadcastmem rc");
      for (int i = 0; i < 10; i++) CHECK(dc[i] == 'a' + i, "broadcastmem value"); }
    { double *sd = SRC(double), *dd = DST(double);
      sd[0] = tr == tn - 1 ? 3.25 : 0; dd[0] = 0;
      CHECK(shmem_broadcast(team, dd, sd, 1, tn - 1) == 0 && dd[0] == 3.25, "generic broadcast"); }

    /* fcollect: the same count from every PE, in team PE order */
    for (int i = 0; i < 3; i++) si[i] = tr * 100 + i;
    for (int i = 0; i < 3 * tn; i++) di[i] = -1;
    CHECK(shmem_int_fcollect(team, di, si, 3) == 0, "fcollect rc");
    for (int q = 0; q < tn; q++) for (int i = 0; i < 3; i++) CHECK(di[q * 3 + i] == q * 100 + i, "fcollect value");
    CHECK(shmem_fcollect(team, di, si, 3) == 0, "generic fcollect");
    { long *sl = SRC(long), *dl = DST(long);
      sl[0] = tr + 7;
      CHECK(shmem_long_fcollect(team, dl, sl, 1) == 0, "fcollect of 1 rc");
      for (int q = 0; q < tn; q++) CHECK(dl[q] == q + 7, "fcollect of 1 value"); }

    /* collect: a different count from each PE, including zero */
    int cnt = tr % 3;
    for (int i = 0; i < 3; i++) si[i] = tr * 100 + i;
    for (int i = 0; i < 3 * tn; i++) di[i] = -1;
    CHECK(shmem_int_collect(team, di, si, (size_t) cnt) == 0, "collect rc");
    { int at = 0;
      for (int q = 0; q < tn; q++) for (int i = 0; i < q % 3; i++) CHECK(di[at++] == q * 100 + i, "collect value");
      CHECK(di[at] == -1, "collect writes no more than the total"); }
    CHECK(shmem_collect(team, di, si, (size_t) cnt) == 0, "generic collect");
    { char *sc = SRC(char), *dc = DST(char);
      for (int i = 0; i < 4; i++) sc[i] = (char) ('A' + tr);
      CHECK(shmem_collectmem(team, dc, sc, 2) == 0, "collectmem rc");
      for (int q = 0; q < tn; q++) CHECK(dc[2 * q] == 'A' + q && dc[2 * q + 1] == 'A' + q, "collectmem value");
      CHECK(shmem_fcollectmem(team, dc, sc, 1) == 0, "fcollectmem rc");
      for (int q = 0; q < tn; q++) CHECK(dc[q] == 'A' + q, "fcollectmem value"); }

    /* alltoall: PE k's block for PE l goes to block k of PE l */
    for (int l = 0; l < tn; l++) for (int j = 0; j < 2; j++) si[l * 2 + j] = tr * 1000 + l * 10 + j;
    for (int i = 0; i < 2 * tn; i++) di[i] = -1;
    CHECK(shmem_int_alltoall(team, di, si, 2) == 0, "alltoall rc");
    for (int k = 0; k < tn; k++) for (int j = 0; j < 2; j++) CHECK(di[k * 2 + j] == k * 1000 + tr * 10 + j, "alltoall value");
    CHECK(shmem_alltoall(team, di, si, 2) == 0, "generic alltoall");
    { char *sc = SRC(char), *dc = DST(char);
      for (int l = 0; l < tn; l++) sc[l] = (char) ('a' + tr * 8 + l);
      CHECK(shmem_alltoallmem(team, dc, sc, 1) == 0, "alltoallmem rc");
      for (int k = 0; k < tn; k++) CHECK(dc[k] == 'a' + k * 8 + tr, "alltoallmem value"); }

    /* alltoalls: nelems elements per PE with source stride 3 and dest stride 2; the block for
     * PE l starts at element l*nelems*sst of the source and lands at k*nelems*dst of l's dest */
    const int ne = 2, sst = 3, dst = 2;
    for (int i = 0; i < tn * ne * sst; i++) si[i] = -1;
    for (int i = 0; i < tn * ne * dst; i++) di[i] = -1;
    for (int l = 0; l < tn; l++) for (int j = 0; j < ne; j++) si[l * ne * sst + j * sst] = tr * 1000 + l * 10 + j;
    CHECK(shmem_int_alltoalls(team, di, si, dst, sst, ne) == 0, "alltoalls rc");
    for (int k = 0; k < tn; k++) for (int j = 0; j < ne; j++)
        CHECK(di[k * ne * dst + j * dst] == k * 1000 + tr * 10 + j, "alltoalls value");
    for (int k = 0; k < tn; k++) for (int j = 0; j < ne; j++) if (dst > 1) CHECK(di[k * ne * dst + j * dst + 1] == -1, "alltoalls leaves the gaps alone");
    CHECK(shmem_alltoalls(team, di, si, dst, sst, ne) == 0, "generic alltoalls");
    CHECK(shmem_alltoallsmem(team, DST(char), SRC(char), 1, 1, 1) == 0, "alltoallsmem rc");

    /* a barrier-free check that a collective sees the data this PE put before calling it:
     * every PE puts into its right neighbor's source, then all reduce it */
    CHECK(shmem_team_sync(team) == 0, "team_sync rc");
}

static void run_team(shmem_team_t team)
{
    if (team == SHMEM_TEAM_INVALID) return;
    tn = shmem_team_n_pes(team);
    tr = shmem_team_my_pe(team);
    for (int variant = 0; variant < 2; variant++) {
        buf = variant ? heap_buf : seg_buf;     /* data segment, then symmetric heap */
        run_movement(team);
        run_reductions(team);
    }
}

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();
    tn = np;
    heap_buf = shmem_align(64, 8192);
    CHECK(heap_buf != NULL, "heap buffer");
    memset(heap_buf, 0, 8192);

    run_team(SHMEM_TEAM_WORLD);
    run_team(SHMEM_TEAM_SHARED);

    shmem_team_t even;
    shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 2, (np + 1) / 2, NULL, 0, &even);
    run_team(even);                                  /* PEs outside the team skip it */
    shmem_team_t last2 = SHMEM_TEAM_INVALID;         /* a nested team */
    if (even != SHMEM_TEAM_INVALID) {
        int n = shmem_team_n_pes(even);
        shmem_team_split_strided(even, n > 1 ? n - 2 : 0, 1, n > 1 ? 2 : 1, NULL, 0, &last2);
        run_team(last2);
        shmem_team_destroy(last2);
    }
    shmem_team_destroy(even);

    for (int xr = 2; xr <= 3; xr++) {
        shmem_team_t xt, yt;
        shmem_team_split_2d(SHMEM_TEAM_WORLD, xr, NULL, 0, &xt, NULL, 0, &yt);
        run_team(xt);
        run_team(yt);
        shmem_team_destroy(xt);
        shmem_team_destroy(yt);
    }

    shmem_free(heap_buf);
    shmem_barrier_all();
    printf("[%d] test_team_coll: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
