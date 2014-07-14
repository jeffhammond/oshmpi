/* -*- C -*-
 *
 * Copyright 2011 Sandia Corporation. Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S.  Government
 * retains certain rights in this software.
 * 
 * This file is part of the Portals SHMEM software package. For license
 * information, see the LICENSE file in the top level directory of the
 * distribution.
 *
 */

#include <shmem.h>

/* 8.1: Initialization Routines */
void start_pes(int npes){ pstart_pes(npes); }

/* 8.2: Query Routines */
int _num_pes(void){ return p_num_pes(); }
int shmem_n_pes(void){ pshmem_n_pes(); }
int _my_pe(void){ return p_my_pe(); }
int shmem_my_pe(void){ return pshmem_my_pe(); }

/* 8.3: Accessibility Query Routines */
int shmem_pe_accessible(int pe){ return pshmem_pe_accessible(pe); }
int shmem_addr_accessible(void *addr, int pe){ return pshmem_addr_accessible(addr, pe); }

/* 8.4: Symmetric Heap Routines */
void *shmalloc(size_t size){ return pshmalloc(size); }
void *shmemalign(size_t alignment, size_t size){ return pshmemalign(alignment, size); }
void *shrealloc(void *ptr, size_t size){ return pshrealloc(ptr, size); }
void shfree(void *ptr){ pshfree(ptr); }

/* 8.5: Remote Pointer Operations */
void *shmem_ptr(void *target, int pe){ return pshmem_ptr(target, pe); }

/* 8.6: Elemental Put Routines */
void shmem_float_p(float *addr, float value, int pe){ pshmem_float_p(addr, value, pe); }
void shmem_double_p(double *addr, double value, int pe){ pshmem_double_p(addr, value, pe); }
void shmem_longdouble_p(long double *addr, long double value, int pe){ pshmem_longdouble_p(addr, value, pe); }
void shmem_char_p(char *addr, char value, int pe){ pshmem_char_p(addr, value, pe); }
void shmem_short_p(short *addr, short value, int pe){ pshmem_short_p(addr, value, pe); }
void shmem_int_p(int *addr, int value, int pe){ pshmem_int_p(addr, value, pe); }
void shmem_long_p(long *addr, long value, int pe){ pshmem_long_p(addr, value, pe); }
void shmem_longlong_p(long long *addr, long long value, int pe){ pshmem_longlong_p(addr, value, pe); }

/* 8.7: Block Data Put Routines */
void shmem_float_put(float *target, const float *source, size_t len, int pe){ pshmem_float_put(target, source, len, pe); }
void shmem_double_put(double *target, const double *source, size_t len, int pe){ pshmem_double_put(target, source, len, pe); }
void shmem_longdouble_put(long double *target, const long double *source, size_t len, int pe){ pshmem_longdouble_put(target, source, len, pe); }
void shmem_char_put(char *target, const char *source, size_t nelems, int pe){ pshmem_char_put(target, source, nelems, pe); }
void shmem_short_put(short *target, const short *source, size_t len, int pe){ pshmem_short_put(target, source, len, pe); }
void shmem_int_put(int *target, const int *source, size_t len, int pe){ pshmem_int_put(target, source, len, pe); }
void shmem_long_put(long *target, const long *source, size_t len, int pe){ pshmem_long_put(target, source, len, pe); }
void shmem_longlong_put(long long *target, const long long *source, size_t len, int pe){ pshmem_longlong_put(target, source, len, pe); }
void shmem_put32(void *target, const void *source, size_t len, int pe){ pshmem_put32(target, source, len, pe); }
void shmem_put64(void *target, const void *source, size_t len, int pe){ pshmem_put64(target, source, len, pe); }
void shmem_put128(void *target, const void *source, size_t len, int pe){ pshmem_put128(target, source, len, pe); }
void shmem_putmem(void *target, const void *source, size_t len, int pe){ pshmem_putmem(target, source, len, pe); }
void shmem_complexf_put(float complex * target, const float complex * source, size_t nelems, int pe){ pshmem_complexf_put(target, source, nelems, pe); }
void shmem_complexd_put(double complex * target, const double complex * source, size_t nelems, int pe){ pshmem_complexd_put(target, source, nelems, pe); }

/* 8.8: Strided Put Routines */
void shmem_float_iput(float *target, const float *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe){ pshmem_float_iput(target, source, tst, sst, len, pe); }
void shmem_double_iput(double *target, const double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe) { pshmem_double_iput(target, source, tst, sst, len, pe); }
void shmem_longdouble_iput(long double *target, const long double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe){ pshmem_longdouble_iput(target, source, tst, sst, len, pe); }
void shmem_short_iput(short *target, const short *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe){ pshmem_short_iput(target, source, tst, sst, len, pe); }
void shmem_int_iput(int *target, const int *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe){ pshmem_int_iput(target, source, tst, sst, len, pe); }
void shmem_long_iput(long *target, const long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe){ pshmem_long_iput(target, source, tst, sst, len, pe); }
void shmem_longlong_iput(long long *target, const long long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe){ pshmem_longlong_iput(target, source, tst, sst, len, pe); }
void shmem_iput32(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe){ pshmem_iput32(target, source, tst, sst, len, pe); }
void shmem_iput64(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe){ pshmem_iput64(target, source, tst, sst, len, pe); }
void shmem_iput128(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe){ pshmem_iput128(target, source, tst, sst, len, pe); }

/* 8.9: Elemental Data Get Routines */
float shmem_float_g(float *addr, int pe);
float pshmem_float_g(float *addr, int pe);
double shmem_double_g(double *addr, int pe);
double pshmem_double_g(double *addr, int pe);
long double shmem_longdouble_g(long double *addr, int pe);
long double pshmem_longdouble_g(long double *addr, int pe);
char shmem_char_g(char *addr, int pe);
char pshmem_char_g(char *addr, int pe);
short shmem_short_g(short *addr, int pe);
short pshmem_short_g(short *addr, int pe);
int shmem_int_g(int *addr, int pe);
int pshmem_int_g(int *addr, int pe);
long shmem_long_g(long *addr, int pe);
long pshmem_long_g(long *addr, int pe);
long long shmem_longlong_g(long long *addr, int pe);
long long pshmem_longlong_g(long long *addr, int pe);

/* 8.10 Block Data Get Routines */
void shmem_float_get(float *target, const float *source, size_t len, int pe);
void pshmem_float_get(float *target, const float *source, size_t len, int pe);
void shmem_double_get(double *target, const double *source, size_t len, int pe);
void pshmem_double_get(double *target, const double *source, size_t len, int pe);
void shmem_longdouble_get(long double *target, const long double *source, size_t len, int pe);
void pshmem_longdouble_get(long double *target, const long double *source, size_t len, int pe);
void shmem_char_get(char *target, const char *source, size_t len, int pe);
void pshmem_char_get(char *target, const char *source, size_t len, int pe);
void shmem_short_get(short *target, const short *source, size_t len, int pe);
void pshmem_short_get(short *target, const short *source, size_t len, int pe);
void shmem_int_get(int *target, const int *source, size_t len, int pe);
void pshmem_int_get(int *target, const int *source, size_t len, int pe);
void shmem_long_get(long *target, const long *source, size_t len, int pe);
void pshmem_long_get(long *target, const long *source, size_t len, int pe);
void shmem_longlong_get(long long *target, const long long *source, size_t len, int pe);
void pshmem_longlong_get(long long *target, const long long *source, size_t len, int pe);
void shmem_get32(void *target, const void *source, size_t len, int pe);
void pshmem_get32(void *target, const void *source, size_t len, int pe);
void shmem_get64(void *target, const void *source, size_t len, int pe);
void pshmem_get64(void *target, const void *source, size_t len, int pe);
void shmem_get128(void *target, const void *source, size_t len, int pe);
void pshmem_get128(void *target, const void *source, size_t len, int pe);
void shmem_getmem(void *target, const void *source, size_t len, int pe);
void pshmem_getmem(void *target, const void *source, size_t len, int pe);
void shmem_complexf_get(float complex * target, const float complex * source, size_t nelems, int pe);
void pshmem_complexf_get(float complex * target, const float complex * source, size_t nelems, int pe);
void shmem_complexd_get(double complex * target, const double complex * source, size_t nelems, int pe);
void pshmem_complexd_get(double complex * target, const double complex * source, size_t nelems, int pe);

/* 8.11: Strided Get Routines */
void shmem_float_iget(float *target, const float *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_float_iget(float *target, const float *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void shmem_double_iget(double *target, const double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_double_iget(double *target, const double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void shmem_longdouble_iget(long double *target, const long double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_longdouble_iget(long double *target, const long double *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void shmem_short_iget(short *target, const short *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_short_iget(short *target, const short *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void shmem_int_iget(int *target, const int *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_int_iget(int *target, const int *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void shmem_long_iget(long *target, const long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_long_iget(long *target, const long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void shmem_longlong_iget(long long *target, const long long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_longlong_iget(long long *target, const long long *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void shmem_iget32(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_iget32(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void shmem_iget64(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_iget64(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void shmem_iget128(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);
void pshmem_iget128(void *target, const void *source, ptrdiff_t tst, ptrdiff_t sst, size_t len, int pe);

/* 8.12: Atomic Memory fetch-and-operate Routines -- Swap */
float shmem_float_swap(float *target, float value, int pe){ return pshmem_float_swap(target, value, pe); }
double shmem_double_swap(double *target, double value, int pe){ return pshmem_double_swap(target, value, pe); }
int shmem_int_swap(int *target, int value, int pe){ return pshmem_int_swap(target, value, pe); }
long shmem_long_swap(long *target, long value, int pe){ return pshmem_long_swap(target, value, pe); }
long long shmem_longlong_swap(long long *target, long long value, int pe){ return pshmem_longlong_swap(target, value, pe); }
long shmem_swap(long *target, long value, int pe){ return pshmem_swap(target, value, pe); }

/* 8.12: Atomic Memory fetch-and-operate Routines -- Cswap */
int shmem_int_cswap(int *target, int cond, int value, int pe){ pshmem_int_cswap(target, cond, value, pe); }
long shmem_long_cswap(long *target, long cond, long value, int pe){ pshmem_long_cswap(target, cond, value, pe); }
long long shmem_longlong_cswap(long long * target, long long cond, long long value, int pe){ pshmem_longlong_cswap(target, cond, value, pe); }

/* 8.12: Atomic Memory fetch-and-operate Routines -- Fetch and Add */
int shmem_int_fadd(int *target, int value, int pe){ return pshmem_int_fadd(target, value, pe); }
long shmem_long_fadd(long *target, long value, int pe){ return pshmem_long_fadd(target, value, pe); }
long long shmem_longlong_fadd(long long *target, long long value, int pe){ return pshmem_longlong_fadd(target, value, pe); }

/* 8.12: Atomic Memory fetch-and-operate Routines -- Fetch and Increment */
int shmem_int_finc(int *target, int pe){ return pshmem_int_finc(target, pe); }
long shmem_long_finc(long *target, int pe){ return pshmem_long_finc(target, pe); }
long long shmem_longlong_finc(long long *target, int pe){ return pshmem_longlong_finc(target, pe); }

/* 8.13: Atomic Memory Operation Routines -- Add */
void shmem_int_add(int *target, int value, int pe){ pshmem_int_add(target, value, pe); }
void shmem_long_add(long *target, long value, int pe){ pshmem_long_add(target, value, pe); }
void shmem_longlong_add(long long *target, long long value, int pe){ pshmem_longlong_add(target, value, pe); }

/* 8.13: Atomic Memory Operation Routines -- Increment */
void shmem_int_inc(int *target, int pe){ pshmem_int_inc(target, pe); }
void shmem_long_inc(long *target, int pe){ pshmem_long_inc(target, pe); }
void shmem_longlong_inc(long long *target, int pe){ pshmem_longlong_inc(target, pe); }

/* 8.14: Point-to-Point Synchronization Routines -- Wait*/
void shmem_short_wait(short *var, short value){ pshmem_short_wait(var, value); }
void shmem_int_wait(int *var, int value){ pshmem_int_wait(var, value); }
void shmem_long_wait(long *var, long value){ pshmem_long_wait(var, value); }
void shmem_longlong_wait(long long *var, long long value){ pshmem_longlong_wait(var, value); }
void shmem_wait(long *ivar, long cmp_value){ pshmem_wait(ivar, cmp_value); }

/* 8.14: Point-to-Point Synchronization Routines -- Wait Until*/
void shmem_short_wait_until(short *var, int cond, short value){ pshmem_short_wait_until(var, cond, value); }
void shmem_int_wait_until(int *var, int cond, int value){ pshmem_int_wait_until(var, cond, value); }
void shmem_long_wait_until(long *var, int cond, long value){ pshmem_long_wait_until(var, cond, value); }
void shmem_longlong_wait_until(long long *var, int cond, long long value){ pshmem_longlong_wait_until(var, cond, value); }
void shmem_wait_until(long *ivar, int cmp, long value){ pshmem_wait_until(ivar, cmp, value); }

/* 8.15: Barrier Synchronization Routines */
void shmem_barrier(int PE_start, int logPE_stride, int PE_size, long *pSync){ pshmem_barrier(PE_start, logPE_stride, PE_size, pSync); }
void shmem_barrier_all(void){ pshmem_barrier_all(); }

void shmem_quiet(void){ pshmem_quiet(); }
void shmem_fence(void){ pshmem_fence(); }

/* 8.16: Reduction Routines */
void shmem_short_and_to_all(short *target, short *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size,
                            short *pWrk, long *pSync);
void pshmem_short_and_to_all(short *target, short *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size,
                            short *pWrk, long *pSync);
void shmem_int_and_to_all(int *target, int *source, int nreduce,
                          int PE_start, int logPE_stride, int PE_size,
                          int *pWrk, long *pSync);
void pshmem_int_and_to_all(int *target, int *source, int nreduce,
                          int PE_start, int logPE_stride, int PE_size,
                          int *pWrk, long *pSync);
void shmem_long_and_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size,
                           long *pWrk, long *pSync);
void pshmem_long_and_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size,
                           long *pWrk, long *pSync);
void shmem_longlong_and_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);
void pshmem_longlong_and_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);


void shmem_short_or_to_all(short *target, short *source, int nreduce, 
                           int PE_start, int logPE_stride, int PE_size, 
                           short *pWrk, long *pSync);
void pshmem_short_or_to_all(short *target, short *source, int nreduce, 
                           int PE_start, int logPE_stride, int PE_size, 
                           short *pWrk, long *pSync);
void shmem_int_or_to_all(int *target, int *source, int nreduce, 
                         int PE_start, int logPE_stride, int PE_size, 
                         int *pWrk, long *pSync);
void pshmem_int_or_to_all(int *target, int *source, int nreduce, 
                         int PE_start, int logPE_stride, int PE_size, 
                         int *pWrk, long *pSync);
void shmem_long_or_to_all(long *target, long *source, int nreduce,
                          int PE_start, int logPE_stride, int PE_size, 
                          long *pWrk, long *pSync);
void pshmem_long_or_to_all(long *target, long *source, int nreduce,
                          int PE_start, int logPE_stride, int PE_size, 
                          long *pWrk, long *pSync);
void shmem_longlong_or_to_all(long long *target, long long *source,
                              int nreduce, int PE_start, int logPE_stride,
                              int PE_size, long long *pWrk, long *pSync);
void pshmem_longlong_or_to_all(long long *target, long long *source,
                              int nreduce, int PE_start, int logPE_stride,
                              int PE_size, long long *pWrk, long *pSync);

void shmem_short_xor_to_all(short *target, short *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size, 
                            short *pWrk, long *pSync);
void pshmem_short_xor_to_all(short *target, short *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size, 
                            short *pWrk, long *pSync);
void shmem_int_xor_to_all(int *target, int *source, int nreduce, 
                          int PE_start, int logPE_stride, int PE_size, 
                          int *pWrk, long *pSync);
void pshmem_int_xor_to_all(int *target, int *source, int nreduce, 
                          int PE_start, int logPE_stride, int PE_size, 
                          int *pWrk, long *pSync);
void shmem_long_xor_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size, 
                           long *pWrk, long *pSync);
void pshmem_long_xor_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size, 
                           long *pWrk, long *pSync);
void shmem_longlong_xor_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);
void pshmem_longlong_xor_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);

void shmem_float_min_to_all(float *target, float *source, int nreduce, 
                            int PE_start, int logPE_stride, int PE_size, 
                            float *pWrk, long *pSync);
void pshmem_float_min_to_all(float *target, float *source, int nreduce, 
                            int PE_start, int logPE_stride, int PE_size, 
                            float *pWrk, long *pSync);
void shmem_double_min_to_all(double *target, double *source, int nreduce,
                             int PE_start, int logPE_stride, int PE_size, 
                             double *pWrk, long *pSync);
void pshmem_double_min_to_all(double *target, double *source, int nreduce,
                             int PE_start, int logPE_stride, int PE_size, 
                             double *pWrk, long *pSync);
void shmem_longdouble_min_to_all(long double *target, long double *source,
                                 int nreduce, int PE_start, int logPE_stride,
                                 int PE_size, long double *pWrk, long *pSync);
void pshmem_longdouble_min_to_all(long double *target, long double *source,
                                 int nreduce, int PE_start, int logPE_stride,
                                 int PE_size, long double *pWrk, long *pSync);
void shmem_short_min_to_all(short *target, short *source, int nreduce, 
                            int PE_start, int logPE_stride, int PE_size, 
                            short *pWrk, long *pSync);
void pshmem_short_min_to_all(short *target, short *source, int nreduce, 
                            int PE_start, int logPE_stride, int PE_size, 
                            short *pWrk, long *pSync);
void shmem_int_min_to_all(int *target, int *source, int nreduce, 
                          int PE_start, int logPE_stride, int PE_size, 
                          int *pWrk, long *pSync);
void pshmem_int_min_to_all(int *target, int *source, int nreduce, 
                          int PE_start, int logPE_stride, int PE_size, 
                          int *pWrk, long *pSync);
void shmem_long_min_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size, 
                           long *pWrk, long *pSync);
void pshmem_long_min_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size, 
                           long *pWrk, long *pSync);
void shmem_longlong_min_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);
void pshmem_longlong_min_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);

void shmem_float_max_to_all(float *target, float *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size,
                            float *pWrk, long *pSync);
void pshmem_float_max_to_all(float *target, float *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size,
                            float *pWrk, long *pSync);
void shmem_double_max_to_all(double *target, double *source, int nreduce,
                             int PE_start, int logPE_stride, int PE_size,
                             double *pWrk, long *pSync);
void pshmem_double_max_to_all(double *target, double *source, int nreduce,
                             int PE_start, int logPE_stride, int PE_size,
                             double *pWrk, long *pSync);
void shmem_longdouble_max_to_all(long double *target, long double *source,
                                 int nreduce, int PE_start, int logPE_stride,
                                 int PE_size, long double *pWrk, long *pSync);
void pshmem_longdouble_max_to_all(long double *target, long double *source,
                                 int nreduce, int PE_start, int logPE_stride,
                                 int PE_size, long double *pWrk, long *pSync);
void shmem_short_max_to_all(short *target, short *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size,
                            short *pWrk, long *pSync);
void pshmem_short_max_to_all(short *target, short *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size,
                            short *pWrk, long *pSync);
void shmem_int_max_to_all(int *target, int *source, int nreduce,
                          int PE_start, int logPE_stride, int PE_size,
                          int *pWrk, long *pSync);
void pshmem_int_max_to_all(int *target, int *source, int nreduce,
                          int PE_start, int logPE_stride, int PE_size,
                          int *pWrk, long *pSync);
void shmem_long_max_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size,
                           long *pWrk, long *pSync);
void pshmem_long_max_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size,
                           long *pWrk, long *pSync);
void shmem_longlong_max_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);
void pshmem_longlong_max_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);

void shmem_float_sum_to_all(float *target, float *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size, 
                            float *pWrk, long *pSync);
void pshmem_float_sum_to_all(float *target, float *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size, 
                            float *pWrk, long *pSync);
void shmem_double_sum_to_all(double *target, double *source, int nreduce, 
                             int PE_start, int logPE_stride, int PE_size, 
                             double *pWrk, long *pSync);
void pshmem_double_sum_to_all(double *target, double *source, int nreduce, 
                             int PE_start, int logPE_stride, int PE_size, 
                             double *pWrk, long *pSync);
void shmem_longdouble_sum_to_all(long double *target, long double *source,
                                 int nreduce, int PE_start, int logPE_stride,
                                 int PE_size, long double *pWrk, long *pSync);
void pshmem_longdouble_sum_to_all(long double *target, long double *source,
                                 int nreduce, int PE_start, int logPE_stride,
                                 int PE_size, long double *pWrk, long *pSync);
void shmem_complexf_sum_to_all(float complex *target, float complex *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, float complex *pWrk, long *pSync);
void pshmem_complexf_sum_to_all(float complex *target, float complex *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, float complex *pWrk, long *pSync);
void shmem_complexd_sum_to_all(double complex *target, double complex *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, double complex *pWrk, long *pSync);
void pshmem_complexd_sum_to_all(double complex *target, double complex *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, double complex *pWrk, long *pSync);
void shmem_short_sum_to_all(short *target, short *source, int nreduce, 
                            int PE_start, int logPE_stride, int PE_size, 
                            short *pWrk, long *pSync);
void pshmem_short_sum_to_all(short *target, short *source, int nreduce, 
                            int PE_start, int logPE_stride, int PE_size, 
                            short *pWrk, long *pSync);
void shmem_int_sum_to_all(int *target, int *source, int nreduce, 
                          int PE_start, int logPE_stride, int PE_size, 
                          int *pWrk, long *pSync);
void pshmem_int_sum_to_all(int *target, int *source, int nreduce, 
                          int PE_start, int logPE_stride, int PE_size, 
                          int *pWrk, long *pSync);
void shmem_long_sum_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size, 
                           long *pWrk, long *pSync);
void pshmem_long_sum_to_all(long *target, long *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size, 
                           long *pWrk, long *pSync);
void shmem_longlong_sum_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);
void pshmem_longlong_sum_to_all(long long *target, long long *source,
                               int nreduce, int PE_start, int logPE_stride,
                               int PE_size, long long *pWrk, long *pSync);
void shmem_float_prod_to_all(float *target, float *source, int nreduce, 
                             int PE_start, int logPE_stride, int PE_size, 
                             float *pWrk, long *pSync);
void pshmem_float_prod_to_all(float *target, float *source, int nreduce, 
                             int PE_start, int logPE_stride, int PE_size, 
                             float *pWrk, long *pSync);
void shmem_double_prod_to_all(double *target, double *source, int nreduce, 
                              int PE_start, int logPE_stride, int PE_size,
                              double *pWrk, long *pSync);
void pshmem_double_prod_to_all(double *target, double *source, int nreduce, 
                              int PE_start, int logPE_stride, int PE_size,
                              double *pWrk, long *pSync);
void shmem_longdouble_prod_to_all(long double *target, long double *source,
                                  int nreduce, int PE_start, int logPE_stride,
                                  int PE_size, long double *pWrk, long *pSync);
void pshmem_longdouble_prod_to_all(long double *target, long double *source,
                                  int nreduce, int PE_start, int logPE_stride,
                                  int PE_size, long double *pWrk, long *pSync);
void shmem_complexf_prod_to_all(float complex *target, float complex *source,
                                int nreduce, int PE_start, int logPE_stride,
                                int PE_size, float complex *pWrk, long *pSync);
void pshmem_complexf_prod_to_all(float complex *target, float complex *source,
                                int nreduce, int PE_start, int logPE_stride,
                                int PE_size, float complex *pWrk, long *pSync);
void shmem_complexd_prod_to_all(double complex *target, 
                                double complex *source, int nreduce, 
                                int PE_start, int logPE_stride, int PE_size, 
                                double complex *pWrk, long *pSync);
void pshmem_complexd_prod_to_all(double complex *target, 
                                double complex *source, int nreduce, 
                                int PE_start, int logPE_stride, int PE_size, 
                                double complex *pWrk, long *pSync);
void shmem_short_prod_to_all(short *target, short *source, int nreduce, 
                             int PE_start, int logPE_stride, int PE_size, 
                             short *pWrk, long *pSync);
void pshmem_short_prod_to_all(short *target, short *source, int nreduce, 
                             int PE_start, int logPE_stride, int PE_size, 
                             short *pWrk, long *pSync);
void shmem_int_prod_to_all(int *target, int *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size, 
                           int *pWrk, long *pSync);
void pshmem_int_prod_to_all(int *target, int *source, int nreduce,
                           int PE_start, int logPE_stride, int PE_size, 
                           int *pWrk, long *pSync);
void shmem_long_prod_to_all(long *target, long *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size, 
                            long *pWrk, long *pSync);
void pshmem_long_prod_to_all(long *target, long *source, int nreduce,
                            int PE_start, int logPE_stride, int PE_size, 
                            long *pWrk, long *pSync);
void shmem_longlong_prod_to_all(long long *target, long long *source,
                                int nreduce, int PE_start, int logPE_stride,
                                int PE_size, long long *pWrk, long *pSync);
void pshmem_longlong_prod_to_all(long long *target, long long *source,
                                int nreduce, int PE_start, int logPE_stride,
                                int PE_size, long long *pWrk, long *pSync);

/* 8.17: Collect Routines */
void shmem_collect32(void *target, const void *source, size_t nlong,
                     int PE_start, int logPE_stride, int PE_size, long *pSync);
void pshmem_collect32(void *target, const void *source, size_t nlong,
                      int PE_start, int logPE_stride, int PE_size, long *pSync);
void shmem_collect64(void *target, const void *source, size_t nlong,
                     int PE_start, int logPE_stride, int PE_size, long *pSync);
void pshmem_collect64(void *target, const void *source, size_t nlong,
                      int PE_start, int logPE_stride, int PE_size, long *pSync);
void shmem_fcollect32(void *target, const void *source, size_t nlong,
                      int PE_start, int logPE_stride, int PE_size,
                      long *pSync);
void pshmem_fcollect32(void *target, const void *source, size_t nlong,
                       int PE_start, int logPE_stride, int PE_size,
                       long *pSync);
void shmem_fcollect64(void *target, const void *source, size_t nlong,
                      int PE_start, int logPE_stride, int PE_size,
                      long *pSync);
void pshmem_fcollect64(void *target, const void *source, size_t nlong,
                       int PE_start, int logPE_stride, int PE_size,
                       long *pSync);

/* 8.18: Broadcast Routines */
void shmem_broadcast32(void *target, const void *source, size_t nlong, 
                       int PE_root, int PE_start, int logPE_stride,
                       int PE_size, long *pSync){
    pshmem_broadcast32(target, source, nlong, PE_root,PE_start, logPE_stride, PE_size, pSync);
}
void shmem_broadcast64(void *target, const void *source, size_t nlong,
                       int PE_root, int PE_start, int logPE_stride,
                       int PE_size, long *pSync){ 
    pshmem_broadcast64(target, source, nlong, PE_root,PE_start, logPE_stride, PE_size, pSync);
}

/* 8.19: Lock Routines */
void shmem_set_lock(long *lock){ pshmem_set_lock(lock); }
void shmem_clear_lock(long *lock){ pshmem_clear_lock(lock); }
int shmem_test_lock(long *lock){ return pshmem_test_lock(lock); }

/* A.1: Cache Management Routines (deprecated) */
#if DEPRECATED
void shmem_set_cache_inv(void) { pshmem_set_cache_inv(); }
void shmem_set_cache_line_inv(void *target){ pshmem_set_cache_line_inv(target); }
void shmem_clear_cache_inv(void){ pshmem_clear_cache_inv(); }
void shmem_clear_cache_line_inv(void *target){ pshmem_clear_cache_line_inv(target); }
void shmem_udcflush(void){ pshmem_udcflush(); }
void shmem_udcflush_line(void *target){ pshmem_udcflush_line(target); }
#endif

 /* Portals extensions */
double shmem_wtime(void){ return pshmem_wtime(); }
char* shmem_nodename(void){ return pshmem_nodename(); }

#if 0 // NOT IMPLEMENTED IN MPI-3 VERSION
/* Signalling puts */
void shmem_putmem_ct(pshmem_ct_t ct, void *target, const void *source, size_t len, int pe);
void pshmem_putmem_ct(pshmem_ct_t ct, void *target, const void *source, size_t len, int pe);
void shmem_ct_create(pshmem_ct_t *ct);
void pshmem_ct_create(pshmem_ct_t *ct);
void shmem_ct_free(pshmem_ct_t *ct);
void pshmem_ct_free(pshmem_ct_t *ct);
long shmem_ct_get(pshmem_ct_t ct);
long pshmem_ct_get(pshmem_ct_t ct);
void shmem_ct_set(pshmem_ct_t ct, long value);
void pshmem_ct_set(pshmem_ct_t ct, long value);
void shmem_ct_wait(pshmem_ct_t ct, long wait_for);
void pshmem_ct_wait(pshmem_ct_t ct, long wait_for);
#endif
