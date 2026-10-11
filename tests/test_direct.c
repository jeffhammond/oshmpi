/* Every kind of operation on symmetric-heap objects.  When the heap is directly addressable
 * (detected at run time) these take the load/store path; otherwise they use MPI.  The checks
 * are the same either way.  A data-segment object is mixed in, since the data segment always
 * uses MPI. */
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

static int seg_counter;                 /* data segment */

#define N 200

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();
    int next = (me + 1) % np, prev = (me + np - 1) % np;

    /* ---- contiguous put and get of several sizes */
    unsigned char *hb = shmem_malloc(8192);
    unsigned char *loc = malloc(8192);
    size_t sizes[] = { 1, 3, 8, 100, 4096 };
    for (int s = 0; s < 5; s++) {
        size_t n = sizes[s];
        for (size_t i = 0; i < n; i++) loc[i] = (unsigned char) (me * 31 + i);
        memset(hb, 0, 8192);
        shmem_barrier_all();
        shmem_putmem(hb, loc, n, next);
        shmem_barrier_all();
        int bad = 0;
        for (size_t i = 0; i < n; i++) if (hb[i] != (unsigned char) (prev * 31 + i)) bad = 1;
        CHECK(!bad, "putmem");
        memset(loc, 0, 8192);
        shmem_getmem(loc, hb, n, next);          /* next's hb holds what we put there */
        bad = 0;
        for (size_t i = 0; i < n; i++) if (loc[i] != (unsigned char) (me * 31 + i)) bad = 1;
        CHECK(!bad, "getmem");
        shmem_barrier_all();
    }
    /* nonblocking */
    for (int i = 0; i < 100; i++) loc[i] = (unsigned char) (me + i);
    shmem_putmem_nbi(hb, loc, 100, next);
    shmem_quiet();
    shmem_barrier_all();
    memset(loc, 0, 100);
    shmem_getmem_nbi(loc, hb, 100, me);
    shmem_quiet();
    { int bad = 0; for (int i = 0; i < 100; i++) if (loc[i] != (unsigned char) (prev + i)) bad = 1; CHECK(!bad, "putmem_nbi/getmem_nbi"); }

    /* ---- elemental p and g */
    int *hi = shmem_calloc(64, sizeof(int));
    double *hd = shmem_calloc(8, sizeof(double));
    long *hl = shmem_calloc(8, sizeof(long));
    shmem_int_p(&hi[5], 1000 + me, next);
    shmem_double_p(&hd[1], 2.5 * me, next);
    shmem_long_p(&hl[2], 77L * me, next);
    shmem_barrier_all();
    CHECK(hi[5] == 1000 + prev, "int_p");
    CHECK(hd[1] == 2.5 * prev, "double_p");
    CHECK(hl[2] == 77L * prev, "long_p");
    CHECK(shmem_int_g(&hi[5], next) == 1000 + me, "int_g");
    CHECK(shmem_double_g(&hd[1], next) == 2.5 * me, "double_g");
    CHECK(shmem_long_g(&hl[2], next) == 77L * me, "long_g");
    shmem_barrier_all();

    /* ---- strided put and get */
    int src[64], out[64];
    memset(hi, 0, 64 * sizeof(int));
    for (int i = 0; i < 64; i++) src[i] = me * 1000 + i;
    shmem_barrier_all();
    shmem_int_iput(hi, src, 2, 3, 10, next);
    shmem_barrier_all();
    { int bad = 0;
      for (int k = 0; k < 10; k++) if (hi[2 * k] != prev * 1000 + 3 * k) bad = 1;
      for (int k = 0; k < 10; k++) if (hi[2 * k + 1] != 0) bad = 1;
      CHECK(!bad, "int_iput"); }
    memset(out, 0, sizeof out);
    shmem_int_iget(out, hi, 1, 2, 10, next);     /* next's hi holds what we put there */
    { int bad = 0; for (int k = 0; k < 10; k++) if (out[k] != me * 1000 + 3 * k) bad = 1; CHECK(!bad, "int_iget"); }
    shmem_barrier_all();

    /* ---- atomics, all PEs on counters at PE 0 */
    int       *c_int   = shmem_calloc(1, sizeof(int));
    long      *c_long  = shmem_calloc(1, sizeof(long));
    long long *c_ll    = shmem_calloc(1, sizeof(long long));
    long      *c_ret   = shmem_calloc(1, sizeof(long));
    long      *c_tick  = shmem_calloc(1, sizeof(long));
    int       *c_lock  = shmem_calloc(1, sizeof(int));
    long      *c_prot  = shmem_calloc(1, sizeof(long));
    unsigned int  *b32 = shmem_calloc(3, sizeof(unsigned int));
    unsigned long *b64 = shmem_calloc(3, sizeof(unsigned long));
    shmem_barrier_all();
    for (int i = 0; i < N; i++) {
        shmem_int_atomic_add(c_int, 1, 0);
        shmem_long_atomic_inc(c_long, 0);
        shmem_longlong_atomic_add(c_ll, 2, 0);
        shmem_int_atomic_add(&seg_counter, 1, 0);            /* data segment: MPI */
        long t = shmem_long_atomic_fetch_inc(c_tick, 0);     /* unique tickets */
        shmem_long_atomic_add(c_ret, t, 0);
    }
    shmem_barrier_all();
    if (me == 0) {
        CHECK(*c_int == np * N, "int atomic_add");
        CHECK(*c_long == (long) np * N, "long atomic_inc");
        CHECK(*c_ll == 2LL * np * N, "longlong atomic_add");
        CHECK(seg_counter == np * N, "data segment atomic_add");
        long m = (long) np * N;
        CHECK(*c_ret == m * (m - 1) / 2, "fetch_inc returns unique tickets");
    }
    shmem_barrier_all();

    /* mutual exclusion with a compare-and-swap lock guarding a plain read-modify-write */
    for (int i = 0; i < 50; i++) {
        while (shmem_int_atomic_compare_swap(c_lock, 0, 1, 0) != 0) ;
        long v = shmem_long_g(c_prot, 0);
        shmem_long_p(c_prot, v + 1, 0);
        shmem_quiet();
        shmem_int_atomic_set(c_lock, 0, 0);
    }
    shmem_barrier_all();
    if (me == 0) CHECK(*c_prot == (long) np * 50, "compare_swap lock mutual exclusion");

    /* swap returns the previous value */
    shmem_barrier_all();
    int old = shmem_int_atomic_swap(&hi[40], 9, me);
    CHECK(old == 0, "swap old value");
    CHECK(shmem_int_atomic_fetch(&hi[40], me) == 9, "fetch after swap");
    CHECK(shmem_int_atomic_compare_swap(&hi[40], 8, 1, me) == 9 && hi[40] == 9, "failed compare_swap leaves value");
    CHECK(shmem_int_atomic_compare_swap(&hi[40], 9, 1, me) == 9 && hi[40] == 1, "compare_swap");
    shmem_barrier_all();

    /* bitwise: every PE ORs its bit; ANDs clear their bit from a full word; XOR twice is a no-op */
    unsigned int full32 = 0xFFFFFFFFu;
    unsigned long full64 = ~0UL;
    if (me == 0) { b32[1] = full32; b64[1] = full64; }
    shmem_barrier_all();
    unsigned int bit32 = 1u << (me % 32);
    unsigned long bit64 = 1UL << (me % 64);
    shmem_uint_atomic_or(&b32[0], bit32, 0);
    shmem_ulong_atomic_or(&b64[0], bit64, 0);
    (void) shmem_uint_atomic_fetch_and(&b32[1], ~bit32, 0);
    shmem_ulong_atomic_and(&b64[1], ~bit64, 0);
    shmem_uint_atomic_xor(&b32[2], bit32, 0);
    (void) shmem_uint_atomic_fetch_xor(&b32[2], bit32, 0);
    shmem_ulong_atomic_xor(&b64[2], bit64, 0);
    shmem_ulong_atomic_xor(&b64[2], bit64, 0);
    shmem_barrier_all();
    if (me == 0) {
        unsigned int e32 = 0; unsigned long e64 = 0;
        for (int p = 0; p < np; p++) { e32 |= 1u << (p % 32); e64 |= 1UL << (p % 64); }
        CHECK(b32[0] == e32, "uint or");
        CHECK(b64[0] == e64, "ulong or");
        CHECK(b32[1] == (full32 & ~e32), "uint fetch_and");
        CHECK(b64[1] == (full64 & ~e64), "ulong and");
        CHECK(b32[2] == 0 && b64[2] == 0, "xor twice");
    }

    /* floating-point set, swap and fetch */
    double *fd = shmem_calloc(1, sizeof(double));
    float  *ff = shmem_calloc(1, sizeof(float));
    shmem_barrier_all();
    shmem_double_atomic_set(fd, 3.25 + me, next);
    shmem_float_atomic_set(ff, 1.5f + me, next);
    shmem_barrier_all();
    CHECK(*fd == 3.25 + prev && *ff == 1.5f + prev, "double/float atomic_set");
    CHECK(shmem_double_atomic_fetch(fd, next) == 3.25 + me, "double atomic_fetch");
    shmem_barrier_all();
    double od = shmem_double_atomic_swap(fd, -1.0, me);
    CHECK(od == 3.25 + prev, "double atomic_swap old value");
    CHECK(shmem_float_atomic_swap(ff, 2.0f, me) == 1.5f + prev, "float atomic_swap old value");
    CHECK(*fd == -1.0 && *ff == 2.0f, "double/float swap new value");

    /* shmem_ptr: if a direct address is offered it must refer to the same memory */
    int *flag = shmem_calloc(1, sizeof(int));
    shmem_barrier_all();
    shmem_int_atomic_set(flag, 41 + me, next);
    shmem_barrier_all();
    int *pp = shmem_ptr(flag, next);
    if (pp != NULL) CHECK(*pp == 41 + me, "shmem_ptr reads the target's memory");
    shmem_barrier_all();

    /* wait_until woken by a remote atomic */
    shmem_int_atomic_set(flag, 0, me);
    shmem_barrier_all();
    shmem_int_atomic_add(flag, 1, next);
    shmem_int_wait_until(flag, SHMEM_CMP_EQ, 1);
    shmem_barrier_all();

    printf("[%d] test_direct: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
