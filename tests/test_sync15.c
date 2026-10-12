/* OpenSHMEM 1.5 point-to-point synchronization on arrays: wait_until_{all,any,some} and
 * test_{all,any,some}, plain and vector, for every type, on heap and data-segment arrays;
 * blocking waits woken by remote puts; status masks; the C11 generic names. */
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

#define NE 8

/* all the local checks for one type on the array a (NE elements, symmetric) */
#define DEFINE_LOCAL(N, T)                                                                      \
static void local_##N(T *a, const char *where)                                                  \
{                                                                                               \
    size_t idx[NE];                                                                             \
    int st[NE], allx[NE], i;                                                                    \
    T vals[NE];                                                                                 \
    (void) where;                                                                               \
    for (i = 0; i < NE; i++) { a[i] = 0; st[i] = 0; allx[i] = 1; vals[i] = (T) 1; }             \
    /* nothing satisfied (EQ 1) */                                                              \
    CHECK(shmem_##N##_test_all(a, NE, NULL, SHMEM_CMP_EQ, 1) == 0, "test_all none");            \
    CHECK(shmem_##N##_test_any(a, NE, NULL, SHMEM_CMP_EQ, 1) == SIZE_MAX, "test_any none");     \
    CHECK(shmem_##N##_test_some(a, NE, idx, NULL, SHMEM_CMP_EQ, 1) == 0, "test_some none");     \
    /* two satisfied: elements 2 and 5 */                                                       \
    a[2] = 1; a[5] = 1;                                                                         \
    CHECK(shmem_##N##_test_all(a, NE, NULL, SHMEM_CMP_EQ, 1) == 0, "test_all two");             \
    { size_t k = shmem_##N##_test_any(a, NE, NULL, SHMEM_CMP_EQ, 1);                            \
      CHECK(k == 2 || k == 5, "test_any two"); }                                                \
    { size_t n = shmem_##N##_test_some(a, NE, idx, NULL, SHMEM_CMP_EQ, 1);                      \
      CHECK(n == 2 && ((idx[0] == 2 && idx[1] == 5) || (idx[0] == 5 && idx[1] == 2)), "test_some two"); } \
    { size_t k = shmem_##N##_wait_until_any(a, NE, NULL, SHMEM_CMP_EQ, 1);                      \
      CHECK(k == 2 || k == 5, "wait_until_any two"); }                                          \
    { size_t n = shmem_##N##_wait_until_some(a, NE, idx, NULL, SHMEM_CMP_EQ, 1);                \
      CHECK(n == 2, "wait_until_some two"); }                                                   \
    /* status mask excludes the satisfied ones */                                               \
    st[2] = 1; st[5] = 1;                                                                       \
    CHECK(shmem_##N##_test_any(a, NE, st, SHMEM_CMP_EQ, 1) == SIZE_MAX, "test_any masked");     \
    CHECK(shmem_##N##_test_some(a, NE, idx, st, SHMEM_CMP_EQ, 1) == 0, "test_some masked");      \
    CHECK(shmem_##N##_test_all(a, NE, st, SHMEM_CMP_EQ, 0) == 1, "test_all masked others zero");\
    /* everything excluded: empty set */                                                        \
    CHECK(shmem_##N##_test_all(a, NE, allx, SHMEM_CMP_EQ, 9) == 1, "test_all empty set");        \
    CHECK(shmem_##N##_test_any(a, NE, allx, SHMEM_CMP_EQ, 0) == SIZE_MAX, "test_any empty set"); \
    CHECK(shmem_##N##_test_some(a, NE, idx, allx, SHMEM_CMP_EQ, 0) == 0, "test_some empty set"); \
    CHECK(shmem_##N##_wait_until_any(a, NE, allx, SHMEM_CMP_EQ, 9) == SIZE_MAX, "wait_until_any empty"); \
    CHECK(shmem_##N##_wait_until_some(a, NE, idx, allx, SHMEM_CMP_EQ, 9) == 0, "wait_until_some empty"); \
    shmem_##N##_wait_until_all(a, NE, allx, SHMEM_CMP_EQ, 9);                                   \
    /* nelems == 0 */                                                                           \
    CHECK(shmem_##N##_test_all(a, 0, NULL, SHMEM_CMP_EQ, 9) == 1, "test_all nelems 0");          \
    CHECK(shmem_##N##_test_any(a, 0, NULL, SHMEM_CMP_EQ, 9) == SIZE_MAX, "test_any nelems 0");   \
    shmem_##N##_wait_until_all(a, 0, NULL, SHMEM_CMP_EQ, 9);                                    \
    /* all satisfied */                                                                         \
    for (i = 0; i < NE; i++) a[i] = 1;                                                          \
    CHECK(shmem_##N##_test_all(a, NE, NULL, SHMEM_CMP_EQ, 1) == 1, "test_all all");              \
    CHECK(shmem_##N##_test_some(a, NE, idx, NULL, SHMEM_CMP_EQ, 1) == NE, "test_some all");      \
    shmem_##N##_wait_until_all(a, NE, NULL, SHMEM_CMP_EQ, 1);                                   \
    CHECK(shmem_##N##_wait_until_some(a, NE, idx, NULL, SHMEM_CMP_EQ, 1) == NE, "wait_until_some all"); \
    /* other comparison operators */                                                            \
    CHECK(shmem_##N##_test_all(a, NE, NULL, SHMEM_CMP_NE, 0) == 1, "test_all NE");               \
    CHECK(shmem_##N##_test_all(a, NE, NULL, SHMEM_CMP_GT, 0) == 1, "test_all GT");               \
    CHECK(shmem_##N##_test_all(a, NE, NULL, SHMEM_CMP_GE, 1) == 1, "test_all GE");               \
    CHECK(shmem_##N##_test_all(a, NE, NULL, SHMEM_CMP_LT, 2) == 1, "test_all LT");               \
    CHECK(shmem_##N##_test_all(a, NE, NULL, SHMEM_CMP_LE, 1) == 1, "test_all LE");               \
    CHECK(shmem_##N##_test_any(a, NE, NULL, SHMEM_CMP_GT, 1) == SIZE_MAX, "test_any GT none");   \
    /* a series of test_any calls returns every satisfied element, not just the first */        \
    { int seen[NE] = { 0 }, calls;                                                              \
      for (calls = 0; calls < 4 * NE; calls++) seen[shmem_##N##_test_any(a, NE, NULL, SHMEM_CMP_EQ, 1)] = 1; \
      for (i = 0; i < NE; i++) CHECK(seen[i], "test_any eventually returns every element"); }    \
    /* vector forms: element i compared with vals[i] */                                         \
    for (i = 0; i < NE; i++) { a[i] = (T) i; vals[i] = (T) i; }                                 \
    CHECK(shmem_##N##_test_all_vector(a, NE, NULL, SHMEM_CMP_EQ, vals) == 1, "test_all_vector"); \
    vals[3] = 100; vals[6] = 100;                                                               \
    CHECK(shmem_##N##_test_all_vector(a, NE, NULL, SHMEM_CMP_EQ, vals) == 0, "test_all_vector mismatch"); \
    { size_t n = shmem_##N##_test_some_vector(a, NE, idx, NULL, SHMEM_CMP_EQ, vals);             \
      CHECK(n == NE - 2, "test_some_vector count");                                             \
      for (size_t k = 0; k < n; k++) CHECK(idx[k] != 3 && idx[k] != 6, "test_some_vector indices"); } \
    { int s2[NE] = { 0 }; s2[3] = 1; s2[6] = 1;                                                 \
      CHECK(shmem_##N##_test_all_vector(a, NE, s2, SHMEM_CMP_EQ, vals) == 1, "test_all_vector masked"); \
      shmem_##N##_wait_until_all_vector(a, NE, s2, SHMEM_CMP_EQ, vals); }                       \
    for (i = 0; i < NE; i++) { a[i] = 0; vals[i] = 0; } vals[4] = 1; a[4] = 1;                  \
    { size_t k = shmem_##N##_test_any_vector(a, NE, NULL, SHMEM_CMP_EQ, vals);                  \
      CHECK(k != SIZE_MAX, "test_any_vector"); }                                                \
    CHECK(shmem_##N##_wait_until_any_vector(a, NE, NULL, SHMEM_CMP_EQ, vals) != SIZE_MAX, "wait_until_any_vector"); \
    CHECK(shmem_##N##_wait_until_some_vector(a, NE, idx, NULL, SHMEM_CMP_EQ, vals) == NE, "wait_until_some_vector"); \
}

#define DEFINE_SEG(N, T) static T seg_##N[NE];
#define RUN_LOCAL(N, T)                                                                         \
    { T *h = shmem_malloc(NE * sizeof(T)); local_##N(h, "heap"); shmem_free(h);                 \
      local_##N(seg_##N, "data segment"); }

OSHMPI_SYNC_TYPES(DEFINE_LOCAL)
OSHMPI_SYNC_TYPES(DEFINE_SEG)

/* ---- blocking waits woken by remote puts, as a chain around the PEs: PE 0 starts, each PE
 * waits for the one before it and then writes to the next; PE 0 finally waits for the last. */
#define CHAIN_ELEMS 4
static int  seg_chain[CHAIN_ELEMS];
static long seg_chain_l[CHAIN_ELEMS];

static void chain(int *a, long *al)
{
    int next = (me + 1) % np;
    int flags[CHAIN_ELEMS];
    size_t idx[CHAIN_ELEMS];
    int round, k;

    for (round = 0; round < 3; round++) {
        for (k = 0; k < CHAIN_ELEMS; k++) { a[k] = 0; al[k] = 0; }
        shmem_barrier_all();

        if (me == 0) usleep(20000);                 /* the others are already blocked */
        else if (round == 0) shmem_int_wait_until_all(a, CHAIN_ELEMS, NULL, SHMEM_CMP_EQ, me + 100);
        else if (round == 1) {
            size_t first = shmem_int_wait_until_any(a, CHAIN_ELEMS, NULL, SHMEM_CMP_EQ, me + 100);
            CHECK(first < CHAIN_ELEMS, "chain wait_until_any");
            shmem_int_wait_until_all(a, CHAIN_ELEMS, NULL, SHMEM_CMP_EQ, me + 100);
        } else {
            size_t got = 0;
            for (k = 0; k < CHAIN_ELEMS; k++) flags[k] = 0;
            while (got < CHAIN_ELEMS) {             /* collect with some, masking what arrived */
                size_t n = shmem_int_wait_until_some(a, CHAIN_ELEMS, idx, flags, SHMEM_CMP_EQ, me + 100);
                for (size_t j = 0; j < n; j++) flags[idx[j]] = 1;
                got += n;
            }
        }
        /* forward: PE (me+1) waits for the value (me+1) */
        for (k = 0; k < CHAIN_ELEMS; k++) {
            shmem_int_p(&a[k], next + 100, next);
            shmem_long_p(&al[k], 10L * (next + 100), next);
            if (k == 1) usleep(2000);
        }
        shmem_quiet();
        if (me == 0) {
            shmem_int_wait_until_all(a, CHAIN_ELEMS, NULL, SHMEM_CMP_EQ, 100);
            shmem_long_wait_until_all(al, CHAIN_ELEMS, NULL, SHMEM_CMP_EQ, 1000L);
        }
        shmem_barrier_all();
        for (k = 0; k < CHAIN_ELEMS; k++) { CHECK(a[k] == me + 100, "chain value"); CHECK(al[k] == 10L * (me + 100), "chain long value"); }
    }
}

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();

#define RUN(N, T) RUN_LOCAL(N, T)
    OSHMPI_SYNC_TYPES(RUN)

    /* C11 generic names */
    {
        int *h = shmem_calloc(NE, sizeof(int));
        unsigned long hu[NE] = { 0 };
        long vals[NE];
        size_t idx[NE];
        for (int i = 0; i < NE; i++) { h[i] = 1; vals[i] = 1; }
        CHECK(shmem_test_all(h, NE, NULL, SHMEM_CMP_EQ, 1) == 1, "generic test_all");
        CHECK(shmem_test_any(h, NE, NULL, SHMEM_CMP_EQ, 2) == SIZE_MAX, "generic test_any");
        CHECK(shmem_test_some(h, NE, idx, NULL, SHMEM_CMP_EQ, 1) == NE, "generic test_some");
        shmem_wait_until_all(h, NE, NULL, SHMEM_CMP_EQ, 1);
        CHECK(shmem_wait_until_any(h, NE, NULL, SHMEM_CMP_EQ, 1) < NE, "generic wait_until_any");
        CHECK(shmem_wait_until_some(h, NE, idx, NULL, SHMEM_CMP_EQ, 1) == NE, "generic wait_until_some");
        (void) hu; (void) vals;
        {
            int ivals[NE];
            for (int i = 0; i < NE; i++) ivals[i] = 1;
            CHECK(shmem_test_all_vector(h, NE, NULL, SHMEM_CMP_EQ, ivals) == 1, "generic test_all_vector");
            CHECK(shmem_test_any_vector(h, NE, NULL, SHMEM_CMP_EQ, ivals) < NE, "generic test_any_vector");
            CHECK(shmem_test_some_vector(h, NE, idx, NULL, SHMEM_CMP_EQ, ivals) == NE, "generic test_some_vector");
            shmem_wait_until_all_vector(h, NE, NULL, SHMEM_CMP_EQ, ivals);
            CHECK(shmem_wait_until_any_vector(h, NE, NULL, SHMEM_CMP_EQ, ivals) < NE, "generic wait_until_any_vector");
            CHECK(shmem_wait_until_some_vector(h, NE, idx, NULL, SHMEM_CMP_EQ, ivals) == NE, "generic wait_until_some_vector");
        }
        shmem_free(h);
    }

    /* the chain, on a heap array and on data-segment arrays */
    {
        int *h = shmem_calloc(CHAIN_ELEMS, sizeof(int));
        long *hl = shmem_calloc(CHAIN_ELEMS, sizeof(long));
        chain(h, hl);
        chain(seg_chain, seg_chain_l);
        shmem_free(hl);
        shmem_free(h);
    }

    printf("[%d] test_sync15: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
