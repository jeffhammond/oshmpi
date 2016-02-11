/*
 * shmem_barrier() test  barrier {-V} {loop-cnt}
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <shmem.h>

#define Rfprintf if (shmem_my_pe() == 0) fprintf
#define Rprintf if (shmem_my_pe() == 0)  printf
#define RDfprintf if (Verbose && shmem_my_pe() == 0) fprintf
#define RDprintf if (Verbose && shmem_my_pe() == 0)  printf

int Verbose;

int
main(int argc, char* argv[])
{
	int c, j,loops;
	int rank, num_ranks;
	char *prog_name;

	shmem_init();
	rank = shmem_my_pe();
	num_ranks = shmem_n_pes();
	if (num_ranks == 1) {
   		Rfprintf(stderr,
			"ERR - Requires > 1 PEs\n");
		shmem_finalize();
		return 0;
	}
	prog_name = strrchr(argv[0],'/');
	if ( prog_name )
		prog_name++;
	else
		prog_name = argv[0];

	while((c=getopt(argc,argv,"v")) != -1) {
		switch(c) {
		  case 'V':
			Verbose++;
			break;
		  default:
   			Rfprintf(stderr,"ERR - unknown -%c ?\n",c);
			shmem_finalize();
			return 1;
		}
	}

	if (optind == argc)
		loops = 30;
	else {
		loops = atoi(argv[optind++]);
		if (loops <= 0 || loops > 1000000) {
    			Rfprintf(stderr,
				"ERR - loops arg out of bounds '%d'?\n", loops);
			shmem_finalize();
			return 1;
		}
	}

	for(j=0; j < loops; j++) {
		//if ( j==0 || (j % 10) == 0 )
    		RDfprintf(stderr,"[%d] pre-barrier(%d)\n", rank,j);

		shmem_barrier_all();  /* sync sender and receiver */

		//if ( j==0 || (j % 10) == 0 )
    		RDfprintf(stderr,"[%d] post barrier(%d)\n", rank,j);
	}

        RDprintf ("%d(%d) Exit\n", rank, num_ranks);

        shmem_finalize();

	return 0;
}
