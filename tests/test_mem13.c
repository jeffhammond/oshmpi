/* OpenSHMEM 1.3 memory management: standard names, zero-size NULL, calloc zeroing, realloc. */
#include <stdio.h>
#include <shmem.h>

int main(void)
{
    int errs = 0;
    shmem_init();
    int me = shmem_my_pe();

    if (shmem_malloc(0) != NULL) { printf("[%d] shmem_malloc(0) != NULL\n", me); errs++; }
    if (shmem_calloc(0, 4) != NULL) { printf("[%d] shmem_calloc(0,4) != NULL\n", me); errs++; }
    if (shmem_calloc(4, 0) != NULL) { printf("[%d] shmem_calloc(4,0) != NULL\n", me); errs++; }
    if (shmem_align(64, 0) != NULL) { printf("[%d] shmem_align(64,0) != NULL\n", me); errs++; }

    long * a = shmem_calloc(16, sizeof(long));
    if (a == NULL) { printf("[%d] shmem_calloc failed\n", me); errs++; }
    else for (int i = 0; i < 16; i++) if (a[i] != 0) { errs++; break; }

    char * b = shmem_malloc(8);
    if (b != NULL) for (int i = 0; i < 8; i++) b[i] = (char)i;
    b = shmem_realloc(b, 64);
    if (b == NULL) { printf("[%d] shmem_realloc failed\n", me); errs++; }
    else for (int i = 0; i < 8; i++) if (b[i] != (char)i) { errs++; break; }

    void * c = shmem_align(64, 8);
    if (c == NULL || ((unsigned long)c % 64) != 0) { printf("[%d] shmem_align bad\n", me); errs++; }

    if (shmem_realloc(b, 0) != NULL) errs++; /* frees b */
    shmem_free(c);
    shmem_free(a);
    shmem_free(NULL);

    int major, minor;
    shmem_info_get_version(&major, &minor);
    if (major != 1 || minor != 6) { printf("[%d] version %d.%d\n", me, major, minor); errs++; }

    if (errs) printf("[%d] test_mem13: %d errors\n", me, errs);
    else printf("[%d] test_mem13: OK\n", me);
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
