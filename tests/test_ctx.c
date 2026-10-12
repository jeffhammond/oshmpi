/* Contexts: every data-movement and atomic routine family through the default context, a
 * created context (default and with options), a context on the world team, and contexts on
 * teams whose PE numbering differs from the world's (a strided team and a reversed
 * subset).  Target PEs given to ctx routines are team-relative, so the team contexts check
 * that the translation lands on the right world PE.  Members do the work; everyone joins
 * the barriers. */
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s (%s)\n", me, __LINE__, m, cname); errs++; } } while (0)

static const char *cname;

#define BUF 4096
static unsigned char *hb;           /* heap buffer: payload area */
static int           *hi;           /* heap ints */
static long          *hl;
static unsigned int  *hu;
static double        *hd;

/* one context together with the team it addresses */
struct cx {
    shmem_ctx_t ctx;
    int member;          /* does this PE belong to the team? */
    int tm, tn;          /* my team-relative PE and the team size */
    int world_of_next;   /* world PE that team PE (tm+1)%tn names; used for the checks */
    int world_of_prev;
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

static void battery(struct cx *c)
{
    shmem_ctx_t x = c->ctx;
    int next = (c->tm + 1) % c->tn;
    unsigned char loc[BUF], back[BUF];

    /* ---- byte-oriented puts: each PE writes to the next team member's buffer */
    memset(hb, 0, BUF);
    shmem_barrier_all();
    if (c->member) {
        pattern(loc, 300, me, 1);
        shmem_ctx_putmem(x, hb, loc, 300, next);
        pattern(loc, 8, me, 2);   shmem_ctx_put8(x, hb + 400, loc, 8, next);
        pattern(loc, 16, me, 3);  shmem_ctx_put16(x, hb + 500, loc, 8, next);
        pattern(loc, 32, me, 4);  shmem_ctx_put32(x, hb + 600, loc, 8, next);
        pattern(loc, 64, me, 5);  shmem_ctx_put64(x, hb + 700, loc, 8, next);
        pattern(loc, 128, me, 6); shmem_ctx_put128(x, hb + 900, loc, 8, next);
        shmem_ctx_quiet(x);
    }
    shmem_barrier_all();
    if (c->member) {
        int from = c->world_of_prev;
        CHECK(same(hb, 300, from, 1), "ctx_putmem");
        CHECK(same(hb + 400, 8, from, 2), "ctx_put8");
        CHECK(same(hb + 500, 16, from, 3), "ctx_put16");
        CHECK(same(hb + 600, 32, from, 4), "ctx_put32");
        CHECK(same(hb + 700, 64, from, 5), "ctx_put64");
        CHECK(same(hb + 900, 128, from, 6), "ctx_put128");
    }
    shmem_barrier_all();

    /* ---- gets read the next member's buffer, which holds what *its* predecessor wrote */
    if (c->member) {
        int owner = c->world_of_next;       /* next's buffer was written by me */
        memset(back, 0, BUF);
        shmem_ctx_getmem(x, back, hb, 300, next);
        CHECK(same(back, 300, me, 1), "ctx_getmem");
        memset(back, 0, BUF);
        shmem_ctx_get8(x, back, hb + 400, 8, next);    CHECK(same(back, 8, me, 2), "ctx_get8");
        shmem_ctx_get16(x, back, hb + 500, 8, next);   CHECK(same(back, 16, me, 3), "ctx_get16");
        shmem_ctx_get32(x, back, hb + 600, 8, next);   CHECK(same(back, 32, me, 4), "ctx_get32");
        shmem_ctx_get64(x, back, hb + 700, 8, next);   CHECK(same(back, 64, me, 5), "ctx_get64");
        shmem_ctx_get128(x, back, hb + 900, 8, next);  CHECK(same(back, 128, me, 6), "ctx_get128");
        (void) owner;
    }
    shmem_barrier_all();

    /* ---- nonblocking, completed by ctx_quiet */
    memset(hb, 0, BUF);
    shmem_barrier_all();
    if (c->member) {
        pattern(loc, 300, me, 7);
        shmem_ctx_putmem_nbi(x, hb, loc, 300, next);
        pattern(loc + 400, 8, me, 8);   shmem_ctx_put8_nbi(x, hb + 400, loc + 400, 8, next);
        pattern(loc + 500, 16, me, 9);  shmem_ctx_put16_nbi(x, hb + 500, loc + 500, 8, next);
        pattern(loc + 600, 32, me, 10); shmem_ctx_put32_nbi(x, hb + 600, loc + 600, 8, next);
        pattern(loc + 700, 64, me, 11); shmem_ctx_put64_nbi(x, hb + 700, loc + 700, 8, next);
        pattern(loc + 900, 128, me, 12); shmem_ctx_put128_nbi(x, hb + 900, loc + 900, 8, next);
        shmem_ctx_quiet(x);
    }
    shmem_barrier_all();
    if (c->member) {
        int from = c->world_of_prev;
        CHECK(same(hb, 300, from, 7), "ctx_putmem_nbi");
        CHECK(same(hb + 400, 8, from, 8), "ctx_put8_nbi");
        CHECK(same(hb + 500, 16, from, 9), "ctx_put16_nbi");
        CHECK(same(hb + 600, 32, from, 10), "ctx_put32_nbi");
        CHECK(same(hb + 700, 64, from, 11), "ctx_put64_nbi");
        CHECK(same(hb + 900, 128, from, 12), "ctx_put128_nbi");
        memset(back, 0, BUF);
        shmem_ctx_getmem_nbi(x, back, hb, 300, next);
        shmem_ctx_get8_nbi(x, back + 400, hb + 400, 8, next);
        shmem_ctx_get16_nbi(x, back + 500, hb + 500, 8, next);
        shmem_ctx_get32_nbi(x, back + 600, hb + 600, 8, next);
        shmem_ctx_get64_nbi(x, back + 700, hb + 700, 8, next);
        shmem_ctx_get128_nbi(x, back + 900, hb + 900, 8, next);
        shmem_ctx_quiet(x);
        CHECK(same(back, 300, me, 7), "ctx_getmem_nbi");
        CHECK(same(back + 400, 8, me, 8), "ctx_get8_nbi");
        CHECK(same(back + 500, 16, me, 9), "ctx_get16_nbi");
        CHECK(same(back + 600, 32, me, 10), "ctx_get32_nbi");
        CHECK(same(back + 700, 64, me, 11), "ctx_get64_nbi");
        CHECK(same(back + 900, 128, me, 12), "ctx_get128_nbi");
    }
    shmem_barrier_all();

    /* ---- strided: sized iput/iget and the typed forms */
    memset(hb, 0, BUF);
    shmem_barrier_all();
    if (c->member) {
        unsigned char s[BUF];
        for (int i = 0; i < 256; i++) s[i] = (unsigned char) (me * 5 + i);
        shmem_ctx_iput8(x, hb, s, 3, 2, 20, next);                  /* bytes: every 3rd <- every 2nd */
        shmem_ctx_iput16(x, hb + 200, s, 2, 3, 10, next);           /* 16-bit elements */
        shmem_ctx_iput32(x, hb + 400, s, 2, 1, 10, next);
        shmem_ctx_iput64(x, hb + 600, s, 2, 1, 10, next);
        shmem_ctx_iput128(x, hb + 1000, s, 2, 1, 4, next);
        shmem_ctx_quiet(x);
    }
    shmem_barrier_all();
    if (c->member) {
        int from = c->world_of_prev;
        int bad = 0;
        for (int k = 0; k < 20; k++) if (hb[3 * k] != (unsigned char) (from * 5 + 2 * k)) bad = 1;
        CHECK(!bad, "ctx_iput8");
        bad = 0;
        for (int k = 0; k < 10; k++)
            for (int b = 0; b < 2; b++)
                if (hb[200 + 4 * k + b] != (unsigned char) (from * 5 + 6 * k + b)) bad = 1;
        CHECK(!bad, "ctx_iput16");
        bad = 0;
        for (int k = 0; k < 10; k++)
            for (int b = 0; b < 4; b++)
                if (hb[400 + 8 * k + b] != (unsigned char) (from * 5 + 4 * k + b)) bad = 1;
        CHECK(!bad, "ctx_iput32");
        bad = 0;
        for (int k = 0; k < 10; k++)
            for (int b = 0; b < 8; b++)
                if (hb[600 + 16 * k + b] != (unsigned char) (from * 5 + 8 * k + b)) bad = 1;
        CHECK(!bad, "ctx_iput64");
        bad = 0;
        for (int k = 0; k < 4; k++)
            for (int b = 0; b < 16; b++)
                if (hb[1000 + 32 * k + b] != (unsigned char) (from * 5 + 16 * k + b)) bad = 1;
        CHECK(!bad, "ctx_iput128");

        /* read back what I wrote to next with strided gets */
        unsigned char g[BUF];
        memset(g, 0, sizeof g);
        shmem_ctx_iget8(x, g, hb, 1, 3, 20, next);
        bad = 0; for (int k = 0; k < 20; k++) if (g[k] != (unsigned char) (me * 5 + 2 * k)) bad = 1;
        CHECK(!bad, "ctx_iget8");
        memset(g, 0, sizeof g);
        shmem_ctx_iget16(x, g, hb + 200, 1, 2, 10, next);
        bad = 0; for (int k = 0; k < 10; k++) for (int b = 0; b < 2; b++) if (g[2 * k + b] != (unsigned char) (me * 5 + 6 * k + b)) bad = 1;
        CHECK(!bad, "ctx_iget16");
        memset(g, 0, sizeof g);
        shmem_ctx_iget32(x, g, hb + 400, 1, 2, 10, next);
        bad = 0; for (int k = 0; k < 10; k++) for (int b = 0; b < 4; b++) if (g[4 * k + b] != (unsigned char) (me * 5 + 4 * k + b)) bad = 1;
        CHECK(!bad, "ctx_iget32");
        memset(g, 0, sizeof g);
        shmem_ctx_iget64(x, g, hb + 600, 1, 2, 10, next);
        bad = 0; for (int k = 0; k < 10; k++) for (int b = 0; b < 8; b++) if (g[8 * k + b] != (unsigned char) (me * 5 + 8 * k + b)) bad = 1;
        CHECK(!bad, "ctx_iget64");
        memset(g, 0, sizeof g);
        shmem_ctx_iget128(x, g, hb + 1000, 1, 2, 4, next);
        bad = 0; for (int k = 0; k < 4; k++) for (int b = 0; b < 16; b++) if (g[16 * k + b] != (unsigned char) (me * 5 + 16 * k + b)) bad = 1;
        CHECK(!bad, "ctx_iget128");
    }
    shmem_barrier_all();

    /* ---- typed elemental and block forms */
    memset(hi, 0, 64 * sizeof(int));
    memset(hd, 0, 8 * sizeof(double));
    shmem_barrier_all();
    if (c->member) {
        int blk[8];
        for (int i = 0; i < 8; i++) blk[i] = me * 100 + i;
        shmem_ctx_int_p(x, &hi[0], 5000 + me, next);
        shmem_ctx_double_p(x, &hd[0], 0.5 + me, next);
        shmem_ctx_long_p(x, &hl[0], 7000L + me, next);
        shmem_ctx_int_put(x, &hi[8], blk, 8, next);
        shmem_ctx_long_put_nbi(x, &hl[8], (long[]){ me, me + 1, me + 2 }, 3, next);
        shmem_ctx_quiet(x);
    }
    shmem_barrier_all();
    if (c->member) {
        int from = c->world_of_prev;
        CHECK(hi[0] == 5000 + from, "ctx_int_p");
        CHECK(hd[0] == 0.5 + from, "ctx_double_p");
        CHECK(hl[0] == 7000L + from, "ctx_long_p");
        int bad = 0; for (int i = 0; i < 8; i++) if (hi[8 + i] != from * 100 + i) bad = 1;
        CHECK(!bad, "ctx_int_put");
        CHECK(hl[8] == from && hl[9] == from + 1 && hl[10] == from + 2, "ctx_long_put_nbi");
        CHECK(shmem_ctx_int_g(x, &hi[0], next) == 5000 + me, "ctx_int_g");
        CHECK(shmem_ctx_double_g(x, &hd[0], next) == 0.5 + me, "ctx_double_g");
        CHECK(shmem_ctx_long_g(x, &hl[0], next) == 7000L + me, "ctx_long_g");
        int g[8];
        shmem_ctx_int_get(x, g, &hi[8], 8, next);
        bad = 0; for (int i = 0; i < 8; i++) if (g[i] != me * 100 + i) bad = 1;
        CHECK(!bad, "ctx_int_get");
    }
    shmem_barrier_all();

    /* ---- atomics, all members on the counters of team PE 0 */
    memset(hi, 0, 64 * sizeof(int));
    memset(hl, 0, 16 * sizeof(long));
    memset(hu, 0, 16 * sizeof(unsigned));
    shmem_barrier_all();
    if (c->member) {
        for (int k = 0; k < 20; k++) {
            shmem_ctx_int_atomic_add(x, &hi[0], 2, 0);
            shmem_ctx_int_atomic_inc(x, &hi[1], 0);
            shmem_ctx_long_atomic_add(x, &hl[0], 3, 0);
            long t = shmem_ctx_long_atomic_fetch_inc(x, &hl[1], 0);        /* unique tickets */
            shmem_ctx_long_atomic_add(x, &hl[2], t, 0);
            (void) shmem_ctx_int_atomic_fetch_add(x, &hi[2], 1, 0);
        }
        shmem_ctx_quiet(x);
        /* bitwise: each member ORs in its team index, XORs twice, clears a bit */
        shmem_ctx_uint_atomic_or(x, &hu[0], 1u << (c->tm % 32), 0);
        shmem_ctx_uint_atomic_xor(x, &hu[1], 1u << (c->tm % 32), 0);
        shmem_ctx_uint_atomic_xor(x, &hu[1], 1u << (c->tm % 32), 0);
        (void) shmem_ctx_uint_atomic_fetch_or(x, &hu[2], 1u << (c->tm % 32), 0);
        (void) shmem_ctx_uint_atomic_fetch_xor(x, &hu[3], 0, 0);
        if (c->tm == 0) shmem_ctx_uint_atomic_set(x, &hu[4], 0xFFFFFFFFu, me);
        shmem_ctx_quiet(x);
    }
    shmem_barrier_all();
    if (c->member) {
        shmem_ctx_uint_atomic_fetch_and(x, &hu[4], 0xFFFFFFFFu, 0);    /* touch the family */
    }
    shmem_barrier_all();
    if (c->member && c->tm == 0) {
        int n = c->tn;
        CHECK(hi[0] == 2 * 20 * n, "ctx_int_atomic_add");
        CHECK(hi[1] == 20 * n, "ctx_int_atomic_inc");
        CHECK(hl[0] == 3L * 20 * n, "ctx_long_atomic_add");
        long m = 20L * n;
        CHECK(hl[1] == m && hl[2] == m * (m - 1) / 2, "ctx_long_atomic_fetch_inc returns unique tickets");
        CHECK(hi[2] == 20 * n, "ctx_int_atomic_fetch_add");
        unsigned all = 0; for (int p = 0; p < n; p++) all |= 1u << (p % 32);
        CHECK(hu[0] == all, "ctx_uint_atomic_or");
        CHECK(hu[1] == 0, "ctx_uint_atomic_xor twice");
        CHECK(hu[2] == all, "ctx_uint_atomic_fetch_or");
    }
    shmem_barrier_all();

    /* swap / compare-swap / fetch / set on my own word through the context's team indexing */
    if (c->member) {
        int mine = c->tm;
        hi[10] = 1;
        CHECK(shmem_ctx_int_atomic_swap(x, &hi[10], 9, mine) == 1, "ctx_int_atomic_swap");
        CHECK(shmem_ctx_int_atomic_compare_swap(x, &hi[10], 8, 4, mine) == 9 && hi[10] == 9, "failed ctx_cswap");
        CHECK(shmem_ctx_int_atomic_compare_swap(x, &hi[10], 9, 4, mine) == 9 && hi[10] == 4, "ctx_cswap");
        shmem_ctx_int_atomic_set(x, &hi[10], 77, mine);
        CHECK(shmem_ctx_int_atomic_fetch(x, &hi[10], mine) == 77, "ctx_int_atomic_set / fetch");
        shmem_ctx_double_atomic_set(x, &hd[1], 2.5, mine);
        CHECK(shmem_ctx_double_atomic_swap(x, &hd[1], 4.5, mine) == 2.5 && hd[1] == 4.5, "ctx_double_atomic_swap");
        CHECK(shmem_ctx_double_atomic_fetch(x, &hd[1], mine) == 4.5, "ctx_double_atomic_fetch");
    }
    shmem_barrier_all();

    /* ---- nonblocking fetching atomics deliver after the context's quiet */
    memset(hi, 0, 64 * sizeof(int));
    shmem_barrier_all();
    if (c->member) {
        int f1 = -1, f2 = -1;
        shmem_ctx_int_atomic_fetch_add_nbi(x, &f1, &hi[20], 1, 0);
        shmem_ctx_int_atomic_fetch_inc_nbi(x, &f2, &hi[20], 0);
        shmem_ctx_quiet(x);
        CHECK(f1 >= 0 && f2 >= 0 && f1 != f2, "ctx nbi fetch atomics");
    }
    shmem_barrier_all();
    if (c->member && c->tm == 0) CHECK(hi[20] == 2 * c->tn, "ctx nbi fetch atomics counted");
    shmem_barrier_all();

    /* ---- ordering: ctx_fence keeps the payload before the flag for one target */
    memset(hb, 0, BUF);
    hi[30] = 0;
    shmem_barrier_all();
    if (c->member) {
        for (int round = 1; round <= 20; round++) {
            pattern(loc, 512, me, round);
            shmem_ctx_putmem(x, hb, loc, 512, next);
            shmem_ctx_fence(x);
            shmem_ctx_int_p(x, &hi[30], round, next);
            int seen;
            while ((seen = shmem_int_atomic_fetch(&hi[30], me)) < round) ;     /* previous PE's round */
            CHECK(same(hb, 512, c->world_of_prev, seen), "ctx_fence orders payload before flag");
            /* keep the rounds in step so a fast sender does not overwrite an unread payload */
            shmem_sync_all();
        }
    } else {
        for (int round = 1; round <= 20; round++) shmem_sync_all();
    }
    shmem_barrier_all();
}

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();

    hb = shmem_malloc(BUF);
    hi = shmem_calloc(64, sizeof(int));
    hl = shmem_calloc(16, sizeof(long));
    hu = shmem_calloc(16, sizeof(unsigned));
    hd = shmem_calloc(8, sizeof(double));

    struct cx c;
    memset(&c, 0, sizeof c);

    /* default context: team PE == world PE */
    cname = "default context";
    c.ctx = SHMEM_CTX_DEFAULT; c.member = 1; c.tm = me; c.tn = np;
    c.world_of_next = (me + 1) % np; c.world_of_prev = (me + np - 1) % np;
    battery(&c);

    /* created contexts, with and without options */
    long opts[] = { 0, SHMEM_CTX_PRIVATE, SHMEM_CTX_SERIALIZED, SHMEM_CTX_NOSTORE, SHMEM_CTX_PRIVATE | SHMEM_CTX_NOSTORE };
    const char *onames[] = { "created context", "private context", "serialized context", "nostore context", "private nostore context" };
    for (int o = 0; o < 5; o++) {
        cname = onames[o];
        shmem_ctx_t x;
        int rc = shmem_ctx_create(opts[o], &x);
        if (rc != 0) { printf("[%d] note: %s not supported (rc=%d)\n", me, onames[o], rc); continue; }
        c.ctx = x;
        battery(&c);
        shmem_ctx_destroy(x);
    }

    /* two live contexts at once, used alternately */
    {
        cname = "interleaved contexts";
        shmem_ctx_t a, b;
        CHECK(shmem_ctx_create(0, &a) == 0 && shmem_ctx_create(0, &b) == 0, "two contexts");
        int next = (me + 1) % np;
        memset(hi, 0, 64 * sizeof(int));
        shmem_barrier_all();
        for (int k = 0; k < 50; k++) {
            shmem_ctx_int_atomic_add(a, &hi[0], 1, next);
            shmem_ctx_int_atomic_add(b, &hi[1], 2, next);
        }
        shmem_ctx_quiet(a);
        shmem_ctx_quiet(b);
        shmem_barrier_all();
        CHECK(hi[0] == 50 && hi[1] == 100, "interleaved contexts");
        shmem_ctx_destroy(a);
        shmem_ctx_destroy(b);
    }

    /* team contexts */
    if (np >= 2) {
        /* a strided team: world PEs 1, 2, ..., np-1 (team PE 0 is world PE 1) */
        shmem_team_t t = SHMEM_TEAM_INVALID;
        int rc = shmem_team_split_strided(SHMEM_TEAM_WORLD, 1, 1, np - 1, NULL, 0, &t);
        CHECK(rc == 0, "split_strided");
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

        /* a team of the even world PEs, stride 2 */
        t = SHMEM_TEAM_INVALID;
        int ne = (np + 1) / 2;
        rc = shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 2, ne, NULL, 0, &t);
        CHECK(rc == 0, "split_strided even");
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

    /* a context on the world team */
    {
        cname = "world team context";
        shmem_ctx_t x;
        memset(&c, 0, sizeof c);
        CHECK(shmem_team_create_ctx(SHMEM_TEAM_WORLD, 0, &x) == 0, "create_ctx on world team");
        c.ctx = x; c.member = 1; c.tm = me; c.tn = np;
        c.world_of_next = (me + 1) % np; c.world_of_prev = (me + np - 1) % np;
        battery(&c);
        shmem_ctx_destroy(x);
    }

    shmem_barrier_all();
    printf("[%d] test_ctx: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
