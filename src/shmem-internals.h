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
#include "dlmalloc.h"

typedef MPI_Aint shmem_offset_t;

/*****************************************************************/
/* TODO convert all the global status into a struct ala ARMCI-MPI */
/* requires TLS if MPI is thread-based */
MPI_Comm  SHMEM_COMM_WORLD;
MPI_Group SHMEM_GROUP_WORLD; /* used for creating logpe comms */

int       shmem_is_initialized;
int       shmem_is_finalized;
int       shmem_world_size, shmem_world_rank;
char      shmem_procname[MPI_MAX_PROCESSOR_NAME];

#ifdef ENABLE_SMP_OPTIMIZATIONS
MPI_Comm  SHMEM_COMM_NODE;
MPI_Group SHMEM_GROUP_NODE; /* may not be needed as global */
int       shmem_world_is_smp;
int       shmem_node_size, shmem_node_rank;
int *     shmem_smp_rank_list;
void **   shmem_smp_sheap_ptrs;
#endif

/* TODO probably want to make these 5 things into a struct typedef */
MPI_Win shmem_etext_win;
int     shmem_etext_size;
void *  shmem_etext_base_ptr;

MPI_Win shmem_sheap_win;
long    shmem_sheap_size;
void *  shmem_sheap_base_ptr;

/* dlmalloc mspace... */
mspace shmem_heap_mspace;

#ifdef ENABLE_MPMD_SUPPORT
int     shmem_running_mpmd;
int     shmem_mpmd_my_appnum;
MPI_Win shmem_mpmd_appnum_win;
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
int shmem_comm_cache_size;
shmem_comm_t * comm_cache;
#endif
/*****************************************************************/

enum shmem_window_id_e { SHMEM_SHEAP_WINDOW = 0, SHMEM_ETEXT_WINDOW = 1, SHMEM_INVALID_WINDOW = -1 };
enum shmem_coll_type_e { SHMEM_BARRIER = 0, SHMEM_BROADCAST = 1, SHMEM_ALLREDUCE = 2, SHMEM_FCOLLECT = 4, SHMEM_COLLECT = 8, SHMEM_ALLTOALL = 16 };

/*****************************************************************/

void oshmpi_warn(char * message);

void oshmpi_abort(int code, char * message);

/* This function is not used because we do not need this information 
 * int oshmpi_address_is_symmetric(size_t my_sheap_base_ptr);
 */

void oshmpi_initialize(int threading);

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

void oshmpi_create_comm(int pe_start, int log_pe_stride, int pe_size,
                        MPI_Comm * comm, MPI_Group * strided_group);

static inline void oshmpi_set_psync(int count, long value, long * pSync)
{
    for (int i=0; i<count; i++)
        pSync[i] = value;
}

void oshmpi_coll(enum shmem_coll_type_e coll, MPI_Datatype mpi_type, MPI_Op reduce_op,
                 void * target, const void * source, size_t len, 
                 int pe_root, int pe_start, int log_pe_stride, int pe_size);

#endif // SHMEM_INTERNALS_H
