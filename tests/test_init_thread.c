/* shmem_init_thread at one requested level (built once per level with -DREQ=...): it must
 * succeed, return a valid level that shmem_query_thread agrees with, and the library must work
 * at that level.  When the level provided covers SERIALIZED or MULTIPLE, two threads also move
 * data: under a mutex for SERIALIZED, unsynchronized (to separate slots) for MULTIPLE. */
#include <pthread.h>
#include <shmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef REQ
#define REQ SHMEM_THREAD_SINGLE
#endif

static int errs, me, np, provided;
#define CHECK(c, m) do { if (!(c)) { printf("[%d] FAIL line %d: %s\n", me, __LINE__, m); errs++; } } while (0)

static int *slots;                       /* heap: one slot per thread */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
    long id = (long) arg;
    int next = (me + 1) % np;
    for (int k = 1; k <= 200; k++) {
        int v = (int) (id * 1000 + me * 10 + k);
        if (provided == SHMEM_THREAD_SERIALIZED) pthread_mutex_lock(&mu);
        shmem_int_p(&slots[id], v, next);
        shmem_quiet();
        int got = shmem_int_g(&slots[id], next);
        if (provided == SHMEM_THREAD_SERIALIZED) pthread_mutex_unlock(&mu);
        if (got != v) { printf("[%d] FAIL thread %ld: wrote %d, read back %d\n", me, id, v, got); errs++; break; }
    }
    return NULL;
}

int main(void)
{
    int rc = shmem_init_thread(REQ, &provided);
    me = shmem_my_pe();
    np = shmem_n_pes();
    CHECK(rc == 0, "shmem_init_thread returns 0");
    CHECK(provided >= SHMEM_THREAD_SINGLE && provided <= SHMEM_THREAD_MULTIPLE, "provided is a valid level");
    int q = -1;
    shmem_query_thread(&q);
    CHECK(q == provided, "query_thread agrees with provided");
    if (me == 0) printf("test_init_thread: requested %d, provided %d\n", REQ, provided);

    int next = (me + 1) % np, prev = (me + np - 1) % np;
    int *x = shmem_calloc(1, sizeof(int));
    slots = shmem_calloc(2, sizeof(int));
    shmem_barrier_all();
    shmem_int_p(x, 100 + me, next);
    shmem_barrier_all();
    CHECK(*x == 100 + prev, "p/barrier on the main thread");
    CHECK(shmem_int_g(x, next) == 100 + me, "g on the main thread");
    shmem_int_atomic_add(x, 1, next);
    shmem_barrier_all();
    CHECK(*x == 101 + prev, "atomic on the main thread");

    if (REQ >= SHMEM_THREAD_SERIALIZED && provided >= REQ) {
        pthread_t t[2];
        for (long i = 0; i < 2; i++) pthread_create(&t[i], NULL, worker, (void *) i);
        for (int i = 0; i < 2; i++) pthread_join(t[i], NULL);
    }
    shmem_barrier_all();
    shmem_free(slots);
    shmem_free(x);
    printf("[%d] test_init_thread(%d): %s\n", me, REQ, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
