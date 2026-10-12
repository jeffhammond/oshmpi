/* Written by Sayan Ghosh; per-variable MCS queue lock rewrite. */

#include <limits.h>
#include "shmem-internals.h"
#include "oshmpi-mcs-lock.h"

/* The OpenSHMEM lock is an MCS queue lock whose state lives in the lock variable itself, so
 * every lock variable is an independent lock and a PE may hold several at once.
 *
 * The variable is a symmetric long that the user initializes to zero on all PEs.  It is used
 * as two ints:
 *
 *   word[LOCK_LAST]   on PE 0 only: the queue tail, as (PE + 1); 0 means the lock is free.
 *   word[LOCK_STATE]  on every PE: bit 31 (LOCK_SIGNAL) is set by the PE ahead of us to pass
 *                     the lock on; the low 31 bits are the PE directly behind us, as (PE + 1).
 *
 * Both parts of LOCK_STATE are only ever OR-ed in by remote PEs, so the successor's link and
 * the predecessor's hand-off can arrive in any order.  A PE waits by polling its own
 * LOCK_STATE word, so there is no point-to-point traffic and no global state.  Waiters are
 * served in the order they swapped into the tail, which is the first-come, first-served
 * order the specification requires.  The calls are not reentrant, as in the specification:
 * a PE must not set a lock it already holds. */

_Static_assert(sizeof(long) >= 2 * sizeof(int), "the lock variable must hold two ints");

#define LOCK_LAST   0
#define LOCK_STATE  1
#define LOCK_SIGNAL INT_MIN
#define LOCK_NEXT_MASK INT_MAX

#define LOCK_WORD(lockp, idx) ((void *) &((volatile int *) (lockp))[idx])

/* The tail lives on PE 0. */
#define TAIL_PE 0

static int oshmpi_lock_state(OSHMPI_VOLATILE long * lockp)
{
  int state;
  oshmpi_fetch (MPI_INT, &state, LOCK_WORD (lockp, LOCK_STATE), shmem_world_rank);
  return state;
}

static void oshmpi_lock_reset_state(OSHMPI_VOLATILE long * lockp)
{
  /* Nothing can be writing our state now: a PE only links behind us or signals us while we
   * are queued, and we are not. */
  int zero = 0;
  oshmpi_set (MPI_INT, LOCK_WORD (lockp, LOCK_STATE), &zero, shmem_world_rank);
}

void oshmpi_lock(OSHMPI_VOLATILE long * lockp)
{
  int me1 = shmem_world_rank + 1;
  int prev;

  oshmpi_lock_reset_state (lockp);

  /* Take the tail, learning who was last in the queue. */
  oshmpi_swap (MPI_INT, &prev, LOCK_WORD (lockp, LOCK_LAST), &me1, TAIL_PE);

  if (prev != 0)
    {
      /* Tell the PE ahead of us that we are behind it, then wait to be handed the lock. */
      int old;
      oshmpi_fetch_op (MPI_INT, MPI_BOR, &old, LOCK_WORD (lockp, LOCK_STATE), &me1, prev - 1);

      while ((oshmpi_lock_state (lockp) & LOCK_SIGNAL) == 0)
        ;
    }
}

void oshmpi_unlock(OSHMPI_VOLATILE long * lockp)
{
  int me1 = shmem_world_rank + 1;
  int state = oshmpi_lock_state (lockp);

  /* The specification has clear_lock quiet the default context first, so that everything
   * done in the critical region is complete before the next PE sees the lock. */
  oshmpi_remote_sync ();

  if ((state & LOCK_NEXT_MASK) == 0)
    {
      /* No known successor: if we are still the tail, the queue becomes empty. */
      int zero = 0, old;
      oshmpi_cswap (MPI_INT, &old, LOCK_WORD (lockp, LOCK_LAST), &zero, &me1, TAIL_PE);
      if (old == me1)
        return;

      /* Someone swapped in behind us but has not linked itself yet; wait for the link. */
      while (((state = oshmpi_lock_state (lockp)) & LOCK_NEXT_MASK) == 0)
        ;
    }

  /* Hand the lock to the PE behind us. */
  int signal = LOCK_SIGNAL, old;
  oshmpi_fetch_op (MPI_INT, MPI_BOR, &old, LOCK_WORD (lockp, LOCK_STATE), &signal,
                   (state & LOCK_NEXT_MASK) - 1);
}

int oshmpi_trylock(OSHMPI_VOLATILE long * lockp)
{
  int me1 = shmem_world_rank + 1;
  int zero = 0, old;

  oshmpi_lock_reset_state (lockp);

  /* The lock is free only if the queue is empty: if the tail is 0, take it. */
  oshmpi_cswap (MPI_INT, &old, LOCK_WORD (lockp, LOCK_LAST), &me1, &zero, TAIL_PE);
  if (old != 0)
    {
      /* Callers retry in a loop; keep MPI progressing for the holder's RMA to us. */
      oshmpi_progress ();
      return 0;
    }
  return 1;
}
