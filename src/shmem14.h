/* BSD-2 License.
 *
 * OpenSHMEM 1.4 additions: communication contexts, the standard-named RMA and
 * atomic routines, bitwise atomics, point-to-point test and sync.
 *
 * This header is included from shmem.h.
 */

#ifndef OSHMPI_SHMEM14_H
#define OSHMPI_SHMEM14_H

#include <stddef.h>
#include <stdint.h>

/* 9.3 Communication management: contexts */
typedef struct oshmpi_ctx_s * shmem_ctx_t;

#define SHMEM_CTX_INVALID ((shmem_ctx_t) 0)
#define SHMEM_CTX_DEFAULT ((shmem_ctx_t) 1)

#define SHMEM_CTX_SERIALIZED (1L<<0)
#define SHMEM_CTX_PRIVATE    (1L<<1)
#define SHMEM_CTX_NOSTORE    (1L<<2)

int  shmem_ctx_create(long options, shmem_ctx_t *ctx);
void shmem_ctx_destroy(shmem_ctx_t ctx);
void shmem_ctx_fence(shmem_ctx_t ctx);
void shmem_ctx_quiet(shmem_ctx_t ctx);

/* Type tables: X(name, C type).
 *
 * RMA types: the standard RMA types and names.  The first group already existed
 * as non-context routines before 1.4 (shmem_short_put etc.). */
#define OSHMPI_RMA_TYPES_PRE14(X) \
    X(float,      float)          \
    X(double,     double)         \
    X(longdouble, long double)    \
    X(char,       char)           \
    X(short,      short)          \
    X(int,        int)            \
    X(long,       long)           \
    X(longlong,   long long)

#define OSHMPI_RMA_TYPES_NEW14(X) \
    X(schar,      signed char)    \
    X(uchar,      unsigned char)  \
    X(ushort,     unsigned short) \
    X(uint,       unsigned int)   \
    X(ulong,      unsigned long)  \
    X(ulonglong,  unsigned long long) \
    X(int8,       int8_t)         \
    X(int16,      int16_t)        \
    X(int32,      int32_t)        \
    X(int64,      int64_t)        \
    X(uint8,      uint8_t)        \
    X(uint16,     uint16_t)       \
    X(uint32,     uint32_t)       \
    X(uint64,     uint64_t)       \
    X(size,       size_t)         \
    X(ptrdiff,    ptrdiff_t)

#define OSHMPI_RMA_TYPES(X) OSHMPI_RMA_TYPES_PRE14(X) OSHMPI_RMA_TYPES_NEW14(X)

/* Standard AMO types: fetch, set, swap, compare_swap, inc, add. */
#define OSHMPI_AMO_TYPES(X)       \
    X(int,        int)            \
    X(long,       long)           \
    X(longlong,   long long)      \
    X(uint,       unsigned int)   \
    X(ulong,      unsigned long)  \
    X(ulonglong,  unsigned long long) \
    X(int32,      int32_t)        \
    X(int64,      int64_t)        \
    X(uint32,     uint32_t)       \
    X(uint64,     uint64_t)       \
    X(size,       size_t)         \
    X(ptrdiff,    ptrdiff_t)

/* Extended AMO types: fetch, set and swap also accept these. */
#define OSHMPI_EXT_AMO_ONLY_TYPES(X) \
    X(float,      float)          \
    X(double,     double)

/* Bitwise AMO types. */
#define OSHMPI_BITWISE_AMO_TYPES(X) \
    X(uint,       unsigned int)   \
    X(ulong,      unsigned long)  \
    X(ulonglong,  unsigned long long) \
    X(int32,      int32_t)        \
    X(int64,      int64_t)        \
    X(uint32,     uint32_t)       \
    X(uint64,     uint64_t)

/* Point-to-point synchronization types: wait_until and test. */
#define OSHMPI_SYNC_TYPES(X)      \
    X(short,      short)          \
    X(int,        int)            \
    X(long,       long)           \
    X(longlong,   long long)      \
    X(ushort,     unsigned short) \
    X(uint,       unsigned int)   \
    X(ulong,      unsigned long)  \
    X(ulonglong,  unsigned long long) \
    X(int32,      int32_t)        \
    X(int64,      int64_t)        \
    X(uint32,     uint32_t)       \
    X(uint64,     uint64_t)       \
    X(size,       size_t)         \
    X(ptrdiff,    ptrdiff_t)

/* RMA: context versions for every type; default-context versions for the types
 * that did not have them before 1.4. */
#define OSHMPI_DECL_RMA_CTX(N, T)                                                              \
    void shmem_ctx_##N##_p(shmem_ctx_t ctx, T *dest, T value, int pe);                         \
    T    shmem_ctx_##N##_g(shmem_ctx_t ctx, const T *src, int pe);                             \
    void shmem_ctx_##N##_put(shmem_ctx_t ctx, T *dest, const T *src, size_t nelems, int pe);   \
    void shmem_ctx_##N##_get(shmem_ctx_t ctx, T *dest, const T *src, size_t nelems, int pe);   \
    void shmem_ctx_##N##_put_nbi(shmem_ctx_t ctx, T *dest, const T *src, size_t nelems, int pe); \
    void shmem_ctx_##N##_get_nbi(shmem_ctx_t ctx, T *dest, const T *src, size_t nelems, int pe); \
    void shmem_ctx_##N##_iput(shmem_ctx_t ctx, T *dest, const T *src, ptrdiff_t dst,          \
                              ptrdiff_t sst, size_t nelems, int pe);                           \
    void shmem_ctx_##N##_iget(shmem_ctx_t ctx, T *dest, const T *src, ptrdiff_t dst,          \
                              ptrdiff_t sst, size_t nelems, int pe);

#define OSHMPI_DECL_RMA_DEFAULT(N, T)                                                          \
    void shmem_##N##_p(T *dest, T value, int pe);                                              \
    T    shmem_##N##_g(const T *src, int pe);                                                  \
    void shmem_##N##_put(T *dest, const T *src, size_t nelems, int pe);                        \
    void shmem_##N##_get(T *dest, const T *src, size_t nelems, int pe);                        \
    void shmem_##N##_put_nbi(T *dest, const T *src, size_t nelems, int pe);                    \
    void shmem_##N##_get_nbi(T *dest, const T *src, size_t nelems, int pe);                    \
    void shmem_##N##_iput(T *dest, const T *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe); \
    void shmem_##N##_iget(T *dest, const T *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe);

OSHMPI_RMA_TYPES(OSHMPI_DECL_RMA_CTX)
OSHMPI_RMA_TYPES_NEW14(OSHMPI_DECL_RMA_DEFAULT)

/* shmem_char_{put,get,p,g,...} existed before 1.4 but the strided versions did not. */
void shmem_char_iput(char *dest, const char *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe);
void shmem_char_iget(char *dest, const char *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe);

/* Sized and untyped RMA: context versions. */
#define OSHMPI_DECL_SIZED_CTX(S)                                                               \
    void shmem_ctx_put##S(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe); \
    void shmem_ctx_get##S(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe); \
    void shmem_ctx_put##S##_nbi(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe); \
    void shmem_ctx_get##S##_nbi(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe); \
    void shmem_ctx_iput##S(shmem_ctx_t ctx, void *dest, const void *src, ptrdiff_t dst,        \
                           ptrdiff_t sst, size_t nelems, int pe);                              \
    void shmem_ctx_iget##S(shmem_ctx_t ctx, void *dest, const void *src, ptrdiff_t dst,        \
                           ptrdiff_t sst, size_t nelems, int pe);

OSHMPI_DECL_SIZED_CTX(8)
OSHMPI_DECL_SIZED_CTX(16)
OSHMPI_DECL_SIZED_CTX(32)
OSHMPI_DECL_SIZED_CTX(64)
OSHMPI_DECL_SIZED_CTX(128)

void shmem_ctx_putmem(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe);
void shmem_ctx_getmem(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe);
void shmem_ctx_putmem_nbi(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe);
void shmem_ctx_getmem_nbi(shmem_ctx_t ctx, void *dest, const void *src, size_t nelems, int pe);

/* Strided 8- and 16-bit routines have no pre-1.4 default-context versions. */
void shmem_iput8(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe);
void shmem_iput16(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe);
void shmem_iget8(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe);
void shmem_iget16(void *dest, const void *src, ptrdiff_t dst, ptrdiff_t sst, size_t nelems, int pe);

/* 9.7 Atomic memory operations */
#define OSHMPI_DECL_AMO_FETCH_SET_SWAP(N, T)                                                   \
    T    shmem_##N##_atomic_fetch(const T *dest, int pe);                                      \
    void shmem_##N##_atomic_set(T *dest, T value, int pe);                                     \
    T    shmem_##N##_atomic_swap(T *dest, T value, int pe);                                    \
    T    shmem_ctx_##N##_atomic_fetch(shmem_ctx_t ctx, const T *dest, int pe);                 \
    void shmem_ctx_##N##_atomic_set(shmem_ctx_t ctx, T *dest, T value, int pe);                \
    T    shmem_ctx_##N##_atomic_swap(shmem_ctx_t ctx, T *dest, T value, int pe);

#define OSHMPI_DECL_AMO_INT(N, T)                                                              \
    T    shmem_##N##_atomic_compare_swap(T *dest, T cond, T value, int pe);                    \
    T    shmem_##N##_atomic_fetch_inc(T *dest, int pe);                                        \
    void shmem_##N##_atomic_inc(T *dest, int pe);                                              \
    T    shmem_##N##_atomic_fetch_add(T *dest, T value, int pe);                               \
    void shmem_##N##_atomic_add(T *dest, T value, int pe);                                     \
    T    shmem_ctx_##N##_atomic_compare_swap(shmem_ctx_t ctx, T *dest, T cond, T value, int pe); \
    T    shmem_ctx_##N##_atomic_fetch_inc(shmem_ctx_t ctx, T *dest, int pe);                   \
    void shmem_ctx_##N##_atomic_inc(shmem_ctx_t ctx, T *dest, int pe);                         \
    T    shmem_ctx_##N##_atomic_fetch_add(shmem_ctx_t ctx, T *dest, T value, int pe);          \
    void shmem_ctx_##N##_atomic_add(shmem_ctx_t ctx, T *dest, T value, int pe);

#define OSHMPI_DECL_AMO_BITWISE(N, T)                                                          \
    T    shmem_##N##_atomic_fetch_and(T *dest, T value, int pe);                               \
    void shmem_##N##_atomic_and(T *dest, T value, int pe);                                     \
    T    shmem_##N##_atomic_fetch_or(T *dest, T value, int pe);                                \
    void shmem_##N##_atomic_or(T *dest, T value, int pe);                                      \
    T    shmem_##N##_atomic_fetch_xor(T *dest, T value, int pe);                               \
    void shmem_##N##_atomic_xor(T *dest, T value, int pe);                                     \
    T    shmem_ctx_##N##_atomic_fetch_and(shmem_ctx_t ctx, T *dest, T value, int pe);          \
    void shmem_ctx_##N##_atomic_and(shmem_ctx_t ctx, T *dest, T value, int pe);                \
    T    shmem_ctx_##N##_atomic_fetch_or(shmem_ctx_t ctx, T *dest, T value, int pe);           \
    void shmem_ctx_##N##_atomic_or(shmem_ctx_t ctx, T *dest, T value, int pe);                 \
    T    shmem_ctx_##N##_atomic_fetch_xor(shmem_ctx_t ctx, T *dest, T value, int pe);          \
    void shmem_ctx_##N##_atomic_xor(shmem_ctx_t ctx, T *dest, T value, int pe);

OSHMPI_AMO_TYPES(OSHMPI_DECL_AMO_FETCH_SET_SWAP)
OSHMPI_EXT_AMO_ONLY_TYPES(OSHMPI_DECL_AMO_FETCH_SET_SWAP)
OSHMPI_AMO_TYPES(OSHMPI_DECL_AMO_INT)
OSHMPI_BITWISE_AMO_TYPES(OSHMPI_DECL_AMO_BITWISE)

/* 9.9 Point-to-point synchronization: test.  (wait_until is declared in shmem.h
 * for the original types and here for the rest.) */
#define OSHMPI_DECL_SYNC(N, T)                                                                 \
    void shmem_##N##_wait_until(volatile T *ivar, int cmp, T cmp_value);                       \
    int  shmem_##N##_test(volatile T *ivar, int cmp, T cmp_value);

OSHMPI_SYNC_TYPES(OSHMPI_DECL_SYNC)

/* 9.8 Collective synchronization: no memory-ordering guarantees, unlike barrier. */
void shmem_sync(int PE_start, int logPE_stride, int PE_size, long *pSync);
void shmem_sync_all(void);

#endif /* OSHMPI_SHMEM14_H */
