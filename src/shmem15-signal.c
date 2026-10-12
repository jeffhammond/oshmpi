/* OpenSHMEM 1.5/1.6 signaling: put-with-signal, signal add / set / fetch and signal wait. */

#include "shmem-internals.h"
#include "shmem-wait.h"

/* The signal is an unsigned 64-bit word.  MPI has no unsigned 64-bit type that the
 * library's atomics recognize, but SUM and REPLACE wrap identically on a signed one. */
#define OSHMPI_SIG_MPI_TYPE (sizeof(long) == 8 ? MPI_LONG : MPI_LONG_LONG)

static void oshmpi_signal_update(uint64_t * sig_addr, uint64_t signal, int sig_op, int pe)
{
    switch (sig_op) {
        case SHMEM_SIGNAL_SET:
            oshmpi_set(OSHMPI_SIG_MPI_TYPE, sig_addr, &signal, pe);
            break;
        case SHMEM_SIGNAL_ADD:
            oshmpi_add(OSHMPI_SIG_MPI_TYPE, sig_addr, &signal, pe);
            break;
        default:
            oshmpi_abort(sig_op, "invalid signal operator");
    }
}

/* Copy the data, make it complete at the target, then update the signal there, so that a PE
 * that sees the signal also sees the data.  On the direct heap path the data are stores and
 * need only a fence before the signal's atomic; on the MPI path they need a flush of the
 * target, since MPI does not order a put before a later atomic on its own. */
static void oshmpi_put_signal(void * dest, const void * source, size_t nbytes,
                              uint64_t * sig_addr, uint64_t signal, int sig_op, int pe, int nbi)
{
    if (nbi)
        oshmpi_put_nbi(MPI_INT8_T, dest, source, nbytes, pe);
    else
        oshmpi_put(MPI_INT8_T, dest, source, nbytes, pe);

    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (nbytes != 0 && oshmpi_direct_ptr(dest, pe) == NULL)
        oshmpi_remote_sync_pe(pe);

    oshmpi_signal_update(sig_addr, signal, sig_op, pe);
}

#define OSHMPI_DEF_PUT_SIGNAL(N, T)                                                            \
void shmem_##N##_put_signal(T *dest, const T *source, size_t nelems, uint64_t *sig_addr,       \
                            uint64_t signal, int sig_op, int pe)                               \
{ oshmpi_put_signal(dest, source, nelems * sizeof(T), sig_addr, signal, sig_op, pe, 0); }      \
void shmem_##N##_put_signal_nbi(T *dest, const T *source, size_t nelems, uint64_t *sig_addr,   \
                                uint64_t signal, int sig_op, int pe)                           \
{ oshmpi_put_signal(dest, source, nelems * sizeof(T), sig_addr, signal, sig_op, pe, 1); }      \
void shmem_ctx_##N##_put_signal(shmem_ctx_t ctx, T *dest, const T *source, size_t nelems,      \
                                uint64_t *sig_addr, uint64_t signal, int sig_op, int pe)       \
{ shmem_##N##_put_signal(dest, source, nelems, sig_addr, signal, sig_op, oshmpi_ctx_pe(ctx, pe)); }    \
void shmem_ctx_##N##_put_signal_nbi(shmem_ctx_t ctx, T *dest, const T *source, size_t nelems,  \
                                    uint64_t *sig_addr, uint64_t signal, int sig_op, int pe)   \
{ shmem_##N##_put_signal_nbi(dest, source, nelems, sig_addr, signal, sig_op, oshmpi_ctx_pe(ctx, pe)); }

OSHMPI_RMA_TYPES(OSHMPI_DEF_PUT_SIGNAL)

/* BYTES is the element size in bytes: 1 for putmem, 1, 2, 4, 8 and 16 for the sized forms. */
#define OSHMPI_DEF_PUT_SIGNAL_SIZED(S, BYTES)                                                  \
void shmem_put##S##_signal(void *dest, const void *source, size_t nelems, uint64_t *sig_addr,  \
                           uint64_t signal, int sig_op, int pe)                                \
{ oshmpi_put_signal(dest, source, nelems * (BYTES), sig_addr, signal, sig_op, pe, 0); }        \
void shmem_put##S##_signal_nbi(void *dest, const void *source, size_t nelems,                  \
                               uint64_t *sig_addr, uint64_t signal, int sig_op, int pe)        \
{ oshmpi_put_signal(dest, source, nelems * (BYTES), sig_addr, signal, sig_op, pe, 1); }        \
void shmem_ctx_put##S##_signal(shmem_ctx_t ctx, void *dest, const void *source, size_t nelems, \
                               uint64_t *sig_addr, uint64_t signal, int sig_op, int pe)        \
{ shmem_put##S##_signal(dest, source, nelems, sig_addr, signal, sig_op, oshmpi_ctx_pe(ctx, pe)); }     \
void shmem_ctx_put##S##_signal_nbi(shmem_ctx_t ctx, void *dest, const void *source,            \
                                   size_t nelems, uint64_t *sig_addr, uint64_t signal,         \
                                   int sig_op, int pe)                                         \
{ shmem_put##S##_signal_nbi(dest, source, nelems, sig_addr, signal, sig_op, oshmpi_ctx_pe(ctx, pe)); }

OSHMPI_DEF_PUT_SIGNAL_SIZED(mem, 1)
OSHMPI_DEF_PUT_SIGNAL_SIZED(8, 1)
OSHMPI_DEF_PUT_SIGNAL_SIZED(16, 2)
OSHMPI_DEF_PUT_SIGNAL_SIZED(32, 4)
OSHMPI_DEF_PUT_SIGNAL_SIZED(64, 8)
OSHMPI_DEF_PUT_SIGNAL_SIZED(128, 16)

void shmem_signal_add(uint64_t *sig_addr, uint64_t signal, int pe)
{
    oshmpi_add(OSHMPI_SIG_MPI_TYPE, sig_addr, &signal, pe);
}

void shmem_ctx_signal_add(shmem_ctx_t ctx, uint64_t *sig_addr, uint64_t signal, int pe)
{
    oshmpi_add(OSHMPI_SIG_MPI_TYPE, sig_addr, &signal, oshmpi_ctx_pe(ctx, pe));
}

void shmem_signal_set(uint64_t *sig_addr, uint64_t signal, int pe)
{
    oshmpi_set(OSHMPI_SIG_MPI_TYPE, sig_addr, &signal, pe);
}

void shmem_ctx_signal_set(shmem_ctx_t ctx, uint64_t *sig_addr, uint64_t signal, int pe)
{
    oshmpi_set(OSHMPI_SIG_MPI_TYPE, sig_addr, &signal, oshmpi_ctx_pe(ctx, pe));
}

uint64_t shmem_signal_fetch(const uint64_t *sig_addr)
{
    uint64_t value;
    oshmpi_fetch(OSHMPI_SIG_MPI_TYPE, &value, sig_addr, shmem_world_rank);
    return value;
}

/* oshmpi_fetch of our own memory is an acquire load that also keeps MPI progressing, which is
 * what a polling loop needs. */
uint64_t shmem_signal_wait_until(uint64_t *sig_addr, int cmp, uint64_t cmp_value)
{
    uint64_t value;
    int satisfied = 0;
    while (!satisfied) {
        oshmpi_fetch(OSHMPI_SIG_MPI_TYPE, &value, sig_addr, shmem_world_rank);
        COMP(cmp, value, cmp_value, satisfied);
    }
    return value;
}

/* the names these had as extensions before they were standardized */
void shmemx_signal_add(uint64_t *sig_addr, uint64_t signal, int pe)
{
    shmem_signal_add(sig_addr, signal, pe);
}

void shmemx_signal_set(uint64_t *sig_addr, uint64_t signal, int pe)
{
    shmem_signal_set(sig_addr, signal, pe);
}
