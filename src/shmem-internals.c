/* BSD-2 License.  Written by Jeff Hammond. */

#include "shmem-internals.h"

/* this code deals with SHMEM communication out of symmetric but non-heap data */
#if defined(HAVE_APPLE_MAC)
#warning Global data support is not working yet on Apple.
    /* https://developer.apple.com/library/mac//documentation/Darwin/Reference/ManPages/10.7/man3/end.3.html */
#include <mach-o/getsect.h>
    unsigned long get_end();
    unsigned long get_etext();
    unsigned long get_edata();
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
extern int       shmem_is_finalized;
extern int       shmem_world_size, shmem_world_rank;
extern char      shmem_procname[MPI_MAX_PROCESSOR_NAME];

#ifdef ENABLE_SMP_OPTIMIZATIONS
extern MPI_Comm  SHMEM_COMM_NODE;
extern MPI_Group SHMEM_GROUP_NODE; /* may not be needed as global */
extern int       shmem_world_is_smp;
extern int       shmem_node_size, shmem_node_rank;
extern int *     shmem_smp_rank_list;
extern void **   shmem_smp_sheap_ptrs;
#endif

/* TODO probably want to make these 5 things into a struct typedef */
extern MPI_Win shmem_etext_win;
extern int     shmem_etext_size;
extern void *  shmem_etext_base_ptr;

extern MPI_Win shmem_sheap_win;
extern long    shmem_sheap_size;
extern void *  shmem_sheap_base_ptr;
/* dlmalloc mspace... */
extern mspace shmem_heap_mspace;

#ifdef EXTENSION_HBW_ALLOCATOR
extern MPI_Win shmem_sheapfast_win;
extern long    shmem_sheapfast_size;
extern void *  shmem_sheapfast_base_ptr;
/* dlmalloc mspace... */
extern mspace shmem_heapfast_mspace;
#endif

#ifdef ENABLE_MPMD_SUPPORT
extern int     shmem_running_mpmd;
extern int     shmem_mpmd_my_appnum;
extern MPI_Win shmem_mpmd_appnum_win;
#endif

/*****************************************************************/

/* Reduce overhead of MPI_Type_size in MPI-bypass Put/Get path.
 * There are other ways to solve this problem, e.g. by adding
 * an argument for the type-size to all the functions that need it. */

static inline int OSHMPI_Type_size(int mpi_type)
{
#ifdef MPICH
    return (((mpi_type)&0x0000ff00)>>8);
#else
    int type_size;
    MPI_Type_size(mpi_type, &type_size);
    return type_size;
#endif
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

void oshmpi_abort(int code, char * message)
{
    if (message!=NULL) {
        printf("[%d] %s \n", shmem_world_rank, message);
        fflush(stdout);
    }
    MPI_Abort(SHMEM_COMM_WORLD, code);
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
        } else {
            MPI_Query_thread(&provided);
        }

        if (threading<provided)
            oshmpi_abort(provided, "Your MPI implementation did not provide the requested thread support.");
    }

    if (!shmem_is_initialized) {

        MPI_Comm_dup(MPI_COMM_WORLD, &SHMEM_COMM_WORLD);
        MPI_Comm_size(SHMEM_COMM_WORLD, &shmem_world_size);
        MPI_Comm_rank(SHMEM_COMM_WORLD, &shmem_world_rank);
        MPI_Comm_group(SHMEM_COMM_WORLD, &SHMEM_GROUP_WORLD);

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
                MPI_Accumulate(&shmem_mpmd_my_appnum, 1, MPI_INT, shmem_world_rank,
                               (MPI_Aint)0, 1, MPI_INT, MPI_REPLACE, shmem_mpmd_appnum_win);
                MPI_Win_flush(shmem_world_rank, shmem_mpmd_appnum_win);
            }
#endif
        }

        shmem_sheap_size = -1;
        if (shmem_world_rank==0) {
            char * env_char = NULL;
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

#ifdef ENABLE_SMP_OPTIMIZATIONS
        {
            MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0 /* key */, MPI_INFO_NULL, &SHMEM_COMM_NODE);
            MPI_Comm_size(SHMEM_COMM_NODE, &shmem_node_size);
            MPI_Comm_rank(SHMEM_COMM_NODE, &shmem_node_rank);
            MPI_Comm_group(SHMEM_COMM_NODE, &SHMEM_GROUP_NODE);

            int result;
            MPI_Comm_compare(SHMEM_COMM_WORLD, SHMEM_COMM_NODE, &result);
            shmem_world_is_smp = (result==MPI_IDENT || result==MPI_CONGRUENT) ? 1 : 0;

            shmem_smp_rank_list  = (int*) malloc( shmem_node_size*sizeof(int) );
            int * temp_rank_list = (int*) malloc( shmem_node_size*sizeof(int) );
            for (int i=0; i<shmem_node_size; i++) {
                temp_rank_list[i] = i;
            }
            /* translate ranks in the node group to world ranks */
            MPI_Group_translate_ranks(SHMEM_GROUP_NODE,  shmem_node_size, temp_rank_list,
                                      SHMEM_GROUP_WORLD, shmem_smp_rank_list);
            free(temp_rank_list);
        }

        if (shmem_world_is_smp) {
            /* There is no performance advantage associated with a contiguous layout of shared memory. */
            MPI_Info_set(sheap_info, "alloc_shared_noncontig", "true");

            int rc = MPI_Win_allocate_shared((MPI_Aint)shmem_sheap_size, 1 /* disp_unit */, sheap_info,
                                             SHMEM_COMM_WORLD, &shmem_sheap_base_ptr, &shmem_sheap_win);
            if (rc!=MPI_SUCCESS) {
                char errmsg[MPI_MAX_ERROR_STRING];
                int errlen;
                MPI_Error_string(rc, errmsg, &errlen);
                printf("MPI_Win_allocate_shared error message = %s\n",errmsg);
                oshmpi_abort(rc, "MPI_Win_allocate_shared failed\n");
            }
            shmem_smp_sheap_ptrs = malloc( shmem_node_size * sizeof(void*) ); assert(shmem_smp_sheap_ptrs!=NULL);
            for (int rank=0; rank<shmem_node_size; rank++) {
                MPI_Aint size; /* unused */
                int      disp; /* unused */
                MPI_Win_shared_query(shmem_sheap_win, rank, &size, &disp, &shmem_smp_sheap_ptrs[rank]);
            }
        } else 
#endif
        {
            MPI_Info_set(sheap_info, "alloc_shm", "true");
            int rc = MPI_Win_allocate((MPI_Aint)shmem_sheap_size, 1 /* disp_unit */, sheap_info,
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
#if SHMEM_DEBUG > 5
        printf("[%d] shmem_sheap_base_ptr=%p\n", shmem_world_rank, shmem_sheap_base_ptr);
#endif
#if SHMEM_DEBUG > 1
        /* This forces a segfault here instead of inside of dlmalloc when Mac
         * is being crappy and not allocating shared memory properly. */
        memset(shmem_sheap_base_ptr,0,shmem_sheap_size);
#endif
        shmem_heap_mspace = create_mspace_with_base(shmem_sheap_base_ptr, shmem_sheap_size, 0 /* locked */);
        if (shmem_heap_mspace==NULL) oshmpi_abort(shmem_world_rank,"create_mspace_with_base failed");

#ifdef EXTENSION_HBW_ALLOCATOR
        {
            if (hbw_check_available()) {
                char * env_char = getenv("OSHMPI_KNL_FAST_HEAP_SIZE");
                shmem_sheapfast_size = (env_char!=NULL) ? atol(env_char) : 128000000L;
            } else {
                oshmpi_warn("No hbw available (will not be used)");
                shmem_sheapfast_size = 0;
            }

            if (shmem_sheapfast_size>0) {

                /* This is not doing to do what we want because it is not going to use shared memory.
                 * For single-node usage, we need to use the JEMalloc interface directly to allocate
                 * shared-memory in fastmem. */
                int rc = hbw_posix_memalign(&shmem_sheapfast_base_ptr, 64, shmem_sheapfast_size);
                if (rc!=0) oshmpi_abort(rc,"hbw_posix_memalign failed to allocate memory");

                shmem_heap_mspace = create_mspace_with_base(shmem_sheapfast_base_ptr, shmem_sheapfast_size, 0 /* locked */);
                if (shmem_heap_mspace==NULL) oshmpi_abort(shmem_world_rank,"create_mspace_with_base (fastmem) failed");

                MPI_Info sheapfast_info=MPI_INFO_NULL;
                MPI_Info_create(&sheapfast_info);
                MPI_Info_set(sheapfast_info, "same_size", "true");

                MPI_Win_create(shmem_sheapfast_base_ptr, shmem_sheapfast_size, 1 /* disp_unit */,
                               sheapfast_info, SHMEM_COMM_WORLD, &shmem_sheapfast_win);

                MPI_Win_lock_all(MPI_MODE_NOCHECK /* use 0 instead if things break */, shmem_sheapfast_win);

                MPI_Info_free(&sheapfast_info);
            }
        }
#endif

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
        MPI_Win_create(shmem_etext_base_ptr, shmem_etext_size, 1 /* disp_unit */,
                       etext_info, SHMEM_COMM_WORLD, &shmem_etext_win);
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

        /* allocate lock */
	oshmpi_allock (SHMEM_COMM_WORLD);

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

void oshmpi_finalize(void)
{
    int flag;
    MPI_Finalized(&flag);

    if (!flag) {
        if (shmem_is_initialized && !shmem_is_finalized) {

       	/* clear locking window */
	oshmpi_deallock ();
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

            /* Must free the sheap mspace BEFORE freeing the window,
             * because it sits on top of the window memory. */
            size_t heap_bytes_freed = destroy_mspace(shmem_heap_mspace);

            MPI_Win_unlock_all(shmem_sheap_win);
            MPI_Win_free(&shmem_sheap_win);

#ifdef EXTENSION_HBW_ALLOCATOR
            if (shmem_sheapfast_size>0) {

                MPI_Win_unlock_all(shmem_sheapfast_win);
                MPI_Win_free(&shmem_sheapfast_win);

                /* Must free the sheap mspace AFTER freeing the window,
                 * because it windows sits on top of that memory. */
                size_t heapfast_bytes_freed = destroy_mspace(shmem_heapfast_mspace);
            }
#endif

#ifdef ENABLE_SMP_OPTIMIZATIONS
            if (shmem_world_is_smp)
                free(shmem_smp_sheap_ptrs);
            free(shmem_smp_rank_list);
            MPI_Group_free(&SHMEM_GROUP_NODE);
            MPI_Comm_free(&SHMEM_COMM_NODE);
#endif

            MPI_Group_free(&SHMEM_GROUP_WORLD);
            MPI_Comm_free(&SHMEM_COMM_WORLD);

            shmem_is_finalized = 1;
        }
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
#ifdef EXTENSION_HBW_ALLOCATOR
    MPI_Win_flush_all(shmem_sheapfast_win);
#endif
    MPI_Win_flush_all(shmem_sheap_win);
    MPI_Win_flush_all(shmem_etext_win);
}

void oshmpi_remote_sync_pe(int pe)
{
#ifdef EXTENSION_HBW_ALLOCATOR
    MPI_Win_flush(pe, shmem_sheapfast_win);
#endif
    MPI_Win_flush(pe, shmem_sheap_win);
    MPI_Win_flush(pe, shmem_etext_win);
}

void oshmpi_local_sync(void)
{
#ifdef ENABLE_SMP_OPTIMIZATIONS
    __sync_synchronize();
#endif
#ifdef EXTENSION_HBW_ALLOCATOR
    MPI_Win_sync(shmem_sheapfast_win);
#endif
    MPI_Win_sync(shmem_sheap_win);
    MPI_Win_sync(shmem_etext_win);
}

/* return 0 on successful lookup, otherwise 1 */
int oshmpi_window_offset(const void *address, const int pe, /* IN  */
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
#ifdef EXTENSION_HBW_ALLOCATOR
    printf("[%d] shmem_sheapfast_base_ptr=%p \n", shmem_world_rank, shmem_sheapfast_base_ptr );
#endif
    fflush(stdout);
#endif

#ifdef EXTENSION_HBW_ALLOCATOR
    ptrdiff_t sheapfast_offset = (intptr_t)address - (intptr_t)shmem_sheapfast_base_ptr;
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
#ifdef EXTENSION_HBW_ALLOCATOR
    else if (0 <= sheapfast_offset && sheapfast_offset <= shmem_sheapfast_size) {
        *win_offset = sheapfast_offset;
        *win_id     = SHMEM_SHEAPFAST_WINDOW;
#if SHMEM_DEBUG>5
        printf("[%d] found address in sheapfast window \n", shmem_world_rank);
        printf("[%d] win_offset=%ld \n", shmem_world_rank, *win_offset);
#endif
        return 0;
    }
#endif
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

#ifdef ENABLE_SMP_OPTIMIZATIONS
    if (shmem_world_is_smp && win_id==SHMEM_SHEAP_WINDOW) {
        int type_size = OSHMPI_Type_size(mpi_type);
        void * ptr = (void*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)target - (intptr_t)shmem_sheap_base_ptr) );
        memcpy(ptr, source, len*type_size);
    } else
#endif
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
        MPI_Accumulate(source, count, tmp_type,                   /* origin */
                       pe, (MPI_Aint)win_offset, count, tmp_type, /* target */
                       MPI_REPLACE,                               /* atomic, ordered Put */
                       win);
#else
        MPI_Put(source, count, tmp_type,                   /* origin */
                pe, (MPI_Aint)win_offset, count, tmp_type, /* target */
                win);
#endif
        if ( unlikely(len>(size_t)INT32_MAX) ) {
            MPI_Type_free(&tmp_type);
        }
        MPI_Win_flush_local(pe, win);
    }
    return;
}

void oshmpi_get(MPI_Datatype mpi_type, void *target, const void *source, size_t len, int pe)
{
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
#ifdef ENABLE_SMP_OPTIMIZATIONS
    if (shmem_world_is_smp && win_id==SHMEM_SHEAP_WINDOW) {
        int type_size = OSHMPI_Type_size(mpi_type);
        void * ptr = (void*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)source - (intptr_t)shmem_sheap_base_ptr) );
        memcpy(target, ptr, len*type_size);
    } else 
#endif
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
        MPI_Get_accumulate(NULL, 0, MPI_DATATYPE_NULL,                /* origin */
                           target, count, tmp_type,                   /* result */
                           pe, (MPI_Aint)win_offset, count, tmp_type, /* remote */
                           MPI_NO_OP,                                 /* atomic, ordered Get */
                           win);
#else
        MPI_Get(target, count, tmp_type,                   /* result */
                pe, (MPI_Aint)win_offset, count, tmp_type, /* remote */
                win);
#endif
        if ( unlikely(len>(size_t)INT32_MAX) ) {
            MPI_Type_free(&tmp_type);
        }
        MPI_Win_flush_local(pe, win);
    }
    return;
}

void oshmpi_put_strided(MPI_Datatype mpi_type, void *target, const void *source, 
                         ptrdiff_t target_ptrdiff, ptrdiff_t source_ptrdiff, size_t len, int pe)
{
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
#ifdef ENABLE_SMP_OPTIMIZATIONS
    if (0) {
        /* TODO */
    } else
#endif
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
        MPI_Accumulate(source, 1, source_type,                   /* origin */
                       pe, (MPI_Aint)win_offset, 1, target_type, /* target */
                       MPI_REPLACE,                              /* atomic, ordered Put */
                       win);
#else
        MPI_Put(source, 1, source_type,                   /* origin */
                pe, (MPI_Aint)win_offset, 1, target_type, /* target */
                win);
#endif
        MPI_Win_flush_local(pe, win);

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
#ifdef ENABLE_SMP_OPTIMIZATIONS
    if (0) {
        /* TODO */
    } else 
#endif
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
        MPI_Get_accumulate(NULL, 0, MPI_DATATYPE_NULL,                   /* origin */
                           target, 1, target_type,                   /* result */
                           pe, (MPI_Aint)win_offset, 1, source_type, /* remote */
                           MPI_NO_OP,                                    /* atomic, ordered Get */
                           win);
#else
        MPI_Get(target, 1, target_type,                   /* result */
                pe, (MPI_Aint)win_offset, 1, source_type, /* remote */
                win);
#endif
        MPI_Win_flush_local(pe, win);

        if (target_stride!=source_stride) 
            MPI_Type_free(&target_type);
        MPI_Type_free(&source_type);
    }

    return;
}

void oshmpi_swap(MPI_Datatype mpi_type, void *output, void *remote, const void *input, int pe)
{
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find swap remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

#ifdef ENABLE_SMP_OPTIMIZATIONS
    if (shmem_world_is_smp && win_id==SHMEM_SHEAP_WINDOW &&
            (mpi_type==MPI_LONG || mpi_type==MPI_INT || mpi_type==MPI_LONG_LONG) ) {
        if (mpi_type==MPI_LONG) {
            long * ptr = (long*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            long tmp = __sync_lock_test_and_set(ptr,*(long*)input);
            *(long*)output = tmp;
        } else if (mpi_type==MPI_INT) {
            int * ptr = (int*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            int tmp = __sync_lock_test_and_set(ptr,*(int*)input);
            *(int*)output = tmp;
        } else if (mpi_type==MPI_LONG_LONG) {
            long long * ptr = (long long*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            long long tmp = __sync_lock_test_and_set(ptr,*(long long*)input);
            *(long long*)output = tmp;
        }
        /* GCC intrinsics give the wrong answer for double swap so we just avoid trying. */
    } else
#endif
    {
        MPI_Fetch_and_op(input, output, mpi_type, pe, win_offset, MPI_REPLACE, win);
        MPI_Win_flush(pe, win);
    }
    return;
}

void oshmpi_cswap(MPI_Datatype mpi_type, void *output, void *remote, const void *input, const void *compare, int pe)
{
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find cswap remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

#ifdef ENABLE_SMP_OPTIMIZATIONS
    if (shmem_world_is_smp && win_id==SHMEM_SHEAP_WINDOW) {
        if (mpi_type==MPI_LONG) {
            long * ptr = (long*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            long tmp = __sync_val_compare_and_swap(ptr,*(long*)compare,*(long*)input);
            *(long*)output = tmp;
        } else if (mpi_type==MPI_INT) {
            int * ptr = (int*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            int tmp = __sync_val_compare_and_swap(ptr,*(int*)compare,*(int*)input);
            *(int*)output = tmp;
        } else if (mpi_type==MPI_LONG_LONG) {
            long long * ptr = (long long*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            long long tmp = __sync_val_compare_and_swap(ptr,*(long long*)compare,*(long long*)input);
            *(long long*)output = tmp;
        } else {
            oshmpi_abort(pe, "oshmpi_cswap: invalid datatype");
        }
    } else
#endif
    {
        MPI_Compare_and_swap(input, compare, output, mpi_type, pe, win_offset, win);
        MPI_Win_flush(pe, win);
    }
    return;
}

void oshmpi_add(MPI_Datatype mpi_type, void *remote, const void *input, int pe)
{
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find add remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

#ifdef ENABLE_SMP_OPTIMIZATIONS
    if (shmem_world_is_smp && win_id==SHMEM_SHEAP_WINDOW) {
        if (mpi_type==MPI_LONG) {
            long * ptr = (long*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            __sync_fetch_and_add(ptr,*(long*)input);
        } else if (mpi_type==MPI_INT) {
            int * ptr = (int*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            __sync_fetch_and_add(ptr,*(int*)input);
        } else if (mpi_type==MPI_LONG_LONG) {
            long long * ptr = (long long*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            __sync_fetch_and_add(ptr,*(long long*)input);
        } else {
            oshmpi_abort(pe, "oshmpi_add: invalid datatype");
        }
    } else
#endif
    {
        MPI_Accumulate(input, 1, mpi_type, pe, win_offset, 1, mpi_type, MPI_SUM, win);
        MPI_Win_flush_local(pe, win);
    }
    return;
}

void oshmpi_fadd(MPI_Datatype mpi_type, void *output, void *remote, const void *input, int pe)
{
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(remote, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find fadd remote");
    }

    MPI_Win win = (win_id==SHMEM_SHEAP_WINDOW) ? shmem_sheap_win : shmem_etext_win;

#ifdef ENABLE_SMP_OPTIMIZATIONS
    if (shmem_world_is_smp && win_id==SHMEM_SHEAP_WINDOW) {
        if (mpi_type==MPI_LONG) {
            long * ptr = (long*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            long tmp = __sync_fetch_and_add(ptr,*(long*)input);
            *(long*)output = tmp;
        } else if (mpi_type==MPI_INT) {
            int * ptr = (int*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            int tmp = __sync_fetch_and_add(ptr,*(int*)input);
            *(int*)output = tmp;
        } else if (mpi_type==MPI_LONG_LONG) {
            long long * ptr = (long long*)( (intptr_t)shmem_smp_sheap_ptrs[pe] + ((intptr_t)remote - (intptr_t)shmem_sheap_base_ptr) );
            long long tmp = __sync_fetch_and_add(ptr,*(long long*)input);
            *(long long*)output = tmp;
        } else {
            oshmpi_abort(pe, "oshmpi_fadd: invalid datatype");
        }
    } else
#endif
    {
        MPI_Fetch_and_op(input, output, mpi_type, pe, win_offset, MPI_SUM, win);
        MPI_Win_flush(pe, win);
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
            if (pe_root>=0) {
                *broot = oshmpi_translate_root(comm_cache[i].group, pe_root);
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
            *broot = oshmpi_translate_root(strided_group, pe_root);
        }

#if ENABLE_COMM_CACHING
        for (int i=0; i<shmem_comm_cache_size; i++) {
            if (comm_cache[i].comm == MPI_COMM_NULL ) {
                comm_cache[i].start = pe_start;
                comm_cache[i].logs  = pe_logs;
                comm_cache[i].size  = pe_size;
                comm_cache[i].comm  = *comm;
                comm_cache[i].group = strided_group;
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
                  void * target, const void * source, size_t len,
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
            MPI_Barrier( comm );
            break;
        case SHMEM_BROADCAST:
            {
                /* For bcast, MPI uses one buffer but SHMEM uses two. */
                /* From the OpenSHMEM 1.0 specification:
                 * "The data is not copied to the target address on the PE specified by PE_root." */
                MPI_Bcast(shmem_world_rank==pe_root ? (void*) source : target,
                         count, tmp_type, broot, comm);
	    }
            break;
        case SHMEM_FCOLLECT:
            MPI_Allgather(source, count, tmp_type, target, count, tmp_type, comm);
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
        case SHMEM_ALLREDUCE:
            /* From the OpenSHMEM 1.0 specification:
            "[The] source and target may be the same array, but they must not be overlapping arrays." */
            MPI_Allreduce((source==target) ? MPI_IN_PLACE : source, target, count, tmp_type, reduce_op, comm);
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
