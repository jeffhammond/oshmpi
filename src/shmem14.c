/* BSD-2 License.
 *
 * OpenSHMEM 1.4 additions: communication contexts, the standard-named RMA and
 * atomic routines, bitwise atomics, point-to-point test and sync.
 *
 * Contexts: every context shares the PE's windows, so all operations on a
 * context are issued the same way as on the default context.  A context's
 * fence and quiet therefore complete everything the PE has outstanding, which
 * is stronger than required (a context is only an ordering and completion
 * domain) and so is correct.
 */

#include "shmem-internals.h"
#include "shmem-wait.h"

/* MPI datatype used for each C type.  Integers use MPI_INT, MPI_LONG and
 * MPI_LONG_LONG by size because the shared-memory fast paths in
 * shmem-internals.c only recognize those. */
#define OSHMPI_MT_INT64 (sizeof(long) == 8 ? MPI_LONG : MPI_LONG_LONG)

#define OSHMPI_MT_float      MPI_FLOAT
#define OSHMPI_MT_double     MPI_DOUBLE
#define OSHMPI_MT_longdouble MPI_LONG_DOUBLE
#define OSHMPI_MT_char       MPI_CHAR
#define OSHMPI_MT_schar      MPI_SIGNED_CHAR
#define OSHMPI_MT_uchar      MPI_UNSIGNED_CHAR
#define OSHMPI_MT_short      MPI_SHORT
#define OSHMPI_MT_ushort     MPI_SHORT
#define OSHMPI_MT_int        MPI_INT
#define OSHMPI_MT_uint       MPI_INT
#define OSHMPI_MT_long       MPI_LONG
#define OSHMPI_MT_ulong      MPI_LONG
#define OSHMPI_MT_longlong   MPI_LONG_LONG
#define OSHMPI_MT_ulonglong  MPI_LONG_LONG
#define OSHMPI_MT_int8       MPI_INT8_T
#define OSHMPI_MT_uint8      MPI_INT8_T
#define OSHMPI_MT_int16      MPI_SHORT
#define OSHMPI_MT_uint16     MPI_SHORT
#define OSHMPI_MT_int32      MPI_INT
#define OSHMPI_MT_uint32     MPI_INT
#define OSHMPI_MT_int64      OSHMPI_MT_INT64
#define OSHMPI_MT_uint64     OSHMPI_MT_INT64
#define OSHMPI_MT_size       (sizeof(size_t) == 8 ? OSHMPI_MT_INT64 : MPI_INT)
#define OSHMPI_MT_ptrdiff    (sizeof(ptrdiff_t) == 8 ? OSHMPI_MT_INT64 : MPI_INT)

/* ---- contexts ---------------------------------------------------------- */

int shmem_ctx_create(long options, shmem_ctx_t *ctx)
{
    struct oshmpi_ctx_s * c = malloc(sizeof(struct oshmpi_ctx_s));
    if (c == NULL) {
        *ctx = SHMEM_CTX_INVALID;
        return -1;
    }
    c->options = options;
    c->team = NULL;
    *ctx = c;
    return 0;
}

void shmem_ctx_destroy(shmem_ctx_t ctx)
{
    if (ctx == SHMEM_CTX_INVALID || ctx == SHMEM_CTX_DEFAULT)
        return;
    shmem_quiet(); /* destroying a context completes its outstanding operations */
    free(ctx);
}

void shmem_ctx_fence(shmem_ctx_t ctx) { (void)ctx; shmem_fence(); }
void shmem_ctx_quiet(shmem_ctx_t ctx) { (void)ctx; shmem_quiet(); }

/* ---- RMA --------------------------------------------------------------- */

#define OSHMPI_DEFINE_RMA_CTX(N, T)                                                            \
void shmem_ctx_##N##_p(shmem_ctx_t ctx, T *dest, T value, int pe)                              \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_put(OSHMPI_MT_##N, dest, &value, 1, pe); }                                 \
T shmem_ctx_##N##_g(shmem_ctx_t ctx, const T *src, int pe)                                     \
{ T v; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_get(OSHMPI_MT_##N, &v, src, 1, pe); return v; }                       \
void shmem_ctx_##N##_put(shmem_ctx_t ctx, T *dest, const T *src, size_t nelems, int pe)        \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_put(OSHMPI_MT_##N, dest, src, nelems, pe); }                               \
void shmem_ctx_##N##_get(shmem_ctx_t ctx, T *dest, const T *src, size_t nelems, int pe)        \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_get(OSHMPI_MT_##N, dest, src, nelems, pe); }                               \
void shmem_ctx_##N##_put_nbi(shmem_ctx_t ctx, T *dest, const T *src, size_t nelems, int pe)    \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_put_nbi(OSHMPI_MT_##N, dest, src, nelems, pe); }                           \
void shmem_ctx_##N##_get_nbi(shmem_ctx_t ctx, T *dest, const T *src, size_t nelems, int pe)    \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_get_nbi(OSHMPI_MT_##N, dest, src, nelems, pe); }                           \
void shmem_ctx_##N##_iput(shmem_ctx_t ctx, T *dest, const T *src, ptrdiff_t dst,               \
                          ptrdiff_t sst, size_t nelems, int pe)                                \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_put_strided(OSHMPI_MT_##N, dest, src, dst, sst, nelems, pe); }             \
void shmem_ctx_##N##_iget(shmem_ctx_t ctx, T *dest, const T *src, ptrdiff_t dst,               \
                          ptrdiff_t sst, size_t nelems, int pe)                                \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_get_strided(OSHMPI_MT_##N, dest, src, dst, sst, nelems, pe); }

#define OSHMPI_DEFINE_RMA_DEFAULT(N, T)                                                        \
void shmem_##N##_p(T *dest, T value, int pe)                                                   \
{ oshmpi_put(OSHMPI_MT_##N, dest, &value, 1, pe); }                                            \
T shmem_##N##_g(const T *src, int pe)                                                          \
{ T v; oshmpi_get(OSHMPI_MT_##N, &v, src, 1, pe); return v; }                                  \
void shmem_##N##_put(T *dest, const T *src, size_t nelems, int pe)                             \
{ oshmpi_put(OSHMPI_MT_##N, dest, src, nelems, pe); }                                          \
void shmem_##N##_get(T *dest, const T *src, size_t nelems, int pe)                             \
{ oshmpi_get(OSHMPI_MT_##N, dest, src, nelems, pe); }                                          \
void shmem_##N##_put_nbi(T *dest, const T *src, size_t nelems, int pe)                         \
{ oshmpi_put_nbi(OSHMPI_MT_##N, dest, src, nelems, pe); }                                      \
void shmem_##N##_get_nbi(T *dest, const T *src, size_t nelems, int pe)                         \
{ oshmpi_get_nbi(OSHMPI_MT_##N, dest, src, nelems, pe); }                                      \
void shmem_##N##_iput(T *dest, const T *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe) \
{ oshmpi_put_strided(OSHMPI_MT_##N, dest, src, dst, sst, nelems, pe); }                        \
void shmem_##N##_iget(T *dest, const T *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe) \
{ oshmpi_get_strided(OSHMPI_MT_##N, dest, src, dst, sst, nelems, pe); }

OSHMPI_RMA_TYPES(OSHMPI_DEFINE_RMA_CTX)
OSHMPI_RMA_TYPES_NEW14(OSHMPI_DEFINE_RMA_DEFAULT)

void shmem_char_iput(char *dest, const char *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe)
{ oshmpi_put_strided(OSHMPI_MT_char, dest, src, dst, sst, nelems, pe); }
void shmem_char_iget(char *dest, const char *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe)
{ oshmpi_get_strided(OSHMPI_MT_char, dest, src, dst, sst, nelems, pe); }

/* Sized RMA moves S/8-byte elements; the datatype is the element size. */
#define OSHMPI_MT_SIZED_8   MPI_INT8_T
#define OSHMPI_MT_SIZED_16  MPI_SHORT
#define OSHMPI_MT_SIZED_32  MPI_INT
#define OSHMPI_MT_SIZED_64  OSHMPI_MT_INT64
#define OSHMPI_MT_SIZED_128 MPI_C_DOUBLE_COMPLEX

#define OSHMPI_DEFINE_SIZED_CTX(S)                                                             \
void shmem_ctx_put##S(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe)     \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_put(OSHMPI_MT_SIZED_##S, dest, src, nelems, pe); }                         \
void shmem_ctx_get##S(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe)     \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_get(OSHMPI_MT_SIZED_##S, dest, src, nelems, pe); }                         \
void shmem_ctx_put##S##_nbi(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe) \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_put_nbi(OSHMPI_MT_SIZED_##S, dest, src, nelems, pe); }                     \
void shmem_ctx_get##S##_nbi(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe) \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_get_nbi(OSHMPI_MT_SIZED_##S, dest, src, nelems, pe); }                     \
void shmem_ctx_iput##S(shmem_ctx_t ctx, void *dest, const void *src, ptrdiff_t dst,            \
                       ptrdiff_t sst, size_t nelems, int pe)                                   \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_put_strided(OSHMPI_MT_SIZED_##S, dest, src, dst, sst, nelems, pe); }       \
void shmem_ctx_iget##S(shmem_ctx_t ctx, void *dest, const void *src, ptrdiff_t dst,            \
                       ptrdiff_t sst, size_t nelems, int pe)                                   \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_get_strided(OSHMPI_MT_SIZED_##S, dest, src, dst, sst, nelems, pe); }

OSHMPI_DEFINE_SIZED_CTX(8)
OSHMPI_DEFINE_SIZED_CTX(16)
OSHMPI_DEFINE_SIZED_CTX(32)
OSHMPI_DEFINE_SIZED_CTX(64)
OSHMPI_DEFINE_SIZED_CTX(128)

void shmem_ctx_putmem(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe)
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_put(MPI_BYTE, dest, src, nelems, pe); }
void shmem_ctx_getmem(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe)
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_get(MPI_BYTE, dest, src, nelems, pe); }
void shmem_ctx_putmem_nbi(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe)
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_put_nbi(MPI_BYTE, dest, src, nelems, pe); }
void shmem_ctx_getmem_nbi(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe)
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_get_nbi(MPI_BYTE, dest, src, nelems, pe); }

void shmem_iput8(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe)
{ oshmpi_put_strided(OSHMPI_MT_SIZED_8, dest, src, dst, sst, nelems, pe); }
void shmem_iput16(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe)
{ oshmpi_put_strided(OSHMPI_MT_SIZED_16, dest, src, dst, sst, nelems, pe); }
void shmem_iget8(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe)
{ oshmpi_get_strided(OSHMPI_MT_SIZED_8, dest, src, dst, sst, nelems, pe); }
void shmem_iget16(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe)
{ oshmpi_get_strided(OSHMPI_MT_SIZED_16, dest, src, dst, sst, nelems, pe); }

/* ---- atomics ----------------------------------------------------------- */

#define OSHMPI_DEFINE_AMO_FETCH_SET_SWAP(N, T)                                                 \
T shmem_ctx_##N##_atomic_fetch(shmem_ctx_t ctx, const T *dest, int pe)                         \
{ T r; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_fetch(OSHMPI_MT_##N, &r, dest, pe); return r; }                       \
void shmem_ctx_##N##_atomic_set(shmem_ctx_t ctx, T *dest, T value, int pe)                     \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_set(OSHMPI_MT_##N, dest, &value, pe); }                                    \
T shmem_ctx_##N##_atomic_swap(shmem_ctx_t ctx, T *dest, T value, int pe)                       \
{ T r; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_swap(OSHMPI_MT_##N, &r, dest, &value, pe); return r; }                \
T shmem_##N##_atomic_fetch(const T *dest, int pe)                                              \
{ return shmem_ctx_##N##_atomic_fetch(SHMEM_CTX_DEFAULT, dest, pe); }                          \
void shmem_##N##_atomic_set(T *dest, T value, int pe)                                          \
{ shmem_ctx_##N##_atomic_set(SHMEM_CTX_DEFAULT, dest, value, pe); }                            \
T shmem_##N##_atomic_swap(T *dest, T value, int pe)                                            \
{ return shmem_ctx_##N##_atomic_swap(SHMEM_CTX_DEFAULT, dest, value, pe); }

#define OSHMPI_DEFINE_AMO_INT(N, T)                                                            \
T shmem_ctx_##N##_atomic_compare_swap(shmem_ctx_t ctx, T *dest, T cond, T value, int pe)       \
{ T r; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_cswap(OSHMPI_MT_##N, &r, dest, &value, &cond, pe); return r; }        \
T shmem_ctx_##N##_atomic_fetch_inc(shmem_ctx_t ctx, T *dest, int pe)                           \
{ T r, one = 1; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_fadd(OSHMPI_MT_##N, &r, dest, &one, pe); return r; }         \
void shmem_ctx_##N##_atomic_inc(shmem_ctx_t ctx, T *dest, int pe)                              \
{ T one = 1; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_add(OSHMPI_MT_##N, dest, &one, pe); }                           \
T shmem_ctx_##N##_atomic_fetch_add(shmem_ctx_t ctx, T *dest, T value, int pe)                  \
{ T r; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_fadd(OSHMPI_MT_##N, &r, dest, &value, pe); return r; }                \
void shmem_ctx_##N##_atomic_add(shmem_ctx_t ctx, T *dest, T value, int pe)                     \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_add(OSHMPI_MT_##N, dest, &value, pe); }                                    \
T shmem_##N##_atomic_compare_swap(T *dest, T cond, T value, int pe)                            \
{ return shmem_ctx_##N##_atomic_compare_swap(SHMEM_CTX_DEFAULT, dest, cond, value, pe); }      \
T shmem_##N##_atomic_fetch_inc(T *dest, int pe)                                                \
{ return shmem_ctx_##N##_atomic_fetch_inc(SHMEM_CTX_DEFAULT, dest, pe); }                      \
void shmem_##N##_atomic_inc(T *dest, int pe)                                                   \
{ shmem_ctx_##N##_atomic_inc(SHMEM_CTX_DEFAULT, dest, pe); }                                   \
T shmem_##N##_atomic_fetch_add(T *dest, T value, int pe)                                       \
{ return shmem_ctx_##N##_atomic_fetch_add(SHMEM_CTX_DEFAULT, dest, value, pe); }               \
void shmem_##N##_atomic_add(T *dest, T value, int pe)                                          \
{ shmem_ctx_##N##_atomic_add(SHMEM_CTX_DEFAULT, dest, value, pe); }

#define OSHMPI_DEFINE_AMO_BITWISE(N, T)                                                        \
T shmem_ctx_##N##_atomic_fetch_and(shmem_ctx_t ctx, T *dest, T value, int pe)                  \
{ T r; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_fetch_op(OSHMPI_MT_##N, MPI_BAND, &r, dest, &value, pe); return r; }  \
void shmem_ctx_##N##_atomic_and(shmem_ctx_t ctx, T *dest, T value, int pe)                     \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_op(OSHMPI_MT_##N, MPI_BAND, dest, &value, pe); }                           \
T shmem_ctx_##N##_atomic_fetch_or(shmem_ctx_t ctx, T *dest, T value, int pe)                   \
{ T r; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_fetch_op(OSHMPI_MT_##N, MPI_BOR, &r, dest, &value, pe); return r; }   \
void shmem_ctx_##N##_atomic_or(shmem_ctx_t ctx, T *dest, T value, int pe)                      \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_op(OSHMPI_MT_##N, MPI_BOR, dest, &value, pe); }                            \
T shmem_ctx_##N##_atomic_fetch_xor(shmem_ctx_t ctx, T *dest, T value, int pe)                  \
{ T r; pe = oshmpi_ctx_pe(ctx, pe); oshmpi_fetch_op(OSHMPI_MT_##N, MPI_BXOR, &r, dest, &value, pe); return r; }  \
void shmem_ctx_##N##_atomic_xor(shmem_ctx_t ctx, T *dest, T value, int pe)                     \
{ pe = oshmpi_ctx_pe(ctx, pe); oshmpi_op(OSHMPI_MT_##N, MPI_BXOR, dest, &value, pe); }                           \
T shmem_##N##_atomic_fetch_and(T *dest, T value, int pe)                                       \
{ return shmem_ctx_##N##_atomic_fetch_and(SHMEM_CTX_DEFAULT, dest, value, pe); }               \
void shmem_##N##_atomic_and(T *dest, T value, int pe)                                          \
{ shmem_ctx_##N##_atomic_and(SHMEM_CTX_DEFAULT, dest, value, pe); }                            \
T shmem_##N##_atomic_fetch_or(T *dest, T value, int pe)                                        \
{ return shmem_ctx_##N##_atomic_fetch_or(SHMEM_CTX_DEFAULT, dest, value, pe); }                \
void shmem_##N##_atomic_or(T *dest, T value, int pe)                                           \
{ shmem_ctx_##N##_atomic_or(SHMEM_CTX_DEFAULT, dest, value, pe); }                             \
T shmem_##N##_atomic_fetch_xor(T *dest, T value, int pe)                                       \
{ return shmem_ctx_##N##_atomic_fetch_xor(SHMEM_CTX_DEFAULT, dest, value, pe); }               \
void shmem_##N##_atomic_xor(T *dest, T value, int pe)                                          \
{ shmem_ctx_##N##_atomic_xor(SHMEM_CTX_DEFAULT, dest, value, pe); }

OSHMPI_AMO_TYPES(OSHMPI_DEFINE_AMO_FETCH_SET_SWAP)
OSHMPI_EXT_AMO_ONLY_TYPES(OSHMPI_DEFINE_AMO_FETCH_SET_SWAP)
OSHMPI_AMO_TYPES(OSHMPI_DEFINE_AMO_INT)
OSHMPI_BITWISE_AMO_TYPES(OSHMPI_DEFINE_AMO_BITWISE)

/* ---- point-to-point synchronization ------------------------------------ */

/* test: a single nonblocking check of the comparison; 1 if it holds, else 0. */
#define OSHMPI_DEFINE_TEST(N, T)                                                               \
int shmem_##N##_test(T *ivar, int cmp, T cmp_value)                                   \
{                                                                                              \
    T cur;                                                                                     \
    oshmpi_fetch(OSHMPI_MT_##N, &cur, (const T *)ivar, shmem_world_rank);                      \
    int ret;                                                                                   \
    COMP(cmp, cur, cmp_value, ret);                                                            \
    return ret;                                                                                \
}

/* wait_until for the types that do not already have it. */
#define OSHMPI_DEFINE_WAIT_UNTIL(N, T)                                                         \
void shmem_##N##_wait_until(T *ivar, int cmp, T cmp_value)                            \
{ T t; SHMEM_WAIT_UNTIL(ivar, cmp, cmp_value, t, OSHMPI_MT_##N); }

#define OSHMPI_SYNC_TYPES_NEW14(X) \
    X(ushort,     unsigned short)  \
    X(uint,       unsigned int)    \
    X(ulong,      unsigned long)   \
    X(ulonglong,  unsigned long long) \
    X(int32,      int32_t)         \
    X(int64,      int64_t)         \
    X(uint32,     uint32_t)        \
    X(uint64,     uint64_t)        \
    X(size,       size_t)          \
    X(ptrdiff,    ptrdiff_t)

OSHMPI_SYNC_TYPES(OSHMPI_DEFINE_TEST)
OSHMPI_SYNC_TYPES_NEW14(OSHMPI_DEFINE_WAIT_UNTIL)

/* ---- collective synchronization ---------------------------------------- */

/* Unlike shmem_barrier, sync ensures completion and visibility of previously issued
 * memory stores but not of remote memory updates issued through OpenSHMEM routines. */
void shmem_sync(int PE_start, int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_local_sync();
    oshmpi_set_psync(SHMEM_BARRIER_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_BARRIER, MPI_DATATYPE_NULL, MPI_OP_NULL, NULL, NULL, 0, 0, 0, -1,
                PE_start, logPE_stride, PE_size);
}

void shmem_sync_all(void)
{
    oshmpi_local_sync();
    MPI_Barrier(SHMEM_COMM_WORLD);
}
