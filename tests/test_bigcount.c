/* Transfers whose element count does not fit in an MPI int (>= 2^31 - 1), which OSHMPI sends
 * through a derived datatype, and strided transfers longer than one MPI call carries.  They
 * need several GiB per PE, so the test is skipped unless OSHMPI_TEST_LARGE is set:
 *   OSHMPI_TEST_LARGE=1   lengths around 2^31 - 1 (about 7 GiB per PE)
 *   OSHMPI_TEST_LARGE=2   also 2 * (2^31 - 1) + 5 bytes (about 13 GiB per PE)
 * Lengths straddle the boundary: just below, exactly at, and just above 2^31 - 1. */
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s (len %zu)\n", me, __LINE__, m, len); errs++; } } while (0)

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

int main(void)
{
    const char *large = getenv("OSHMPI_TEST_LARGE");
    int level = large ? atoi(large) : 0;
    if (level < 1) { printf("test_bigcount: skipped (set OSHMPI_TEST_LARGE=1 to run)\n"); return 77; }

    const size_t B = (size_t) INT32_MAX;
    size_t lens[8]; int nl = 0;
    lens[nl++] = B - 1; lens[nl++] = B; lens[nl++] = B + 1;
    if (level >= 2) lens[nl++] = 2 * B + 5;
    size_t maxlen = lens[nl - 1];

    /* heap: the buffer plus a margin; the setting is per PE */
    char sz[64];
    snprintf(sz, sizeof sz, "%zu", maxlen + (1 << 20));
    setenv("SHMEM_SYMMETRIC_SIZE", sz, 1);

    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();
    int next = (me + 1) % np, prev = (me + np - 1) % np;

    unsigned char *H = shmem_malloc(maxlen);          /* symmetric */
    unsigned char *L = malloc(maxlen);                /* source */
    unsigned char *W = malloc(maxlen);                /* destination of gets */
    if (!H || !L || !W) { printf("[%d] FAIL: cannot allocate %zu bytes\n", me, maxlen); shmem_global_exit(2); }

    for (int k = 0; k < nl; k++) {
        size_t len = lens[k];
        shmem_barrier_all();

        /* blocking put, then the neighbor checks it and gets it back */
        fill(L, len, me + 1);
        memset(H, 0, len);
        shmem_barrier_all();
        shmem_putmem(H, L, len, next);
        shmem_barrier_all();
        CHECK(first_bad(H, len, prev + 1) == len, "putmem");
        memset(W, 0, len);
        shmem_getmem(W, H, len, next);                 /* what I put there */
        CHECK(first_bad(W, len, me + 1) == len, "getmem");
        shmem_barrier_all();

        /* nonblocking */
        fill(L, len, me + 11);
        memset(H, 0, len);
        shmem_barrier_all();
        shmem_putmem_nbi(H, L, len, next);
        shmem_quiet();
        shmem_barrier_all();
        CHECK(first_bad(H, len, prev + 11) == len, "putmem_nbi");
        memset(W, 0, len);
        shmem_getmem_nbi(W, H, len, next);
        shmem_quiet();
        CHECK(first_bad(W, len, me + 11) == len, "getmem_nbi");
        shmem_barrier_all();

        /* through a context */
        fill(L, len, me + 21);
        memset(H, 0, len);
        shmem_barrier_all();
        shmem_ctx_putmem(SHMEM_CTX_DEFAULT, H, L, len, next);
        shmem_ctx_quiet(SHMEM_CTX_DEFAULT);
        shmem_barrier_all();
        CHECK(first_bad(H, len, prev + 21) == len, "ctx_putmem");
        memset(W, 0, len);
        shmem_ctx_getmem(SHMEM_CTX_DEFAULT, W, H, len, next);
        CHECK(first_bad(W, len, me + 21) == len, "ctx_getmem");
        shmem_barrier_all();

        if (me == 0) { printf("test_bigcount: %zu bytes done\n", len); fflush(stdout); }
    }

    /* a strided transfer longer than one MPI call carries (2^30 elements per call) */
    {
        size_t len = ((size_t) 1 << 30) + 7;
        fill(L, len, me + 31);
        memset(H, 0, len);
        shmem_barrier_all();
        shmem_iput8(H, L, 1, 1, len, next);
        shmem_barrier_all();
        CHECK(first_bad(H, len, prev + 31) == len, "iput8 over one chunk");
        memset(W, 0, len);
        shmem_iget8(W, H, 1, 1, len, next);
        CHECK(first_bad(W, len, me + 31) == len, "iget8 over one chunk");
        shmem_barrier_all();
        /* every second byte */
        size_t half = len / 2;
        memset(H, 0xEE, len);
        shmem_barrier_all();
        shmem_iput8(H, L, 2, 2, half, next);
        shmem_barrier_all();
        size_t bad = half;
        for (size_t i = 0; i < half; i++) if (H[2 * i] != pat(2 * i, prev + 31) || H[2 * i + 1] != 0xEE) { bad = i; break; }
        CHECK(bad == half, "iput8 stride 2 over one chunk");
        shmem_barrier_all();
    }

    shmem_free(H);
    free(L);
    free(W);
    shmem_barrier_all();
    printf("[%d] test_bigcount: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
