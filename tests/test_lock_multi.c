/* Locks are per variable: distinct locks are independent, may be held together, and each one
 * gives mutual exclusion and first-come, first-served ordering. */
#include <shmem.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define ITERS 200

static long static_lock;          /* lock in the data segment */
static long static_lock_b;
static int  static_counter;       /* protected by static_lock, lives on PE 0 */
static int  order;                /* FIFO check: service position counter, on PE 0 */
static int  errs;

#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

int main(void)
{
    shmem_init();
    int me = shmem_my_pe(), np = shmem_n_pes();
    long *heap_lock = shmem_calloc(1, sizeof(long));
    int  *heap_counter = shmem_calloc(1, sizeof(int));

    /* 1. Holding one lock does not block acquiring a different one (single PE). */
    shmem_set_lock(&static_lock);
    shmem_set_lock(&static_lock_b);
    shmem_set_lock(heap_lock);
    shmem_clear_lock(heap_lock);
    shmem_clear_lock(&static_lock_b);
    shmem_clear_lock(&static_lock);
    shmem_barrier_all();

    /* 2. test_lock: free lock is acquired (returns 0), held lock is refused (returns 1),
     *    and a different lock is unaffected by it. */
    if (me == 0) {
        CHECK(shmem_test_lock(&static_lock) == 0, "test_lock on a free lock");
        CHECK(shmem_test_lock(&static_lock_b) == 0, "test_lock on a different free lock");
        shmem_clear_lock(&static_lock_b);
    }
    shmem_barrier_all();
    if (me != 0) CHECK(shmem_test_lock(&static_lock) == 1, "test_lock on a held lock");
    shmem_barrier_all();
    if (me == 0) shmem_clear_lock(&static_lock);
    shmem_barrier_all();

    /* 3. Mutual exclusion: unprotected read-modify-write of a counter on PE 0, under a lock
     *    in the data segment and under a lock on the heap, with a second lock held across it. */
    for (int i = 0; i < ITERS; i++) {
        shmem_set_lock(&static_lock);
        shmem_set_lock(heap_lock);
        int v = shmem_int_g(&static_counter, 0);
        shmem_int_p(&static_counter, v + 1, 0);
        shmem_clear_lock(heap_lock);
        shmem_clear_lock(&static_lock);

        shmem_set_lock(heap_lock);
        int w = shmem_int_g(heap_counter, 0);
        shmem_int_p(heap_counter, w + 1, 0);
        shmem_clear_lock(heap_lock);
    }
    shmem_barrier_all();
    if (me == 0) {
        CHECK(static_counter == np * ITERS, "counter under the static lock");
        CHECK(*heap_counter == np * ITERS, "counter under the heap lock");
    }

    /* 4. test_lock spin with contention, then reuse of the same lock after it drained. */
    int got = 0;
    while (got < ITERS) {
        if (shmem_test_lock(&static_lock_b) == 0) {
            int v = shmem_int_g(&static_counter, 0);
            shmem_int_p(&static_counter, v + 1, 0);
            shmem_clear_lock(&static_lock_b);
            got++;
        }
    }
    shmem_barrier_all();
    if (me == 0) CHECK(static_counter == 2 * np * ITERS, "counter under test_lock");

    /* 5. First-come, first-served.  PE 0 holds the lock while PEs 1..np-1 arrive in rank
     *    order, 50 ms apart; once PE 0 lets go, they must be served in that order. */
    shmem_barrier_all();
    if (me == 0) shmem_set_lock(&static_lock);
    shmem_barrier_all();
    if (me != 0) {
        usleep(50000 * me);
        shmem_set_lock(&static_lock);
        int pos = shmem_int_atomic_fetch_inc(&order, 0);
        CHECK(pos == me - 1, "lock was not granted in arrival order");
        shmem_clear_lock(&static_lock);
    } else {
        /* Keep polling (which makes MPI progress) for the whole hold time, so the others'
         * queue operations on this PE complete as soon as they are issued. */
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        do {
            (void) shmem_int_atomic_fetch(&order, 0);
            clock_gettime(CLOCK_MONOTONIC, &t1);
        } while ((t1.tv_sec - t0.tv_sec) + 1e-9 * (t1.tv_nsec - t0.tv_nsec) < 0.05 * (np + 2));
        shmem_clear_lock(&static_lock);
    }
    shmem_barrier_all();

    shmem_free(heap_counter);
    shmem_free(heap_lock);
    printf("[%d] test_lock_multi: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
