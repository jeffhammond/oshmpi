/* OpenSHMEM 1.6 additions that are not collectives.  Included from shmem15.h, inside its
 * extern "C" block.  The scans are in shmem15-team.h next to the reductions. */

#ifndef OSHMPI_SHMEM16_H
#define OSHMPI_SHMEM16_H

#include "shmem14.h"

void shmem_query_initialized(int *initialized);

void shmem_pe_quiet(const int *target_pes, size_t npes);
void shmem_ctx_pe_quiet(shmem_ctx_t ctx, const int *target_pes, size_t npes);

/* Interleaved block transfers: nblocks blocks of bsize elements, with block strides dst and sst */
#define OSHMPI_DECL_IB(N, T)                                                                   \
    void shmem_##N##_ibput(T *dest, const T *src, ptrdiff_t dst, ptrdiff_t sst, size_t bsize,  \
                           size_t nblocks, int pe);                                            \
    void shmem_##N##_ibget(T *dest, const T *src, ptrdiff_t dst, ptrdiff_t sst, size_t bsize,  \
                           size_t nblocks, int pe);                                            \
    void shmem_ctx_##N##_ibput(shmem_ctx_t ctx, T *dest, const T *src, ptrdiff_t dst,          \
                               ptrdiff_t sst, size_t bsize, size_t nblocks, int pe);           \
    void shmem_ctx_##N##_ibget(shmem_ctx_t ctx, T *dest, const T *src, ptrdiff_t dst,          \
                               ptrdiff_t sst, size_t bsize, size_t nblocks, int pe);
OSHMPI_RMA_TYPES(OSHMPI_DECL_IB)

#define OSHMPI_DECL_IB_SIZED(S)                                                                \
    void shmem_ibput##S(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst,             \
                        size_t bsize, size_t nblocks, int pe);                                 \
    void shmem_ibget##S(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst,             \
                        size_t bsize, size_t nblocks, int pe);                                 \
    void shmem_ctx_ibput##S(shmem_ctx_t ctx, void *dest, const void *src, ptrdiff_t dst,       \
                            ptrdiff_t sst, size_t bsize, size_t nblocks, int pe);              \
    void shmem_ctx_ibget##S(shmem_ctx_t ctx, void *dest, const void *src, ptrdiff_t dst,       \
                            ptrdiff_t sst, size_t bsize, size_t nblocks, int pe);
OSHMPI_DECL_IB_SIZED(8)
OSHMPI_DECL_IB_SIZED(16)
OSHMPI_DECL_IB_SIZED(32)
OSHMPI_DECL_IB_SIZED(64)
OSHMPI_DECL_IB_SIZED(128)

#endif /* OSHMPI_SHMEM16_H */
