/* Written by Sayan Ghosh. */

#ifndef OSHMPI_MCS_LOCK_H
#define OSHMPI_MCS_LOCK_H

#include "shmem-internals.h"

/* MPI Lock */
extern MPI_Win oshmpi_lock_win;
extern int * oshmpi_lock_base;

typedef struct oshmpi_lock_s
{
  int prev;
  int next;
} oshmpi_lock_t;

void oshmpi_allock(MPI_Comm comm);
void oshmpi_deallock(void);
void oshmpi_lock(OSHMPI_VOLATILE long * lockp);
void oshmpi_unlock(OSHMPI_VOLATILE long * lockp);
int  oshmpi_trylock(OSHMPI_VOLATILE long * lockp);

#endif /* OSHMPI_MCS_LOCK_H */
