/* The symmetric heap size from the environment.  The variable and value come from the build
 * (-DENVNAME, -DSZ) so one source makes several tests; WANT is the number of bytes the value
 * means.  The heap must hold at least that much (the specification says "at least"), which
 * is checked by allocating all of it and moving data through it.  (-DEXHAUST instead fills the
 * heap and checks that running out gives NULL.) */
#include <shmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ENVNAME
#define ENVNAME "SHMEM_SYMMETRIC_SIZE"
#endif

int main(void)
{
    setenv("SHMEM_SYMMETRIC_SIZE", "", 1);
    unsetenv("SHMEM_SYMMETRIC_SIZE");
    setenv(ENVNAME, SZ, 1);
    shmem_init();
    int me = shmem_my_pe(), np = shmem_n_pes(), errs = 0;
    size_t want = WANT;

#ifdef EXHAUST
    /* The heap is a fixed region: when a request does not fit, shmem_malloc must return NULL
     * (never memory outside the symmetric heap, which RMA could not address).  Take blocks
     * well above dlmalloc's mmap threshold until the heap is full, using each. */
    {
        enum { MAXB = 64 };
        unsigned char *blk[MAXB];
        int nb = 0;
        size_t chunk = 300 * 1024;                   /* > 256 KiB */
        while (nb < MAXB && (blk[nb] = shmem_malloc(chunk)) != NULL) nb++;
        if (nb == MAXB) { printf("[%d] FAIL: heap of %s never filled\n", me, SZ); errs++; }
        if (nb == 0) { printf("[%d] FAIL: no block of %zu fits in a heap of %s\n", me, chunk, SZ); errs++; }
        for (int b = 0; b < nb; b++) {
            memset(blk[b], b + 1, chunk);
            shmem_barrier_all();
            unsigned char one;
            shmem_getmem(&one, blk[b] + chunk - 1, 1, (me + 1) % np);     /* aborts if not addressable */
            if (one != (unsigned char) (b + 1)) { printf("[%d] FAIL: block %d\n", me, b); errs++; }
            shmem_barrier_all();
        }
        for (int b = 0; b < nb; b++) shmem_free(blk[b]);
        shmem_barrier_all();
        printf("[%d] test_symsize exhaust %s: %d blocks, %s\n", me, SZ, nb, errs ? "FAILED" : "OK");
        shmem_finalize();
        return errs != 0;
    }
#endif

    unsigned char *buf = shmem_malloc(want);
    if (buf == NULL) { printf("[%d] FAIL: shmem_malloc(%zu) failed with %s=%s\n", me, want, ENVNAME, SZ); errs++; }
    shmem_barrier_all();
    if (buf != NULL) {
        for (size_t i = 0; i < want; i++) buf[i] = (unsigned char) (me + i * 7);
        shmem_barrier_all();
        unsigned char *back = malloc(want);
        shmem_getmem(back, buf, want, (me + 1) % np);
        for (size_t i = 0; i < want; i++)
            if (back[i] != (unsigned char) ((me + 1) % np + i * 7)) { errs++; printf("[%d] FAIL: byte %zu\n", me, i); break; }
        free(back);
        shmem_free(buf);
    }
    shmem_barrier_all();
    printf("[%d] test_symsize %s=%s (%zu bytes): %s\n", me, ENVNAME, SZ, want, errs ? "FAILED" : "OK");
    shmem_finalize();
    return errs != 0;
}
