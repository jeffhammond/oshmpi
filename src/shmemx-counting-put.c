/* BSD-2 License.  Written by Jeff Hammond. */

#ifdef EXTENSION_COUNTING_PUT

#include "shmemx.h"
#include "shmem-internals.h"

void shmemx_ct_create(shmemx_ct_t *ct)
{
    *ct = shmalloc(sizeof(long));
}

void shmemx_ct_free(shmemx_ct_t *ct)
{
    shfree(*ct);
}

long shmemx_ct_get(shmemx_ct_t ct)
{
    long output;
    oshmpi_fetch(MPI_LONG, &output, ct, shmem_world_rank);
    return output;
}

void shmemx_ct_set(shmemx_ct_t ct, long value)
{
    oshmpi_set(MPI_LONG, ct, &value, shmem_world_rank);
}

void shmemx_ct_wait(shmemx_ct_t ct, long wait_for)
{
    while (wait_for != shmemx_ct_get(ct));
}

void shmemx_putmem_ct(shmemx_ct_t ct, void *target, const void *source, size_t len, int pe)
{
    shmem_putmem(target, source, len, pe);
    oshmpi_remote_sync_pe(pe);
    shmem_long_add(ct, 1, pe);
}

#endif
