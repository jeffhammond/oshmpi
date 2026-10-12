/* PEs that poll their own memory (atomic_fetch, wait_until, test_lock) must still let
 * remote atomics and lock hand-offs targeting them complete. */
#include <shmem.h>
#include <stdio.h>

static int  ring = -1;       /* data-segment variables */
static int  flag = 0;
static int  count = 0;
static long lock = 0;

int main(void)
{
    shmem_init();
    int me = shmem_my_pe(), np = shmem_n_pes(), errs = 0;

    /* ring: PE0 starts it, each PE spins on atomic_fetch of its own variable, then sets the next PE's */
    if (me == 0) shmem_int_atomic_set(&ring, np - 1, 0);
    while (shmem_int_atomic_fetch(&ring, me) < 0) ;
    shmem_int_atomic_set(&ring, me, (me + 1) % np);
    shmem_barrier_all();
    if (ring != (me + np - 1) % np) { printf("[%d] ring got %d\n", me, ring); errs++; }

    /* wait_until: every PE is woken by a remote atomic from the next PE */
    shmem_barrier_all();
    shmem_int_atomic_add(&flag, 1, (me + 1) % np);
    shmem_int_wait_until(&flag, SHMEM_CMP_GE, 1);

    /* test_lock: spin for the lock while its holder updates every PE */
    shmem_barrier_all();
    while (count < np) {
        if (shmem_test_lock(&lock) == 0) {
            for (int pe = 0; pe < np; pe++) shmem_int_atomic_add(&count, 1, pe);
            shmem_clear_lock(&lock);
            shmem_int_wait_until(&count, SHMEM_CMP_GE, np);
        }
    }
    shmem_barrier_all();
    if (count != np) { printf("[%d] count %d != %d\n", me, count, np); errs++; }

    printf("[%d] test_spin: %s\n", me, errs ? "FAILED" : "OK");
    fflush(stdout);
    shmem_finalize();
    return errs != 0;
}
