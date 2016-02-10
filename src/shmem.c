/* -*- C -*-
 *
 * Copyright 2011 Sandia Corporation. Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S.  Government
 * retains certain rights in this software.
 *
 * Copyright (c) 2015 Intel Corporation. All rights reserved.
 * This software is available to you under the BSD license.
 *
 * information, see the LICENSE file in the top level directory of the
 * distribution.
 *
 */

/* This file was derived from shmem.h from the Portals4-SHMEM project.
 * Most of the implementation code was written by Jeff Hammond.
 * Small portions were taken from Sandia OpenSHMEM.
 */

#include "shmem.h"
#include "shmem-internals.h"
#include "shmem-wait.h"
#include "oshmpi-mcs-lock.h"
#include "dlmalloc.h"

void start_pes(int npes)
{
    oshmpi_initialize(MPI_THREAD_SINGLE);
    atexit(oshmpi_finalize);
    return;
}

void shmem_init(void)
{
    oshmpi_initialize(MPI_THREAD_SINGLE);
    atexit(oshmpi_finalize);
    return;
}

void shmem_finalize(void)
{
    oshmpi_finalize();
    return;
}

void shmem_global_exit(int status)
{
    oshmpi_abort(status,NULL);
    return;
}

void shmem_info_get_version(int *major, int *minor)
{
    *major = SHMEM_MAJOR_VERSION;
    *minor = SHMEM_MINOR_VERSION;
    return;
}

void shmem_info_get_name(char *name)
{
    strncpy(name, SHMEM_VENDOR_STRING, SHMEM_MAX_NAME_LEN);
    name[SHMEM_MAX_NAME_LEN-1] = '\0'; /* Ensure string is null terminated */
    return;
}

/* Query Routines */
int _num_pes(void) { return shmem_world_size; }
int shmem_n_pes(void) { return shmem_world_size; }
int _my_pe(void) { return shmem_world_rank; }
int shmem_my_pe(void) { return shmem_world_rank; }

/* Accessibility Query Routines */
int shmem_pe_accessible(int pe)
{
    /* TODO: detect MPMD launching, i.e. if PE is running same binary as me.
     *       MPI_APPNUM attribute of MPI_COMM_WORLD (MPI-3 10.5.3) is the way.
     *       Create a window containing these values so that any PE can Get it
     *       and compare against the local value. */
#ifdef ENABLE_MPMD_SUPPORT
    if (shmem_running_mpmd) {
        int pe_appnum;
        /* Don't need a valid PE check since these operations will fail in that case. */
        MPI_Fetch_and_op(NULL, &pe_appnum, MPI_INT, pe, 0, MPI_NO_OP, shmem_mpmd_appnum_win);
        MPI_Win_flush(pe, shmem_mpmd_appnum_win);
        return (shmem_mpmd_my_appnum == pe_appnum);
    }
#else
    return ( 0<=pe && pe<=shmem_world_size );
#endif
}

int shmem_addr_accessible(OSHMPI_CONST void *addr, int pe)
{
    if (0<=pe && pe<=shmem_world_size) {
        /* neither of these two variables is used here */
        enum shmem_window_id_e win_id;
        shmem_offset_t win_offset;
        /* oshmpi_window_offset returns 0 on successful pointer lookup */
        return (0==oshmpi_window_offset(addr, pe, &win_id, &win_offset));
    } else {
        return 0;
    }
}

/* Symmetric Heap Routines */

void *shmem_align(size_t alignment, size_t size)
{
    return mspace_memalign(shmem_heap_mspace, alignment, size);
}

void *shmemalign(size_t alignment, size_t size)
{
    return mspace_memalign(shmem_heap_mspace, alignment, size);
}

void *shmem_malloc(size_t size)
{
    return mspace_malloc(shmem_heap_mspace, size);
}
void *shmalloc(size_t size)
{
    return mspace_malloc(shmem_heap_mspace, size);
}

void *shmem_realloc(void *ptr, size_t size)
{
    return mspace_realloc(shmem_heap_mspace, ptr, size);
}
void *shrealloc(void *ptr, size_t size)
{
    return mspace_realloc(shmem_heap_mspace, ptr, size);
}

void shmem_free(void *ptr)
{
    return mspace_free(shmem_heap_mspace, ptr);
}

void shfree(void *ptr)
{
    return mspace_free(shmem_heap_mspace, ptr);
}

void shmem_quiet(void)
{
    /* The Portals4 interpretation of quiet is
     * "remote completion of all pending events",
     * which I take to mean remote completion of RMA.
     * However, I think the spec only requires this to be an ordering point. */
    /* OpenSHMEM 1.1 says that quiet implies remote completion. (August 2014) */
    oshmpi_remote_sync();
    oshmpi_local_sync();
}

void shmem_fence(void)
{
    /* ENABLE_RMA_ORDERING means "RMA operations are ordered" */
#ifndef ENABLE_RMA_ORDERING
    /* Doing fence as quiet is scalable; the per-rank method is not.
     *  - Keith Underwood on OpenSHMEM list */
    /* OpenSHMEM 1.1 says that fence implies only ordering. (August 2014) */
    oshmpi_remote_sync();
#endif
    oshmpi_local_sync();
}

/* Remote Pointer Operations */
void *shmem_ptr(OSHMPI_CONST void *target, int pe)
{
#ifdef ENABLE_SMP_OPTIMIZATIONS
    enum shmem_window_id_e win_id;
    shmem_offset_t win_offset;

    if (oshmpi_window_offset(target, pe, &win_id, &win_offset)) {
        oshmpi_abort(pe, "oshmpi_window_offset failed to find source");
    }

    if (shmem_world_is_smp && win_id==SHMEM_SHEAP_WINDOW) {
        return (void*)(shmem_smp_sheap_ptrs[pe] + (target - shmem_sheap_base_ptr));
    } else
#endif
    {
        return (void*)(pe==shmem_world_rank ? target : NULL);
    }
}

/* Elemental Put Routines */
void shmem_float_p(float *addr, float v, int pe)
{
    oshmpi_put(MPI_FLOAT, addr, &v, 1, pe);
}
void shmem_double_p(double *addr, double v, int pe)
{
    oshmpi_put(MPI_DOUBLE, addr, &v, 1, pe);
}
void shmem_longdouble_p(long double *addr, long double v, int pe)
{
    oshmpi_put(MPI_LONG_DOUBLE, addr, &v, 1, pe);
}
void shmem_char_p(char *addr, char v, int pe)
{
    oshmpi_put(MPI_CHAR, addr, &v, 1, pe);
}
void shmem_short_p(short *addr, short v, int pe)
{
    oshmpi_put(MPI_SHORT, addr, &v, 1, pe);
}
void shmem_int_p(int *addr, int v, int pe)
{
    oshmpi_put(MPI_INT, addr, &v, 1, pe);
}
void shmem_long_p(long *addr, long v, int pe)
{
    oshmpi_put(MPI_LONG, addr, &v, 1, pe);
}
void shmem_longlong_p(long long *addr, long long v, int pe)
{
    oshmpi_put(MPI_LONG_LONG, addr, &v, 1, pe);
}

/* Block Data Put Routines */
void shmem_float_put(float *target, const float *source, size_t len, int pe)
{
    oshmpi_put(MPI_FLOAT, target, source, len, pe);
}
void shmem_double_put(double *target, const double *source, size_t len, int pe)
{
    oshmpi_put(MPI_DOUBLE, target, source, len, pe);
}
void shmem_longdouble_put(long double *target, const long double *source, size_t len, int pe)
{
    oshmpi_put(MPI_LONG_DOUBLE, target, source, len, pe);
}
void shmem_char_put(char *target, const char *source, size_t len, int pe)
{
    oshmpi_put(MPI_CHAR, target, source, len, pe);
}
void shmem_short_put(short *target, const short *source, size_t len, int pe)
{
    oshmpi_put(MPI_SHORT, target, source, len, pe);
}
void shmem_int_put(int *target, const int *source, size_t len, int pe)
{
    oshmpi_put(MPI_INT, target, source, len, pe);
}
void shmem_long_put(long *target, const long *source, size_t len, int pe)
{
    oshmpi_put(MPI_LONG, target, source, len, pe);
}
void shmem_longlong_put(long long *target, const long long *source, size_t len, int pe)
{
    oshmpi_put(MPI_LONG_LONG, target, source, len, pe);
}
void shmem_put8(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put(MPI_INT8_T, target, source, len, pe);
}
void shmem_put16(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put(MPI_INT16_T, target, source, len, pe);
}
void shmem_put32(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put(MPI_INT32_T, target, source, len, pe);
}
void shmem_put64(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put(MPI_DOUBLE, target, source, len, pe);
}
void shmem_put128(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put(MPI_C_DOUBLE_COMPLEX, target, source, len, pe);
}
void shmem_putmem(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put(MPI_BYTE, target, source, len, pe);
}
/* these are extensions */
void shmem_complexf_put(float complex * target, const float complex * source, size_t len, int pe)
{
    oshmpi_put(MPI_COMPLEX, target, source, len, pe);
}
void shmem_complexd_put(double complex * target, const double complex * source, size_t len, int pe)
{
    oshmpi_put(MPI_DOUBLE_COMPLEX, target, source, len, pe);
}

/* Elemental Data Get Routines */
float shmem_float_g(OSHMPI_CONST float *addr, int pe)
{
    float v;
    oshmpi_get(MPI_FLOAT, &v, addr, 1, pe);
    return v;
}
double shmem_double_g(OSHMPI_CONST double *addr, int pe)
{
    double v;
    oshmpi_get(MPI_DOUBLE, &v, addr, 1, pe);
    return v;
}
long double shmem_longdouble_g(OSHMPI_CONST long double *addr, int pe)
{
    long double v;
    oshmpi_get(MPI_LONG_DOUBLE, &v, addr, 1, pe);
    return v;
}
char shmem_char_g(OSHMPI_CONST char *addr, int pe)
{
    char v;
    oshmpi_get(MPI_CHAR, &v, addr, 1, pe);
    return v;
}
short shmem_short_g(OSHMPI_CONST short *addr, int pe)
{
    short v;
    oshmpi_get(MPI_SHORT, &v, addr, 1, pe);
    return v;
}
int shmem_int_g(OSHMPI_CONST int *addr, int pe)
{
    int v;
    oshmpi_get(MPI_INT, &v, addr, 1, pe);
    return v;
}
long shmem_long_g(OSHMPI_CONST long *addr, int pe)
{
    long v;
    oshmpi_get(MPI_LONG, &v, addr, 1, pe);
    return v;
}
long long shmem_longlong_g(OSHMPI_CONST long long *addr, int pe)
{
    long long v;
    oshmpi_get(MPI_LONG_LONG, &v, addr, 1, pe);
    return v;
}

/* Block Data Get Routines */
void shmem_float_get(float *target, const float *source, size_t len, int pe)
{
    oshmpi_get(MPI_FLOAT, target, source, len, pe);
}
void shmem_double_get(double *target, const double *source, size_t len, int pe)
{
    oshmpi_get(MPI_DOUBLE, target, source, len, pe);
}
void shmem_longdouble_get(long double *target, const long double *source, size_t len, int pe)
{
    oshmpi_get(MPI_LONG_DOUBLE, target, source, len, pe);
}
void shmem_char_get(char *target, const char *source, size_t len, int pe)
{
    oshmpi_get(MPI_CHAR, target, source, len, pe);
}
void shmem_short_get(short *target, const short *source, size_t len, int pe)
{
    oshmpi_get(MPI_SHORT, target, source, len, pe);
}
void shmem_int_get(int *target, const int *source, size_t len, int pe)
{
    oshmpi_get(MPI_INT, target, source, len, pe);
}
void shmem_long_get(long *target, const long *source, size_t len, int pe)
{
    oshmpi_get(MPI_LONG, target, source, len, pe);
}
void shmem_longlong_get(long long *target, const long long *source, size_t len, int pe)
{
    oshmpi_get(MPI_LONG_LONG, target, source, len, pe);
}
void shmem_get8(void *target, const void *source, size_t len, int pe)
{
    oshmpi_get(MPI_INT8_T, target, source, len, pe);
}
void shmem_get16(void *target, const void *source, size_t len, int pe)
{
    oshmpi_get(MPI_INT16_T, target, source, len, pe);
}
void shmem_get32(void *target, const void *source, size_t len, int pe)
{
    oshmpi_get(MPI_INT32_T, target, source, len, pe);
}
void shmem_get64(void *target, const void *source, size_t len, int pe)
{
    oshmpi_get(MPI_DOUBLE, target, source, len, pe);
}
void shmem_get128(void *target, const void *source, size_t len, int pe)
{
    oshmpi_get(MPI_C_DOUBLE_COMPLEX, target, source, len, pe);
}
void shmem_getmem(void *target, const void *source, size_t len, int pe)
{
    oshmpi_get(MPI_BYTE, target, source, len, pe);
}
/* these are extensions */
void shmem_complexf_get(float complex * target, const float complex * source, size_t len, int pe)
{
    oshmpi_get(MPI_COMPLEX, target, source, len, pe);
}
void shmem_complexd_get(double complex * target, const double complex * source, size_t len, int pe)
{
    oshmpi_get(MPI_DOUBLE_COMPLEX, target, source, len, pe);
}

/* Strided Put Routines */
void shmem_float_iput(float *target, const float *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_put_strided(MPI_FLOAT, target, source, tst, sst, len, pe);
}
void shmem_double_iput(double *target, const double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_put_strided(MPI_DOUBLE, target, source, tst, sst, len, pe);
}
void shmem_longdouble_iput(long double *target, const long double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_put_strided(MPI_LONG_DOUBLE, target, source, tst, sst, len, pe);
}
void shmem_short_iput(short *target, const short *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_put_strided(MPI_SHORT, target, source, tst, sst, len, pe);
}
void shmem_int_iput(int *target, const int *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_put_strided(MPI_INT, target, source, tst, sst, len, pe);
}
void shmem_long_iput(long *target, const long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_put_strided(MPI_LONG, target, source, tst, sst, len, pe);
}
void shmem_longlong_iput(long long *target, const long long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_put_strided(MPI_LONG_LONG, target, source, tst, sst, len, pe);
}
void shmem_iput32(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_put_strided(MPI_INT32_T, target, source, tst, sst, len, pe);
}
void shmem_iput64(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    /* FIXME Why not MPI_INT64_T ? */
    oshmpi_put_strided(MPI_DOUBLE, target, source, tst, sst, len, pe);
}
void shmem_iput128(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    /* FIXME Why not use MPI_INT64_T and len*=2 ?
     * I recall something we tried before didn't work, but what was it? */
    oshmpi_put_strided(MPI_C_DOUBLE_COMPLEX, target, source, tst, sst, len, pe);
}

/* Strided Get Routines */
void shmem_float_iget(float *target, const float *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_FLOAT, target, source, tst, sst, len, pe);
}
void shmem_double_iget(double *target, const double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_DOUBLE, target, source, tst, sst, len, pe);
}
void shmem_longdouble_iget(long double *target, const long double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_LONG_DOUBLE, target, source, tst, sst, len, pe);
}
void shmem_short_iget(short *target, const short *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_SHORT, target, source, tst, sst, len, pe);
}
void shmem_int_iget(int *target, const int *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_INT, target, source, tst, sst, len, pe);
}
void shmem_long_iget(long *target, const long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_LONG, target, source, tst, sst, len, pe);
}
void shmem_longlong_iget(long long *target, const long long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_LONG_LONG, target, source, tst, sst, len, pe);
}
void shmem_iget32(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_INT32_T, target, source, tst, sst, len, pe);
}
void shmem_iget64(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_DOUBLE, target, source, tst, sst, len, pe);
}
void shmem_iget128(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe)
{
    oshmpi_get_strided(MPI_C_DOUBLE_COMPLEX, target, source, tst, sst, len, pe);
}

/* Nonblocking remote memory access routines -- Put */
void shmem_float_put_nbi(float *target, const float *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_FLOAT, target, source, len, pe);
}
void shmem_double_put_nbi(double *target, const double *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_DOUBLE, target, source, len, pe);
}
void shmem_longdouble_put_nbi(long double *target, const long double *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_LONG_DOUBLE, target, source, len, pe);
}
void shmem_char_put_nbi(char *target, const char *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_CHAR, target, source, len, pe);
}
void shmem_short_put_nbi(short *target, const short *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_SHORT, target, source, len, pe);
}
void shmem_int_put_nbi(int *target, const int *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_INT, target, source, len, pe);
}
void shmem_long_put_nbi(long *target, const long *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_LONG, target, source, len, pe);
}
void shmem_longlong_put_nbi(long long *target, const long long *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_LONG_LONG, target, source, len, pe);
}
void shmem_put8_nbi(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_INT8_T, target, source, len, pe);
}
void shmem_put16_nbi(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_INT16_T, target, source, len, pe);
}
void shmem_put32_nbi(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_INT32_T, target, source, len, pe);
}
void shmem_put64_nbi(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_DOUBLE, target, source, len, pe);
}
void shmem_put128_nbi(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_C_DOUBLE_COMPLEX, target, source, len, pe);
}
void shmem_putmem_nbi(void *target, const void *source, size_t len, int pe)
{
    oshmpi_put_nbi(MPI_BYTE, target, source, len, pe);
}

/* Nonblocking remote memory access routines -- get_nbi */
void shmem_float_get_nbi(float *target_nbi, const float *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_FLOAT, target_nbi, source, len, pe);
}
void shmem_double_get_nbi(double *target_nbi, const double *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_DOUBLE, target_nbi, source, len, pe);
}
void shmem_longdouble_get_nbi(long double *target_nbi, const long double *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_LONG_DOUBLE, target_nbi, source, len, pe);
}
void shmem_char_get_nbi(char *target_nbi, const char *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_CHAR, target_nbi, source, len, pe);
}
void shmem_short_get_nbi(short *target_nbi, const short *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_SHORT, target_nbi, source, len, pe);
}
void shmem_int_get_nbi(int *target_nbi, const int *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_INT, target_nbi, source, len, pe);
}
void shmem_long_get_nbi(long *target_nbi, const long *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_LONG, target_nbi, source, len, pe);
}
void shmem_longlong_get_nbi(long long *target_nbi, const long long *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_LONG_LONG, target_nbi, source, len, pe);
}
void shmem_get8_nbi(void *target_nbi, const void *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_INT8_T, target_nbi, source, len, pe);
}
void shmem_get16_nbi(void *target_nbi, const void *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_INT16_T, target_nbi, source, len, pe);
}
void shmem_get32_nbi(void *target_nbi, const void *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_INT32_T, target_nbi, source, len, pe);
}
void shmem_get64_nbi(void *target_nbi, const void *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_DOUBLE, target_nbi, source, len, pe);
}
void shmem_get128_nbi(void *target_nbi, const void *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_C_DOUBLE_COMPLEX, target_nbi, source, len, pe);
}
void shmem_getmem_nbi(void *target_nbi, const void *source, size_t len, int pe)
{
    oshmpi_get_nbi(MPI_BYTE, target_nbi, source, len, pe);
}

/* Naming conventions for shorthand:
 * r = return v
 * c = comparand
 * v = v (input)
 * t = target
 */

/* Atomic Memory fetch-and-operate Routines -- Swap */
float shmem_float_swap(float *t, float v, int pe)
{
    float r;
    oshmpi_swap(MPI_FLOAT, &r, t, &v, pe) ;
    return r;
}
double shmem_double_swap(double *t, double v, int pe)
{
    double r;
    oshmpi_swap(MPI_DOUBLE, &r, t, &v, pe) ;
    return r;
}
int shmem_int_swap(int *t, int v, int pe)
{
    int r;
    oshmpi_swap(MPI_INT, &r, t, &v, pe) ;
    return r;
}
long shmem_long_swap(long *t, long v, int pe)
{
    long r;
    oshmpi_swap(MPI_LONG, &r, t, &v, pe) ;
    return r;
}
long long shmem_longlong_swap(long long *t, long long v, int pe)
{
    long long r;
    oshmpi_swap(MPI_LONG_LONG, &r, t, &v, pe) ;
    return r;
}
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ < 201112L)
long shmem_swap(long *t, long v, int pe)
{
    long r;
    oshmpi_swap(MPI_LONG, &r, t, &v, pe) ;
    return r;
}
#endif

/* Atomic Memory fetch-and-operate Routines -- Cswap */
int shmem_int_cswap(int *t, int c, int v, int pe)
{
    int r;
    oshmpi_cswap(MPI_INT, &r, t, &v, &c, pe) ;
    return r;
}
long shmem_long_cswap(long *t, long c, long v, int pe)
{
    long r;
    oshmpi_cswap(MPI_LONG, &r, t, &v, &c, pe) ;
    return r;
}
long long shmem_longlong_cswap(long long * t, long long c, long long v, int pe)
{
    long long r;
    oshmpi_cswap(MPI_LONG_LONG, &r, t, &v, &c, pe) ;
    return r;
}

#ifndef USE_SAME_OP_NO_OP

/* Atomic Memory fetch-and-operate Routines -- Fetch and Add */
int shmem_int_fadd(int *t, int v, int pe)
{
    int r;
    oshmpi_fadd(MPI_INT, &r, t, &v, pe);
    return r;
}
long shmem_long_fadd(long *t, long v, int pe)
{
    long r;
    oshmpi_fadd(MPI_LONG, &r, t, &v, pe);
    return r;
}
long long shmem_longlong_fadd(long long *t, long long v, int pe)
{
    long long r;
    oshmpi_fadd(MPI_LONG_LONG, &r, t, &v, pe);
    return r;
}

/* Atomic Memory fetch-and-operate Routines -- Fetch and Increment */
int shmem_int_finc(int *t, int pe)
{
    int r, v=1;
    oshmpi_fadd(MPI_INT, &r, t, &v, pe);
    return r;
}
long shmem_long_finc(long *t, int pe)
{
    long r, v=1;
    oshmpi_fadd(MPI_LONG, &r, t, &v, pe);
    return r;
}
long long shmem_longlong_finc(long long *t, int pe)
{
    long long r, v=1;
    oshmpi_fadd(MPI_LONG_LONG, &r, t, &v, pe);
    return r;
}

/* Atomic Memory Operation Routines -- Add */
void shmem_int_add(int *t, int v, int pe)
{
    oshmpi_add(MPI_INT, t, &v, pe);
}
void shmem_long_add(long *t, long v, int pe)
{
    oshmpi_add(MPI_LONG, t, &v, pe);
}
void shmem_longlong_add(long long *t, long long v, int pe)
{
    oshmpi_add(MPI_LONG_LONG, t, &v, pe);
}

/* Atomic Memory Operation Routines -- Increment */
void shmem_int_inc(int *t, int pe)
{
    int v=1;
    oshmpi_add(MPI_INT, t, &v, pe);
}
void shmem_long_inc(long *t, int pe)
{
    long v=1;
    oshmpi_add(MPI_LONG, t, &v, pe);
}
void shmem_longlong_inc(long long *t, int pe)
{
    long long v=1;
    oshmpi_add(MPI_LONG_LONG, t, &v, pe);
}

/* Atomic Memory fetch-and-operate Routines -- Fetch */
float shmem_float_fetch(OSHMPI_CONST float *t, int pe)
{
    float r;
    oshmpi_fetch(MPI_FLOAT, &r, t, pe) ;
    return r;
}
double shmem_double_fetch(OSHMPI_CONST double *t, int pe)
{
    double r;
    oshmpi_fetch(MPI_DOUBLE, &r, t, pe) ;
    return r;
}
int shmem_int_fetch(OSHMPI_CONST int *t, int pe)
{
    int r;
    oshmpi_fetch(MPI_INT, &r, t, pe) ;
    return r;
}
long shmem_long_fetch(OSHMPI_CONST long *t, int pe)
{
    long r;
    oshmpi_fetch(MPI_LONG, &r, t, pe) ;
    return r;
}
long long shmem_longlong_fetch(OSHMPI_CONST long long *t, int pe)
{
    long long r;
    oshmpi_fetch(MPI_LONG_LONG, &r, t, pe) ;
    return r;
}

/* Atomic Memory fetch-and-operate Routines -- Set */
void shmem_float_set(float *t, float v, int pe)
{
    oshmpi_set(MPI_FLOAT, t, &v, pe) ;
}
void shmem_double_set(double *t, double v, int pe)
{
    oshmpi_set(MPI_DOUBLE, t, &v, pe) ;
}
void shmem_int_set(int *t, int v, int pe)
{
    oshmpi_set(MPI_INT, t, &v, pe) ;
}
void shmem_long_set(long *t, long v, int pe)
{
    oshmpi_set(MPI_LONG, t, &v, pe) ;
}
void shmem_longlong_set(long long *t, long long v, int pe)
{
    oshmpi_set(MPI_LONG_LONG, t, &v, pe) ;
}

#else

#warning shmem_{int,long,longlong}_{inc,add,finc,fadd} have been disabled.

#endif // USE_SAME_OP_NO_OP

/* Point-to-Point Synchronization Routines -- Wait */
void shmem_short_wait(OSHMPI_VOLATILE short *var, short v)
{ short t; SHMEM_WAIT(var, v, t, MPI_SHORT); }
void shmem_int_wait(OSHMPI_VOLATILE int *var, int v)
{ int t; SHMEM_WAIT(var, v, t, MPI_INT); }
void shmem_long_wait(OSHMPI_VOLATILE long *var, long v)
{ long t; SHMEM_WAIT(var, v, t, MPI_LONG); }
void shmem_longlong_wait(OSHMPI_VOLATILE long long *var, long long v)
{ long long t; SHMEM_WAIT(var, v, t, MPI_LONG_LONG); }
void shmem_wait(OSHMPI_VOLATILE long *var, long v)
{ long t; SHMEM_WAIT(var, v, t, MPI_LONG); }

/* Point-to-Point Synchronization Routines -- Wait Until */
void shmem_short_wait_until(OSHMPI_VOLATILE short *var, int c, short v)
{ short t; SHMEM_WAIT_UNTIL(var, c, v, t, MPI_SHORT); }
void shmem_int_wait_until(OSHMPI_VOLATILE int *var, int c, int v)
{ int t; SHMEM_WAIT_UNTIL(var, c, v, t, MPI_INT); }
void shmem_long_wait_until(OSHMPI_VOLATILE long *var, int c, long v)
{ long t; SHMEM_WAIT_UNTIL(var, c, v, t, MPI_LONG); }
void shmem_longlong_wait_until(OSHMPI_VOLATILE long long *var, int c, long long v)
{ long long t; SHMEM_WAIT_UNTIL(var, c, v, t, MPI_LONG_LONG); }
void shmem_wait_until(OSHMPI_VOLATILE long *var, int c, long v)
{ long t; SHMEM_WAIT_UNTIL(var, c, v, t, MPI_LONG); }

/* Barrier Synchronization Routines */

void shmem_barrier(int PE_start, int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_remote_sync();
    oshmpi_local_sync();
    oshmpi_set_psync(SHMEM_BARRIER_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_BARRIER, MPI_DATATYPE_NULL, MPI_OP_NULL, NULL, NULL, 0 /* stride */, 0 /* stride */, 0 /* count */, -1 /* root */,  PE_start, logPE_stride, PE_size);
}

void shmem_barrier_all(void)
{
    oshmpi_remote_sync();
    oshmpi_local_sync();
    MPI_Barrier(SHMEM_COMM_WORLD);
    //oshmpi_coll(SHMEM_BARRIER, MPI_DATATYPE_NULL, MPI_OP_NULL, NULL, NULL, 0 /* stride */, 0 /* stride */, 0 /* count */, -1 /* root */, 0, 0, shmem_world_size );
}

/* Broadcast Routines */

void shmem_broadcast32(void *target, const void *source, size_t nlong, int PE_root, int PE_start, int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_BCAST_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_BROADCAST, MPI_INT32_T, MPI_OP_NULL, target, source, 0 /* stride */, 0 /* stride */, nlong, PE_root, PE_start, logPE_stride, PE_size);
}
void shmem_broadcast64(void *target, const void *source, size_t nlong, int PE_root, int PE_start, int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_BCAST_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_BROADCAST, MPI_INT64_T, MPI_OP_NULL, target, source, 0 /* stride */, 0 /* stride */, nlong, PE_root, PE_start, logPE_stride, PE_size);
}

/* Collect Routines */

void shmem_collect32(void *target, const void *source, size_t nlong, int PE_start, int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_COLLECT_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_COLLECT, MPI_INT32_T, MPI_OP_NULL, target, source, 0 /* stride */, 0 /* stride */, nlong, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_collect64(void *target, const void *source, size_t nlong, int PE_start, int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_COLLECT_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_COLLECT, MPI_INT64_T, MPI_OP_NULL, target, source, 0 /* stride */, 0 /* stride */, nlong, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_fcollect32(void *target, const void *source, size_t nlong, int PE_start, int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_COLLECT_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_FCOLLECT,  MPI_INT32_T, MPI_OP_NULL, target, source, 0 /* stride */, 0 /* stride */, nlong, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_fcollect64(void *target, const void *source, size_t nlong, int PE_start, int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_COLLECT_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_FCOLLECT,  MPI_INT64_T, MPI_OP_NULL, target, source, 0 /* stride */, 0 /* stride */, nlong, -1 /* root */, PE_start, logPE_stride, PE_size);
}

/* 8.6: All-to-all routines */
void shmem_alltoall32(void *dest, const void *source, size_t nelems, int PE_start,
                      int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_COLLECT_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLTOALL,  MPI_INT32_T, MPI_OP_NULL, dest, source, 0 /* stride */, 0 /* stride */, nelems, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_alltoall64(void *dest, const void *source, size_t nelems, int PE_start,
                      int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_COLLECT_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLTOALL,  MPI_INT64_T, MPI_OP_NULL, dest, source, 0 /* stride */, 0 /* stride */, nelems, -1 /* root */, PE_start, logPE_stride, PE_size);
}

void shmem_alltoalls32(void *dest, const void *source, ptrdiff_t dst,
                       ptrdiff_t sst, size_t nelems, int PE_start,
                       int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_COLLECT_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLTOALLS,  MPI_INT64_T, MPI_OP_NULL, dest, source, dst, sst, nelems, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_alltoalls64(void *dest, const void *source, ptrdiff_t dst,
                       ptrdiff_t sst, size_t nelems, int PE_start,
                       int logPE_stride, int PE_size, long *pSync)
{
    oshmpi_set_psync(SHMEM_COLLECT_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLTOALLS,  MPI_INT64_T, MPI_OP_NULL, dest, source, dst, sst, nelems, -1 /* root */, PE_start, logPE_stride, PE_size);
}

/* Reduction Routines */

void shmem_short_and_to_all(short *target, OSHMPI_CONST short *source, int nreduce, int PE_start, int logPE_stride, int PE_size, short *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_SHORT, MPI_LAND, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_int_and_to_all(int *target, OSHMPI_CONST int *source, int nreduce, int PE_start, int logPE_stride, int PE_size, int *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_INT, MPI_LAND, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_long_and_to_all(long *target, OSHMPI_CONST long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG, MPI_LAND, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longlong_and_to_all(long long *target, OSHMPI_CONST long long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_LONG, MPI_LAND, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}

void shmem_short_or_to_all(short *target, OSHMPI_CONST short *source, int nreduce, int PE_start, int logPE_stride, int PE_size, short *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_SHORT, MPI_BOR, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_int_or_to_all(int *target, OSHMPI_CONST int *source, int nreduce, int PE_start, int logPE_stride, int PE_size, int *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_INT, MPI_BOR, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_long_or_to_all(long *target, OSHMPI_CONST long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG, MPI_BOR, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longlong_or_to_all(long long *target, OSHMPI_CONST long long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_LONG, MPI_BOR, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}

void shmem_short_xor_to_all(short *target, OSHMPI_CONST short *source, int nreduce, int PE_start, int logPE_stride, int PE_size, short *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_SHORT, MPI_BXOR, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_int_xor_to_all(int *target, OSHMPI_CONST int *source, int nreduce, int PE_start, int logPE_stride, int PE_size, int *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_INT, MPI_BXOR, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_long_xor_to_all(long *target, OSHMPI_CONST long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG, MPI_BXOR, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longlong_xor_to_all(long long *target, OSHMPI_CONST long long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_LONG, MPI_BXOR, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}

void shmem_float_min_to_all(float *target, OSHMPI_CONST float *source, int nreduce, int PE_start, int logPE_stride, int PE_size, float *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_FLOAT, MPI_MIN, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_double_min_to_all(double *target, OSHMPI_CONST double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, double *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_DOUBLE, MPI_MIN, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longdouble_min_to_all(long double *target, OSHMPI_CONST long double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long double *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_DOUBLE, MPI_MIN, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_short_min_to_all(short *target, OSHMPI_CONST short *source, int nreduce, int PE_start, int logPE_stride, int PE_size, short *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_SHORT, MPI_MIN, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_int_min_to_all(int *target, OSHMPI_CONST int *source, int nreduce, int PE_start, int logPE_stride, int PE_size, int *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_INT, MPI_MIN, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_long_min_to_all(long *target, OSHMPI_CONST long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG, MPI_MIN, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longlong_min_to_all(long long *target, OSHMPI_CONST long long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_LONG, MPI_MIN, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}

void shmem_float_max_to_all(float *target, OSHMPI_CONST float *source, int nreduce, int PE_start, int logPE_stride, int PE_size, float *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_FLOAT, MPI_MAX, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_double_max_to_all(double *target, OSHMPI_CONST double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, double *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_DOUBLE, MPI_MAX, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longdouble_max_to_all(long double *target, OSHMPI_CONST long double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long double *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_DOUBLE, MPI_MAX, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_short_max_to_all(short *target, OSHMPI_CONST short *source, int nreduce, int PE_start, int logPE_stride, int PE_size, short *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_SHORT, MPI_MAX, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_int_max_to_all(int *target, OSHMPI_CONST int *source, int nreduce, int PE_start, int logPE_stride, int PE_size, int *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_INT, MPI_MAX, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_long_max_to_all(long *target, OSHMPI_CONST long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG, MPI_MAX, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longlong_max_to_all(long long *target, OSHMPI_CONST long long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_LONG, MPI_MAX, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}

void shmem_float_sum_to_all(float *target, OSHMPI_CONST float *source, int nreduce, int PE_start, int logPE_stride, int PE_size, float *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_FLOAT, MPI_SUM, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_double_sum_to_all(double *target, OSHMPI_CONST double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, double *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_DOUBLE, MPI_SUM, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longdouble_sum_to_all(long double *target, OSHMPI_CONST long double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long double *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_DOUBLE, MPI_SUM, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_short_sum_to_all(short *target, OSHMPI_CONST short *source, int nreduce, int PE_start, int logPE_stride, int PE_size, short *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_SHORT, MPI_SUM, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_int_sum_to_all(int *target, OSHMPI_CONST int *source, int nreduce, int PE_start, int logPE_stride, int PE_size, int *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_INT, MPI_SUM, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_long_sum_to_all(long *target, OSHMPI_CONST long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG, MPI_SUM, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longlong_sum_to_all(long long *target, OSHMPI_CONST long long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_LONG, MPI_SUM, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}

void shmem_float_prod_to_all(float *target, OSHMPI_CONST float *source, int nreduce, int PE_start, int logPE_stride, int PE_size, float *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_FLOAT, MPI_PROD, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_double_prod_to_all(double *target, OSHMPI_CONST double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, double *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_DOUBLE, MPI_PROD, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longdouble_prod_to_all(long double *target, OSHMPI_CONST long double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long double *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_DOUBLE, MPI_PROD, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_short_prod_to_all(short *target, OSHMPI_CONST short *source, int nreduce, int PE_start, int logPE_stride, int PE_size, short *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_SHORT, MPI_PROD, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_int_prod_to_all(int *target, OSHMPI_CONST int *source, int nreduce, int PE_start, int logPE_stride, int PE_size, int *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_INT, MPI_PROD, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_long_prod_to_all(long *target, OSHMPI_CONST long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG, MPI_PROD, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}
void shmem_longlong_prod_to_all(long long *target, OSHMPI_CONST long long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long long *pWrk, long *pSync)
{
    oshmpi_set_psync(SHMEM_REDUCE_SYNC_SIZE, SHMEM_SYNC_VALUE, pSync);
    oshmpi_coll(SHMEM_ALLREDUCE, MPI_LONG_LONG, MPI_PROD, target, source, 0, 0, nreduce, -1 /* root */, PE_start, logPE_stride, PE_size);
}

/* Lock Routines */
void shmem_set_lock(OSHMPI_VOLATILE long * lock) { oshmpi_lock(lock); }
void shmem_clear_lock(OSHMPI_VOLATILE long * lock) { oshmpi_unlock(lock); }
int  shmem_test_lock(OSHMPI_VOLATILE long * lock) { return oshmpi_trylock(lock); }

/* Cache Management Routines (deprecated) */
void shmem_set_cache_inv(void) { return; }
void shmem_set_cache_line_inv(void *target) { return; }
void shmem_clear_cache_inv(void) { return; }
void shmem_clear_cache_line_inv(void *target) { return; }
void shmem_udcflush(void) { return; }
void shmem_udcflush_line(void *target) { return; }

/* Portals extensions */
double shmem_wtime(void) { return MPI_Wtime(); }

char* shmem_nodename(void)
{
    /* In general, nodename != procname, of course, but there are
     * many implementations where this will be true because the
     * procname is just the IP address. */
    int namelen = 0;
    memset(shmem_procname, '\0', MPI_MAX_PROCESSOR_NAME);
    MPI_Get_processor_name( shmem_procname, &namelen );
    return shmem_procname;
}

