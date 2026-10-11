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

struct oshmpi_team_s {
    MPI_Comm comm;
    int      size;
    int      rank;          /* calling PE's number in the team */
    int *    world;         /* world PE number of each team PE */
    int      num_contexts;
    int      predefined;
};

struct oshmpi_team_s oshmpi_team_world_obj;
struct oshmpi_team_s oshmpi_team_shared_obj;

static pthread_once_t team_once = PTHREAD_ONCE_INIT;
static int            team_keyval = MPI_KEYVAL_INVALID;

/* Run at the start of MPI_Finalize (attribute on MPI_COMM_SELF): free what we created. */
static int team_finalize_cb(MPI_Comm comm, int keyval, void * attr, void * extra)
{
    (void) comm; (void) keyval; (void) attr; (void) extra;
    if (oshmpi_team_shared_obj.comm != MPI_COMM_NULL && oshmpi_team_shared_obj.comm != 0) {
        MPI_Comm_free(&oshmpi_team_shared_obj.comm);
        free(oshmpi_team_shared_obj.world);
        oshmpi_team_shared_obj.world = NULL;
    }
    free(oshmpi_team_world_obj.world);
    oshmpi_team_world_obj.world = NULL;
    return MPI_SUCCESS;
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

    MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, team_finalize_cb, &team_keyval, NULL);
    MPI_Comm_set_attr(MPI_COMM_SELF, team_keyval, NULL);
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
    MPI_Comm_free(&team->comm);
    free(team->world);
    free(team);
}

/* ---- team contexts -------------------------------------------------------- */

/* The first member matches the layout of the context object that shmem_ctx_create makes, so
 * shmem_ctx_destroy can free one of these.  The magic number tells them apart. */
struct team_ctx {
    long         options;
    uint64_t     magic;
    shmem_team_t team;
};
#define TEAM_CTX_MAGIC 0x4f5348504d495443ULL

static const struct team_ctx * as_team_ctx(shmem_ctx_t ctx)
{
    if (ctx == SHMEM_CTX_INVALID || ctx == SHMEM_CTX_DEFAULT) return NULL;
    const struct team_ctx * c = (const struct team_ctx *) ctx;
    return c->magic == TEAM_CTX_MAGIC ? c : NULL;
}

int shmem_team_create_ctx(shmem_team_t team, long options, shmem_ctx_t * ctx)
{
    oshmpi_team_init();
    *ctx = SHMEM_CTX_INVALID;
    if (team == SHMEM_TEAM_INVALID) return 1;
    struct team_ctx * c = malloc(sizeof(*c));
    if (c == NULL) return 1;
    c->options = options;
    c->magic = TEAM_CTX_MAGIC;
    c->team = team;
    *ctx = (shmem_ctx_t) c;
    return 0;
}

int shmem_ctx_get_team(shmem_ctx_t ctx, shmem_team_t * team)
{
    oshmpi_team_init();
    if (ctx == SHMEM_CTX_INVALID) { *team = SHMEM_TEAM_INVALID; return 1; }
    const struct team_ctx * c = as_team_ctx(ctx);
    *team = c ? c->team : SHMEM_TEAM_WORLD;
    return 0;
}

int oshmpi_ctx_pe(shmem_ctx_t ctx, int pe)
{
    const struct team_ctx * c = as_team_ctx(ctx);
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

static int bytes_ok(size_t bytes) { return bytes <= (size_t) INT_MAX; }

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
    if (team == SHMEM_TEAM_INVALID || !bytes_ok(bytes) || !bytes_ok(bytes * (size_t) team->size)) return 1;
    coll_begin();
    MPI_Allgather(source, (int) bytes, MPI_BYTE, dest, (int) bytes, MPI_BYTE, team->comm);
    coll_end();
    return 0;
}

static int collect_bytes(shmem_team_t team, void * dest, const void * source, size_t bytes)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID || !bytes_ok(bytes)) return 1;
    coll_begin();
    int n = team->size, mine = (int) bytes, rc = 0;
    int * counts = malloc(n * sizeof(int));
    int * displs = malloc(n * sizeof(int));
    MPI_Allgather(&mine, 1, MPI_INT, counts, 1, MPI_INT, team->comm);
    size_t total = 0;
    for (int i = 0; i < n; i++) { displs[i] = (int) total; total += (size_t) counts[i]; }
    if (!bytes_ok(total)) rc = 1;   /* every PE sees the same counts, so every PE agrees */
    else MPI_Allgatherv(source, mine, MPI_BYTE, dest, counts, displs, MPI_BYTE, team->comm);
    free(displs);
    free(counts);
    coll_end();
    return rc;
}

static int alltoall_bytes(shmem_team_t team, void * dest, const void * source, size_t bytes)
{
    oshmpi_team_init();
    if (team == SHMEM_TEAM_INVALID || !bytes_ok(bytes) || !bytes_ok(bytes * (size_t) team->size)) return 1;
    coll_begin();
    MPI_Alltoall(source, (int) bytes, MPI_BYTE, dest, (int) bytes, MPI_BYTE, team->comm);
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
    if (team == SHMEM_TEAM_INVALID || dst < 1 || sst < 1 || nelems > (size_t) INT_MAX ||
        dst > INT_MAX || sst > INT_MAX || !bytes_ok(esz))
        return 1;
    coll_begin();
    int count = (int) nelems;
    MPI_Datatype elem, tmp, stype, dtype;
    MPI_Type_contiguous((int) esz, MPI_BYTE, &elem);
    MPI_Type_commit(&elem);

    MPI_Type_vector(count, 1, (int) sst, elem, &tmp);
    MPI_Type_create_resized(tmp, 0, (MPI_Aint) count * sst * (MPI_Aint) esz, &stype);
    MPI_Type_commit(&stype);
    MPI_Type_free(&tmp);

    MPI_Type_vector(count, 1, (int) dst, elem, &tmp);
    MPI_Type_create_resized(tmp, 0, (MPI_Aint) count * dst * (MPI_Aint) esz, &dtype);
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
