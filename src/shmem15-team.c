/* OpenSHMEM 1.5/1.6: teams, team contexts, and team collectives.
 *
 * A team is an MPI communicator plus the world PE number of each member, indexed by team PE
 * number.  Predefined teams are created lazily, on first use of any routine here (see
 * oshmpi_team_init); a team made by a split is freed by shmem_team_destroy.  Team collectives
 * are MPI collectives on the team's communicator, bracketed by the same memory-completion
 * steps as shmem_barrier_all. */

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "shmem-internals.h"
#include "type_contiguous_x.h"

struct oshmpi_team_s {
    MPI_Comm comm;
    int      size;
    int      rank;          /* calling PE's number in the team */
    int *    world;         /* world PE number of each team PE */
    int      num_contexts;
    int      predefined;
    struct oshmpi_team_s * next, * prev;   /* all teams made by splits, freed at finalize if leaked */
};

struct oshmpi_team_s oshmpi_team_world_obj;
struct oshmpi_team_s oshmpi_team_shared_obj;

static pthread_once_t team_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t team_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct oshmpi_team_s * team_list;

/* Called from oshmpi_finalize, before SHMEM_COMM_WORLD is freed: free what team_init_once made. */
void oshmpi_team_finalize(void)
{
    /* Teams the program did not destroy. */
    while (team_list != NULL) shmem_team_destroy(team_list);
    if (oshmpi_team_shared_obj.comm != MPI_COMM_NULL && oshmpi_team_shared_obj.comm != 0) {
        MPI_Comm_free(&oshmpi_team_shared_obj.comm);
        free(oshmpi_team_shared_obj.world);
        oshmpi_team_shared_obj.world = NULL;
    }
    free(oshmpi_team_world_obj.world);
    oshmpi_team_world_obj.world = NULL;
    team_once = (pthread_once_t) PTHREAD_ONCE_INIT;      /* a later initialization makes them again */
}

static void team_init_once(void)
{
    struct oshmpi_team_s * w = &oshmpi_team_world_obj;
    w->comm = SHMEM_COMM_WORLD;
    w->size = shmem_world_size;
    w->rank = shmem_world_rank;
    w->world = malloc(w->size * sizeof(int));
    for (int i = 0; i < w->size; i++) w->world[i] = i;
    w->num_contexts = INT_MAX;
    w->predefined = 1;

    struct oshmpi_team_s * s = &oshmpi_team_shared_obj;
    MPI_Comm_split_type(SHMEM_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &s->comm);
    MPI_Comm_size(s->comm, &s->size);
    MPI_Comm_rank(s->comm, &s->rank);
    s->world = malloc(s->size * sizeof(int));
    int me = shmem_world_rank;
    MPI_Allgather(&me, 1, MPI_INT, s->world, 1, MPI_INT, s->comm);
    s->num_contexts = INT_MAX;
    s->predefined = 1;
}

void oshmpi_team_init(void)
{
    pthread_once(&team_once, team_init_once);
}

/* ---- queries ------------------------------------------------------------ */

int shmem_team_my_pe(shmem_team_t team)
{
    oshmpi_team_init();
    return team == SHMEM_TEAM_INVALID ? -1 : team->rank;
}

int shmem_team_n_pes(shmem_team_t team)
{
    oshmpi_team_init();
    return team == SHMEM_TEAM_INVALID ? -1 : team->size;
}

int shmem_team_get_config(shmem_team_t team, long config_mask, shmem_team_config_t * config)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID) return 1;
    if (config_mask != 0 && config != NULL && (config_mask & SHMEM_TEAM_NUM_CONTEXTS))
        config->num_contexts = team->num_contexts;
    return 0;
}

int shmem_team_translate_pe(shmem_team_t src_team, int src_pe, shmem_team_t dest_team)
{
    oshmpi_team_init();
    if (src_team == SHMEM_TEAM_INVALID || dest_team == SHMEM_TEAM_INVALID) return -1;
    if (src_pe < 0 || src_pe >= src_team->size) return -1;
    int wpe = src_team->world[src_pe];
    if (dest_team == SHMEM_TEAM_WORLD) return wpe;
    for (int i = 0; i < dest_team->size; i++)
        if (dest_team->world[i] == wpe) return i;
    return -1;
}

void * shmem_team_ptr(shmem_team_t team, const void * dest, int pe)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID || pe < 0 || pe >= team->size) return NULL;
    return shmem_ptr(dest, team->world[pe]);
}

/* ---- creation and destruction -------------------------------------------- */

static shmem_team_t team_make(MPI_Comm comm, int size, int rank, int * world,
                              const shmem_team_config_t * config, long mask)
{
    struct oshmpi_team_s * t = malloc(sizeof(*t));
    if (t == NULL) return SHMEM_TEAM_INVALID;
    t->comm = comm;
    t->size = size;
    t->rank = rank;
    t->world = world;
    t->num_contexts = (config != NULL && (mask & SHMEM_TEAM_NUM_CONTEXTS)) ? config->num_contexts : 0;
    t->predefined = 0;
    pthread_mutex_lock(&team_list_mutex);
    t->prev = NULL;
    t->next = team_list;
    if (team_list != NULL) team_list->prev = t;
    team_list = t;
    pthread_mutex_unlock(&team_list_mutex);
    return t;
}

int shmem_team_split_strided(shmem_team_t parent, int start, int stride, int size,
                             const shmem_team_config_t * config, long config_mask,
                             shmem_team_t * new_team)
{
    oshmpi_team_init();
    *new_team = SHMEM_TEAM_INVALID;
    if (parent == SHMEM_TEAM_INVALID) return 1;

    long n = parent->size;
    int valid = size > 0 && start >= 0 && start < n;
    if (valid) {
        if (stride == 0) valid = (size == 1);
        else {
            long last = (long) start + (long) stride * (size - 1);
            valid = last >= 0 && last < n;
        }
    }
    if (!valid) return 1;   /* every PE evaluates the same arguments, so every PE agrees */

    int me = parent->rank, idx = -1;
    if (stride == 0) { if (me == start) idx = 0; }
    else if ((me - start) % stride == 0) {
        long q = (me - start) / stride;
        if (q >= 0 && q < size) idx = (int) q;
    }

    int rc = 0;
    if (idx >= 0) {
        int * ranks = malloc(size * sizeof(int));
        int * world = malloc(size * sizeof(int));
        for (int i = 0; i < size; i++) { ranks[i] = start + stride * i; world[i] = parent->world[ranks[i]]; }
        MPI_Group pg, g;
        MPI_Comm_group(parent->comm, &pg);
        MPI_Group_incl(pg, size, ranks, &g);
        MPI_Comm comm;
        MPI_Comm_create_group(parent->comm, g, 0, &comm);
        MPI_Group_free(&g);
        MPI_Group_free(&pg);
        free(ranks);
        shmem_team_t t = team_make(comm, size, idx, world, config, config_mask);
        if (t == SHMEM_TEAM_INVALID) { MPI_Comm_free(&comm); free(world); rc = 1; }
        else *new_team = t;
    }
    return rc;
}

int shmem_team_split_2d(shmem_team_t parent, int xrange,
                        const shmem_team_config_t * xconfig, long xmask, shmem_team_t * xteam,
                        const shmem_team_config_t * yconfig, long ymask, shmem_team_t * yteam)
{
    oshmpi_team_init();
    *xteam = SHMEM_TEAM_INVALID;
    *yteam = SHMEM_TEAM_INVALID;
    if (parent == SHMEM_TEAM_INVALID || xrange <= 0) return 1;

    int n = parent->size, me = parent->rank;
    int x = me % xrange, y = me / xrange;
    int yrange = (n + xrange - 1) / xrange;
    (void) yrange;

    /* x-axis team: the PEs with the same y (a row); y-axis team: the PEs with the same x */
    MPI_Comm xc, yc;
    MPI_Comm_split(parent->comm, y, me, &xc);
    MPI_Comm_split(parent->comm, x, me, &yc);

    int xsize = 0, ysize = 0;
    for (int p = y * xrange; p < n && p < (y + 1) * xrange; p++) xsize++;
    for (int p = x; p < n; p += xrange) ysize++;
    int * xworld = malloc(xsize * sizeof(int));
    int * yworld = malloc(ysize * sizeof(int));
    for (int i = 0; i < xsize; i++) xworld[i] = parent->world[y * xrange + i];
    for (int i = 0; i < ysize; i++) yworld[i] = parent->world[x + i * xrange];

    shmem_team_t xt = team_make(xc, xsize, x, xworld, xconfig, xmask);
    shmem_team_t yt = team_make(yc, ysize, y, yworld, yconfig, ymask);
    if (xt == SHMEM_TEAM_INVALID || yt == SHMEM_TEAM_INVALID) {
        if (xt != SHMEM_TEAM_INVALID) free(xt);
        if (yt != SHMEM_TEAM_INVALID) free(yt);
        MPI_Comm_free(&xc); MPI_Comm_free(&yc);
        free(xworld); free(yworld);
        return 1;
    }
    *xteam = xt;
    *yteam = yt;
    return 0;
}

void shmem_team_destroy(shmem_team_t team)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID || team->predefined) return;
    pthread_mutex_lock(&team_list_mutex);
    if (team->prev != NULL) team->prev->next = team->next; else team_list = team->next;
    if (team->next != NULL) team->next->prev = team->prev;
    pthread_mutex_unlock(&team_list_mutex);
    MPI_Comm_free(&team->comm);
    free(team->world);
    free(team);
}

/* ---- team contexts -------------------------------------------------------- */

/* A team context is a struct oshmpi_ctx_s (see shmem-internals.h) with its team set, so
 * shmem_ctx_destroy frees it like any other context. */
static const struct oshmpi_ctx_s * as_team_ctx(shmem_ctx_t ctx)
{
    if (ctx == SHMEM_CTX_INVALID || ctx == SHMEM_CTX_DEFAULT) return NULL;
    const struct oshmpi_ctx_s * c = (const struct oshmpi_ctx_s *) ctx;
    return c->team != NULL ? c : NULL;
}

int shmem_team_create_ctx(shmem_team_t team, long options, shmem_ctx_t * ctx)
{
    oshmpi_team_init();
    *ctx = SHMEM_CTX_INVALID;
    if (team == SHMEM_TEAM_INVALID) return 1;
    struct oshmpi_ctx_s * c = malloc(sizeof(*c));
    if (c == NULL) return 1;
    c->options = options;
    c->team = team;
    *ctx = (shmem_ctx_t) c;
    return 0;
}

int shmem_ctx_get_team(shmem_ctx_t ctx, shmem_team_t * team)
{
    oshmpi_team_init();
    if (ctx == SHMEM_CTX_INVALID) { *team = SHMEM_TEAM_INVALID; return 1; }
    const struct oshmpi_ctx_s * c = as_team_ctx(ctx);
    *team = c ? c->team : SHMEM_TEAM_WORLD;
    return 0;
}

int oshmpi_ctx_pe(shmem_ctx_t ctx, int pe)
{
    const struct oshmpi_ctx_s * c = as_team_ctx(ctx);
    if (c == NULL || pe < 0 || pe >= c->team->size) return pe;
    return c->team->world[pe];
}

/* ---- collectives ---------------------------------------------------------- */

/* Complete this PE's outstanding RMA and fence before a collective, and fence after it, as
 * shmem_barrier_all does, so that earlier puts are visible to the collective and the
 * collective's result is visible to later loads. */
static void coll_begin(void)
{
    oshmpi_remote_sync();
    oshmpi_local_sync();
}

static void coll_end(void)
{
    oshmpi_local_sync();
}

int shmem_team_sync(shmem_team_t team)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID) return 1;
    coll_begin();
    MPI_Barrier(team->comm);
    coll_end();
    return 0;
}

/* An MPI count is an int.  Moving 'bytes' bytes per rank, when MPI may count 'span' bytes in
 * total, takes a plain count if that fits and otherwise one element of a derived type. */
static MPI_Datatype bytes_type(size_t bytes, size_t span, int * count, int * big)
{
    if (span < (size_t) INT_MAX) { *count = (int) bytes; *big = 0; return MPI_BYTE; }
    MPI_Datatype t;
    *count = 1;
    *big = 1;
    MPIX_Type_contiguous_x(bytes, MPI_BYTE, &t);
    MPI_Type_commit(&t);
    return t;
}

static int bcast_bytes(shmem_team_t team, void * dest, const void * source, size_t bytes, int root)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID || root < 0 || root >= team->size) return 1;
    coll_begin();
    if (team->rank == root && dest != source) memmove(dest, source, bytes);
    const size_t chunk = (size_t) 1 << 30;
    for (size_t off = 0; off < bytes; off += chunk) {
        size_t n = bytes - off < chunk ? bytes - off : chunk;
        MPI_Bcast((char *) dest + off, (int) n, MPI_BYTE, root, team->comm);
    }
    coll_end();
    return 0;
}

static int fcollect_bytes(shmem_team_t team, void * dest, const void * source, size_t bytes)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID) return 1;
    coll_begin();
    int count, big;
    MPI_Datatype t = bytes_type(bytes, bytes * (size_t) team->size, &count, &big);
    MPI_Allgather(source, count, t, dest, count, t, team->comm);
    if (big) MPI_Type_free(&t);
    coll_end();
    return 0;
}

static int collect_bytes(shmem_team_t team, void * dest, const void * source, size_t bytes)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID) return 1;
    coll_begin();
    oshmpi_collect_bytes(team->comm, dest, source, bytes);
    coll_end();
    return 0;
}

static int alltoall_bytes(shmem_team_t team, void * dest, const void * source, size_t bytes)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID) return 1;
    coll_begin();
    int count, big;
    MPI_Datatype t = bytes_type(bytes, bytes * (size_t) team->size, &count, &big);
    MPI_Alltoall(source, count, t, dest, count, t, team->comm);
    if (big) MPI_Type_free(&t);
    coll_end();
    return 0;
}

/* Strided all-to-all.  PE k's block for PE l is the nelems elements starting at element
 * l*nelems*sst of its source (and lands at element k*nelems*dst of l's dest): one strided
 * block whose extent spans nelems*stride elements, so consecutive blocks are adjacent. */
static int alltoalls_bytes(shmem_team_t team, void * dest, const void * source, ptrdiff_t dst,
                           ptrdiff_t sst, size_t nelems, size_t esz)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID || dst < 1 || sst < 1 || esz > (size_t) INT_MAX)
        return 1;
    coll_begin();
    MPI_Datatype elem, tmp, stype, dtype;
    MPI_Type_contiguous((int) esz, MPI_BYTE, &elem);
    MPI_Type_commit(&elem);

    OSHMPI_Type_strided_x(nelems, sst, elem, &tmp);
    MPI_Type_create_resized(tmp, 0, (MPI_Aint) nelems * sst * (MPI_Aint) esz, &stype);
    MPI_Type_commit(&stype);
    MPI_Type_free(&tmp);

    OSHMPI_Type_strided_x(nelems, dst, elem, &tmp);
    MPI_Type_create_resized(tmp, 0, (MPI_Aint) nelems * dst * (MPI_Aint) esz, &dtype);
    MPI_Type_commit(&dtype);
    MPI_Type_free(&tmp);

    MPI_Alltoall(source, 1, stype, dest, 1, dtype, team->comm);

    MPI_Type_free(&dtype);
    MPI_Type_free(&stype);
    MPI_Type_free(&elem);
    coll_end();
    return 0;
}

int shmem_broadcastmem(shmem_team_t team, void * dest, const void * source, size_t nelems, int PE_root)
{ return bcast_bytes(team, dest, source, nelems, PE_root); }
int shmem_collectmem(shmem_team_t team, void * dest, const void * source, size_t nelems)
{ return collect_bytes(team, dest, source, nelems); }
int shmem_fcollectmem(shmem_team_t team, void * dest, const void * source, size_t nelems)
{ return fcollect_bytes(team, dest, source, nelems); }
int shmem_alltoallmem(shmem_team_t team, void * dest, const void * source, size_t nelems)
{ return alltoall_bytes(team, dest, source, nelems); }
int shmem_alltoallsmem(shmem_team_t team, void * dest, const void * source, ptrdiff_t dst,
                       ptrdiff_t sst, size_t nelems)
{ return alltoalls_bytes(team, dest, source, dst, sst, nelems, 1); }

#define OSHMPI_TEAM_COLL_DEF(N, T)                                                                 \
    int shmem_##N##_broadcast(shmem_team_t team, T * dest, const T * source, size_t nelems,        \
                              int PE_root)                                                         \
    { return bcast_bytes(team, dest, source, nelems * sizeof(T), PE_root); }                       \
    int shmem_##N##_collect(shmem_team_t team, T * dest, const T * source, size_t nelems)          \
    { return collect_bytes(team, dest, source, nelems * sizeof(T)); }                              \
    int shmem_##N##_fcollect(shmem_team_t team, T * dest, const T * source, size_t nelems)         \
    { return fcollect_bytes(team, dest, source, nelems * sizeof(T)); }                             \
    int shmem_##N##_alltoall(shmem_team_t team, T * dest, const T * source, size_t nelems)         \
    { return alltoall_bytes(team, dest, source, nelems * sizeof(T)); }                             \
    int shmem_##N##_alltoalls(shmem_team_t team, T * dest, const T * source, ptrdiff_t dst,        \
                              ptrdiff_t sst, size_t nelems)                                        \
    { return alltoalls_bytes(team, dest, source, dst, sst, nelems, sizeof(T)); }
OSHMPI_RMA_TYPES(OSHMPI_TEAM_COLL_DEF)

/* ---- reductions ------------------------------------------------------------ */

static MPI_Datatype mpi_integer(size_t size, int is_signed)
{
    switch (size) {
        case 1: return is_signed ? MPI_INT8_T  : MPI_UINT8_T;
        case 2: return is_signed ? MPI_INT16_T : MPI_UINT16_T;
        case 4: return is_signed ? MPI_INT32_T : MPI_UINT32_T;
        default: return is_signed ? MPI_INT64_T : MPI_UINT64_T;
    }
}

/* the MPI datatype of each C type in the reduction tables */
#define OSHMPI_MPIT_uchar      mpi_integer(sizeof(unsigned char), 0)
#define OSHMPI_MPIT_ushort     mpi_integer(sizeof(unsigned short), 0)
#define OSHMPI_MPIT_uint       mpi_integer(sizeof(unsigned int), 0)
#define OSHMPI_MPIT_ulong      mpi_integer(sizeof(unsigned long), 0)
#define OSHMPI_MPIT_ulonglong  mpi_integer(sizeof(unsigned long long), 0)
#define OSHMPI_MPIT_int8       MPI_INT8_T
#define OSHMPI_MPIT_int16      MPI_INT16_T
#define OSHMPI_MPIT_int32      MPI_INT32_T
#define OSHMPI_MPIT_int64      MPI_INT64_T
#define OSHMPI_MPIT_uint8      MPI_UINT8_T
#define OSHMPI_MPIT_uint16     MPI_UINT16_T
#define OSHMPI_MPIT_uint32     MPI_UINT32_T
#define OSHMPI_MPIT_uint64     MPI_UINT64_T
#define OSHMPI_MPIT_size       mpi_integer(sizeof(size_t), 0)
#define OSHMPI_MPIT_char       mpi_integer(sizeof(char), (char) -1 < 0)  /* MPI_MAX is not defined on MPI_CHAR */
#define OSHMPI_MPIT_schar      mpi_integer(sizeof(signed char), 1)
#define OSHMPI_MPIT_short      mpi_integer(sizeof(short), 1)
#define OSHMPI_MPIT_int        mpi_integer(sizeof(int), 1)
#define OSHMPI_MPIT_long       mpi_integer(sizeof(long), 1)
#define OSHMPI_MPIT_longlong   mpi_integer(sizeof(long long), 1)
#define OSHMPI_MPIT_ptrdiff    mpi_integer(sizeof(ptrdiff_t), 1)
#define OSHMPI_MPIT_float      MPI_FLOAT
#define OSHMPI_MPIT_double     MPI_DOUBLE
#define OSHMPI_MPIT_longdouble MPI_LONG_DOUBLE
#define OSHMPI_MPIT_complexd   MPI_C_DOUBLE_COMPLEX
#define OSHMPI_MPIT_complexf   MPI_C_FLOAT_COMPLEX

/* dest and source must be identical (an in-place reduction) or not overlap at all */
static int reduce_impl(shmem_team_t team, void * dest, const void * source, size_t nreduce,
                       size_t esz, MPI_Datatype type, MPI_Op op)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID) return 1;
    coll_begin();
    const size_t chunk = (size_t) 1 << 28;
    size_t done = 0;
    do {
        size_t n = nreduce - done < chunk ? nreduce - done : chunk;
        char * d = (char *) dest + done * esz;
        const char * s = (const char *) source + done * esz;
        MPI_Allreduce(dest == source ? MPI_IN_PLACE : (void *) s, d, (int) n, type, op, team->comm);
        done += n;
    } while (done < nreduce);
    coll_end();
    return 0;
}

#define OSHMPI_TEAM_RED_DEF(N, T, OPNAME, MPIOP)                                                   \
    int shmem_##N##_##OPNAME##_reduce(shmem_team_t team, T * dest, const T * source, size_t nreduce) \
    { return reduce_impl(team, dest, source, nreduce, sizeof(T), OSHMPI_MPIT_##N, MPIOP); }

#define OSHMPI_TEAM_RED_DEF_BIT(N, T)                                                              \
    OSHMPI_TEAM_RED_DEF(N, T, and, MPI_BAND)                                                       \
    OSHMPI_TEAM_RED_DEF(N, T, or,  MPI_BOR)                                                        \
    OSHMPI_TEAM_RED_DEF(N, T, xor, MPI_BXOR)
#define OSHMPI_TEAM_RED_DEF_MINMAX(N, T)                                                           \
    OSHMPI_TEAM_RED_DEF(N, T, max, MPI_MAX)                                                        \
    OSHMPI_TEAM_RED_DEF(N, T, min, MPI_MIN)
#define OSHMPI_TEAM_RED_DEF_SUMPROD(N, T)                                                          \
    OSHMPI_TEAM_RED_DEF(N, T, sum,  MPI_SUM)                                                       \
    OSHMPI_TEAM_RED_DEF(N, T, prod, MPI_PROD)
OSHMPI_TEAM_RED_BIT_TYPES(OSHMPI_TEAM_RED_DEF_BIT)
OSHMPI_TEAM_RED_MINMAX_TYPES(OSHMPI_TEAM_RED_DEF_MINMAX)
OSHMPI_TEAM_RED_SUMPROD_TYPES(OSHMPI_TEAM_RED_DEF_SUMPROD)

/* ---- scans (1.6): inclusive and exclusive prefix sums ---- */

static int scan_impl(shmem_team_t team, void * dest, const void * source, size_t nelems,
                     size_t esz, MPI_Datatype type, int exclusive)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID) return 1;
    coll_begin();
    const size_t chunk = (size_t) 1 << 28;
    size_t done = 0;
    while (done < nelems) {
        size_t n = nelems - done < chunk ? nelems - done : chunk;
        char * d = (char *) dest + done * esz;
        const void * s = dest == source ? MPI_IN_PLACE : (const void *) ((const char *) source + done * esz);
        if (exclusive) {
            MPI_Exscan(s, d, (int) n, type, MPI_SUM, team->comm);
            if (team->rank == 0) memset(d, 0, n * esz);   /* MPI leaves PE 0's result undefined */
        } else {
            MPI_Scan(s, d, (int) n, type, MPI_SUM, team->comm);
        }
        done += n;
    }
    coll_end();
    return 0;
}

#define OSHMPI_TEAM_SCAN_DEF(N, T)                                                                 \
    int shmem_##N##_sum_inscan(shmem_team_t team, T * dest, const T * source, size_t nelems)       \
    { return scan_impl(team, dest, source, nelems, sizeof(T), OSHMPI_MPIT_##N, 0); }               \
    int shmem_##N##_sum_exscan(shmem_team_t team, T * dest, const T * source, size_t nelems)       \
    { return scan_impl(team, dest, source, nelems, sizeof(T), OSHMPI_MPIT_##N, 1); }
OSHMPI_TEAM_RED_SUMPROD_TYPES(OSHMPI_TEAM_SCAN_DEF)
