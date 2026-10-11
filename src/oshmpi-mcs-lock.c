/* Written by Sayan Ghosh. */

#include "shmem-internals.h"
#include "oshmpi-mcs-lock.h"

#define NEXT_DISP 1
#define PREV_DISP 0
#define TAIL_DISP 2 //Only has meaning in PE 0
#define LOCK_DISP 3
#define TAIL 	  0

MPI_Win oshmpi_lock_win = MPI_WIN_NULL;
int * oshmpi_lock_base = NULL;

void oshmpi_allock(MPI_Comm comm)
{
  MPI_Info lock_info=MPI_INFO_NULL;
  MPI_Info_create(&lock_info);

  /* We define the sheap size to be symmetric and assume it for the global static data. */
  MPI_Info_set(lock_info, "same_size", "true");

  MPI_Win_allocate (4 * sizeof (int), sizeof (int), lock_info,
		    comm, &oshmpi_lock_base, &oshmpi_lock_win);
  oshmpi_lock_base[NEXT_DISP] = -1;
  oshmpi_lock_base[PREV_DISP] = -1;
  oshmpi_lock_base[TAIL_DISP] = -1;
  oshmpi_lock_base[LOCK_DISP] = -1;
  MPI_Win_lock_all (TAIL, oshmpi_lock_win);

  MPI_Info_free(&lock_info);

  return;
}

void oshmpi_deallock(void)
{
  MPI_Win_unlock_all (oshmpi_lock_win);
  MPI_Win_free (&oshmpi_lock_win);
  return;
}

#define LOCK_TAG 999

/* The lock is an MCS queue lock: TAIL_DISP (on rank TAIL) holds the last rank in the queue,
 * NEXT_DISP (on each rank) holds the rank that queued directly behind it, and the
 * lock is handed to that rank with a message. */

void oshmpi_lock(OSHMPI_VOLATILE long * lockp)
{
  oshmpi_lock_t *lock = (oshmpi_lock_t *) lockp;
  int token;
  /* Replace the tail with myself, learning who was last */
  OSHMPI_MPI_RMA(MPI_Fetch_and_op (&shmem_world_rank, &(lock->prev), MPI_INT, TAIL,
		    TAIL_DISP, MPI_REPLACE, oshmpi_lock_win));
  OSHMPI_MPI_RMA(MPI_Win_flush (TAIL, oshmpi_lock_win));

  /* Previous proc holding lock will eventually notify */
  if (lock->prev != -1)
    {
      /* Tell the previous proc that I am behind it, then wait for the hand-off */
      OSHMPI_MPI_RMA(MPI_Accumulate (&shmem_world_rank, 1, MPI_INT, lock->prev, NEXT_DISP,
		      1, MPI_INT, MPI_REPLACE, oshmpi_lock_win));
      OSHMPI_MPI_RMA(MPI_Win_flush (lock->prev, oshmpi_lock_win));
      MPI_Recv (&token, 1, MPI_INT, lock->prev, LOCK_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
  /* Hold lock */
  oshmpi_lock_base[LOCK_DISP] = 1;
  OSHMPI_MPI_RMA(MPI_Win_sync (oshmpi_lock_win));

  return;
}

void oshmpi_unlock(OSHMPI_VOLATILE long * lockp)
{
  oshmpi_lock_t *lock = (oshmpi_lock_t *) lockp;
  int nil = -1;

  /* Release lock */
  oshmpi_lock_base[LOCK_DISP] = -1;
  OSHMPI_MPI_RMA(MPI_Win_sync (oshmpi_lock_win));

  /* Determine my next process */
  OSHMPI_MPI_RMA(MPI_Fetch_and_op (NULL, &(lock->next), MPI_INT, shmem_world_rank,
		    NEXT_DISP, MPI_NO_OP, oshmpi_lock_win));
  OSHMPI_MPI_RMA(MPI_Win_flush (shmem_world_rank, oshmpi_lock_win));

  if (lock->next == -1)
    {
      /* No known successor: if I am still the tail, the queue is now empty. */
      int old_tail = -1;
      OSHMPI_MPI_RMA(MPI_Compare_and_swap (&nil, &shmem_world_rank, &old_tail, MPI_INT,
			    TAIL, TAIL_DISP, oshmpi_lock_win));
      OSHMPI_MPI_RMA(MPI_Win_flush (TAIL, oshmpi_lock_win));
      if (old_tail == shmem_world_rank)
	return;

      /* Someone queued behind me but has not yet told me; wait for it. */
      while (lock->next == -1)
	{
	  OSHMPI_MPI_RMA(MPI_Fetch_and_op (NULL, &(lock->next), MPI_INT, shmem_world_rank,
			    NEXT_DISP, MPI_NO_OP, oshmpi_lock_win));
	  OSHMPI_MPI_RMA(MPI_Win_flush (shmem_world_rank, oshmpi_lock_win));
	  if (lock->next == -1) oshmpi_progress ();
	}
    }

  /* Reset my next slot for the next time I queue, then hand over the lock */
  OSHMPI_MPI_RMA(MPI_Accumulate (&nil, 1, MPI_INT, shmem_world_rank, NEXT_DISP,
		  1, MPI_INT, MPI_REPLACE, oshmpi_lock_win));
  OSHMPI_MPI_RMA(MPI_Win_flush (shmem_world_rank, oshmpi_lock_win));
  MPI_Send (&shmem_world_rank, 1, MPI_INT, lock->next, LOCK_TAG, MPI_COMM_WORLD);

  return;
}

int oshmpi_trylock(OSHMPI_VOLATILE long * lockp)
{
  int nil = -1, prev = -1;
  oshmpi_lock_t *lock = (oshmpi_lock_t *) lockp;
  /* The lock is free only if the queue is empty: if the tail is -1, take it. */
  OSHMPI_MPI_RMA(MPI_Compare_and_swap (&shmem_world_rank, &nil, &prev, MPI_INT,
			TAIL, TAIL_DISP, oshmpi_lock_win));
  OSHMPI_MPI_RMA(MPI_Win_flush (TAIL, oshmpi_lock_win));
  lock->prev = prev;
  if (prev != -1)
    {
      /* Callers retry in a loop; keep MPI progressing for the lock holder's RMA to us. */
      oshmpi_progress ();
      return 0;
    }

  /* Hold lock */
  oshmpi_lock_base[LOCK_DISP] = 1;
  OSHMPI_MPI_RMA(MPI_Win_sync (oshmpi_lock_win));

  return 1;
}
