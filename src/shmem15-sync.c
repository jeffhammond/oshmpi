/* OpenSHMEM 1.5: point-to-point synchronization on arrays of variables:
 * wait_until_{all,any,some} and test_{all,any,some}, plain and _vector.
 *
 * Every poll is an oshmpi_fetch of the element at this PE, which is an atomic load where the
 * memory is directly addressable and which also makes MPI progress, so a PE that waits here
 * still lets remote atomics aimed at it complete.  The any/some forms start each scan at a
 * rotating index, so an element that stays satisfied is returned by a series of calls even
 * when lower-numbered elements are also satisfied, as the specification requires. */

#include <stdint.h>
#include "shmem-wait.h"

/* the MPI datatypes used for the sync types, as in the 1.4 wait_until */
#define OSHMPI_P2P_MT_short      MPI_SHORT
#define OSHMPI_P2P_MT_int        MPI_INT
#define OSHMPI_P2P_MT_long       MPI_LONG
#define OSHMPI_P2P_MT_longlong   MPI_LONG_LONG
#define OSHMPI_P2P_MT_ushort     MPI_SHORT
#define OSHMPI_P2P_MT_uint       MPI_INT
#define OSHMPI_P2P_MT_ulong      MPI_LONG
#define OSHMPI_P2P_MT_ulonglong  MPI_LONG_LONG
#define OSHMPI_P2P_MT_int32      MPI_INT
#define OSHMPI_P2P_MT_uint32     MPI_INT
#define OSHMPI_P2P_MT_int64      (sizeof(long) == 8 ? MPI_LONG : MPI_LONG_LONG)
#define OSHMPI_P2P_MT_uint64     (sizeof(long) == 8 ? MPI_LONG : MPI_LONG_LONG)
#define OSHMPI_P2P_MT_size       (sizeof(size_t) == 8 ? OSHMPI_P2P_MT_int64 : MPI_INT)
#define OSHMPI_P2P_MT_ptrdiff    (sizeof(ptrdiff_t) == 8 ? OSHMPI_P2P_MT_int64 : MPI_INT)

static size_t oshmpi_p2p_rotor;

/* where a scan of n elements starts */
static inline size_t oshmpi_p2p_start(size_t n)
{
    return n ? __atomic_fetch_add(&oshmpi_p2p_rotor, 1, __ATOMIC_RELAXED) % n : 0;
}

static inline int oshmpi_p2p_excluded(const int *status, size_t i)
{
    return status != NULL && status[i] != 0;
}

/* VSUF, VDECL, VNAME and VEXPR select the scalar or the vector form of the comparison value:
 * its declaration, its name, and its value for element i. */
#define OSHMPI_DEFINE_SYNC15_FORM(N, T, VSUF, VDECL, VNAME, VEXPR)                                  \
                                                                                               \
static inline int oshmpi_p2p_holds_##N##VSUF(T *ivars, size_t i, int cmp, VDECL)                \
{                                                                                              \
    T cur;                                                                                     \
    int ret;                                                                                   \
    oshmpi_fetch(OSHMPI_P2P_MT_##N, &cur, &ivars[i], shmem_world_rank);                        \
    T val = (VEXPR);                                                                           \
    COMP(cmp, cur, val, ret);                                                                  \
    return ret;                                                                                \
}                                                                                              \
                                                                                               \
int shmem_##N##_test_all##VSUF(T *ivars, size_t nelems, const int *status, int cmp, VDECL)     \
{                                                                                              \
    for (size_t i = 0; i < nelems; i++) {                                                      \
        if (oshmpi_p2p_excluded(status, i)) continue;                                          \
        if (!oshmpi_p2p_holds_##N##VSUF(ivars, i, cmp, VNAME)) return 0; \
    }                                                                                          \
    return 1;                                                                                  \
}                                                                                              \
                                                                                               \
size_t shmem_##N##_test_any##VSUF(T *ivars, size_t nelems, const int *status, int cmp, VDECL)  \
{                                                                                              \
    size_t start = oshmpi_p2p_start(nelems);                                                   \
    for (size_t k = 0; k < nelems; k++) {                                                      \
        size_t i = (start + k) % nelems;                                                       \
        if (oshmpi_p2p_excluded(status, i)) continue;                                          \
        if (oshmpi_p2p_holds_##N##VSUF(ivars, i, cmp, VNAME)) return i;                   \
    }                                                                                          \
    return SIZE_MAX;                                                                           \
}                                                                                              \
                                                                                               \
size_t shmem_##N##_test_some##VSUF(T *ivars, size_t nelems, size_t *indices, const int *status, \
                                   int cmp, VDECL)                                              \
{                                                                                              \
    size_t n = 0;                                                                              \
    for (size_t i = 0; i < nelems; i++) {                                                      \
        if (oshmpi_p2p_excluded(status, i)) continue;                                          \
        if (oshmpi_p2p_holds_##N##VSUF(ivars, i, cmp, VNAME)) indices[n++] = i;           \
    }                                                                                          \
    return n;                                                                                  \
}                                                                                              \
                                                                                               \
void shmem_##N##_wait_until_all##VSUF(T *ivars, size_t nelems, const int *status, int cmp, VDECL) \
{                                                                                              \
    for (size_t i = 0; i < nelems; i++) {                                                      \
        if (oshmpi_p2p_excluded(status, i)) continue;                                          \
        while (!oshmpi_p2p_holds_##N##VSUF(ivars, i, cmp, VNAME)) ;                       \
    }                                                                                          \
}                                                                                              \
                                                                                               \
size_t shmem_##N##_wait_until_any##VSUF(T *ivars, size_t nelems, const int *status, int cmp, VDECL) \
{                                                                                              \
    size_t included = 0;                                                                       \
    for (size_t i = 0; i < nelems; i++) if (!oshmpi_p2p_excluded(status, i)) included++;       \
    if (included == 0) return SIZE_MAX;                                                        \
    for (;;) {                                                                                 \
        size_t i = shmem_##N##_test_any##VSUF(ivars, nelems, status, cmp, VNAME);         \
        if (i != SIZE_MAX) return i;                                                           \
    }                                                                                          \
}                                                                                              \
                                                                                               \
size_t shmem_##N##_wait_until_some##VSUF(T *ivars, size_t nelems, size_t *indices,             \
                                         const int *status, int cmp, VDECL)                     \
{                                                                                              \
    size_t included = 0;                                                                       \
    for (size_t i = 0; i < nelems; i++) if (!oshmpi_p2p_excluded(status, i)) included++;       \
    if (included == 0) return 0;                                                               \
    for (;;) {                                                                                 \
        size_t n = shmem_##N##_test_some##VSUF(ivars, nelems, indices, status, cmp, VNAME); \
        if (n != 0) return n;                                                                  \
    }                                                                                          \
}

#define OSHMPI_DEFINE_SYNC15(N, T)                                                             \
    OSHMPI_DEFINE_SYNC15_FORM(N, T, ,         T cmp_value,         cmp_value,      cmp_value)  \
    OSHMPI_DEFINE_SYNC15_FORM(N, T, _vector,  const T *cmp_values, cmp_values,     cmp_values[i])

OSHMPI_SYNC_TYPES(OSHMPI_DEFINE_SYNC15)
