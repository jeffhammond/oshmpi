OSHCC = oshcc
CFLAGS = -g -O0 -Wall

OSHRUN = mpiexec.hydra

PERF_TESTS = msgrate

UNIT_TESTS = accessible_ping asym_alloc barrier bcast bcast_flood big_reduction bigget bigput c11_test_shmem_add c11_test_shmem_fetch c11_test_shmem_g c11_test_shmem_get c11_test_shmem_inc c11_test_shmem_p c11_test_shmem_put c11_test_shmem_set circular_shift cswap fcollect64 get1 get_g get_nbi global_exit hello ipgm iput-iget iput128 iput32 iput64 iput_double iput_float iput_long iput_longdouble iput_longlong iput_short lfinc max_reduction micro_unit_shmem ns ping pingpong-short pingpong put1 put_nbi set_fetch shmalloc shmem_info shmemalign shrealloc spam sping strided_put swap1 swapm test_lock_cswap to_all

# these tests do not validate because unfortunate text in the OpenSHMEM spec
PENDING_TESTS = alltoall alltoalls

# these take a long time, which may indicate a bug in OSHMPI shmem_wait(_until), etc.
XFAIL_TESTS = shmemlatency atomic_inc micro_unit_shmem set_lock test_lock waituntil

# mt_a2a requires pthread_barrier, which is not portable
# shmem_ct requires contexts extension, and my inclusion of shmemx.h is busted
# shmem is just horrible
BAD_TESTS = mt_a2a shmem_ct shmem

TESTS=$(UNIT_TESTS) $(PERF_TESTS)

.PHONY: all check

all: $(TESTS)

%: %.c
	$(OSHCC) $(CFLAGS) $< -o $@

check: $(TESTS)
	for test in $(TESTS) ; do echo "======= $${test} =======" ; $(OSHRUN) -n 2 ./$${test} | tee $${test}.log ; done

clean:
	-rm -f *.o
	-rm -f $(TESTS)
	-rm -rf *.dSYM
	-rm -f *.log
