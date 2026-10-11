/* Many threads per PE issuing RMA at once under SHMEM_THREAD_MULTIPLE.  Open MPI's
 * osc/ucx hangs or crashes on this unless OSHMPI serializes the RMA calls.
 *
 * The threads all wait at a barrier before returning.  Open MPI's osc/ucx also frees a
 * thread's UCX context when the thread exits, and does so without synchronizing with other
 * threads that are still issuing RMA, so a thread that exits early can crash a PE (seen in
 * ctx_flush / _tlocal_ctx_rec_cleanup).  That is independent of OSHMPI, so the test avoids
 * it and checks what OSHMPI is responsible for. */
#include <shmem.h>
#include <pthread.h>
#include <stdio.h>

#define T 8
#define N 500

static pthread_barrier_t bar;
static int dest;
static int me, np;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < N; i++) {
        int pe = (me + i) % np;
        shmem_int_atomic_add(&dest, 1, pe);
        (void)shmem_int_g(&dest, pe);
    }
    pthread_barrier_wait(&bar);
    return NULL;
}

int main(void)
{
    int tl;
    if (shmem_init_thread(SHMEM_THREAD_MULTIPLE, &tl) != 0 || tl != SHMEM_THREAD_MULTIPLE) {
        printf("test_mt_rma: SHMEM_THREAD_MULTIPLE not available, skipped\n");
        shmem_finalize();
        return 0;
    }
    me = shmem_my_pe();
    np = shmem_n_pes();

    pthread_barrier_init(&bar, NULL, T);
    pthread_t th[T];
    for (int i = 0; i < T; i++) pthread_create(&th[i], NULL, worker, NULL);
    for (int i = 0; i < T; i++) pthread_join(th[i], NULL);
    shmem_barrier_all();

    int errs = (dest != T * N);
    if (errs) printf("[%d] dest %d, expected %d\n", me, dest, T * N);
    printf("[%d] test_mt_rma: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs;
}
