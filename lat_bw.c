#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <shmem.h>

#define DEFAULT_TEST 		1
#define DEFAULT_ITERS		10
#define DEFAULT_LOG2_SIZE	8

int _world_rank, _world_size;

/* Aggregate b/w and latency */
/* long* msg_size bytes is sent from lo_rank to lo_rank+1, lo_rank+2 .... hi_rank 
   all other processes sends data back to lo_rank
 */
long pSync0[_SHMEM_BARRIER_SYNC_SIZE],
     pSync1[_SHMEM_BARRIER_SYNC_SIZE],
     pSync2[_SHMEM_BARRIER_SYNC_SIZE];
double pWrk0[_SHMEM_REDUCE_MIN_WRKDATA_SIZE], 
       pWrk1[_SHMEM_REDUCE_MIN_WRKDATA_SIZE],
       pWrk2[_SHMEM_REDUCE_MIN_WRKDATA_SIZE];    

void ping_pong_lbw(int lo_rank, 
		int hi_rank, 
		int logPE_stride,
		unsigned int msg_size /* actual msg size is 2^msg_size */)
{
	if ((hi_rank - lo_rank + 1) > _world_size)
		return;

	unsigned int nelems_long = (1 << msg_size);

	long * sendbuf = shmalloc(nelems_long*sizeof(long));
	long * recvbuf = shmalloc(nelems_long*sizeof(long));
	/* Initialize arrays */
	for (int i = 0; i < nelems_long; i++) {
		sendbuf[i] = i;
		recvbuf[i] = -99;
	}
	shmem_barrier(lo_rank, logPE_stride, (hi_rank - lo_rank + 1), pSync1);
	
	double time_start = shmem_wtime();
	
	/* From PE lo_rank+1 till hi_rank with increments of logPE_stride << 1 */
	if (_world_rank == lo_rank) {
		for (int i = lo_rank+1; i < hi_rank+1; i+=(1 << logPE_stride))  
			shmem_long_put(recvbuf, sendbuf, nelems_long, i);
	}
	else {
#if DEBUG > 2		
	       	printf("[%d]: Waiting for puts to complete from rank = %d\n", _world_rank, lo_rank);	
#endif	
		for (int i = 0; i < nelems_long; i++)
			shmem_wait(&recvbuf[i], i);
	}
	
	for (int i = 0; i < nelems_long; i++) 
		recvbuf[i] = -99;
	
	shmem_barrier(lo_rank, logPE_stride, (hi_rank - lo_rank + 1), pSync0);

	/* From rest of the PEs to PE lo_rank */
	if (_world_rank != lo_rank) {
		shmem_long_put(sendbuf, recvbuf, nelems_long, lo_rank);
	}
	else { 
#if DEBUG > 2		
	       	printf("[%d]: Waiting for puts to complete from rank = %d\n", lo_rank, _world_rank);	
#endif	
		for (int i = 0; i < nelems_long; i++)
			shmem_wait(&sendbuf[i], -99);
	}
	shmem_barrier(lo_rank, logPE_stride, (hi_rank - lo_rank + 1), pSync1);
	double time_end = shmem_wtime();

	/* Compute average by reducing this total_clock_time */
	double clock_time_PE = time_end - time_start;
	double total_clock_time, max_clock_time, min_clock_time;
	shmem_double_sum_to_all(&total_clock_time, &clock_time_PE, 1, 
			0, 0, (hi_rank-lo_rank+1), pWrk0, pSync0);
	shmem_double_max_to_all(&max_clock_time, &clock_time_PE, 1, 
			0, 0, (hi_rank-lo_rank+1), pWrk1, pSync1);
	shmem_double_min_to_all(&min_clock_time, &clock_time_PE, 1, 
			0, 0, (hi_rank-lo_rank+1), pWrk2, pSync2);    
	//MPI_Reduce(&clock_time_PE, &total_clock_time, 1, MPI_DOUBLE, MPI_SUM, 0, SHMEM_COMM_WORLD);
	if (_world_rank == 0) {
		printf("Avg. Latency : %f, Avg. Bandwidth: %f MB/s for Message size: %lu bytes\n", (total_clock_time/_world_size), 
				(double)((nelems_long*sizeof(long))/(double)(total_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Max. Latency : %f, Min. Bandwidth: %f MB/s for Message size: %lu bytes\n", (max_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(max_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Min. Latency : %f, Max. Bandwidth: %f MB/s for Message size: %lu bytes\n", (min_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(min_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
	}
	/* Verify */
	if (_world_rank == 0) {

	}

	shfree(sendbuf);
	shfree(recvbuf);

	return;

}

/* long* msg_size bytes is PUT from lo_rank to lo_rank+1, lo_rank+1 to lo_rank+2 .... hi_rank to lo_rank
 * in the fashion of a natural ring
 */
void natural_ring_lbw (unsigned int msg_size /* actual msg size is 2^msg_size */)
{
	unsigned int nelems_long = (1 << msg_size);

	long * sendbuf = shmalloc(nelems_long*sizeof(long));
	long * recvbuf = shmalloc(nelems_long*sizeof(long));

	/* Initialize arrays */
	for (int i = 0; i < nelems_long; i++) {
		sendbuf[i] = i;
		recvbuf[i] = -99;
	}

	unsigned int target_rank = (_world_rank + 1)%_world_size;

	double time_start = shmem_wtime();

	shmem_long_put(sendbuf, recvbuf, nelems_long, target_rank);

	shmem_fence();

	shmem_long_get(recvbuf, sendbuf, nelems_long, target_rank);

	shmem_fence();    

	double time_end = shmem_wtime();

	/* Compute average by reducing this total_clock_time */
	double clock_time_PE = time_end - time_start;
	double total_clock_time, max_clock_time, min_clock_time;
	shmem_double_sum_to_all(&total_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk0, pSync0);
	shmem_double_max_to_all(&max_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk1, pSync1);
	shmem_double_min_to_all(&min_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk2, pSync2);    
	//MPI_Reduce(&clock_time_PE, &total_clock_time, 1, MPI_DOUBLE, MPI_SUM, 0, SHMEM_COMM_WORLD);
	if (_world_rank == 0) {
		printf("Avg. Latency : %f, Avg. Bandwidth: %f MB/s for Message size: %lu bytes\n", (total_clock_time/_world_size), 
				(double)((nelems_long*sizeof(long))/(double)(total_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Max. Latency : %f, Min. Bandwidth: %f MB/s for Message size: %lu bytes\n", (max_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(max_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Min. Latency : %f, Max. Bandwidth: %f MB/s for Message size: %lu bytes\n", (min_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(min_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
	}
	/* Verify */
	if (_world_rank == 0) {

	}

	shfree(sendbuf);
	shfree(recvbuf);

	return;

}

/* target_rank = world_size - world_rank */
void link_contended_lbw (unsigned int msg_size /* actual msg size is 2^msg_size */)
{
	unsigned int nelems_long = (1 << msg_size);

	long * sendbuf = shmalloc(nelems_long*sizeof(long));
	long * recvbuf = shmalloc(nelems_long*sizeof(long));

	/* Initialize arrays */
	for (int i = 0; i < nelems_long; i++) {
		sendbuf[i] = i;
		recvbuf[i] = -99;
	}

	unsigned int target_rank = (_world_size - _world_rank - 1);

	double time_start = shmem_wtime();

	shmem_long_put(recvbuf, sendbuf, nelems_long, target_rank);

	shmem_quiet();

	shmem_long_get(sendbuf, recvbuf, nelems_long, target_rank);

	shmem_barrier_all();

	double time_end = shmem_wtime();
	/* Compute average by reducing this total_clock_time */
	double clock_time_PE = time_end - time_start;
	double total_clock_time, max_clock_time, min_clock_time;
	shmem_double_sum_to_all(&total_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk0, pSync0);
	shmem_double_max_to_all(&max_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk1, pSync1);
	shmem_double_min_to_all(&min_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk2, pSync2);    
	//MPI_Reduce(&clock_time_PE, &total_clock_time, 1, MPI_DOUBLE, MPI_SUM, 0, SHMEM_COMM_WORLD);
	if (_world_rank == 0) {
		printf("Avg. Latency : %f, Avg. Bandwidth: %f MB/s for Message size: %lu bytes\n", (total_clock_time/_world_size), 
				(double)((nelems_long*sizeof(long))/(double)(total_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Max. Latency : %f, Min. Bandwidth: %f MB/s for Message size: %lu bytes\n", (max_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(max_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Min. Latency : %f, Max. Bandwidth: %f MB/s for Message size: %lu bytes\n", (min_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(min_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
	}
	/* Verify */
	if (_world_rank == 0) {

	}

	shfree(sendbuf);
	shfree(recvbuf);

	return;

}

/* scatter/gather: send/receive data from PE_root to/from all other PEs */
void scatter_gather_lbw (int PE_root,
		int logPE_stride,
		int scatter_or_gather, /* 1 for scatter, 0 for gather */
		unsigned int msg_size /* actual msg size is 2^msg_size */)
{
	long * sendbuf, * recvbuf;
	if (PE_root < 0 && PE_root >= _world_size)
		return;

	unsigned int nelems_long = (1 << msg_size); 

	if (scatter_or_gather) {
		sendbuf = shmalloc(nelems_long*_world_size*sizeof(long));
		recvbuf = shmalloc(nelems_long*sizeof(long));

		/* Initialize arrays */
		for (int i = 0; i < nelems_long; i++) {
			recvbuf[i] = -99;
		}

		for (int i = 0; i < (nelems_long*_world_size); i++) {
			sendbuf[i] = i;
		}  

	}
	else {
		sendbuf = shmalloc(nelems_long*sizeof(long));
		recvbuf = shmalloc(nelems_long * _world_size * sizeof(long));

		/* Initialize arrays */
		for (int i = 0; i < (nelems_long*_world_size); i++) {
			recvbuf[i] = -99;
		}

		for (int i = 0; i < nelems_long; i++) {
			sendbuf[i] = i;
		}  

	}

	shmem_barrier_all();

	double time_start = shmem_wtime();

	if (_world_rank == PE_root && scatter_or_gather) {/* Scatter to rest of the PEs */
		for (int i = 0; i < _world_size; i+=(1 << logPE_stride)) {
			if (i != PE_root)
				shmem_long_put((sendbuf+i*nelems_long), recvbuf, nelems_long, i);
		}
	}

	if (_world_rank == PE_root && !scatter_or_gather) {/* Gather from rest of the PEs */
		for (int i = 0; i < _world_size; i++) {
			if (i != PE_root)
				shmem_long_get((recvbuf+i*nelems_long), sendbuf, nelems_long, i);
		}
	}

	shmem_barrier_all();

	double time_end = shmem_wtime();  

	/* Compute average by reducing this total_clock_time */
	double clock_time_PE = time_end - time_start;
	double total_clock_time, max_clock_time, min_clock_time;
	shmem_double_sum_to_all(&total_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk0, pSync0);
	shmem_double_max_to_all(&max_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk1, pSync1);
	shmem_double_min_to_all(&min_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk2, pSync2);    
	//MPI_Reduce(&clock_time_PE, &total_clock_time, 1, MPI_DOUBLE, MPI_SUM, 0, SHMEM_COMM_WORLD);
	if (_world_rank == 0) {
		printf("Avg. Latency : %f, Avg. Bandwidth: %f MB/s for Message size: %lu bytes\n", (total_clock_time/_world_size), 
				(double)((nelems_long*sizeof(long))/(double)(total_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Max. Latency : %f, Min. Bandwidth: %f MB/s for Message size: %lu bytes\n", (max_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(max_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Min. Latency : %f, Max. Bandwidth: %f MB/s for Message size: %lu bytes\n", (min_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(min_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
	}
	/* Verify */
	if (_world_rank == 0) {

	}

	shfree(sendbuf);
	shfree(recvbuf);

	return;

}

/* all-to-all pattern */
void a2a_lbw (int logPE_stride,
		unsigned int msg_size /* actual msg size is 2^msg_size */)
{	
	unsigned int nelems_long = (1 << msg_size); 
	
	long * sendbuf = shmalloc(nelems_long*_world_size*sizeof(long));
	long * recvbuf = shmalloc(nelems_long*_world_size*sizeof(long));

	/* Initialize arrays */
	for (int i = 0; i < (nelems_long*_world_size); i++) {
		recvbuf[i] = -99;
		sendbuf[i] = i;
	}

	shmem_barrier_all();

	double time_start = shmem_wtime();

	for (int i = 0; i < _world_size; i+=(1 << logPE_stride)) {
		shmem_long_put((sendbuf+i*nelems_long), (recvbuf+i*nelems_long), nelems_long, i);
	}

	shmem_barrier_all();

	double time_end = shmem_wtime();  

	/* Compute average by reducing this total_clock_time */
	double clock_time_PE = time_end - time_start;
	double total_clock_time, max_clock_time, min_clock_time;
	shmem_double_sum_to_all(&total_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk0, pSync0);
	shmem_double_max_to_all(&max_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk1, pSync1);
	shmem_double_min_to_all(&min_clock_time, &clock_time_PE, 1, 
			0, 0, _world_size, pWrk2, pSync2);    
	//MPI_Reduce(&clock_time_PE, &total_clock_time, 1, MPI_DOUBLE, MPI_SUM, 0, SHMEM_COMM_WORLD);
	if (_world_rank == 0) {
		printf("Avg. Latency : %f, Avg. Bandwidth: %f MB/s for Message size: %lu bytes\n", (total_clock_time/_world_size), 
				(double)((nelems_long*sizeof(long))/(double)(total_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Max. Latency : %f, Min. Bandwidth: %f MB/s for Message size: %lu bytes\n", (max_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(max_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
		printf("Min. Latency : %f, Max. Bandwidth: %f MB/s for Message size: %lu bytes\n", (min_clock_time), 
				(double)((nelems_long*sizeof(long))/(double)(min_clock_time * 1024 * 1024)), nelems_long*sizeof(long));
	}
	/* Verify */
	if (_world_rank == 0) {

	}

	shfree(sendbuf);
	shfree(recvbuf);

	return;

}

int main(int argc, char * argv[])
{
	/* DEFAULT */
	unsigned int log2_size = DEFAULT_LOG2_SIZE;
	int which_test = DEFAULT_TEST;
	int iterations = DEFAULT_ITERS;

	/*OpenSHMEM initilization*/
	start_pes (0);
	_world_size = _num_pes ();
	_world_rank = _my_pe ();
	/* wait for user to input runtime params */
	for(int j=0; j < _SHMEM_BARRIER_SYNC_SIZE; j++) {
		pSync0[j] = pSync1[j] = pSync2[j] = _SHMEM_SYNC_VALUE;
	} 

	/* argument processing */
	if (argc < 2) {
		if (_world_rank == 0)
			printf ("Expected: ./a.out <log-base-2-start-size> <1-7> <iterations>..using defaults\n");
	}
	else {
		log2_size = atoi(argv[1]);
		which_test = atoi(argv[2]);
		iterations = atoi(argv[3]);
	}
	
	iterations = 1 << iterations;
	if(iterations <= log2_size)
		iterations *= log2_size;
#if SHMEM_DEBUG > 1	
	if (_world_rank == 0)
		printf("log2_start_size = %u, which_test = %d and iterations = %d\n", 
				log2_size, which_test, iterations);
#endif

	switch(which_test) {
		case 1:
			if (_world_rank == 0)
				printf("Ping-Pong tests\n");
			for (int i = log2_size; i < iterations; i*=2)
				ping_pong_lbw(0,_world_size-1, 0, i);
			break;
		case 2:
			if (_world_rank == 0)
				printf("Natural ring test\n");
			for (int i = log2_size; i < iterations; i*=2)
				natural_ring_lbw (i);
			break;
		case 3:
			if (_world_rank == 0)
				printf("Link contended test\n");
			for (int i = log2_size; i < iterations; i*=2)
				link_contended_lbw (i);
			break;
		case 4:
			if (_world_rank == 0)
				printf("Scatter test\n");
			for (int i = log2_size; i < iterations; i*=2)
				scatter_gather_lbw (0, 0, 1, i);
			break;
		case 5:
			if (_world_rank == 0)
				printf("Gather test\n");
			for (int i = log2_size; i < iterations; i*=2)
				scatter_gather_lbw (0, 0, 0, i);
			break;
		case 6:
			if (_world_rank == 0)
				printf("All-to-all test\n");
			for (int i = log2_size; i < iterations; i*=2)
				a2a_lbw (0, i);
			break;
		case 7:
			if (_world_rank == 0)
				printf("Ping-Pong tests\n");
			for (int i = log2_size; i < iterations; i*=2)
				ping_pong_lbw(0,_world_size-1, 0, i);
			if (_world_rank == 0)
				printf("Natural ring test\n");
			for (int i = log2_size; i < iterations; i*=2)
				natural_ring_lbw (i);
			if (_world_rank == 0)
				printf("Link contended test\n");
			for (int i = log2_size; i < iterations; i*=2)
				link_contended_lbw (i);
			if (_world_rank == 0)
				printf("Scatter test\n");
			for (int i = log2_size; i < iterations; i*=2)
				scatter_gather_lbw (0, 0, 1, i);
			if (_world_rank == 0)
				printf("Gather test\n");
			for (int i = log2_size; i < iterations; i*=2)
				scatter_gather_lbw (0, 0, 0, i);
			if (_world_rank == 0)
				printf("All-to-all test\n");
			for (int i = log2_size; i < iterations; i*=2)
				a2a_lbw (0, i);
			break;
		default:
			printf("Enter a number between 1-7\n");
	}

	return 0;
}