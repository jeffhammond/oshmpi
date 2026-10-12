/* BSD-2 License.  Written by Jeff Hammond. */

#ifndef SHMEM_INTERNALS_H
#define SHMEM_INTERNALS_H

#include "shmemconf.h"

#include <mpi.h>
#if (MPI_VERSION < 2)
#  error It appears that you have been living under a rock.
#elif (MPI_VERSION < 3) 
#  if defined(MPICH2)
#    error Get the latest MPICH, MVAPICH2 or CrayMPI for MPI-3 support.
#  else
#    error You need MPI-3.  Try MPICH or one of its derivatives.
#  endif
#endif

#include "shmem.h"
#include "oshmpi-mcs-lock.h"
#include "compiler-utils.h"
#include "type_contiguous_x.h"

#define ONLY_MSPACES 1
#include <pthread.h>
#include "oshmpi-direct.h"
#include "dlmalloc.h"

typedef MPI_Aint shmem_offset_t;

/* A communication context.  'team' is the team the context was created for with
 * shmem_team_create_ctx; it is NULL for contexts of the world team, including those made by
 * shmem_ctx_create. */
struct oshmpi_team_s;
struct oshmpi_ctx_s {
    long options;
    struct oshmpi_team_s * team;
};

/*****************************************************************/
/* TODO convert all the global status into a struct ala ARMCI-MPI */
/* requires TLS if MPI is thread-based */
extern MPI_Comm  SHMEM_COMM_WORLD;
extern MPI_Group SHMEM_GROUP_WORLD; /* used for creating logpe comms */

extern int       shmem_is_initialized;
extern int       shmem_thread_provided;   /* MPI_THREAD_* level provided by MPI */
extern int       shmem_is_finalized;
extern int       shmem_world_size, shmem_world_rank;
extern char      shmem_procname[MPI_MAX_PROCESSOR_NAME];


/* TODO probably want to make these 5 things into a struct typedef */
extern MPI_Win shmem_etext_win;
extern long    shmem_etext_size;
extern void *  shmem_etext_base_ptr;

extern MPI_Win shmem_sheap_win;
extern long    shmem_sheap_size;
extern void *  shmem_sheap_base_ptr;

/* dlmalloc mspace... */
extern mspace shmem_heap_mspace;

#ifdef ENABLE_MPMD_SUPPORT
extern int     shmem_running_mpmd;
extern int     shmem_mpmd_my_appnum;
extern MPI_Win shmem_mpmd_appnum_win;
#endif

#if ENABLE_COMM_CACHING
typedef struct {
    int start;
    int logs;
    int size;
    MPI_Comm comm;
    /* TODO Eliminate the need for the group (used in translation of root). */
    MPI_Group group;
} shmem_comm_t;

/* Cache lookup is currently linear so this should not be large. */
/* Current use of array-based cache is horrible. 
 * TODO Use a linked list like a real programmer. */
extern int shmem_comm_cache_size;
extern shmem_comm_t * comm_cache;
#endif
/*****************************************************************/

enum shmem_window_id_e { SHMEM_SHEAP_WINDOW = 0, SHMEM_ETEXT_WINDOW = 1, SHMEM_INVALID_WINDOW = -1 };
enum shmem_coll_type_e { SHMEM_BARRIER = 0, SHMEM_BROADCAST = 1, SHMEM_ALLREDUCE = 2, SHMEM_FCOLLECT = 4, SHMEM_COLLECT = 8, SHMEM_ALLTOALL = 16, SHMEM_ALLTOALLS = 32 };

/*****************************************************************/

void oshmpi_warn(char * message);

void oshmpi_abort(int code, char * message);

/* This function is not used because we do not need this information 
 * int oshmpi_address_is_symmetric(size_t my_sheap_base_ptr);
 */

void oshmpi_initialize(int threading);
int  oshmpi_query_thread(void); /* returns the SHMEM_THREAD_* level provided */

void oshmpi_finalize(void);

void oshmpi_remote_sync(void);
void oshmpi_local_sync(void);

/* used internally only */
void oshmpi_remote_sync_pe(int);

/* return 0 on successful lookup, otherwise 1 */
int oshmpi_window_offset(const OSHMPI_VOLATILE void *address, const int pe,
                         enum shmem_window_id_e * win_id, shmem_offset_t * win_offset);

void oshmpi_put(MPI_Datatype mpi_type, void *target, const void *source, size_t len, int pe);
void oshmpi_get(MPI_Datatype mpi_type, void *target, const void *source, size_t len, int pe);
void oshmpi_put_nbi(MPI_Datatype mpi_type, void *target, const void *source, size_t len, int pe);
void oshmpi_get_nbi(MPI_Datatype mpi_type, void *target, const void *source, size_t len, int pe);
void oshmpi_put_strided(MPI_Datatype mpi_type, void *target, const void *source, 
                        ptrdiff_t target_ptrdiff, ptrdiff_t source_ptrdiff, size_t len, int pe);
void oshmpi_get_strided(MPI_Datatype mpi_type, void *target, const void *source, 
                        ptrdiff_t target_ptrdiff, ptrdiff_t source_ptrdiff, size_t len, int pe);

void oshmpi_swap(MPI_Datatype mpi_type, void *output, void *remote, const void *input, int pe);
void oshmpi_cswap(MPI_Datatype mpi_type, void *output, void *remote, const void *input, const void *compare, int pe);
void oshmpi_add(MPI_Datatype mpi_type, void *remote, const void *input, int pe);
void oshmpi_fadd(MPI_Datatype mpi_type, void *output, void *remote, const void *input, int pe);
void oshmpi_fetch(MPI_Datatype mpi_type, void *output, OSHMPI_CONST void *remote, int pe);
void oshmpi_set(MPI_Datatype mpi_type, void *remote, const void *input, int pe);
/* bitwise (and other MPI_Op) atomics */
void oshmpi_op(MPI_Datatype mpi_type, MPI_Op op, void *remote, const void *input, int pe);
void oshmpi_fetch_op(MPI_Datatype mpi_type, MPI_Op op, void *output, void *remote, const void *input, int pe);

void oshmpi_create_comm(int pe_start, int log_pe_stride, int pe_size,
                        MPI_Comm * comm, MPI_Group * strided_group);

/* Drive MPI progress from a polling loop.  A rank that spins on its own window memory
 * makes no other MPI call, and Open MPI's UCX one-sided component (and others) only
 * services incoming RMA/atomics from inside the progress engine, so the PE that is
 * supposed to wake the poller would never complete.  An MPI_Iprobe on a communicator
 * that carries no point-to-point traffic enters the progress engine and nothing else. */
static inline void oshmpi_progress(void)
{
    int flag;
    MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, SHMEM_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
}

/* Open MPI's osc/ucx hangs or crashes when several threads issue RMA on a window at once
 * (even though MPI_THREAD_MULTIPLE allows it), so in that mode every RMA, flush and sync
 * call is serialized.  Single-threaded runs take no lock. */
extern pthread_mutex_t oshmpi_rma_mutex;
#define OSHMPI_MPI_RMA(call)                                                         \
    do {                                                                             \
        int oshmpi_mt_ = (shmem_thread_provided == MPI_THREAD_MULTIPLE);             \
        if (oshmpi_mt_) pthread_mutex_lock(&oshmpi_rma_mutex);                       \
        call;                                                                        \
        if (oshmpi_mt_) pthread_mutex_unlock(&oshmpi_rma_mutex);                     \
    } while (0)

static inline void oshmpi_set_psync(int count, long value, long * pSync)
{
    for (int i=0; i<count; i++)
        pSync[i] = value;
}

void oshmpi_collect_bytes(MPI_Comm comm, void * target, const void * source, size_t bytes);
void oshmpi_coll(enum shmem_coll_type_e coll, MPI_Datatype mpi_type, MPI_Op reduce_op,
                 void * target, const void * source,
                 ptrdiff_t target_ptrdiff, ptrdiff_t source_ptrdiff,
                 size_t len,
                 int pe_root, int pe_start, int log_pe_stride, int pe_size);

#endif // SHMEM_INTERNALS_H
