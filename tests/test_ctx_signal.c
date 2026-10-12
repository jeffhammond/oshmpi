/* Put-with-signal and the signal update routines through contexts.  The spec makes the target
 * PE of a ctx routine relative to the team the context was made on, so contexts on teams whose
 * numbering differs from the world's (a strided team and an even-PE team) check that the
 * data and the signal land on the right world PE and on no other.  For every context: every
 * ctx put-with-signal variant (all standard types, the sized and byte forms, blocking and
 * nonblocking) with SET and ADD, ctx_signal_set / ctx_signal_add, the C11 forms with a leading
 * context, and a ring in which a PE that sees the signal must see all of the payload.  Members
 * of the team do the work; everyone joins the barriers. */
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int errs, me, np;
static const char *cname;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s (%s)\n", me, __LINE__, m, cname); errs++; } } while (0)

#define NB 256                /* payload bytes of one step */
#define ELEMS 1024            /* payload ints of the ring */
#define ROUNDS 20

static char     *hpay;        /* heap payload */
static uint64_t *hsig;        /* heap signal */
static uint64_t *hack;
static int      *hring;       /* heap ring payload */
static uint64_t  seg_sig, seg_ack;
static int       seg_ring[ELEMS];

struct cx {
    shmem_ctx_t ctx;
    int member;
    int tm, tn;                      /* my team PE and the team size */
    int world_of_next, world_of_prev;
};

static void pattern(unsigned char *p, size_t n, int who, int salt)
{
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char) (who * 37 + salt * 11 + i * 3);
}

static int same(const unsigned char *p, size_t n, int who, int salt)
{
    for (size_t i = 0; i < n; i++) if (p[i] != (unsigned char) (who * 37 + salt * 11 + i * 3)) return 0;
    return 1;
}

static int zero(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i] != 0) return 0;
    return 1;
}

/* the standard RMA types, and the byte-sized forms with their element size in bytes */
#define TYPES(X)                                                                               \
    X(float, float) X(double, double) X(longdouble, long double) X(char, char)                 \
    X(schar, signed char) X(short, short) X(int, int) X(long, long) X(longlong, long long)     \
    X(uchar, unsigned char) X(ushort, unsigned short) X(uint, unsigned int)                    \
    X(ulong, unsigned long) X(ulonglong, unsigned long long)                                   \
    X(int8, int8_t) X(int16, int16_t) X(int32, int32_t) X(int64, int64_t)                      \
    X(uint8, uint8_t) X(uint16, uint16_t) X(uint32, uint32_t) X(uint64, uint64_t)              \
    X(size, size_t) X(ptrdiff, ptrdiff_t)
#define SIZED(X) X(mem, 1) X(8, 1) X(16, 2) X(32, 4) X(64, 8) X(128, 16)

enum {
#define X(N, T) T_##N,
    TYPES(X)
#undef X
#define X(N, B) S_##N,
    SIZED(X)
#undef X
    NVARIANTS
};

static const char *vname(int v)
{
    static const char *names[] = {
#define X(N, T) #N,
        TYPES(X)
#undef X
#define X(N, B) "put" #N,
        SIZED(X)
#undef X
    };
    return names[v];
}

/* one ctx put-with-signal call of variant v: NB bytes from src to dst on team PE tpe */
static void do_put_signal(shmem_ctx_t x, int v, int nbi, void *dst, const void *src, uint64_t *sig,
                          uint64_t val, int op, int tpe)
{
    switch (v) {
#define X(N, T)                                                                                \
        case T_##N:                                                                            \
            if (nbi) shmem_ctx_##N##_put_signal_nbi(x, (T *) dst, (const T *) src, NB / sizeof(T), sig, val, op, tpe); \
            else     shmem_ctx_##N##_put_signal(x, (T *) dst, (const T *) src, NB / sizeof(T), sig, val, op, tpe);     \
            break;
        TYPES(X)
#undef X
#define X(N, B)                                                                                \
        case S_##N:                                                                            \
            if (nbi) shmem_ctx_put##N##_signal_nbi(x, dst, src, NB / (B), sig, val, op, tpe);   \
            else     shmem_ctx_put##N##_signal(x, dst, src, NB / (B), sig, val, op, tpe);       \
            break;
        SIZED(X)
#undef X
    }
}

/* every variant, blocking and nonblocking, SET and ADD: each member sends to the next member;
 * afterwards the member after me has the payload and the signal, and non-members have neither */
static void variants(struct cx *c, uint64_t *sig, const char *where)
{
    int next = (c->tm + 1) % c->tn;
    unsigned char src[NB];
    for (int v = 0; v < NVARIANTS; v++) {
        for (int nbi = 0; nbi < 2; nbi++) {
            for (int op = SHMEM_SIGNAL_SET; op <= SHMEM_SIGNAL_ADD; op++) {
                const uint64_t base = 1000, val = 7 + v;
                memset(hpay, 0, NB);
                shmem_uint64_atomic_set(sig, base, me);
                shmem_barrier_all();
                if (c->member) {
                    pattern(src, NB, me, v);
                    do_put_signal(c->ctx, v, nbi, hpay, src, sig, val, op, next);
                    if (nbi) shmem_ctx_quiet(c->ctx);
                }
                shmem_barrier_all();
                if (c->member) {
                    uint64_t want = op == SHMEM_SIGNAL_SET ? val : base + val;
                    int ok = shmem_signal_fetch(sig) == want && same((unsigned char *) hpay, NB, c->world_of_prev, v);
                    if (!ok) {
                        printf("[%d] FAIL: shmem_ctx_%s_put_signal%s %s on %s: signal %llu (want %llu), payload %s\n",
                               me, vname(v), nbi ? "_nbi" : "", op == SHMEM_SIGNAL_SET ? "SET" : "ADD", cname,
                               (unsigned long long) shmem_signal_fetch(sig), (unsigned long long) want,
                               same((unsigned char *) hpay, NB, c->world_of_prev, v) ? "ok" : "wrong");
                        errs++;
                    }
                } else {
                    CHECK(shmem_signal_fetch(sig) == base && zero((unsigned char *) hpay, NB),
                          "a non-member received a signal or payload");
                }
                shmem_barrier_all();
            }
        }
    }
    (void) where;
}

/* ctx_signal_set / ctx_signal_add / generic forms, to the next member */
static void plain_signals(struct cx *c, uint64_t *sig)
{
    int next = (c->tm + 1) % c->tn;
    shmem_uint64_atomic_set(sig, 0, me);
    shmem_barrier_all();
    if (c->member) {
        shmem_ctx_signal_set(c->ctx, sig, 100 + me, next);
        shmem_ctx_quiet(c->ctx);
    }
    shmem_barrier_all();
    if (c->member) CHECK(shmem_signal_fetch(sig) == (uint64_t) (100 + c->world_of_prev), "ctx_signal_set");
    else CHECK(shmem_signal_fetch(sig) == 0, "ctx_signal_set reached a non-member");
    shmem_barrier_all();
    if (c->member) {
        shmem_ctx_signal_add(c->ctx, sig, 5, next);
        shmem_signal_add(c->ctx, sig, 7, next);               /* C11 form with a context */
        shmem_signal_set(c->ctx, sig + 1, 3, next);           /* ditto */
        shmem_ctx_quiet(c->ctx);
    }
    shmem_barrier_all();
    if (c->member) {
        CHECK(shmem_signal_fetch(sig) == (uint64_t) (100 + c->world_of_prev + 12), "ctx_signal_add");
        CHECK(shmem_signal_fetch(sig + 1) == 3, "generic signal_set with a context");
    } else {
        CHECK(shmem_signal_fetch(sig) == 0 && shmem_signal_fetch(sig + 1) == 0, "signal update reached a non-member");
    }
    shmem_barrier_all();
    shmem_uint64_atomic_set(sig + 1, 0, me);
}

static void fill(int *p, int round, int owner)
{
    for (int i = 0; i < ELEMS; i++) p[i] = round * 100000 + owner * 1000 + i % 977;
}

static int check(const int *p, int round, int owner)
{
    for (int i = 0; i < ELEMS; i++) if (p[i] != round * 100000 + owner * 1000 + i % 977) return 0;
    return 1;
}

/* a ring along the team: a member that sees round r's signal sees round r's whole payload */
static void ring(struct cx *c, int *pay, uint64_t *sig, uint64_t *ack, int nbi, const char *what)
{
    int next = (c->tm + 1) % c->tn, prev = (c->tm + c->tn - 1) % c->tn;
    int *src = malloc(ELEMS * sizeof(int));
    shmem_uint64_atomic_set(sig, 0, me);
    shmem_uint64_atomic_set(ack, 0, me);
    memset(pay, 0, ELEMS * sizeof(int));
    shmem_barrier_all();
    if (c->member) {
        for (int r = 1; r <= ROUNDS; r++) {
            fill(src, r, me);
            if (nbi) shmem_ctx_int_put_signal_nbi(c->ctx, pay, src, ELEMS, sig, r, SHMEM_SIGNAL_SET, next);
            else     shmem_ctx_int_put_signal(c->ctx, pay, src, ELEMS, sig, r, SHMEM_SIGNAL_SET, next);
            if (nbi) shmem_ctx_quiet(c->ctx);
            shmem_signal_wait_until(sig, SHMEM_CMP_EQ, (uint64_t) r);
            if (!check(pay, r, c->world_of_prev)) {
                printf("[%d] FAIL: payload wrong once the signal was seen, round %d (%s, %s)\n", me, r, what, cname);
                errs++;
            }
            shmem_ctx_signal_set(c->ctx, ack, (uint64_t) r, prev);
            shmem_signal_wait_until(ack, SHMEM_CMP_EQ, (uint64_t) r);
        }
    }
    shmem_barrier_all();
    if (!c->member) CHECK(shmem_signal_fetch(sig) == 0 && shmem_signal_fetch(ack) == 0, "ring reached a non-member");
    free(src);
    shmem_barrier_all();
}

static void battery(struct cx *c)
{
    variants(c, hsig, "heap signal");
    variants(c, &seg_sig, "data-segment signal");
    plain_signals(c, hsig);
    for (int nbi = 0; nbi < 2; nbi++) {
        ring(c, hring, hsig, hack, nbi, "heap");
        ring(c, seg_ring, &seg_sig, &seg_ack, nbi, "data segment");
    }
    ring(c, hring, &seg_sig, &seg_ack, 0, "heap payload, data-segment signal");
    ring(c, seg_ring, hsig, hack, 1, "data-segment payload, heap signal");
}

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();

    hpay = shmem_calloc(NB, 1);
    hsig = shmem_calloc(2, sizeof(uint64_t));
    hack = shmem_calloc(1, sizeof(uint64_t));
    hring = shmem_calloc(ELEMS, sizeof(int));

    struct cx c;
    memset(&c, 0, sizeof c);

    cname = "default context";
    c.ctx = SHMEM_CTX_DEFAULT; c.member = 1; c.tm = me; c.tn = np;
    c.world_of_next = (me + 1) % np; c.world_of_prev = (me + np - 1) % np;
    battery(&c);

    cname = "created context";
    {
        shmem_ctx_t x;
        CHECK(shmem_ctx_create(0, &x) == 0, "ctx_create");
        c.ctx = x;
        battery(&c);
        shmem_ctx_destroy(x);
    }

    cname = "world team context";
    {
        shmem_ctx_t x;
        CHECK(shmem_team_create_ctx(SHMEM_TEAM_WORLD, 0, &x) == 0, "create_ctx on world team");
        c.ctx = x;
        battery(&c);
        shmem_ctx_destroy(x);
    }

    if (np >= 2) {
        /* world PEs 1..np-1, so team PE 0 is world PE 1 */
        shmem_team_t t = SHMEM_TEAM_INVALID;
        CHECK(shmem_team_split_strided(SHMEM_TEAM_WORLD, 1, 1, np - 1, NULL, 0, &t) == 0, "split_strided");
        cname = "strided team context";
        shmem_ctx_t x = SHMEM_CTX_INVALID;
        memset(&c, 0, sizeof c);
        if (t != SHMEM_TEAM_INVALID) {
            c.member = 1; c.tm = shmem_team_my_pe(t); c.tn = shmem_team_n_pes(t);
            c.world_of_next = 1 + (c.tm + 1) % c.tn;
            c.world_of_prev = 1 + (c.tm + c.tn - 1) % c.tn;
            CHECK(shmem_team_create_ctx(t, 0, &x) == 0, "create_ctx on strided team");
        }
        c.ctx = x;
        battery(&c);
        if (x != SHMEM_CTX_INVALID) shmem_ctx_destroy(x);
        if (t != SHMEM_TEAM_INVALID) shmem_team_destroy(t);

        /* the even world PEs */
        t = SHMEM_TEAM_INVALID;
        CHECK(shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 2, (np + 1) / 2, NULL, 0, &t) == 0, "split_strided even");
        cname = "even team context";
        memset(&c, 0, sizeof c);
        x = SHMEM_CTX_INVALID;
        if (t != SHMEM_TEAM_INVALID) {
            c.member = 1; c.tm = shmem_team_my_pe(t); c.tn = shmem_team_n_pes(t);
            c.world_of_next = 2 * ((c.tm + 1) % c.tn);
            c.world_of_prev = 2 * ((c.tm + c.tn - 1) % c.tn);
            CHECK(shmem_team_create_ctx(t, SHMEM_CTX_PRIVATE, &x) == 0, "create_ctx on even team");
        }
        c.ctx = x;
        battery(&c);
        if (x != SHMEM_CTX_INVALID) shmem_ctx_destroy(x);
        if (t != SHMEM_TEAM_INVALID) shmem_team_destroy(t);
    }

    shmem_barrier_all();
    shmem_free(hring);
    shmem_free(hack);
    shmem_free(hsig);
    shmem_free(hpay);
    printf("[%d] test_ctx_signal: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
