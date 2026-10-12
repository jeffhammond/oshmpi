/* OpenSHMEM 1.5/1.6: teams, team contexts, and team collectives.
 * Included from shmem.h inside its extern "C" block, after shmem14.h. */

#ifndef OSHMPI_SHMEM15_TEAM_H
#define OSHMPI_SHMEM15_TEAM_H

/* ---- team handles and configuration ------------------------------------ */

typedef struct oshmpi_team_s * shmem_team_t;

typedef struct {
    int num_contexts;
} shmem_team_config_t;

extern struct oshmpi_team_s oshmpi_team_world_obj;
extern struct oshmpi_team_s oshmpi_team_shared_obj;

#define SHMEM_TEAM_INVALID ((shmem_team_t) 0)
#define SHMEM_TEAM_WORLD   (&oshmpi_team_world_obj)
#define SHMEM_TEAM_SHARED  (&oshmpi_team_shared_obj)

#define SHMEM_TEAM_NUM_CONTEXTS (1L<<0)

int   shmem_team_my_pe(shmem_team_t team);
int   shmem_team_n_pes(shmem_team_t team);
int   shmem_team_get_config(shmem_team_t team, long config_mask, shmem_team_config_t * config);
int   shmem_team_translate_pe(shmem_team_t src_team, int src_pe, shmem_team_t dest_team);
int   shmem_team_split_strided(shmem_team_t parent_team, int start, int stride, int size,
                               const shmem_team_config_t * config, long config_mask,
                               shmem_team_t * new_team);
int   shmem_team_split_2d(shmem_team_t parent_team, int xrange,
                          const shmem_team_config_t * xaxis_config, long xaxis_mask,
                          shmem_team_t * xaxis_team,
                          const shmem_team_config_t * yaxis_config, long yaxis_mask,
                          shmem_team_t * yaxis_team);
void  shmem_team_destroy(shmem_team_t team);
void *shmem_team_ptr(shmem_team_t team, const void * dest, int pe);
int   shmem_team_sync(shmem_team_t team);

/* contexts created from a team */
int   shmem_team_create_ctx(shmem_team_t team, long options, shmem_ctx_t * ctx);
int   shmem_ctx_get_team(shmem_ctx_t ctx, shmem_team_t * team);

/* Internal hooks.
 *
 * oshmpi_team_init creates the predefined teams (collective; it is also called lazily by every
 * routine here).  oshmpi_ctx_pe turns a PE number relative to a context's team into a world
 * PE number; it returns pe unchanged for contexts that were not created from a team. */
void  oshmpi_team_init(void);
void  oshmpi_team_finalize(void);
int   oshmpi_ctx_pe(shmem_ctx_t ctx, int pe);

/* ---- team-based collectives: standard RMA types ------------------------- */

#define OSHMPI_TEAM_COLL_DECL(N, T)                                                                \
    int shmem_##N##_broadcast(shmem_team_t team, T * dest, const T * source, size_t nelems,        \
                              int PE_root);                                                        \
    int shmem_##N##_collect(shmem_team_t team, T * dest, const T * source, size_t nelems);         \
    int shmem_##N##_fcollect(shmem_team_t team, T * dest, const T * source, size_t nelems);        \
    int shmem_##N##_alltoall(shmem_team_t team, T * dest, const T * source, size_t nelems);        \
    int shmem_##N##_alltoalls(shmem_team_t team, T * dest, const T * source, ptrdiff_t dst,        \
                              ptrdiff_t sst, size_t nelems);
OSHMPI_RMA_TYPES(OSHMPI_TEAM_COLL_DECL)

int shmem_broadcastmem(shmem_team_t team, void * dest, const void * source, size_t nelems, int PE_root);
int shmem_collectmem(shmem_team_t team, void * dest, const void * source, size_t nelems);
int shmem_fcollectmem(shmem_team_t team, void * dest, const void * source, size_t nelems);
int shmem_alltoallmem(shmem_team_t team, void * dest, const void * source, size_t nelems);
int shmem_alltoallsmem(shmem_team_t team, void * dest, const void * source, ptrdiff_t dst,
                       ptrdiff_t sst, size_t nelems);

/* ---- team-based reductions ----------------------------------------------- */

/* integer types that support AND, OR and XOR */
#define OSHMPI_TEAM_RED_BIT_TYPES(X) \
    X(uchar,     unsigned char)      \
    X(ushort,    unsigned short)     \
    X(uint,      unsigned int)       \
    X(ulong,     unsigned long)      \
    X(ulonglong, unsigned long long) \
    X(int8,      int8_t)             \
    X(int16,     int16_t)            \
    X(int32,     int32_t)            \
    X(int64,     int64_t)            \
    X(uint8,     uint8_t)            \
    X(uint16,    uint16_t)           \
    X(uint32,    uint32_t)           \
    X(uint64,    uint64_t)           \
    X(size,      size_t)

/* types that support MAX and MIN */
#define OSHMPI_TEAM_RED_MINMAX_TYPES(X) \
    OSHMPI_TEAM_RED_BIT_TYPES(X)        \
    X(char,       char)                 \
    X(schar,      signed char)          \
    X(short,      short)                \
    X(int,        int)                  \
    X(long,       long)                 \
    X(longlong,   long long)            \
    X(ptrdiff,    ptrdiff_t)            \
    X(float,      float)                \
    X(double,     double)               \
    X(longdouble, long double)

/* types that support SUM and PROD */
#define OSHMPI_TEAM_RED_SUMPROD_TYPES(X) \
    OSHMPI_TEAM_RED_MINMAX_TYPES(X)      \
    X(complexd,   double _Complex)       \
    X(complexf,   float _Complex)

#define OSHMPI_TEAM_RED_DECL_BIT(N, T)                                                             \
    int shmem_##N##_and_reduce(shmem_team_t team, T * dest, const T * source, size_t nreduce);     \
    int shmem_##N##_or_reduce(shmem_team_t team, T * dest, const T * source, size_t nreduce);      \
    int shmem_##N##_xor_reduce(shmem_team_t team, T * dest, const T * source, size_t nreduce);
#define OSHMPI_TEAM_RED_DECL_MINMAX(N, T)                                                          \
    int shmem_##N##_max_reduce(shmem_team_t team, T * dest, const T * source, size_t nreduce);     \
    int shmem_##N##_min_reduce(shmem_team_t team, T * dest, const T * source, size_t nreduce);
#define OSHMPI_TEAM_RED_DECL_SUMPROD(N, T)                                                         \
    int shmem_##N##_sum_reduce(shmem_team_t team, T * dest, const T * source, size_t nreduce);     \
    int shmem_##N##_prod_reduce(shmem_team_t team, T * dest, const T * source, size_t nreduce);
OSHMPI_TEAM_RED_BIT_TYPES(OSHMPI_TEAM_RED_DECL_BIT)
OSHMPI_TEAM_RED_MINMAX_TYPES(OSHMPI_TEAM_RED_DECL_MINMAX)
OSHMPI_TEAM_RED_SUMPROD_TYPES(OSHMPI_TEAM_RED_DECL_SUMPROD)

/* 1.6 inclusive and exclusive prefix sums */
#define OSHMPI_TEAM_SCAN_DECL(N, T)                                                                \
    int shmem_##N##_sum_inscan(shmem_team_t team, T * dest, const T * source, size_t nelems);      \
    int shmem_##N##_sum_exscan(shmem_team_t team, T * dest, const T * source, size_t nelems);
OSHMPI_TEAM_RED_SUMPROD_TYPES(OSHMPI_TEAM_SCAN_DECL)

/* ---- C11 type-generic bindings -------------------------------------------- */

#if !defined(__cplusplus) && !defined(SHMEM_INTERNAL_INCLUDE)

/* shmem_sync(team) is the 1.5 form; the 1.4 active-set routine shmem_sync(PE_start,
 * logPE_stride, PE_size, pSync) keeps working: the number of arguments decides.  The
 * parentheses stop the macro from expanding to itself. */
#define OSHMPI_SYNC_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define shmem_sync(...) \
    OSHMPI_SYNC_SEL(__VA_ARGS__, (shmem_sync), 0, 0, shmem_team_sync, 0)(__VA_ARGS__)

/* BEGIN GENERATED by tools/gen-shmem15-team.py */
#define shmem_broadcast(team, dest, source, nelems, PE_root) \
    _Generic(&(dest)[0],  \
             float *: shmem_float_broadcast,  \
             double *: shmem_double_broadcast,  \
             long double *: shmem_longdouble_broadcast,  \
             char *: shmem_char_broadcast,  \
             signed char *: shmem_schar_broadcast,  \
             short *: shmem_short_broadcast,  \
             int *: shmem_int_broadcast,  \
             long *: shmem_long_broadcast,  \
             long long *: shmem_longlong_broadcast,  \
             unsigned char *: shmem_uchar_broadcast,  \
             unsigned short *: shmem_ushort_broadcast,  \
             unsigned int *: shmem_uint_broadcast,  \
             unsigned long *: shmem_ulong_broadcast,  \
             unsigned long long *: shmem_ulonglong_broadcast  \
            )(team, dest, source, nelems, PE_root)

#define shmem_collect(team, dest, source, nelems) \
    _Generic(&(dest)[0],  \
             float *: shmem_float_collect,  \
             double *: shmem_double_collect,  \
             long double *: shmem_longdouble_collect,  \
             char *: shmem_char_collect,  \
             signed char *: shmem_schar_collect,  \
             short *: shmem_short_collect,  \
             int *: shmem_int_collect,  \
             long *: shmem_long_collect,  \
             long long *: shmem_longlong_collect,  \
             unsigned char *: shmem_uchar_collect,  \
             unsigned short *: shmem_ushort_collect,  \
             unsigned int *: shmem_uint_collect,  \
             unsigned long *: shmem_ulong_collect,  \
             unsigned long long *: shmem_ulonglong_collect  \
            )(team, dest, source, nelems)

#define shmem_fcollect(team, dest, source, nelems) \
    _Generic(&(dest)[0],  \
             float *: shmem_float_fcollect,  \
             double *: shmem_double_fcollect,  \
             long double *: shmem_longdouble_fcollect,  \
             char *: shmem_char_fcollect,  \
             signed char *: shmem_schar_fcollect,  \
             short *: shmem_short_fcollect,  \
             int *: shmem_int_fcollect,  \
             long *: shmem_long_fcollect,  \
             long long *: shmem_longlong_fcollect,  \
             unsigned char *: shmem_uchar_fcollect,  \
             unsigned short *: shmem_ushort_fcollect,  \
             unsigned int *: shmem_uint_fcollect,  \
             unsigned long *: shmem_ulong_fcollect,  \
             unsigned long long *: shmem_ulonglong_fcollect  \
            )(team, dest, source, nelems)

#define shmem_alltoall(team, dest, source, nelems) \
    _Generic(&(dest)[0],  \
             float *: shmem_float_alltoall,  \
             double *: shmem_double_alltoall,  \
             long double *: shmem_longdouble_alltoall,  \
             char *: shmem_char_alltoall,  \
             signed char *: shmem_schar_alltoall,  \
             short *: shmem_short_alltoall,  \
             int *: shmem_int_alltoall,  \
             long *: shmem_long_alltoall,  \
             long long *: shmem_longlong_alltoall,  \
             unsigned char *: shmem_uchar_alltoall,  \
             unsigned short *: shmem_ushort_alltoall,  \
             unsigned int *: shmem_uint_alltoall,  \
             unsigned long *: shmem_ulong_alltoall,  \
             unsigned long long *: shmem_ulonglong_alltoall  \
            )(team, dest, source, nelems)

#define shmem_alltoalls(team, dest, source, dst, sst, nelems) \
    _Generic(&(dest)[0],  \
             float *: shmem_float_alltoalls,  \
             double *: shmem_double_alltoalls,  \
             long double *: shmem_longdouble_alltoalls,  \
             char *: shmem_char_alltoalls,  \
             signed char *: shmem_schar_alltoalls,  \
             short *: shmem_short_alltoalls,  \
             int *: shmem_int_alltoalls,  \
             long *: shmem_long_alltoalls,  \
             long long *: shmem_longlong_alltoalls,  \
             unsigned char *: shmem_uchar_alltoalls,  \
             unsigned short *: shmem_ushort_alltoalls,  \
             unsigned int *: shmem_uint_alltoalls,  \
             unsigned long *: shmem_ulong_alltoalls,  \
             unsigned long long *: shmem_ulonglong_alltoalls  \
            )(team, dest, source, dst, sst, nelems)

#if __SIZEOF_LONG__ == 8
#define shmem_and_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_and_reduce,  \
             unsigned short *: shmem_ushort_and_reduce,  \
             unsigned int *: shmem_uint_and_reduce,  \
             unsigned long *: shmem_ulong_and_reduce,  \
             unsigned long long *: shmem_ulonglong_and_reduce,  \
             signed char *: shmem_int8_and_reduce,  \
             short *: shmem_int16_and_reduce,  \
             int *: shmem_int32_and_reduce,  \
             long long *: shmem_int64_and_reduce,  \
             long *: shmem_int64_and_reduce  \
            )(team, dest, source, nreduce)
#else
#define shmem_and_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_and_reduce,  \
             unsigned short *: shmem_ushort_and_reduce,  \
             unsigned int *: shmem_uint_and_reduce,  \
             unsigned long *: shmem_ulong_and_reduce,  \
             unsigned long long *: shmem_ulonglong_and_reduce,  \
             signed char *: shmem_int8_and_reduce,  \
             short *: shmem_int16_and_reduce,  \
             int *: shmem_int32_and_reduce,  \
             long long *: shmem_int64_and_reduce,  \
             long *: shmem_int32_and_reduce  \
            )(team, dest, source, nreduce)
#endif

#if __SIZEOF_LONG__ == 8
#define shmem_or_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_or_reduce,  \
             unsigned short *: shmem_ushort_or_reduce,  \
             unsigned int *: shmem_uint_or_reduce,  \
             unsigned long *: shmem_ulong_or_reduce,  \
             unsigned long long *: shmem_ulonglong_or_reduce,  \
             signed char *: shmem_int8_or_reduce,  \
             short *: shmem_int16_or_reduce,  \
             int *: shmem_int32_or_reduce,  \
             long long *: shmem_int64_or_reduce,  \
             long *: shmem_int64_or_reduce  \
            )(team, dest, source, nreduce)
#else
#define shmem_or_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_or_reduce,  \
             unsigned short *: shmem_ushort_or_reduce,  \
             unsigned int *: shmem_uint_or_reduce,  \
             unsigned long *: shmem_ulong_or_reduce,  \
             unsigned long long *: shmem_ulonglong_or_reduce,  \
             signed char *: shmem_int8_or_reduce,  \
             short *: shmem_int16_or_reduce,  \
             int *: shmem_int32_or_reduce,  \
             long long *: shmem_int64_or_reduce,  \
             long *: shmem_int32_or_reduce  \
            )(team, dest, source, nreduce)
#endif

#if __SIZEOF_LONG__ == 8
#define shmem_xor_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_xor_reduce,  \
             unsigned short *: shmem_ushort_xor_reduce,  \
             unsigned int *: shmem_uint_xor_reduce,  \
             unsigned long *: shmem_ulong_xor_reduce,  \
             unsigned long long *: shmem_ulonglong_xor_reduce,  \
             signed char *: shmem_int8_xor_reduce,  \
             short *: shmem_int16_xor_reduce,  \
             int *: shmem_int32_xor_reduce,  \
             long long *: shmem_int64_xor_reduce,  \
             long *: shmem_int64_xor_reduce  \
            )(team, dest, source, nreduce)
#else
#define shmem_xor_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_xor_reduce,  \
             unsigned short *: shmem_ushort_xor_reduce,  \
             unsigned int *: shmem_uint_xor_reduce,  \
             unsigned long *: shmem_ulong_xor_reduce,  \
             unsigned long long *: shmem_ulonglong_xor_reduce,  \
             signed char *: shmem_int8_xor_reduce,  \
             short *: shmem_int16_xor_reduce,  \
             int *: shmem_int32_xor_reduce,  \
             long long *: shmem_int64_xor_reduce,  \
             long *: shmem_int32_xor_reduce  \
            )(team, dest, source, nreduce)
#endif

#define shmem_max_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_max_reduce,  \
             unsigned short *: shmem_ushort_max_reduce,  \
             unsigned int *: shmem_uint_max_reduce,  \
             unsigned long *: shmem_ulong_max_reduce,  \
             unsigned long long *: shmem_ulonglong_max_reduce,  \
             char *: shmem_char_max_reduce,  \
             signed char *: shmem_schar_max_reduce,  \
             short *: shmem_short_max_reduce,  \
             int *: shmem_int_max_reduce,  \
             long *: shmem_long_max_reduce,  \
             long long *: shmem_longlong_max_reduce,  \
             float *: shmem_float_max_reduce,  \
             double *: shmem_double_max_reduce,  \
             long double *: shmem_longdouble_max_reduce  \
            )(team, dest, source, nreduce)

#define shmem_min_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_min_reduce,  \
             unsigned short *: shmem_ushort_min_reduce,  \
             unsigned int *: shmem_uint_min_reduce,  \
             unsigned long *: shmem_ulong_min_reduce,  \
             unsigned long long *: shmem_ulonglong_min_reduce,  \
             char *: shmem_char_min_reduce,  \
             signed char *: shmem_schar_min_reduce,  \
             short *: shmem_short_min_reduce,  \
             int *: shmem_int_min_reduce,  \
             long *: shmem_long_min_reduce,  \
             long long *: shmem_longlong_min_reduce,  \
             float *: shmem_float_min_reduce,  \
             double *: shmem_double_min_reduce,  \
             long double *: shmem_longdouble_min_reduce  \
            )(team, dest, source, nreduce)

#define shmem_sum_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_sum_reduce,  \
             unsigned short *: shmem_ushort_sum_reduce,  \
             unsigned int *: shmem_uint_sum_reduce,  \
             unsigned long *: shmem_ulong_sum_reduce,  \
             unsigned long long *: shmem_ulonglong_sum_reduce,  \
             char *: shmem_char_sum_reduce,  \
             signed char *: shmem_schar_sum_reduce,  \
             short *: shmem_short_sum_reduce,  \
             int *: shmem_int_sum_reduce,  \
             long *: shmem_long_sum_reduce,  \
             long long *: shmem_longlong_sum_reduce,  \
             float *: shmem_float_sum_reduce,  \
             double *: shmem_double_sum_reduce,  \
             long double *: shmem_longdouble_sum_reduce,  \
             double _Complex *: shmem_complexd_sum_reduce,  \
             float _Complex *: shmem_complexf_sum_reduce  \
            )(team, dest, source, nreduce)

#define shmem_prod_reduce(team, dest, source, nreduce) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_prod_reduce,  \
             unsigned short *: shmem_ushort_prod_reduce,  \
             unsigned int *: shmem_uint_prod_reduce,  \
             unsigned long *: shmem_ulong_prod_reduce,  \
             unsigned long long *: shmem_ulonglong_prod_reduce,  \
             char *: shmem_char_prod_reduce,  \
             signed char *: shmem_schar_prod_reduce,  \
             short *: shmem_short_prod_reduce,  \
             int *: shmem_int_prod_reduce,  \
             long *: shmem_long_prod_reduce,  \
             long long *: shmem_longlong_prod_reduce,  \
             float *: shmem_float_prod_reduce,  \
             double *: shmem_double_prod_reduce,  \
             long double *: shmem_longdouble_prod_reduce,  \
             double _Complex *: shmem_complexd_prod_reduce,  \
             float _Complex *: shmem_complexf_prod_reduce  \
            )(team, dest, source, nreduce)

#define shmem_sum_inscan(team, dest, source, nelems) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_sum_inscan,  \
             unsigned short *: shmem_ushort_sum_inscan,  \
             unsigned int *: shmem_uint_sum_inscan,  \
             unsigned long *: shmem_ulong_sum_inscan,  \
             unsigned long long *: shmem_ulonglong_sum_inscan,  \
             char *: shmem_char_sum_inscan,  \
             signed char *: shmem_schar_sum_inscan,  \
             short *: shmem_short_sum_inscan,  \
             int *: shmem_int_sum_inscan,  \
             long *: shmem_long_sum_inscan,  \
             long long *: shmem_longlong_sum_inscan,  \
             float *: shmem_float_sum_inscan,  \
             double *: shmem_double_sum_inscan,  \
             long double *: shmem_longdouble_sum_inscan,  \
             double _Complex *: shmem_complexd_sum_inscan,  \
             float _Complex *: shmem_complexf_sum_inscan  \
            )(team, dest, source, nelems)

#define shmem_sum_exscan(team, dest, source, nelems) \
    _Generic(&(dest)[0],  \
             unsigned char *: shmem_uchar_sum_exscan,  \
             unsigned short *: shmem_ushort_sum_exscan,  \
             unsigned int *: shmem_uint_sum_exscan,  \
             unsigned long *: shmem_ulong_sum_exscan,  \
             unsigned long long *: shmem_ulonglong_sum_exscan,  \
             char *: shmem_char_sum_exscan,  \
             signed char *: shmem_schar_sum_exscan,  \
             short *: shmem_short_sum_exscan,  \
             int *: shmem_int_sum_exscan,  \
             long *: shmem_long_sum_exscan,  \
             long long *: shmem_longlong_sum_exscan,  \
             float *: shmem_float_sum_exscan,  \
             double *: shmem_double_sum_exscan,  \
             long double *: shmem_longdouble_sum_exscan,  \
             double _Complex *: shmem_complexd_sum_exscan,  \
             float _Complex *: shmem_complexf_sum_exscan  \
            )(team, dest, source, nelems)
/* END GENERATED */

#endif /* C11 bindings */

#endif /* OSHMPI_SHMEM15_TEAM_H */
