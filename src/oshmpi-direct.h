/* Direct load/store access to the symmetric heap, detected at run time.
 *
 * The heap is always created with MPI_Win_allocate.  After that, MPI_Win_shared_query is
 * asked for every PE's base address (MPI 4.1 lets it be called on any allocated window; it
 * reports a null pointer where the memory is not directly addressable, and older MPI
 * libraries raise an error that oshmpi_direct_init turns into "not available").  Only if
 * every PE's heap is mapped here, which in practice means the whole job is on one node, is
 * the direct path enabled, and then it is used for every operation on the heap by every PE.
 * Nothing touches the heap through MPI RMA in that mode, so hardware atomics are never mixed
 * with MPI atomics on the same word.  The data segment is not covered and always uses MPI. */

#ifndef OSHMPI_DIRECT_H
#define OSHMPI_DIRECT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <mpi.h>

/* nonzero when every PE's symmetric heap is mapped in this process */
extern int      shmem_sheap_direct;
/* [pe]: base of PE's heap as mapped here (our own entry is our local base) */
extern char **  shmem_sheap_peer_base;

/* defined in shmem-internals.c; the heap base and length of this PE */
extern void *   shmem_sheap_base_ptr;
extern long     shmem_sheap_size;

void oshmpi_direct_init(MPI_Win sheap_win);
void oshmpi_direct_finalize(void);

/* The address of 'addr' (a symmetric address in our heap) in PE 'pe', or NULL if the
 * address is not in the heap or the direct path is not enabled. */
static inline void * oshmpi_direct_ptr(const volatile void * addr, int pe)
{
    if (!shmem_sheap_direct) return NULL;
    uintptr_t off = (uintptr_t) addr - (uintptr_t) shmem_sheap_base_ptr;
    if (off >= (uintptr_t) shmem_sheap_size) return NULL;
    return shmem_sheap_peer_base[pe] + off;
}

/* Size of an MPI datatype, without an MPI call for the basic types the library uses. */
static inline int oshmpi_type_size(MPI_Datatype t)
{
    if (t == MPI_INT)           return (int) sizeof(int);
    if (t == MPI_LONG)          return (int) sizeof(long);
    if (t == MPI_LONG_LONG)     return (int) sizeof(long long);
    if (t == MPI_SHORT)         return (int) sizeof(short);
    if (t == MPI_FLOAT)         return (int) sizeof(float);
    if (t == MPI_DOUBLE)        return (int) sizeof(double);
    if (t == MPI_CHAR || t == MPI_SIGNED_CHAR || t == MPI_UNSIGNED_CHAR || t == MPI_INT8_T) return 1;
    int size;
    MPI_Type_size(t, &size);
    return size;
}

/* One atomic read-modify-write on an element of 1, 2, 4 or 8 bytes in directly addressable
 * memory.  'op' is MPI_NO_OP (fetch), MPI_REPLACE (set / swap), MPI_SUM, MPI_BAND, MPI_BOR or
 * MPI_BXOR; if 'compare' is not NULL it is a compare-and-swap of 'input' for 'compare'.  The
 * previous value is stored to 'output' when that is not NULL.  Returns 0 if the element size
 * or operation is not supported.  Sequentially consistent. */
#define OSHMPI_DIRECT_RMW_CASE(U)                                                          \
    do {                                                                                   \
        U *q = (U *) p;                                                                    \
        U v = 0, old;                                                                      \
        if (input) memcpy(&v, input, sizeof(U));                                           \
        if (compare) {                                                                     \
            memcpy(&old, compare, sizeof(U));                                              \
            __atomic_compare_exchange_n(q, &old, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);\
        } else if (op == MPI_NO_OP)   old = __atomic_load_n(q, __ATOMIC_SEQ_CST);          \
        else if (op == MPI_REPLACE)   old = __atomic_exchange_n(q, v, __ATOMIC_SEQ_CST);   \
        else if (op == MPI_SUM)       old = __atomic_fetch_add(q, v, __ATOMIC_SEQ_CST);    \
        else if (op == MPI_BAND)      old = __atomic_fetch_and(q, v, __ATOMIC_SEQ_CST);    \
        else if (op == MPI_BOR)       old = __atomic_fetch_or(q, v, __ATOMIC_SEQ_CST);     \
        else if (op == MPI_BXOR)      old = __atomic_fetch_xor(q, v, __ATOMIC_SEQ_CST);    \
        else return 0;                                                                     \
        if (output) memcpy(output, &old, sizeof(U));                                       \
        return 1;                                                                          \
    } while (0)

static inline int oshmpi_direct_rmw(MPI_Datatype t, MPI_Op op, void * p, const void * input,
                                    const void * compare, void * output)
{
    switch (oshmpi_type_size(t)) {
        case 1: OSHMPI_DIRECT_RMW_CASE(uint8_t);
        case 2: OSHMPI_DIRECT_RMW_CASE(uint16_t);
        case 4: OSHMPI_DIRECT_RMW_CASE(uint32_t);
        case 8: OSHMPI_DIRECT_RMW_CASE(uint64_t);
        default: return 0;
    }
}

/* count elements of elsize bytes, with the given strides in elements */
static inline void oshmpi_direct_strided(char * dst, ptrdiff_t dst_stride, const char * src,
                                         ptrdiff_t src_stride, size_t count, size_t elsize)
{
    for (size_t i = 0; i < count; i++)
        memcpy(dst + (ptrdiff_t) i * dst_stride * (ptrdiff_t) elsize,
               src + (ptrdiff_t) i * src_stride * (ptrdiff_t) elsize, elsize);
}

#endif /* OSHMPI_DIRECT_H */
