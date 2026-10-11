/* BSD-2 License.  Written by Jeff Hammond. */

#include <strings.h>
#include "shmem-internals.h"

/* Definitions of the globals declared extern in shmem-internals.h. */
pthread_mutex_t oshmpi_rma_mutex = PTHREAD_MUTEX_INITIALIZER;
MPI_Comm  SHMEM_COMM_WORLD;
MPI_Group SHMEM_GROUP_WORLD;
int       shmem_is_initialized;
int       shmem_thread_provided;
int       shmem_is_finalized;
int       shmem_world_size, shmem_world_rank;
char      shmem_procname[MPI_MAX_PROCESSOR_NAME];
int       shmem_sheap_direct;
char **   shmem_sheap_peer_base;
MPI_Win shmem_etext_win;
int     shmem_etext_size;
void *  shmem_etext_base_ptr;
MPI_Win shmem_sheap_win;
long    shmem_sheap_size;
void *  shmem_sheap_base_ptr;
mspace shmem_heap_mspace;
#ifdef ENABLE_MPMD_SUPPORT
int     shmem_running_mpmd;
int     shmem_mpmd_my_appnum;
MPI_Win shmem_mpmd_appnum_win;
#endif
#if ENABLE_COMM_CACHING
int shmem_comm_cache_size;
shmem_comm_t * comm_cache;
#endif

/* this code deals with SHMEM communication out of symmetric but non-heap data */
#if defined(HAVE_APPLE_MAC)
    /* The legacy end(3) accessors (get_etext, get_end) are deprecated and do not describe
     * the data of the main executable on current macOS.  Use the runtime address and size
     * of the executable's __DATA segment instead; this is correct in the presence of ASLR.
     * https://developer.apple.com/library/mac//documentation/Darwin/Reference/ManPages/10.7/man3/end.3.html */
#include <mach-o/getsect.h>
#include <crt_externs.h>
    static unsigned long oshmpi_data_segment(unsigned long * size)
    {
        unsigned long sz = 0;
        const void * hdr = (const void *)_NSGetMachExecuteHeader();
#ifdef __LP64__
        unsigned long base = (unsigned long)getsegmentdata((const struct mach_header_64 *)hdr, "__DATA", &sz);
#else
        unsigned long base = (unsigned long)getsegmentdata((const struct mach_header *)hdr, "__DATA", &sz);
#endif
        *size = sz;
        return base;
    }
    /* getsect.h declares the deprecated get_etext et al; use our own names for the callers below. */
    #define get_etext oshmpi_apple_data_begin
    #define get_edata oshmpi_apple_data_end
    #define get_end   oshmpi_apple_data_end
    static unsigned long oshmpi_apple_data_begin(void) { unsigned long sz; return oshmpi_data_segment(&sz); }
    static unsigned long oshmpi_apple_data_end(void)   { unsigned long sz; unsigned long b = oshmpi_data_segment(&sz); return b + sz; }
#elif defined(HAVE_LINUX)
    #include <unistd.h>
    /* http://man7.org/linux/man-pages/man3/end.3.html */
    extern char data_start;
    extern char etext;
    extern char edata;
    extern char end;
    static unsigned long get_etext() { return (unsigned long)&data_start;   }
    static unsigned long get_end()   { return (unsigned long)&end;   }
    /* Static causes the compiler to warn that these are unused, which is correct. */
    //static unsigned long get_etext() { return (unsigned long)&etext; }
    //static unsigned long get_edata() { return (unsigned long)&edata; }
#elif defined(HAVE_AIX)
#warning AIX is completely untested.
    /* http://pic.dhe.ibm.com/infocenter/aix/v6r1/topic/com.ibm.aix.basetechref/doc/basetrf1/_end.htm */
    extern _end;
    extern _etext;
    extern _edata;
    static unsigned long get_end()   { return &_end;   }
    static unsigned long get_etext() { return &_etext; }
    static unsigned long get_edata() { return &_edata; }
#elif defined(HAVE_BSD)
#  error BSD is not supported yet.
#else
#  error Unknown and unsupported operating system.
#endif

/*****************************************************************/
/* TODO convert all the global status into a struct ala ARMCI-MPI */
/* requires TLS if MPI is thread-based */
extern MPI_Comm  SHMEM_COMM_WORLD;
extern MPI_Group SHMEM_GROUP_WORLD; /* used for creating logpe comms */

extern int       shmem_is_initialized;
extern int       shmem_thread_provided;
extern int       shmem_is_finalized;
extern int       shmem_world_size, shmem_world_rank;
extern char      shmem_procname[MPI_MAX_PROCESSOR_NAME];


/* TODO probably want to make these 5 things into a struct typedef */
extern MPI_Win shmem_etext_win;
extern int     shmem_etext_size;
extern void *  shmem_etext_base_ptr;

extern MPI_Win shmem_sheap_win;
extern long    shmem_sheap_size;
extern void *  shmem_sheap_base_ptr;

#ifdef ENABLE_MPMD_SUPPORT
extern int     shmem_running_mpmd;
extern int     shmem_mpmd_my_appnum;
extern MPI_Win shmem_mpmd_appnum_win;
#endif

/*****************************************************************/

/* Reduce overhead of MPI_Type_size in MPI-bypass Put/Get path.
 * There are other ways to solve this problem, e.g. by adding
 * an argument for the type-size to all the functions that need it. */

static inline int OSHMPI_Type_size(MPI_Datatype mpi_type)
{
    int type_size;
    MPI_Type_size(mpi_type, &type_size);
    return type_size;
}

/*****************************************************************/

/* Detect, at run time, whether every PE's symmetric heap is directly addressable here (see
 * oshmpi-direct.h).  Collective on SHMEM_COMM_WORLD: either every PE uses the direct path or
 * none does, so hardware atomics and MPI atomics never touch the same heap word.
 * OSHMPI_DIRECT=0 turns it off. */
void oshmpi_direct_init(MPI_Win win)
{
    const char * env = getenv("OSHMPI_DIRECT");
    int wanted = !(env != NULL && (!strcmp(env, "0") || !strcasecmp(env, "off") ||
                                   !strcasecmp(env, "no") || !strcasecmp(env, "false")));
    int ok = wanted;
    char ** base = NULL;

    if (ok) {
        base = malloc(shmem_world_size * sizeof(char *));
        assert(base != NULL);

        /* Before MPI 4.1, and where the memory is not shared, the query raises an error. */
        MPI_Errhandler old;
        MPI_Win_get_errhandler(win, &old);
        MPI_Win_set_errhandler(win, MPI_ERRORS_RETURN);
        for (int r = 0; r < shmem_world_size && ok; r++) {
            MPI_Aint size = 0;
            int disp = 0;
            void * ptr = NULL;
            int rc = MPI_Win_shared_query(win, r, &size, &disp, &ptr);
            if (rc != MPI_SUCCESS || ptr == NULL || size < (MPI_Aint) shmem_sheap_size) ok = 0;
            base[r] = (r == shmem_world_rank) ? (char *) shmem_sheap_base_ptr : (char *) ptr;
        }
        MPI_Win_set_errhandler(win, old);
        MPI_Errhandler_free(&old);
    }

    int all_ok;
    MPI_Allreduce(&ok, &all_ok, 1, MPI_INT, MPI_MIN, SHMEM_COMM_WORLD);

    if (all_ok) {
        shmem_sheap_peer_base = base;
        shmem_sheap_direct = 1;
    } else {
        free(base);
        shmem_sheap_peer_base = NULL;
        shmem_sheap_direct = 0;
    }
    if (shmem_world_rank == 0 && getenv("OSHMPI_VERBOSE") != NULL) {
        printf("OSHMPI direct access to the symmetric heap is %s\n",
               all_ok ? "enabled" : (wanted ? "not available" : "disabled by OSHMPI_DIRECT"));
    }
}

void oshmpi_direct_finalize(void)
{
    free(shmem_sheap_peer_base);
    shmem_sheap_peer_base = NULL;
    shmem_sheap_direct = 0;
}

/*****************************************************************/

void oshmpi_warn(char * message)
{
#if SHMEM_DEBUG > 0
    printf("[%d] %s \n", shmem_world_rank, message);
    fflush(stdout);
#endif
    return;
}

static int shmem_mpi_owner = 0;   /* 1 if OSHMPI called MPI_Init and so must call MPI_Finalize */

void oshmpi_abort(int code, char * message)
{
    if (message!=NULL) {
        printf("[%d] %s \n", shmem_world_rank, message);
        fflush(stdout);
    }
    /* abort the whole job: MPICH does not reliably reap peers when aborting a duplicated communicator */
    MPI_Abort(MPI_COMM_WORLD, code);
    return;
}

void oshmpi_initialize(int threading)
{
    {
        int flag;
        MPI_Initialized(&flag);

        int provided;
        if (!flag) {
            MPI_Init_thread(NULL, NULL, threading, &provided);
            shmem_mpi_owner = 1;
        } else {
            MPI_Query_thread(&provided);
        }

        /* The caller finds out what was provided with shmem_query_thread;
         * having less than requested is not fatal, as for MPI. */
        shmem_thread_provided = provided;
    }

    if (!shmem_is_initialized) {

        MPI_Comm_dup(MPI_COMM_WORLD, &SHMEM_COMM_WORLD);
        MPI_Comm_size(SHMEM_COMM_WORLD, &shmem_world_size);
        MPI_Comm_rank(SHMEM_COMM_WORLD, &shmem_world_rank);
        MPI_Comm_group(SHMEM_COMM_WORLD, &SHMEM_GROUP_WORLD);

        /* OpenSHMEM 1.4 environment variables SHMEM_VERSION and SHMEM_INFO */
        if (shmem_world_rank==0) {
            /* SMA_VERSION, SMA_INFO and SMA_DEBUG are deprecated in 1.4 but still accepted */
            if (getenv("SHMEM_VERSION") != NULL || getenv("SHMEM_INFO") != NULL ||
                getenv("SMA_VERSION") != NULL || getenv("SMA_INFO") != NULL) {
                printf("%s, OpenSHMEM %d.%d\n", SHMEM_VENDOR_STRING,
                       SHMEM_MAJOR_VERSION, SHMEM_MINOR_VERSION);
            }
            if (getenv("SHMEM_INFO") != NULL || getenv("SMA_INFO") != NULL) {
                printf("SHMEM_SYMMETRIC_SIZE  Size of the symmetric heap (bytes; K, M, G suffixes)\n"
                       "SHMEM_VERSION         Print the library version at start-up\n"
                       "SHMEM_INFO            Print this information at start-up\n"
                       "SHMEM_DEBUG           Accepted; no additional debugging output\n"
                       "SMA_*                 Deprecated aliases of the SHMEM_* variables above\n");
            }
            fflush(stdout);
        }

        {
            /* Check for MPMD usage. */
            void * pappnum = NULL;
            int appnum = 0;
            int is_set=0;
            MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_APPNUM, &pappnum, &is_set);
            if (is_set) {
                assert(pappnum!=NULL);
                appnum = *(int*)pappnum;
            }
#ifndef ENABLE_MPMD_SUPPORT
            /* If any rank detects MPMD, we abort.  No need to check collectively. */
            if (is_set && appnum) {
                oshmpi_abort(appnum, "You need to enable MPMD support in the build.");
            }
#else
            /* This may not be necessary but it is safer to check on all ranks. */
            MPI_Allreduce(MPI_IN_PLACE, &is_set, 1, MPI_INT, MPI_MAX, SHMEM_COMM_WORLD);
            if (is_set) {
                shmem_mpmd_my_appnum = appnum;

                /* Check for appnum homogeneity. */
                int appmin, appmax;
                MPI_Allreduce(&appnum, &appmin, 1, MPI_INT, MPI_MIN, SHMEM_COMM_WORLD);
                MPI_Allreduce(&appnum, &appmax, 1, MPI_INT, MPI_MAX, SHMEM_COMM_WORLD);
                shmem_running_mpmd = (appmin != appmax) ? 1 : 0;
            } else {
                shmem_running_mpmd = 0;
            }

            if (shmem_running_mpmd) {
                /* Never going to need direct access; in any case, base is a window attribute. */
                void * shmem_mpmd_appnum_base;
                MPI_Win_allocate((MPI_Aint)sizeof(int), sizeof(int) /* disp_unit */, MPI_INFO_NULL, SHMEM_COMM_WORLD,
                                 &shmem_mpmd_appnum_base, &shmem_mpmd_appnum_win);
                MPI_Win_lock_all(0, shmem_mpmd_appnum_win);

                /* Write my appnum into appropriate location in window. */
                OSHMPI_MPI_RMA(MPI_Accumulate(&shmem_mpmd_my_appnum, 1, MPI_INT, shmem_world_rank,
                               (MPI_Aint)0, 1, MPI_INT, MPI_REPLACE, shmem_mpmd_appnum_win));
                OSHMPI_MPI_RMA(MPI_Win_flush(shmem_world_rank, shmem_mpmd_appnum_win));
            }
#endif
        }

        shmem_sheap_size = -1;
        if (shmem_world_rank==0) {
            char * env_char = NULL;
            /* OpenSHMEM 1.4 environment variable */
            env_char = getenv("SHMEM_SYMMETRIC_SIZE");
            if (env_char==NULL) {
                /* This is the same as OpenMPI's OpenSHMEM:
                 * http://www.mellanox.com/related-docs/prod_software/Mellanox_ScalableSHMEM_User_Manual_v2.2.pdf */
                /* This is for Portals SHMEM (older version):
                 * http://portals-shmem.googlecode.com/svn-history/r159/trunk/src/symmetric_heap.c */
                env_char = getenv("SHMEM_SYMMETRIC_HEAP_SIZE");
            }
            if (env_char==NULL) {
                /* This is for SGI SHMEM:
                 * http://techpubs.sgi.com/library/tpl/cgi-bin/getdoc.cgi?coll=linux&db=man&fname=/usr/share/catman/man3/shmalloc.3.html */
                /* This is for OpenSHMEM on IBM PE:
                 * http://www-01.ibm.com/support/knowledgecenter/SSFK3V_1.3.0/com.ibm.cluster.protocols.v1r3.pp300.doc/bl511_envars.htm */
                /* This is for Portals SHMEM (older version?):
                 * https://github.com/jeffhammond/portals-shmem/blob/master/README */
                env_char = getenv("SMA_SYMMETRIC_SIZE");
            }
            if (env_char==NULL) {
                /* This is for Portals SHMEM:
                 * https://github.com/jeffhammond/portals-shmem/blob/master/src/init.c#L162 */
                env_char = getenv("SYMMETRIC_SIZE");
            }
            if (env_char==NULL) {
                /* This is for Cray SHMEM on X1:
                 * http://docs.cray.com/books/S-2179-52/html-S-2179-52/z1034699298pvl.html */
                env_char = getenv("X1_SYMMETRIC_HEAP_SIZE");
            }
            if (env_char==NULL) {
                /* This is for Cray SHMEM on XT/XE/XK/XC:
                 * http://docs.cray.com/books/S-2179-52/html-S-2179-52/z1034699298pvl.html */
                env_char = getenv("XT_SYMMETRIC_HEAP_SIZE");
            }
            if (env_char==NULL) {
                /* This is for MVAPICH2-X:
                 * http://mvapich.cse.ohio-state.edu/static/media/mvapich/mvapich2-x-2.1rc1-userguide.pdf */
                env_char = getenv("OOSHM_SYMMETRIC_HEAP_SIZE");
            }
            if (env_char!=NULL) {
                long units = 1L;
                if      ( NULL != strstr(env_char,"G") ) units = 1000000000L;
                else if ( NULL != strstr(env_char,"M") ) units = 1000000L;
                else if ( NULL != strstr(env_char,"K") ) units = 1000L;
                else                                     units = 1L;

                int num_count = strspn(env_char, "0123456789");
                memset( &env_char[num_count], ' ', strlen(env_char)-num_count);
                shmem_sheap_size = units * atol(env_char);
            }
        }
        /* There is a way to eliminate this extra broadcast, but Jeff is lazy. */
        MPI_Bcast( &shmem_sheap_size, 1, MPI_LONG, 0, SHMEM_COMM_WORLD );

        if (shmem_sheap_size == -1) {
#if defined(__linux__)
            int ppn;
            MPI_Comm commtemp;
            MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0 /* key */, MPI_INFO_NULL, &commtemp);
            MPI_Comm_size(commtemp, &ppn);
            MPI_Comm_free(&commtemp);
            ssize_t pagesize   = sysconf(_SC_PAGESIZE);
            ssize_t availpages = sysconf(_SC_AVPHYS_PAGES);
            if (pagesize<0 || availpages<0) {
                oshmpi_warn("sysconf failed\n");
                shmem_sheap_size = 128000000L;
            } else {
                size_t totalmem  = pagesize*availpages/ppn;
                /* If totalmem > 2GiB, assume it is incorrect.
                 * Let user set explicitly for such cases. */
                shmem_sheap_size = (totalmem < (1L<<31)) ? totalmem : (1L<<31);
            }
#else
            /* No joke, if one sets this to 120M to 128M, it segfaults on Mac. */
            shmem_sheap_size = 100000000L;
#endif
            MPI_Bcast( &shmem_sheap_size, 1, MPI_LONG, 0, SHMEM_COMM_WORLD );
        }
        if (shmem_world_rank==0) {
            printf("OSHMPI symmetric heap size is %ld\n",shmem_sheap_size);
        }

        MPI_Info sheap_info=MPI_INFO_NULL, etext_info=MPI_INFO_NULL;
        MPI_Info_create(&sheap_info);
        MPI_Info_create(&etext_info);

        /* We define the sheap size to be symmetric and assume it for the global static data. */
        MPI_Info_set(sheap_info, "same_size", "true");
        MPI_Info_set(etext_info, "same_size", "true");

#if 0 /* def ENABLE_RMA_ORDERING - this is not ready */
        /* ENABLE_RMA_ORDERING means "RMA operations are ordered"
         * i.e.  */
        /* Given the additional synchronization overhead required,
         * there is no discernible performance benefit to this. */
        MPI_Info_set(sheap_info, "accumulate_ordering", "");
        MPI_Info_set(etext_info, "accumulate_ordering", "");
#endif

        {
            MPI_Info_set(sheap_info, "alloc_shm", "true");
            int rc = MPI_Win_allocate((MPI_Aint)(shmem_sheap_size + 128*sizeof(size_t)), 1 /* disp_unit */, sheap_info,
                                      SHMEM_COMM_WORLD, &shmem_sheap_base_ptr, &shmem_sheap_win);
            if (rc!=MPI_SUCCESS) {
                char errmsg[MPI_MAX_ERROR_STRING];
                int errlen;
                MPI_Error_string(rc, errmsg, &errlen);
                printf("MPI_Win_allocate_shared error message = %s\n",errmsg);
                oshmpi_abort(rc, "MPI_Win_allocate_shared failed\n");
            }
        }
        MPI_Win_lock_all(MPI_MODE_NOCHECK /* use 0 instead if things break */, shmem_sheap_win);

        /* dlmalloc mspace constructor.
         * locked may not need to be 0 if SHMEM makes no multithreaded access... */
	/* Part (less than 128*sizeof(size_t) bytes) of this space is used for bookkeeping, 
	 * so the capacity must be at least this large */
	shmem_sheap_size += 128*sizeof(size_t);
        oshmpi_direct_init(shmem_sheap_win);
#if SHMEM_DEBUG > 5
        printf("[%d] shmem_sheap_base_ptr=%p\n", shmem_world_rank, shmem_sheap_base_ptr);
#endif
#if SHMEM_DEBUG > 1
        /* This forces a segfault here instead of inside of dlmalloc when Mac
         * is being crappy and not allocating shared memory properly. */
        memset(shmem_sheap_base_ptr,0,shmem_sheap_size);
#endif
        shmem_heap_mspace = create_mspace_with_base(shmem_sheap_base_ptr, shmem_sheap_size,
                                                    (shmem_thread_provided == MPI_THREAD_MULTIPLE) /* locked */);

	shmem_etext_base_ptr = (void*) get_etext();
        unsigned long long_etext_size   = get_end() - (unsigned long)shmem_etext_base_ptr;
        assert(long_etext_size<(unsigned long)INT32_MAX);
        shmem_etext_size = (int)long_etext_size;

#if defined(HAVE_APPLE_MAC) && SHMEM_DEBUG > 5
        printf("[%d] get_etext()       = %p \n", shmem_world_rank, (void*)get_etext() );
        printf("[%d] get_edata()       = %p \n", shmem_world_rank, (void*)get_edata() );
        printf("[%d] get_end()         = %p \n", shmem_world_rank, (void*)get_end()   );
        //printf("[%d] long_etext_size   = %lu \n", shmem_world_rank, long_etext_size );
        printf("[%d] shmem_etext_size  = %d  \n", shmem_world_rank, shmem_etext_size );
        //printf("[%d] my_etext_base_ptr = %p  \n", shmem_world_rank, my_etext_base_ptr );
        fflush(stdout);
#endif

#ifdef ABUSE_MPICH_FOR_GLOBALS
        MPI_Win_create_dynamic(etext_info, SHMEM_COMM_WORLD, &shmem_etext_win);
#else
        MPI_Win_create(shmem_etext_base_ptr, shmem_etext_size, 1 /* disp_unit */, etext_info, SHMEM_COMM_WORLD, 
                       &shmem_etext_win);
#endif
        MPI_Win_lock_all(0, shmem_etext_win);

        MPI_Info_free(&etext_info);
        MPI_Info_free(&sheap_info);

        /* It is hard if not impossible to implement SHMEM without the UNIFIED model. */
        {
            int   sheap_flag = 0;
            int * sheap_model = NULL;
            MPI_Win_get_attr(shmem_sheap_win, MPI_WIN_MODEL, &sheap_model, &sheap_flag);
            int   etext_flag = 0;
            int * etext_model = NULL;
            MPI_Win_get_attr(shmem_etext_win, MPI_WIN_MODEL, &etext_model, &etext_flag);
            /*
	    if (*sheap_model != MPI_WIN_UNIFIED || *etext_model != MPI_WIN_UNIFIED) {
                oshmpi_abort(1, "You cannot use this implementation of SHMEM without the UNIFIED model.\n");
            }
	    */
        }

#if ENABLE_COMM_CACHING
        shmem_comm_cache_size = 16;
        comm_cache = malloc(shmem_comm_cache_size * sizeof(shmem_comm_t) ); assert(comm_cache!=NULL);
        for (int i=0; i<shmem_comm_cache_size; i++) {
            comm_cache[i].start = -1;
            comm_cache[i].logs  = -1;
            comm_cache[i].size  = -1;
            comm_cache[i].comm  = MPI_COMM_NULL;
            comm_cache[i].group = MPI_GROUP_NULL;
        }
#endif

        MPI_Barrier(SHMEM_COMM_WORLD);

        shmem_is_initialized = 1;
    }
    return;
}

int oshmpi_query_thread(void)
{
    switch (shmem_thread_provided) {
        case MPI_THREAD_MULTIPLE:   return 3; /* SHMEM_THREAD_MULTIPLE   */
        case MPI_THREAD_SERIALIZED: return 2; /* SHMEM_THREAD_SERIALIZED */
        case MPI_THREAD_FUNNELED:   return 1; /* SHMEM_THREAD_FUNNELED   */
        default:                    return 0; /* SHMEM_THREAD_SINGLE     */
    }
}

void oshmpi_finalize(void)
{
    int flag;
    MPI_Finalized(&flag);

    if (!flag) {
        if (shmem_is_initialized && !shmem_is_finalized) {

#if ENABLE_COMM_CACHING
            for (int i=0; i<shmem_comm_cache_size; i++) {
                if (comm_cache[i].comm != MPI_COMM_NULL) {
                    MPI_Comm_free( &(comm_cache[i].comm) );
                    MPI_Group_free( &(comm_cache[i].group) );
                }
            }
            free(comm_cache);
#endif
            MPI_Barrier(SHMEM_COMM_WORLD);

#ifdef ENABLE_MPMD_SUPPORT
            if (shmem_running_mpmd) {
                MPI_Win_unlock_all(shmem_mpmd_appnum_win);
                MPI_Win_free(&shmem_mpmd_appnum_win);
            }
#endif
            MPI_Win_unlock_all(shmem_etext_win);
            MPI_Win_free(&shmem_etext_win);

            MPI_Win_unlock_all(shmem_sheap_win);
            MPI_Win_free(&shmem_sheap_win);

            oshmpi_direct_finalize();

            MPI_Group_free(&SHMEM_GROUP_WORLD);
            MPI_Comm_free(&SHMEM_COMM_WORLD);

            shmem_is_finalized = 1;
        }
        /* If the user initialized MPI, the user finalizes it. */
        if (shmem_mpi_owner)
            MPI_Finalize();
    }
    return;
}

/* quiet and fence are all about ordering.  
 * If put is already ordered, then these are no-ops.
 * fence only works on a single (implicit) remote PE, so
 * we track the last one that was targeted.
 * If any remote PE has been targeted, then quiet 
 * will flush all PEs. 
 */

void oshmpi_remote_sync(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST); /* direct stores to the heap */
    OSHMPI_MPI_RMA(MPI_Win_flush_all(shmem_sheap_win));
    OSHMPI_MPI_RMA(MPI_Win_flush_all(shmem_etext_win));
}

void oshmpi_remote_sync_pe(int pe)
{
    OSHMPI_MPI_RMA(MPI_Win_flush(pe, shmem_sheap_win));
    OSHMPI_MPI_RMA(MPI_Win_flush(pe, shmem_etext_win));
}

void oshmpi_local_sync(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    OSHMPI_MPI_RMA(MPI_Win_sync(shmem_sheap_win));
    OSHMPI_MPI_RMA(MPI_Win_sync(shmem_etext_win));
}

/* return 0 on successful lookup, otherwise 1 */
int oshmpi_window_offset(const OSHMPI_VOLATILE void *address, const int pe, /* IN  */
                         enum shmem_window_id_e * win_id,   /* OUT */
                         shmem_offset_t * win_offset)       /* OUT */
{
#if SHMEM_DEBUG>3
    printf("[%d] oshmpi_window_offset: address=%p, pe=%d \n", shmem_world_rank, address, pe);
    fflush(stdout);
#endif

#if SHMEM_DEBUG>5
    printf("[%d] shmem_etext_base_ptr=%p \n", shmem_world_rank, shmem_etext_base_ptr );
    printf("[%d] shmem_sheap_base_ptr=%p \n", shmem_world_rank, shmem_sheap_base_ptr );
    fflush(stdout);
#endif

    ptrdiff_t sheap_offset = (intptr_t)address - (intptr_t)shmem_sheap_base_ptr;
    ptrdiff_t etext_offset = (intptr_t)address - (intptr_t)shmem_etext_base_ptr;

    if (0 <= sheap_offset && sheap_offset <= shmem_sheap_size) {
        *win_offset = sheap_offset;
        *win_id     = SHMEM_SHEAP_WINDOW;
#if SHMEM_DEBUG>5
        printf("[%d] found address in sheap window \n", shmem_world_rank);
        printf("[%d] win_offset=%ld \n", shmem_world_rank, *win_offset);
#endif
        return 0;
    }
    else if (0 <= etext_offset && etext_offset <= shmem_etext_size) {
        *win_offset = etext_offset;
        *win_id     = SHMEM_ETEXT_WINDOW;
#if SHMEM_DEBUG>5
        printf("[%d] found address in etext window \n", shmem_world_rank);
        printf("[%d] win_offset=%ld \n", shmem_world_rank, *win_offset);
#endif
        return 0;
    }
    else {
        *win_offset  = (shmem_offset_t)NULL;
        *win_id      = SHMEM_INVALID_WINDOW;
#if SHMEM_DEBUG>5
        printf("[%d] did not find address in a valid window \n", shmem_world_rank);
#endif
        return 1;
    }
}

void oshmpi_put(MPI_Datatype mpi_type, void *target, const void *source, size_t len, int pe)
{
    if (len == 0) return; /* zero-length transfers are no-ops; pointers need not be valid */

    {
        void *dp = oshmpi_direct_ptr(target, pe);
        if (dp) { memcpy(dp, source, len * (size_t) oshmpi_type_size(mpi_type)); return; }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

#if SHMEM_DEBUG>3
    printf("[%d] oshmpi_put: type=%d, target=%p, source=%p, len=%zu, pe=%d \n",
            shmem_world_rank, mpi_type, target, source, len, pe);
    fflush(stdout);
#endif

    if (oshmpi_window_offset(target, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find put target");
    }

#if SHMEM_DEBUG>3
    printf("[%d] win_id=%d, offset=%lld \n", 
           shmem_world_rank, win_id, (long long)win_offset);
    fflush(stdout);
#endif

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    {
        int count = 0;
        MPI_Datatype tmp_type;
        if ( likely(len<(size_t)INT32_MAX) ) { /* need second check if size_t is signed */
            count = len;
            tmp_type = mpi_type;
        } else {
            count = 1;
            MPIX_Type_contiguous_x(len, mpi_type, &tmp_type);
            MPI_Type_commit(&tmp_type);
        }
#ifdef ENABLE_RMA_ORDERING
        /* ENABLE_RMA_ORDERING means "RMA operations are ordered" */
        OSHMPI_MPI_RMA(MPI_Accumulate(source, count, tmp_type,                   /* origin */
                       pe, (MPI_Aint)win_offset, count, tmp_type, /* target */
                       MPI_REPLACE,                               /* atomic, ordered Put */
                       win));
#else
        OSHMPI_MPI_RMA(MPI_Put(source, count, tmp_type,                   /* origin */
                pe, (MPI_Aint)win_offset, count, tmp_type, /* target */
                win));
#endif
        if ( unlikely(len>(size_t)INT32_MAX) ) {
            MPI_Type_free(&tmp_type);
        }
        OSHMPI_MPI_RMA(MPI_Win_flush_local(pe, win));
    }
    return;
}

void oshmpi_get(MPI_Datatype mpi_type, void *target, const void *source, size_t len, int pe)
{
    if (len == 0) return; /* zero-length transfers are no-ops; pointers need not be valid */

    {
        void *dp = oshmpi_direct_ptr(source, pe);
        if (dp) { memcpy(target, dp, len * (size_t) oshmpi_type_size(mpi_type)); __atomic_thread_fence(__ATOMIC_ACQUIRE); return; }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

#if SHMEM_DEBUG>3
    printf("[%d] oshmpi_get: type=%d, target=%p, source=%p, len=%zu, pe=%d \n", 
            shmem_world_rank, mpi_type, target, source, len, pe);
    fflush(stdout);
#endif

    if (oshmpi_window_offset(source, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find get source");
    }

#if SHMEM_DEBUG>3
    printf("[%d] win_id=%d, offset=%lld \n", 
           shmem_world_rank, win_id, (long long)win_offset);
    fflush(stdout);
#endif

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;
    {
        int count = 0;
        MPI_Datatype tmp_type;
        if ( likely(len<(size_t)INT32_MAX) ) { /* need second check if size_t is signed */
            count = len;
            tmp_type = mpi_type;
        } else {
            count = 1;
            MPIX_Type_contiguous_x(len, mpi_type, &tmp_type);
            MPI_Type_commit(&tmp_type);
        }
#ifdef ENABLE_RMA_ORDERING
        /* ENABLE_RMA_ORDERING means "RMA operations are ordered" */
        OSHMPI_MPI_RMA(MPI_Get_accumulate(NULL, 0, MPI_DATATYPE_NULL,                /* origin */
                           target, count, tmp_type,                   /* result */
                           pe, (MPI_Aint)win_offset, count, tmp_type, /* remote */
                           MPI_NO_OP,                                 /* atomic, ordered Get */
                           win));
#else
        OSHMPI_MPI_RMA(MPI_Get(target, count, tmp_type,                   /* result */
                pe, (MPI_Aint)win_offset, count, tmp_type, /* remote */
                win));
#endif
        if ( unlikely(len>(size_t)INT32_MAX) ) {
            MPI_Type_free(&tmp_type);
        }
        OSHMPI_MPI_RMA(MPI_Win_flush_local(pe, win));
    }
    return;
}

void oshmpi_put_nbi(MPI_Datatype mpi_type, void *target, const void *source, size_t len, int pe)
{
    if (len == 0) return; /* zero-length transfers are no-ops; pointers need not be valid */

    {
        void *dp = oshmpi_direct_ptr(target, pe);
        if (dp) { memcpy(dp, source, len * (size_t) oshmpi_type_size(mpi_type)); return; }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

#if SHMEM_DEBUG>3
    printf("[%d] oshmpi_put_nbi: type=%d, target=%p, source=%p, len=%zu, pe=%d \n",
            shmem_world_rank, mpi_type, target, source, len, pe);
    fflush(stdout);
#endif

    if (oshmpi_window_offset(target, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find put target");
    }

#if SHMEM_DEBUG>3
    printf("[%d] win_id=%d, offset=%lld \n",
           shmem_world_rank, win_id, (long long)win_offset);
    fflush(stdout);
#endif

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    {
        int count = 0;
        MPI_Datatype tmp_type;
        if ( likely(len<(size_t)INT32_MAX) ) { /* need second check if size_t is signed */
            count = len;
            tmp_type = mpi_type;
        } else {
            count = 1;
            MPIX_Type_contiguous_x(len, mpi_type, &tmp_type);
            MPI_Type_commit(&tmp_type);
        }
#ifdef ENABLE_RMA_ORDERING
        /* ENABLE_RMA_ORDERING means "RMA operations are ordered" */
        OSHMPI_MPI_RMA(MPI_Accumulate(source, count, tmp_type,                   /* origin */
                       pe, (MPI_Aint)win_offset, count, tmp_type, /* target */
                       MPI_REPLACE,                               /* atomic, ordered Put */
                       win));
#else
        OSHMPI_MPI_RMA(MPI_Put(source, count, tmp_type,                   /* origin */
                pe, (MPI_Aint)win_offset, count, tmp_type, /* target */
                win));
#endif
        if ( unlikely(len>(size_t)INT32_MAX) ) {
            MPI_Type_free(&tmp_type);
        }
    }
    return;
}

void oshmpi_get_nbi(MPI_Datatype mpi_type, void *target, const void *source, size_t len, int pe)
{
    if (len == 0) return; /* zero-length transfers are no-ops; pointers need not be valid */

    {
        void *dp = oshmpi_direct_ptr(source, pe);
        if (dp) { memcpy(target, dp, len * (size_t) oshmpi_type_size(mpi_type)); __atomic_thread_fence(__ATOMIC_ACQUIRE); return; }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

#if SHMEM_DEBUG>3
    printf("[%d] oshmpi_get_nbi: type=%d, target=%p, source=%p, len=%zu, pe=%d \n",
            shmem_world_rank, mpi_type, target, source, len, pe);
    fflush(stdout);
#endif

    if (oshmpi_window_offset(source, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find get source");
    }

#if SHMEM_DEBUG>3
    printf("[%d] win_id=%d, offset=%lld \n",
           shmem_world_rank, win_id, (long long)win_offset);
    fflush(stdout);
#endif

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;
    {
        int count = 0;
        MPI_Datatype tmp_type;
        if ( likely(len<(size_t)INT32_MAX) ) { /* need second check if size_t is signed */
            count = len;
            tmp_type = mpi_type;
        } else {
            count = 1;
            MPIX_Type_contiguous_x(len, mpi_type, &tmp_type);
            MPI_Type_commit(&tmp_type);
        }
#ifdef ENABLE_RMA_ORDERING
        /* ENABLE_RMA_ORDERING means "RMA operations are ordered" */
        OSHMPI_MPI_RMA(MPI_Get_accumulate(NULL, 0, MPI_DATATYPE_NULL,                /* origin */
                           target, count, tmp_type,                   /* result */
                           pe, (MPI_Aint)win_offset, count, tmp_type, /* remote */
                           MPI_NO_OP,                                 /* atomic, ordered Get */
                           win));
#else
        OSHMPI_MPI_RMA(MPI_Get(target, count, tmp_type,                   /* result */
                pe, (MPI_Aint)win_offset, count, tmp_type, /* remote */
                win));
#endif
        if ( unlikely(len>(size_t)INT32_MAX) ) {
            MPI_Type_free(&tmp_type);
        }
    }
    return;
}

void oshmpi_put_strided(MPI_Datatype mpi_type, void *target, const void *source, 
                         ptrdiff_t target_ptrdiff, ptrdiff_t source_ptrdiff, size_t len, int pe)
{
    if (len == 0) return; /* zero-length transfers are no-ops; pointers need not be valid */

#if SHMEM_DEBUG>3
    printf("[%d] oshmpi_put_strided: type=%d, target=%p, source=%p, len=%zu, pe=%d \n", 
                    shmem_world_rank, mpi_type, target, source, len, pe);
    fflush(stdout);
#endif

    int count = 0;
    if ( likely(len<(size_t)INT32_MAX) ) { /* need second check if size_t is signed */
        count = len;
    } else {
        /* TODO generate derived type ala BigMPI */
        oshmpi_abort(len%INT32_MAX, "oshmpi_put_strided: count exceeds the range of a 32b integer");
    }

    {
        void *dp = oshmpi_direct_ptr(target, pe);
        if (dp) { oshmpi_direct_strided(dp, target_ptrdiff, source, source_ptrdiff, len, (size_t) oshmpi_type_size(mpi_type)); return; }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(target, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find iput target");
    }
#if SHMEM_DEBUG>3
    printf("[%d] win_id=%d, offset=%lld \n", 
           shmem_world_rank, win_id, (long long)win_offset);
    fflush(stdout);
#endif

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;
    {
        assert( (ptrdiff_t)INT32_MIN<target_ptrdiff && target_ptrdiff<(ptrdiff_t)INT32_MAX );
        assert( (ptrdiff_t)INT32_MIN<source_ptrdiff && source_ptrdiff<(ptrdiff_t)INT32_MAX );

        int target_stride = (int) target_ptrdiff;
        int source_stride = (int) source_ptrdiff;

        MPI_Datatype source_type;
        MPI_Type_vector(count, 1, source_stride, mpi_type, &source_type);
        MPI_Type_commit(&source_type);

        MPI_Datatype target_type;
        if (target_stride!=source_stride) {
            MPI_Type_vector(count, 1, target_stride, mpi_type, &target_type);
            MPI_Type_commit(&target_type);
        } else {
            target_type = source_type;
        }

#ifdef ENABLE_RMA_ORDERING
        /* ENABLE_RMA_ORDERING means "RMA operations are ordered" */
        OSHMPI_MPI_RMA(MPI_Accumulate(source, 1, source_type,                   /* origin */
                       pe, (MPI_Aint)win_offset, 1, target_type, /* target */
                       MPI_REPLACE,                              /* atomic, ordered Put */
                       win));
#else
        OSHMPI_MPI_RMA(MPI_Put(source, 1, source_type,                   /* origin */
                pe, (MPI_Aint)win_offset, 1, target_type, /* target */
                win));
#endif
        OSHMPI_MPI_RMA(MPI_Win_flush_local(pe, win));

        if (target_stride!=source_stride) {
            MPI_Type_free(&target_type);
        }
        MPI_Type_free(&source_type);
    }

    return;
}

void oshmpi_get_strided(MPI_Datatype mpi_type, void *target, const void *source, 
                         ptrdiff_t target_ptrdiff, ptrdiff_t source_ptrdiff, size_t len, int pe)
{
    if (len == 0) return; /* zero-length transfers are no-ops; pointers need not be valid */

#if SHMEM_DEBUG>3
    printf("[%d] oshmpi_get_strided: type=%d, target=%p, source=%p, len=%zu, pe=%d \n", 
                    shmem_world_rank, mpi_type, target, source, len, pe);
    fflush(stdout);
#endif

    int count = 0;
    if ( likely(len<(size_t)INT32_MAX) ) { /* need second check if size_t is signed */
        count = len;
    } else {
        /* TODO generate derived type ala BigMPI */
        oshmpi_abort(len%INT32_MAX, "oshmpi_get_strided: count exceeds the range of a 32b integer");
    }

    {
        void *dp = oshmpi_direct_ptr(source, pe);
        if (dp) { oshmpi_direct_strided(target, target_ptrdiff, dp, source_ptrdiff, len, (size_t) oshmpi_type_size(mpi_type)); __atomic_thread_fence(__ATOMIC_ACQUIRE); return; }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(source, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find iget source");
    }
#if SHMEM_DEBUG>3
    printf("[%d] win_id=%d, offset=%lld \n", 
           shmem_world_rank, win_id, (long long)win_offset);
    fflush(stdout);
#endif

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;
    {
        assert( (ptrdiff_t)INT32_MIN<target_ptrdiff && target_ptrdiff<(ptrdiff_t)INT32_MAX );
        assert( (ptrdiff_t)INT32_MIN<source_ptrdiff && source_ptrdiff<(ptrdiff_t)INT32_MAX );

        int target_stride = (int) target_ptrdiff;
        int source_stride = (int) source_ptrdiff;

        MPI_Datatype source_type;
        MPI_Type_vector(count, 1, source_stride, mpi_type, &source_type);
        MPI_Type_commit(&source_type);

        MPI_Datatype target_type;
        if (target_stride!=source_stride) {
            MPI_Type_vector(count, 1, target_stride, mpi_type, &target_type);
            MPI_Type_commit(&target_type);
        } else {
            target_type = source_type;
        }

#ifdef ENABLE_RMA_ORDERING
        /* ENABLE_RMA_ORDERING means "RMA operations are ordered" */
        OSHMPI_MPI_RMA(MPI_Get_accumulate(NULL, 0, MPI_DATATYPE_NULL,                   /* origin */
                           target, 1, target_type,                   /* result */
                           pe, (MPI_Aint)win_offset, 1, source_type, /* remote */
                           MPI_NO_OP,                                    /* atomic, ordered Get */
                           win));
#else
        OSHMPI_MPI_RMA(MPI_Get(target, 1, target_type,                   /* result */
                pe, (MPI_Aint)win_offset, 1, source_type, /* remote */
                win));
#endif
        OSHMPI_MPI_RMA(MPI_Win_flush_local(pe, win));

        if (target_stride!=source_stride) 
            MPI_Type_free(&target_type);
        MPI_Type_free(&source_type);
    }

    return;
}

void oshmpi_set(MPI_Datatype mpi_type, void *remote, const void *input, int pe)
{
    {
        void *dp = oshmpi_direct_ptr(remote, pe);
        if (dp) {
            if (!oshmpi_direct_rmw(mpi_type, MPI_REPLACE, dp, input, NULL, NULL)) oshmpi_abort(pe, "unsupported atomic datatype");
            return;
        }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find set remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    {
        OSHMPI_MPI_RMA(MPI_Accumulate(input, 1, mpi_type, pe, win_offset, 1, mpi_type, MPI_REPLACE, win));
        OSHMPI_MPI_RMA(MPI_Win_flush(pe, win));
    }
    return;
}

void oshmpi_fetch(MPI_Datatype mpi_type, void *output, OSHMPI_CONST void *remote, int pe)
{
    {
        void *dp = oshmpi_direct_ptr(remote, pe);
        if (dp) {
            if (!oshmpi_direct_rmw(mpi_type, MPI_NO_OP, dp, NULL, NULL, output)) oshmpi_abort(pe, "unsupported atomic datatype");
            /* A fetch of our own memory is a poll.  The data segment still uses MPI, so a PE
             * spinning on a heap word must keep MPI progressing for the others' RMA to it. */
            if (pe == shmem_world_rank) oshmpi_progress();
            return;
        }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find fadd remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    /* A fetch of this PE's own memory is an atomic load.  Going through MPI instead makes a
     * PE that polls its own variable (shmem_wait_until, shmem_test, a spin on atomic_fetch)
     * take the window's per-target accumulate lock over and over, which starves a remote
     * PE trying to update that variable: Open MPI's osc/ucx serializes non-native
     * accumulates, REPLACE included, with a compare-and-swap on that lock.  The window is
     * in the UNIFIED model, so an acquire load of the aligned word sees remote updates. */
    if (pe == shmem_world_rank) {
        int size;
        MPI_Type_size(mpi_type, &size);
        if (size == 1 || size == 2 || size == 4 || size == 8) {
            switch (size) {
                case 1: { uint8_t  v = __atomic_load_n((const volatile uint8_t *)remote,  __ATOMIC_ACQUIRE); memcpy(output, &v, 1); break; }
                case 2: { uint16_t v = __atomic_load_n((const volatile uint16_t *)remote, __ATOMIC_ACQUIRE); memcpy(output, &v, 2); break; }
                case 4: { uint32_t v = __atomic_load_n((const volatile uint32_t *)remote, __ATOMIC_ACQUIRE); memcpy(output, &v, 4); break; }
                default:{ uint64_t v = __atomic_load_n((const volatile uint64_t *)remote, __ATOMIC_ACQUIRE); memcpy(output, &v, 8); break; }
            }
            /* Callers poll with this routine; keep MPI progressing so remote updates to
             * this PE's memory can complete (see oshmpi_progress). */
            oshmpi_progress();
            return;
        }
    }

    {
        OSHMPI_MPI_RMA(MPI_Fetch_and_op(NULL, output, mpi_type, pe, win_offset, MPI_NO_OP, win));
        OSHMPI_MPI_RMA(MPI_Win_flush(pe, win));
    }
    return;
}
void oshmpi_swap(MPI_Datatype mpi_type, void *output, void *remote, const void *input, int pe)
{
    {
        void *dp = oshmpi_direct_ptr(remote, pe);
        if (dp) {
            if (!oshmpi_direct_rmw(mpi_type, MPI_REPLACE, dp, input, NULL, output)) oshmpi_abort(pe, "unsupported atomic datatype");
            return;
        }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find swap remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    {
        OSHMPI_MPI_RMA(MPI_Fetch_and_op(input, output, mpi_type, pe, win_offset, MPI_REPLACE, win));
        OSHMPI_MPI_RMA(MPI_Win_flush(pe, win));
    }
    return;
}

void oshmpi_cswap(MPI_Datatype mpi_type, void *output, void *remote, const void *input, const void *compare, int pe)
{
    {
        void *dp = oshmpi_direct_ptr(remote, pe);
        if (dp) {
            if (!oshmpi_direct_rmw(mpi_type, MPI_REPLACE, dp, input, compare, output)) oshmpi_abort(pe, "unsupported atomic datatype");
            return;
        }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find cswap remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    {
        OSHMPI_MPI_RMA(MPI_Compare_and_swap(input, compare, output, mpi_type, pe, win_offset, win));
        OSHMPI_MPI_RMA(MPI_Win_flush(pe, win));
    }
    return;
}

void oshmpi_add(MPI_Datatype mpi_type, void *remote, const void *input, int pe)
{
    {
        void *dp = oshmpi_direct_ptr(remote, pe);
        if (dp) {
            if (!oshmpi_direct_rmw(mpi_type, MPI_SUM, dp, input, NULL, NULL)) oshmpi_abort(pe, "unsupported atomic datatype");
            return;
        }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find add remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    {
        OSHMPI_MPI_RMA(MPI_Accumulate(input, 1, mpi_type, pe, win_offset, 1, mpi_type, MPI_SUM, win));
        OSHMPI_MPI_RMA(MPI_Win_flush_local(pe, win));
    }
    return;
}

void oshmpi_op(MPI_Datatype mpi_type, MPI_Op op, void *remote, const void *input, int pe)
{
    {
        void *dp = oshmpi_direct_ptr(remote, pe);
        if (dp) {
            if (!oshmpi_direct_rmw(mpi_type, op, dp, input, NULL, NULL)) oshmpi_abort(pe, "unsupported atomic datatype");
            return;
        }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find atomic op remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    OSHMPI_MPI_RMA(MPI_Accumulate(input, 1, mpi_type, pe, win_offset, 1, mpi_type, op, win));
    OSHMPI_MPI_RMA(MPI_Win_flush_local(pe, win));
    return;
}
void oshmpi_fetch_op(MPI_Datatype mpi_type, MPI_Op op, void *output, void *remote, const void *input, int pe)
{
    {
        void *dp = oshmpi_direct_ptr(remote, pe);
        if (dp) {
            if (!oshmpi_direct_rmw(mpi_type, op, dp, input, NULL, output)) oshmpi_abort(pe, "unsupported atomic datatype");
            return;
        }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find fetch atomic op remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    OSHMPI_MPI_RMA(MPI_Fetch_and_op(input, output, mpi_type, pe, win_offset, op, win));
    OSHMPI_MPI_RMA(MPI_Win_flush(pe, win));
    return;
}
void oshmpi_fadd(MPI_Datatype mpi_type, void *output, void *remote, const void *input, int pe)
{
    {
        void *dp = oshmpi_direct_ptr(remote, pe);
        if (dp) {
            if (!oshmpi_direct_rmw(mpi_type, MPI_SUM, dp, input, NULL, output)) oshmpi_abort(pe, "unsupported atomic datatype");
            return;
        }
    }
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find fadd remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

    {
        OSHMPI_MPI_RMA(MPI_Fetch_and_op(input, output, mpi_type, pe, win_offset, MPI_SUM, win));
        OSHMPI_MPI_RMA(MPI_Win_flush(pe, win));
    }
    return;
}

static inline int oshmpi_translate_root(MPI_Group strided_group, int pe_root)
{
#if SHMEM_DEBUG > 4
    printf("[%d] oshmpi_translate_root(..,%d) \n", shmem_world_rank, pe_root);
#endif
    /* Broadcasts require us to translate the root from the world reference frame
     * to the strided subcommunicator frame. */
    {
        /* TODO
         * It should be possible to sidestep the generic translation for the 
         * special cases allowed by SHMEM. */
        int world_ranks[1] = { pe_root };
        int strided_ranks[1];
        MPI_Group_translate_ranks(SHMEM_GROUP_WORLD, 1 /* count */, world_ranks, 
                                  strided_group, strided_ranks);
        return strided_ranks[0];
    }
}

/* TODO 
 * One might assume that the same subcomms are used more than once and thus caching these is prudent.
 */
static inline void oshmpi_acquire_comm(int pe_start, int pe_logs, int pe_size, /* IN  */ 
                                        MPI_Comm * comm,                        /* OUT */
                                        int pe_root,                            /* IN  */
                                        int * broot)                            /* OUT */
{
    /* fastpath for world */
    if (pe_start==0 && pe_logs==0 && pe_size==shmem_world_size) {
        *comm  = SHMEM_COMM_WORLD;
        *broot = pe_root;
        return;
    }

#if ENABLE_COMM_CACHING
    for (int i=0; i<shmem_comm_cache_size; i++)
    {
        if (pe_start == comm_cache[i].start &&
            pe_logs  == comm_cache[i].logs  &&
            pe_size  == comm_cache[i].size  ) 
        {
            *comm  = comm_cache[i].comm;
            /* PE_root is an ordinal within the active set, which is the rank in the new communicator */
            if (pe_root>=0) {
                *broot = pe_root;
            }
            return;
        }
    }
#endif
    {
        MPI_Group strided_group;

        /* List of processes in the group that will be created. */
        int * pe_list = malloc(pe_size*sizeof(int)); assert(pe_list!=NULL);

        /* Implement 2^pe_logs with bitshift. */
        int pe_stride = 1<<pe_logs;
        for (int i=0; i<pe_size; i++)
            pe_list[i] = pe_start + i*pe_stride;

        MPI_Group_incl(SHMEM_GROUP_WORLD, pe_size, pe_list, &strided_group);
        /* Unlike the MPI-2 variant (MPI_Comm_create), this is only collective on the group. */
        /* We use pe_start as the tag because that should sufficiently disambiguate 
         * simultaneous calls to this function on disjoint groups. */
        MPI_Comm_create_group(SHMEM_COMM_WORLD, strided_group, pe_start /* tag */, comm); 

        if (pe_root>=0) {
            *broot = pe_root;
        }

#if ENABLE_COMM_CACHING
        for (int i=0; i<shmem_comm_cache_size; i++) {
            if (comm_cache[i].comm == MPI_COMM_NULL ) {
                comm_cache[i].start = pe_start;
                comm_cache[i].logs  = pe_logs;
                comm_cache[i].size  = pe_size;
                comm_cache[i].comm  = *comm;
                comm_cache[i].group = strided_group;
                free(pe_list);
                return;
            }
        }
#endif
        /* This point is reached only if caching fails so free the group here. */
        MPI_Group_free(&strided_group);

        free(pe_list);
    }
    return;
}

static inline void oshmpi_release_comm(int pe_start, int pe_logs, int pe_size, /* IN  */ 
                                        MPI_Comm * comm)                        /* OUT */
{
    if (pe_start==0 && pe_logs==0 && pe_size==shmem_world_size) {
        return;
    }

#if ENABLE_COMM_CACHING
    for (int i=0; i<shmem_comm_cache_size; i++) {
        if (comm_cache[i].comm == *comm ) {
            /* If our comm is cached, do nothing. */
            return;
        }
    }
#endif
    {
        MPI_Comm_free(comm);
    }
    return;
}

void oshmpi_coll(enum shmem_coll_type_e coll, MPI_Datatype mpi_type, MPI_Op reduce_op,
                  void * target, const void * source,
                  ptrdiff_t target_ptrdiff, ptrdiff_t source_ptrdiff, /* <--- ALLTOALLS only */
                  size_t len,
                  int pe_root, int pe_start, int pe_logs, int pe_size)
{
    int broot = 0;
    MPI_Comm comm;

    oshmpi_acquire_comm(pe_start, pe_logs, pe_size, &comm,
                         pe_root, &broot);

    int count = 0;
    MPI_Datatype tmp_type;
    if ( likely(len<(size_t)INT32_MAX) ) {
        count = len;
        tmp_type = mpi_type;
    } else {
        count = 1;
        MPIX_Type_contiguous_x(len, mpi_type, &tmp_type);
        MPI_Type_commit(&tmp_type);
    }

    switch (coll) {
        case SHMEM_BARRIER:
            {
                MPI_Barrier( comm );
            }
            break;
        case SHMEM_BROADCAST:
            {
                /* For bcast, MPI uses one buffer but SHMEM uses two. */
                /* From the OpenSHMEM 1.0 specification:
                 * "The data is not copied to the target address on the PE specified by PE_root." */
                /* pe_root is relative to the active set; the root's world rank is: */
                MPI_Bcast(shmem_world_rank==(pe_start + pe_root*(1<<pe_logs)) ? (void*) source : target,
                         count, tmp_type, broot, comm);
	    }
            break;
        case SHMEM_FCOLLECT:
            {
                MPI_Allgather(source, count, tmp_type, target, count, tmp_type, comm);
            }
            break;
        case SHMEM_COLLECT:
            {
                int * rcounts = malloc(pe_size*sizeof(int)); assert(rcounts!=NULL);
                int * rdispls = malloc(pe_size*sizeof(int)); assert(rdispls!=NULL);
                MPI_Allgather(&count, 1, MPI_INT, rcounts, 1, MPI_INT, comm);
                rdispls[0] = 0;
                for (int i=1; i<pe_size; i++) {
                    rdispls[i] = rdispls[i-1] + rcounts[i-1];
                }
                MPI_Allgatherv(source, count, tmp_type, target, rcounts, rdispls, tmp_type, comm);
                free(rdispls);
                free(rcounts);
            }
            break;
        case SHMEM_ALLTOALL:
            {
                MPI_Alltoall(source, count, tmp_type, target, count, tmp_type, comm);
            }
            break;
        case SHMEM_ALLTOALLS:
            {
                assert( (ptrdiff_t)INT32_MIN<target_ptrdiff && target_ptrdiff<(ptrdiff_t)INT32_MAX );
                assert( (ptrdiff_t)INT32_MIN<source_ptrdiff && source_ptrdiff<(ptrdiff_t)INT32_MAX );

                int target_stride = (int) target_ptrdiff;
                int source_stride = (int) source_ptrdiff;

                /* PE k's block for PE l is the nelems elements starting at element
                 * l*nelems*sst of the source (and lands at k*nelems*dst in the target):
                 * one strided block, whose extent must span nelems*stride elements
                 * so that consecutive blocks are adjacent in the array. */
                MPI_Aint lb, elem_extent;
                MPI_Type_get_extent(mpi_type, &lb, &elem_extent);

                MPI_Datatype source_type, target_type, tmp;
                MPI_Type_vector(count, 1, source_stride, mpi_type, &tmp);
                MPI_Type_create_resized(tmp, 0, (MPI_Aint)count * source_stride * elem_extent, &source_type);
                MPI_Type_commit(&source_type);
                MPI_Type_free(&tmp);

                MPI_Type_vector(count, 1, target_stride, mpi_type, &tmp);
                MPI_Type_create_resized(tmp, 0, (MPI_Aint)count * target_stride * elem_extent, &target_type);
                MPI_Type_commit(&target_type);
                MPI_Type_free(&tmp);

                MPI_Alltoall(source, 1, source_type, target, 1, target_type, comm);

                MPI_Type_free(&target_type);
                MPI_Type_free(&source_type);
            }
            break;
        case SHMEM_ALLREDUCE:
            {
                /* From the OpenSHMEM 1.0 specification:
                "[The] source and target may be the same array, but they must not be overlapping arrays." */
                MPI_Allreduce((source==target) ? MPI_IN_PLACE : source, target, count, tmp_type, reduce_op, comm);
            }
            break;
        default:
            oshmpi_abort(coll, "Unsupported collective type.");
            break;
    }

    if ( unlikely(len>(size_t)INT32_MAX) ) {
        MPI_Type_free(&tmp_type);
    }

    oshmpi_release_comm(pe_start, pe_logs, pe_size, &comm);

    return;
}
