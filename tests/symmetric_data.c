#include <stdio.h>
#include <stdlib.h>
#include <shmem.h>

int a = 1;
static int b = 2;
int c;
static int d;

int main(int argc, char* argv[])
{
    c = 3;
    d = 4;

    shmem_init();

    printf("&a = %p\n", &a);
    printf("&b = %p\n", &b);
    printf("&c = %p\n", &c);
    printf("&d = %p\n", &d);

    int me = shmem_my_pe();
    int np = shmem_n_pes();

    shmem_barrier_all();

    int* p[4] = {&a,&b,&c,&d};
    for (int i=0; i<np; i++) {
        for (int j=0; j<4; j++) {
            int accessible = shmem_addr_accessible(p[j],i);
            printf("address %p (p[%d]) %s accessible at PE %d\n",
                    p[j], j, accessible ? "is" : "is not", i);
        }
    }

    shmem_barrier_all();

    shmem_finalize();

    return 0;
}
