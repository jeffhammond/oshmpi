/* OpenSHMEM 1.4 C11 type-generic bindings, with and without a context. */
#include <stdio.h>
#include <stdint.h>
#include <shmem.h>

static int errs = 0, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

static int32_t  si32[2];
static uint64_t su64[2];
static size_t   ssz[2];
static double   sdb[2];
static short    ssh[2];
static unsigned short sush[2];
static int32_t  bi32;
static int64_t  bi64;
static uint32_t bu32;
static int      bint;
static long     blong;
static long long bllong;

int main(void)
{
    shmem_init();
    me = shmem_my_pe(); np = shmem_n_pes();
    int right = (me + 1) % np, left = (me + np - 1) % np;
    shmem_ctx_t ctx;
    CHECK(shmem_ctx_create(0, &ctx) == 0, "ctx_create");

    int32_t a = me + 10;  uint64_t b = me + 20; double c = me + 0.5;
    shmem_barrier_all();
    shmem_put(si32, &a, 1, right);          /* int32_t -> shmem_int_put */
    shmem_put(su64, &b, 1, right);          /* uint64_t */
    shmem_put(sdb, &c, 1, right);
    shmem_p(&ssz[0], (size_t)(me + 30), right);
    shmem_barrier_all();
    CHECK(si32[0] == left + 10, "generic put int32");
    CHECK(su64[0] == (uint64_t)(left + 20), "generic put uint64");
    CHECK(sdb[0] == left + 0.5, "generic put double");
    CHECK(ssz[0] == (size_t)(left + 30), "generic p size_t");
    CHECK(shmem_g(&si32[0], right) == me + 10, "generic g");

    /* context forms */
    shmem_barrier_all();
    shmem_put(ctx, si32, &a, 1, right);
    shmem_ctx_quiet(ctx);
    shmem_barrier_all();
    CHECK(si32[0] == left + 10, "generic ctx put");
    CHECK(shmem_g(ctx, &si32[0], right) == me + 10, "generic ctx g");

    /* atomics */
    shmem_barrier_all();
    shmem_atomic_set(&su64[1], 0, 0);
    shmem_barrier_all();
    shmem_atomic_add(&su64[1], 3, 0);
    uint64_t prev = shmem_atomic_fetch_add(&su64[1], 4, 0);
    CHECK(prev <= (uint64_t)np * 8, "generic fetch_add");   /* the other PEs' later incs may already be in */
    shmem_atomic_inc(ctx, &su64[1], 0);
    shmem_ctx_quiet(ctx);
    shmem_barrier_all();
    CHECK(shmem_atomic_fetch(&su64[1], 0) == (uint64_t)np * 8, "generic atomic totals");
    shmem_barrier_all();
    shmem_atomic_set(&sdb[1], 1.5, me);
    CHECK(shmem_atomic_swap(&sdb[1], 2.5, me) == 1.5, "generic double swap");
    CHECK(shmem_atomic_compare_swap(&si32[1], 0, 5, me) == 0, "generic cswap");
    shmem_atomic_or(&su64[0], (uint64_t)1 << 40, me);
    CHECK((su64[0] >> 40) & 1, "generic or");

    /* bitwise AMOs on the signed and exact-width types (local PE, with and without ctx) */
    bi32 = 0xF0; bi64 = 0xF0; bu32 = 0xF0; bint = 0xF0; blong = 0xF0; bllong = 0xF0;
    shmem_barrier_all();
    shmem_atomic_and(&bi32, 0x3C, me);
    CHECK(bi32 == 0x30, "generic and int32_t");
    CHECK(shmem_atomic_fetch_or(&bi32, 0x0F, me) == 0x30 && bi32 == 0x3F, "generic fetch_or int32_t");
    shmem_atomic_xor(&bi64, 0xFF, me);
    CHECK(bi64 == 0x0F, "generic xor int64_t");
    CHECK(shmem_atomic_fetch_and(&bi64, 0x03, me) == 0x0F && bi64 == 0x03, "generic fetch_and int64_t");
    shmem_atomic_or(&bu32, 0x0F, me);
    CHECK(bu32 == 0xFF, "generic or uint32_t");
    CHECK(shmem_atomic_fetch_xor(ctx, &bint, 0xFF, me) == 0xF0 && bint == 0x0F, "generic ctx fetch_xor int");
    shmem_atomic_and(ctx, &blong, 0x30, me);
    CHECK(blong == 0x30, "generic ctx and long");
    shmem_atomic_or(ctx, &bllong, 0x100, me);
    CHECK(bllong == 0x1F0, "generic ctx or long long");

    /* wait_until / test */
    shmem_barrier_all();
    shmem_atomic_set(&si32[1], 77, right);
    shmem_wait_until(&si32[1], SHMEM_CMP_EQ, 77);
    CHECK(shmem_test(&si32[1], SHMEM_CMP_EQ, 77) == 1, "generic test");
    shmem_wait_until(&sush[0], SHMEM_CMP_EQ, 0);         /* unsigned short */
    shmem_wait_until(&ssh[0], SHMEM_CMP_EQ, 0);          /* short */
    shmem_barrier_all();

    shmem_ctx_destroy(ctx);
    printf("[%d] test_generic: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
