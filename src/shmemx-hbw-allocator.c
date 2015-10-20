/* BSD-2 License.  Written by Jeff Hammond. */

#include "shmem-internals.h"
#include "shmemx.h"

#ifdef EXTENSION_HBW_ALLOCATOR

#ifdef EXTENSION_HBW_ALLOCATOR
# ifdef HAVE_HBWMALLOC_H
#  include <hbwmalloc.h>
# else
   /* These definitions are here for debugging only.
    * They must be removed before merging to master. */
   int hbw_check_available(void) { return 0; }
   void* hbw_malloc(size_t size) { return NULL; };
   void* hbw_calloc(size_t nmemb, size_t size) { return NULL; };
   void* hbw_realloc (void *ptr, size_t size) { return NULL; };
   void hbw_free(void *ptr) { return; };
   int hbw_posix_memalign(void **memptr, size_t alignment, size_t size) { return NULL; };
   int hbw_posix_memalign_psize(void **memptr, size_t alignment, size_t size, int pagesize) { return NULL; };
   int hbw_get_policy(void) { return 0; };
   int hbw_set_policy(int mode) { return -1; };
# endif
#endif

void * shmem_hbw_malloc(size_t size);
void * shmem_hbw_align(size_t alignment, size_t size);
void * shmem_hbw_realloc(void *ptr, size_t size);
void   shmem_hbw_free(void *ptr);

#else

static int shmemx_hbw_allocator_is_not_available = 0;

#endif
