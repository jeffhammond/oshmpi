/* Teams: predefined teams, strided and 2-D splits, translation, nesting, configuration,
 * destroy, team contexts, team_ptr and team_sync.  Works for any number of PEs. */
#include <shmem.h>
#include <stdio.h>
#include <stdlib.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

static int seg_flag;        /* data segment */

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();

    /* ---- predefined teams */
    CHECK(shmem_team_my_pe(SHMEM_TEAM_WORLD) == me, "world my_pe");
    CHECK(shmem_team_n_pes(SHMEM_TEAM_WORLD) == np, "world n_pes");
    CHECK(shmem_team_my_pe(SHMEM_TEAM_INVALID) == -1, "invalid my_pe");
    CHECK(shmem_team_n_pes(SHMEM_TEAM_INVALID) == -1, "invalid n_pes");
    int ns = shmem_team_n_pes(SHMEM_TEAM_SHARED);
    CHECK(ns >= 1 && ns <= np, "shared n_pes in range");
    CHECK(shmem_team_my_pe(SHMEM_TEAM_SHARED) >= 0 && shmem_team_my_pe(SHMEM_TEAM_SHARED) < ns, "shared my_pe in range");
    CHECK(shmem_team_translate_pe(SHMEM_TEAM_SHARED, shmem_team_my_pe(SHMEM_TEAM_SHARED), SHMEM_TEAM_WORLD) == me,
          "shared -> world translation of myself");
    CHECK(shmem_team_translate_pe(SHMEM_TEAM_WORLD, me, SHMEM_TEAM_WORLD) == me, "world -> world");
    CHECK(shmem_team_translate_pe(SHMEM_TEAM_WORLD, np, SHMEM_TEAM_WORLD) == -1, "translate out of range");
    CHECK(shmem_team_translate_pe(SHMEM_TEAM_INVALID, 0, SHMEM_TEAM_WORLD) == -1, "translate from invalid");

    shmem_team_config_t cfg;
    cfg.num_contexts = -1;
    CHECK(shmem_team_get_config(SHMEM_TEAM_WORLD, 0, NULL) == 0, "get_config with mask 0");
    CHECK(shmem_team_get_config(SHMEM_TEAM_WORLD, SHMEM_TEAM_NUM_CONTEXTS, &cfg) == 0 && cfg.num_contexts != -1,
          "get_config on world");
    CHECK(shmem_team_get_config(SHMEM_TEAM_INVALID, SHMEM_TEAM_NUM_CONTEXTS, &cfg) != 0, "get_config on invalid");

    /* ---- strided split: the even PEs */
    shmem_team_t even = SHMEM_TEAM_WORLD;
    int neven = (np + 1) / 2;
    shmem_team_config_t want = { 3 };
    CHECK(shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 2, neven, &want, SHMEM_TEAM_NUM_CONTEXTS, &even) == 0,
          "split_strided evens");
    if (me % 2 == 0) {
        CHECK(even != SHMEM_TEAM_INVALID, "even PE has a team");
        CHECK(shmem_team_n_pes(even) == neven, "even n_pes");
        CHECK(shmem_team_my_pe(even) == me / 2, "even my_pe");
        CHECK(shmem_team_translate_pe(even, shmem_team_my_pe(even), SHMEM_TEAM_WORLD) == me, "even -> world");
        CHECK(shmem_team_translate_pe(SHMEM_TEAM_WORLD, me, even) == me / 2, "world -> even");
        shmem_team_config_t got = { 0 };
        CHECK(shmem_team_get_config(even, SHMEM_TEAM_NUM_CONTEXTS, &got) == 0 && got.num_contexts == 3, "config kept");
    } else {
        CHECK(even == SHMEM_TEAM_INVALID, "odd PE gets the invalid team");
    }
    if (np > 1) CHECK(shmem_team_translate_pe(SHMEM_TEAM_WORLD, 1, even) == -1, "odd PE is not in the even team");

    /* ---- nested split of the even team: its last two PEs, reversed */
    shmem_team_t sub = SHMEM_TEAM_WORLD;
    int rc = shmem_team_split_strided(even, neven - 1, -1, neven > 1 ? 2 : 1, NULL, 0, &sub);
    if (me % 2 == 0 || even != SHMEM_TEAM_INVALID) {
        CHECK(rc == 0, "nested split succeeds");
    }
    if (even != SHMEM_TEAM_INVALID) {
        int sz = neven > 1 ? 2 : 1;
        int mine = shmem_team_my_pe(even);
        int in = mine >= neven - sz;
        if (in) {
            CHECK(sub != SHMEM_TEAM_INVALID, "member of nested team");
            CHECK(shmem_team_n_pes(sub) == sz, "nested n_pes");
            CHECK(shmem_team_my_pe(sub) == neven - 1 - mine, "negative stride reverses the order");
        } else {
            CHECK(sub == SHMEM_TEAM_INVALID, "non-member of nested team");
        }
        shmem_team_destroy(sub);
    }

    /* ---- invalid triplets: every PE gets nonzero and the invalid team */
    shmem_team_t bad = SHMEM_TEAM_WORLD;
    CHECK(shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 1, np + 1, NULL, 0, &bad) != 0 && bad == SHMEM_TEAM_INVALID,
          "size too large");
    bad = SHMEM_TEAM_WORLD;
    CHECK(shmem_team_split_strided(SHMEM_TEAM_WORLD, np, 1, 1, NULL, 0, &bad) != 0 && bad == SHMEM_TEAM_INVALID,
          "start out of range");
    bad = SHMEM_TEAM_WORLD;
    CHECK(shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 1, 0, NULL, 0, &bad) != 0 && bad == SHMEM_TEAM_INVALID,
          "size zero");
    bad = SHMEM_TEAM_WORLD;
    CHECK(shmem_team_split_strided(SHMEM_TEAM_INVALID, 0, 1, 1, NULL, 0, &bad) != 0 && bad == SHMEM_TEAM_INVALID,
          "invalid parent");

    /* ---- a team of one PE (stride 0) */
    shmem_team_t one = SHMEM_TEAM_WORLD;
    CHECK(shmem_team_split_strided(SHMEM_TEAM_WORLD, np - 1, 0, 1, NULL, 0, &one) == 0, "stride 0 size 1");
    if (me == np - 1) CHECK(one != SHMEM_TEAM_INVALID && shmem_team_n_pes(one) == 1 && shmem_team_my_pe(one) == 0, "one-PE team");
    else CHECK(one == SHMEM_TEAM_INVALID, "not in the one-PE team");
    shmem_team_destroy(one);

    /* ---- 2-D split */
    for (int xr = 1; xr <= np + 1; xr++) {
        shmem_team_t xt, yt;
        CHECK(shmem_team_split_2d(SHMEM_TEAM_WORLD, xr, NULL, 0, &xt, NULL, 0, &yt) == 0, "split_2d");
        int x = me % xr, y = me / xr;
        int xsize = 0, ysize = 0;
        for (int p = y * xr; p < np && p < (y + 1) * xr; p++) xsize++;
        for (int p = x; p < np; p += xr) ysize++;
        CHECK(shmem_team_n_pes(xt) == xsize && shmem_team_my_pe(xt) == x, "x-axis team");
        CHECK(shmem_team_n_pes(yt) == ysize && shmem_team_my_pe(yt) == y, "y-axis team");
        CHECK(shmem_team_translate_pe(xt, 0, SHMEM_TEAM_WORLD) == y * xr, "x-axis root is the start of the row");
        CHECK(shmem_team_translate_pe(yt, 0, SHMEM_TEAM_WORLD) == x, "y-axis root is the top of the column");
        shmem_team_destroy(xt);
        shmem_team_destroy(yt);
    }
    { shmem_team_t xt, yt;
      CHECK(shmem_team_split_2d(SHMEM_TEAM_WORLD, 0, NULL, 0, &xt, NULL, 0, &yt) != 0 &&
            xt == SHMEM_TEAM_INVALID && yt == SHMEM_TEAM_INVALID, "split_2d with xrange 0"); }

    /* ---- contexts */
    shmem_team_t t;
    shmem_ctx_t ctx = SHMEM_CTX_DEFAULT;
    CHECK(shmem_ctx_get_team(SHMEM_CTX_DEFAULT, &t) == 0 && t == SHMEM_TEAM_WORLD, "default ctx is on the world team");
    CHECK(shmem_ctx_get_team(SHMEM_CTX_INVALID, &t) != 0 && t == SHMEM_TEAM_INVALID, "invalid ctx");
    CHECK(shmem_ctx_create(0, &ctx) == 0, "ctx_create");
    CHECK(shmem_ctx_get_team(ctx, &t) == 0 && t == SHMEM_TEAM_WORLD, "ctx_create context is on the world team");
    shmem_ctx_destroy(ctx);
    CHECK(shmem_team_create_ctx(SHMEM_TEAM_INVALID, 0, &ctx) != 0 && ctx == SHMEM_CTX_INVALID, "create_ctx on invalid team");
    if (even != SHMEM_TEAM_INVALID) {
        CHECK(shmem_team_create_ctx(even, 0, &ctx) == 0 && ctx != SHMEM_CTX_INVALID, "team_create_ctx");
        CHECK(shmem_ctx_get_team(ctx, &t) == 0 && t == even, "ctx_get_team returns the team");
        shmem_ctx_destroy(ctx);
    }

    /* ---- team_ptr agrees with shmem_ptr on the world team */
    int * heap = shmem_calloc(1, sizeof(int));
    int next = (me + 1) % np;
    void * p1 = shmem_ptr(heap, next);
    void * p2 = shmem_team_ptr(SHMEM_TEAM_WORLD, heap, next);
    CHECK(p1 == p2, "team_ptr on the world team matches shmem_ptr");
    CHECK(shmem_team_ptr(SHMEM_TEAM_INVALID, heap, 0) == NULL, "team_ptr on the invalid team");

    /* ---- team_sync: puts before it are visible after it (heap and data segment) */
    shmem_barrier_all();
    shmem_int_p(heap, 100 + me, next);
    shmem_int_atomic_set(&seg_flag, 200 + me, next);
    CHECK(shmem_team_sync(SHMEM_TEAM_WORLD) == 0, "team_sync");
    int prev = (me + np - 1) % np;
    CHECK(*heap == 100 + prev, "heap put visible after team_sync");
    CHECK(seg_flag == 200 + prev, "data-segment atomic visible after team_sync");
    CHECK(shmem_sync(SHMEM_TEAM_WORLD) == 0, "C11 shmem_sync(team)");
    CHECK(shmem_team_sync(SHMEM_TEAM_INVALID) != 0, "team_sync on the invalid team");

    /* a sync over a sub-team involves only its members */
    if (even != SHMEM_TEAM_INVALID) CHECK(shmem_team_sync(even) == 0, "team_sync on a sub-team");

    shmem_free(heap);
    shmem_team_destroy(even);
    shmem_barrier_all();
    printf("[%d] test_team: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
