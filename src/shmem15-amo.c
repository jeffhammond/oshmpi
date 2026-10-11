/* OpenSHMEM 1.5/1.6: nonblocking atomics, malloc with hints and context sessions.
 *
 * The nonblocking atomics are completed inside the call.  The specification only says the
 * fetched value is valid after a subsequent quiet, so finishing early is allowed, and it lets
 * these routines reuse the blocking atomics (and with them the direct and MPI paths). */

#include "shmem-internals.h"

/* ---- nonblocking atomics ----------------------------------------------- */

#define OSHMPI_DEFINE_AMO_NBI_FETCH_SWAP(N, T)                                                 \
void shmem_ctx_##N##_atomic_fetch_nbi(shmem_ctx_t ctx, T *fetch, const T *source, int pe)      \
{ *fetch = shmem_ctx_##N##_atomic_fetch(ctx, source, pe); }                                    \
void shmem_##N##_atomic_fetch_nbi(T *fetch, const T *source, int pe)                           \
{ shmem_ctx_##N##_atomic_fetch_nbi(SHMEM_CTX_DEFAULT, fetch, source, pe); }                    \
void shmem_ctx_##N##_atomic_swap_nbi(shmem_ctx_t ctx, T *fetch, T *dest, T value, int pe)      \
{ *fetch = shmem_ctx_##N##_atomic_swap(ctx, dest, value, pe); }                                \
void shmem_##N##_atomic_swap_nbi(T *fetch, T *dest, T value, int pe)                           \
{ shmem_ctx_##N##_atomic_swap_nbi(SHMEM_CTX_DEFAULT, fetch, dest, value, pe); }

#define OSHMPI_DEFINE_AMO_NBI_INT(N, T)                                                        \
void shmem_ctx_##N##_atomic_compare_swap_nbi(shmem_ctx_t ctx, T *fetch, T *dest, T cond,       \
                                             T value, int pe)                                  \
{ *fetch = shmem_ctx_##N##_atomic_compare_swap(ctx, dest, cond, value, pe); }                  \
void shmem_##N##_atomic_compare_swap_nbi(T *fetch, T *dest, T cond, T value, int pe)           \
{ shmem_ctx_##N##_atomic_compare_swap_nbi(SHMEM_CTX_DEFAULT, fetch, dest, cond, value, pe); }   \
void shmem_ctx_##N##_atomic_fetch_inc_nbi(shmem_ctx_t ctx, T *fetch, T *dest, int pe)          \
{ *fetch = shmem_ctx_##N##_atomic_fetch_inc(ctx, dest, pe); }                                  \
void shmem_##N##_atomic_fetch_inc_nbi(T *fetch, T *dest, int pe)                               \
{ shmem_ctx_##N##_atomic_fetch_inc_nbi(SHMEM_CTX_DEFAULT, fetch, dest, pe); }                  \
void shmem_ctx_##N##_atomic_fetch_add_nbi(shmem_ctx_t ctx, T *fetch, T *dest, T value, int pe) \
{ *fetch = shmem_ctx_##N##_atomic_fetch_add(ctx, dest, value, pe); }                           \
void shmem_##N##_atomic_fetch_add_nbi(T *fetch, T *dest, T value, int pe)                      \
{ shmem_ctx_##N##_atomic_fetch_add_nbi(SHMEM_CTX_DEFAULT, fetch, dest, value, pe); }

#define OSHMPI_DEFINE_AMO_NBI_BITWISE(N, T)                                                    \
void shmem_ctx_##N##_atomic_fetch_and_nbi(shmem_ctx_t ctx, T *fetch, T *dest, T value, int pe) \
{ *fetch = shmem_ctx_##N##_atomic_fetch_and(ctx, dest, value, pe); }                           \
void shmem_##N##_atomic_fetch_and_nbi(T *fetch, T *dest, T value, int pe)                      \
{ shmem_ctx_##N##_atomic_fetch_and_nbi(SHMEM_CTX_DEFAULT, fetch, dest, value, pe); }           \
void shmem_ctx_##N##_atomic_fetch_or_nbi(shmem_ctx_t ctx, T *fetch, T *dest, T value, int pe)  \
{ *fetch = shmem_ctx_##N##_atomic_fetch_or(ctx, dest, value, pe); }                            \
void shmem_##N##_atomic_fetch_or_nbi(T *fetch, T *dest, T value, int pe)                       \
{ shmem_ctx_##N##_atomic_fetch_or_nbi(SHMEM_CTX_DEFAULT, fetch, dest, value, pe); }            \
void shmem_ctx_##N##_atomic_fetch_xor_nbi(shmem_ctx_t ctx, T *fetch, T *dest, T value, int pe) \
{ *fetch = shmem_ctx_##N##_atomic_fetch_xor(ctx, dest, value, pe); }                           \
void shmem_##N##_atomic_fetch_xor_nbi(T *fetch, T *dest, T value, int pe)                      \
{ shmem_ctx_##N##_atomic_fetch_xor_nbi(SHMEM_CTX_DEFAULT, fetch, dest, value, pe); }

OSHMPI_AMO_TYPES(OSHMPI_DEFINE_AMO_NBI_FETCH_SWAP)
OSHMPI_EXT_AMO_ONLY_TYPES(OSHMPI_DEFINE_AMO_NBI_FETCH_SWAP)
OSHMPI_AMO_TYPES(OSHMPI_DEFINE_AMO_NBI_INT)
OSHMPI_BITWISE_AMO_TYPES(OSHMPI_DEFINE_AMO_NBI_BITWISE)

/* ---- malloc with hints ------------------------------------------------- */

/* The hints only describe how the memory will be used.  Without an optimization to apply, the
 * specification says the behavior is that of shmem_malloc, including its barrier on exit and
 * the null pointer for a zero size. */
void *shmem_malloc_with_hints(size_t size, long hints)
{
    (void) hints;
    return shmem_malloc(size);
}

/* ---- context sessions -------------------------------------------------- */

/* Sessions and their configuration are hints that "do not imply the occurrence of any
 * completion or memory ordering operations", and an implementation may ignore them, so both
 * routines have nothing to do.  A call with SHMEM_CTX_INVALID performs no action either way. */
void shmem_ctx_session_start(shmem_ctx_t ctx, long options,
                             const shmem_ctx_session_config_t *config, long config_mask)
{
    (void) ctx; (void) options; (void) config; (void) config_mask;
}

void shmem_ctx_session_stop(shmem_ctx_t ctx)
{
    (void) ctx;
}
