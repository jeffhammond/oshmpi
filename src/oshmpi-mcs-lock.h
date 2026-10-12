/* Written by Sayan Ghosh. */

#ifndef OSHMPI_MCS_LOCK_H
#define OSHMPI_MCS_LOCK_H

#include "shmem-internals.h"

void oshmpi_lock(OSHMPI_VOLATILE long * lockp);
void oshmpi_unlock(OSHMPI_VOLATILE long * lockp);
/* returns 1 if the lock was acquired, 0 if it is held */
int  oshmpi_trylock(OSHMPI_VOLATILE long * lockp);

#endif /* OSHMPI_MCS_LOCK_H */
