/* Signaling: put-with-signal for every type, putmem and the sized forms, blocking and
 * nonblocking, with and without a context, SET and ADD, signal_fetch / signal_set /
 * signal_add / signal_wait_until, heap and data-segment signals and payloads.  The ordering
 * check is the point: a PE that sees the signal must see the whole payload. */
#include <shmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int errs, me, np;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

#define ELEMS 1024                    /* payload elements; large enough to expose reordering */
#define ROUNDS 40

static uint64_t seg_sig;              /* data-segment signal */
static uint64_t seg_ack;
static int      seg_payload[ELEMS];   /* data-segment payload */

static uint64_t *hsig, *hack;         /* heap signals */
static int      *hpay;                /* heap payload */

static void fill(int *p, int round, int owner)
{
    for (int i = 0; i < ELEMS; i++) p[i] = round * 100000 + owner * 1000 + i % 977;
}

static int check(const int *p, int round, int owner)
{
    for (int i = 0; i < ELEMS; i++)
        if (p[i] != round * 100000 + owner * 1000 + i % 977) return 0;
    return 1;
}

/* A ring: every PE sends round r to the next PE, waits for the previous PE's round r signal,
 * verifies the payload, and acknowledges so that the sender can reuse the buffers. */
enum { BLOCKING, NONBLOCKING, CTX_BLOCKING, CTX_NONBLOCKING, GENERIC, GENERIC_CTX };

static void ring(int mode, int *pay, uint64_t *sig, uint64_t *ack, const char *what)
{
    int next = (me + 1) % np, prev = (me + np - 1) % np;
    int *src = malloc(ELEMS * sizeof(int));
    shmem_ctx_t ctx = SHMEM_CTX_INVALID;
    if (mode == CTX_BLOCKING || mode == CTX_NONBLOCKING || mode == GENERIC_CTX)
        CHECK(shmem_ctx_create(0, &ctx) == 0, "ctx_create");

    shmem_barrier_all();
    shmem_uint64_atomic_set(sig, 0, me);
    shmem_uint64_atomic_set(ack, 0, me);
    memset(pay, 0, ELEMS * sizeof(int));
    shmem_barrier_all();

    for (int r = 1; r <= ROUNDS; r++) {
        fill(src, r, me);
        switch (mode) {
            case BLOCKING:       shmem_int_put_signal(pay, src, ELEMS, sig, r, SHMEM_SIGNAL_SET, next); break;
            case NONBLOCKING:    shmem_int_put_signal_nbi(pay, src, ELEMS, sig, r, SHMEM_SIGNAL_SET, next); break;
            case CTX_BLOCKING:   shmem_ctx_int_put_signal(ctx, pay, src, ELEMS, sig, r, SHMEM_SIGNAL_SET, next); break;
            case CTX_NONBLOCKING:shmem_ctx_int_put_signal_nbi(ctx, pay, src, ELEMS, sig, r, SHMEM_SIGNAL_SET, next); break;
            case GENERIC:        shmem_put_signal(pay, src, ELEMS, sig, r, SHMEM_SIGNAL_SET, next); break;
            case GENERIC_CTX:    shmem_put_signal_nbi(ctx, pay, src, ELEMS, sig, r, SHMEM_SIGNAL_SET, next); break;
        }
        if (mode == NONBLOCKING || mode == CTX_NONBLOCKING || mode == GENERIC_CTX) shmem_quiet();

        uint64_t got = shmem_signal_wait_until(sig, SHMEM_CMP_EQ, (uint64_t) r);
        CHECK(got == (uint64_t) r, what);
        if (!check(pay, r, prev)) { printf("[%d] payload wrong after signal, round %d (%s)\n", me, r, what); errs++; }
        shmem_signal_set(ack, (uint64_t) r, prev);                 /* acknowledge */
        shmem_signal_wait_until(ack, SHMEM_CMP_EQ, (uint64_t) r);  /* the next PE received it */
    }
    free(src);
    if (ctx != SHMEM_CTX_INVALID) shmem_ctx_destroy(ctx);
    shmem_barrier_all();
}

int main(void)
{
    shmem_init();
    me = shmem_my_pe();
    np = shmem_n_pes();

    hsig = shmem_calloc(1, sizeof(uint64_t));
    hack = shmem_calloc(1, sizeof(uint64_t));
    hpay = shmem_calloc(ELEMS, sizeof(int));

    /* ---- ring, every flavor, payload and signal both on the heap and both in the data segment */
    for (int mode = BLOCKING; mode <= GENERIC_CTX; mode++) {
        ring(mode, hpay, hsig, hack, "heap signal");
        ring(mode, seg_payload, &seg_sig, &seg_ack, "data-segment signal");
    }
    /* mixed kinds: payload on the heap, signal in the data segment, and the reverse */
    ring(BLOCKING, hpay, &seg_sig, &seg_ack, "heap payload, data-segment signal");
    ring(NONBLOCKING, seg_payload, hsig, hack, "data-segment payload, heap signal");

    /* ---- SIGNAL_ADD: every PE adds to PE 0's signal and deposits its own payload slot */
    {
        int *slots = shmem_calloc((size_t) np * 4, sizeof(int));
        uint64_t *asig = shmem_calloc(1, sizeof(uint64_t));
        shmem_barrier_all();
        int mine[4] = { me + 1, me + 2, me + 3, me + 4 };
        shmem_int_put_signal(&slots[me * 4], mine, 4, asig, 3, SHMEM_SIGNAL_ADD, 0);
        if (me == 0) {
            uint64_t v = shmem_signal_wait_until(asig, SHMEM_CMP_GE, 3 * (uint64_t) np);
            CHECK(v == 3 * (uint64_t) np, "signal add total");
            int bad = 0;
            for (int p = 0; p < np; p++)
                for (int k = 0; k < 4; k++) if (slots[p * 4 + k] != p + 1 + k) bad = 1;
            CHECK(!bad, "payloads present once the add total is reached");
        }
        shmem_barrier_all();
        shmem_free(asig);
        shmem_free(slots);
    }

    /* ---- standalone signal_set / signal_add / signal_fetch, also with a context */
    {
        uint64_t *s = shmem_calloc(1, sizeof(uint64_t));
        shmem_ctx_t ctx;
        CHECK(shmem_ctx_create(0, &ctx) == 0, "ctx_create");
        shmem_barrier_all();
        shmem_signal_set(s, 100 + me, (me + 1) % np);
        shmem_barrier_all();
        CHECK(shmem_signal_fetch(s) == (uint64_t) (100 + (me + np - 1) % np), "signal_set / signal_fetch");
        shmem_signal_add(s, 5, me);
        shmem_ctx_signal_add(ctx, s, 7, me);
        shmem_signal_add(ctx, s, 1, me);                            /* C11 form with a context */
        CHECK(shmem_signal_fetch(s) == (uint64_t) (100 + (me + np - 1) % np + 13), "signal_add");
        shmem_ctx_signal_set(ctx, s, 9, me);
        CHECK(shmem_signal_wait_until(s, SHMEM_CMP_EQ, 9) == 9, "ctx_signal_set");
        CHECK(shmem_signal_wait_until(s, SHMEM_CMP_NE, 0) == 9, "wait_until NE");
        CHECK(shmem_signal_wait_until(s, SHMEM_CMP_LT, 10) == 9, "wait_until LT");
        shmem_ctx_destroy(ctx);
        shmem_barrier_all();
        shmem_free(s);
    }

    /* ---- every type, and the byte and sized forms, once */
    {
        char *buf = shmem_calloc(256, 1);
        char *src = malloc(256);
        uint64_t *s = shmem_calloc(1, sizeof(uint64_t));
        int next = (me + 1) % np, prev = (me + np - 1) % np;
        for (int i = 0; i < 256; i++) src[i] = (char) (me * 3 + i);
        uint64_t round = 0;

#define EXPECT_ROUND(what, nbytes)                                                              \
        do {                                                                                    \
            round++;                                                                            \
            shmem_signal_wait_until(s, SHMEM_CMP_EQ, round);                                    \
            int bad = 0;                                                                        \
            for (int i = 0; i < (nbytes); i++) if (buf[i] != (char) (prev * 3 + i)) bad = 1;    \
            CHECK(!bad, what);                                                                  \
            memset(buf, 0, 256);                                                                \
            shmem_barrier_all();                                                                \
        } while (0)

#define TYPED(N, T, ...)                                                                        \
        shmem_barrier_all();                                                                    \
        shmem_##N##_put_signal((T *) buf, (const T *) src, 256 / sizeof(T), s, round + 1, SHMEM_SIGNAL_SET, next); \
        EXPECT_ROUND("shmem_" #N "_put_signal", (int) (256 / sizeof(T) * sizeof(T)));           \
        shmem_##N##_put_signal_nbi((T *) buf, (const T *) src, 256 / sizeof(T), s, round + 1, SHMEM_SIGNAL_SET, next); \
        shmem_quiet();                                                                          \
        EXPECT_ROUND("shmem_" #N "_put_signal_nbi", (int) (256 / sizeof(T) * sizeof(T)));
#define X(N, T) TYPED(N, T)
        X(float, float) X(double, double) X(longdouble, long double) X(char, char)
        X(schar, signed char) X(short, short) X(int, int) X(long, long) X(longlong, long long)
        X(uchar, unsigned char) X(ushort, unsigned short) X(uint, unsigned int)
        X(ulong, unsigned long) X(ulonglong, unsigned long long)
        X(int8, int8_t) X(int16, int16_t) X(int32, int32_t) X(int64, int64_t)
        X(uint8, uint8_t) X(uint16, uint16_t) X(uint32, uint32_t) X(uint64, uint64_t)
        X(size, size_t) X(ptrdiff, ptrdiff_t)
#undef X

#define SIZED(S, B)                                                                             \
        shmem_barrier_all();                                                                    \
        shmem_put##S##_signal(buf, src, 256 / (B), s, round + 1, SHMEM_SIGNAL_SET, next);       \
        EXPECT_ROUND("shmem_put" #S "_signal", 256);                                            \
        shmem_put##S##_signal_nbi(buf, src, 256 / (B), s, round + 1, SHMEM_SIGNAL_SET, next);   \
        shmem_quiet();                                                                          \
        EXPECT_ROUND("shmem_put" #S "_signal_nbi", 256);
        SIZED(mem, 1) SIZED(8, 1) SIZED(16, 2) SIZED(32, 4) SIZED(64, 8) SIZED(128, 16)
#undef SIZED

        /* zero-length: only the signal is delivered */
        shmem_barrier_all();
        shmem_putmem_signal(buf, src, 0, s, round + 1, SHMEM_SIGNAL_SET, next);
        round++;
        CHECK(shmem_signal_wait_until(s, SHMEM_CMP_EQ, round) == round, "zero-length put_signal");
        shmem_barrier_all();

        free(src);
        shmem_free(s);
        shmem_free(buf);
    }

    shmem_free(hpay);
    shmem_free(hack);
    shmem_free(hsig);
    printf("[%d] test_signal: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
