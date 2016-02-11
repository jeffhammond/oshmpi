OSHCC = oshcc
CFLAGS = -g -O0 -Wall

UNIT_TESTS = accessible_ping alltoall alltoalls asym_alloc atomic_inc barrier bcast bcast_flood big_reduction bigget bigput c11_test_shmem_add c11_test_shmem_fetch c11_test_shmem_g c11_test_shmem_get c11_test_shmem_inc c11_test_shmem_p c11_test_shmem_put c11_test_shmem_set circular_shift cswap fcollect64 get1 get_g get_nbi global_exit hello ipgm iput-iget iput128 iput32 iput64 iput_double iput_float iput_long iput_longdouble iput_longlong iput_short lfinc max_reduction micro_unit_shmem mt_a2a ns ping pingpong-short pingpong put1 put_nbi set_fetch set_lock shmalloc shmem_ct shmem_info shmemalign shrealloc spam sping strided_put swap1 swapm test_lock test_lock_cswap to_all waituntil

PERF_TESTS = msgrate shmem shmemlatency

TESTS = $(TESTS_UNIT) $(PERF_TESTS)

.PHONY: all
all: $(TESTS)

%: %.c
	$(OSHCC) $(CFLAGS) $< -o $@

clean:
	-rm -f *.o
	-rm -f $(TESTS)
	-rm -rf *.dSYM
