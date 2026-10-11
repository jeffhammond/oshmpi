/* team collectives: placeholder; replaced by the real test. */
#include <shmem.h>
#include <stdio.h>

int main(void)
{
    shmem_init();
    printf("[%d] test_team_coll: OK (placeholder)\n", shmem_my_pe());
    fflush(stdout);
    shmem_finalize();
    return 0;
}
