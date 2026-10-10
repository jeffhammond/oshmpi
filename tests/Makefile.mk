#
# Copyright (C) 2014. See LICENSE in top-level directory.
#

check_PROGRAMS += tests/barrier_performance \
                  tests/get_performance \
                  tests/lat_bw \
                  tests/osu_oshm_put_mr \
		  tests/osu_oshm_atomics \
                  tests/put_performance \
                  tests/test_etext \
                  tests/test_sheap \
                  tests/test_start \
                  tests/test_atomics \
                  tests/test_swap_cswap \
                  tests/test_mem13 \
                  tests/test_lock \
                  tests/test_zero_comm \
         tests/test_thread \
         tests/test_14 \
                  tests/test_thread \
                  tests/test_14 \
                  # end

TESTS += tests/barrier_performance \
         tests/get_performance \
         tests/lat_bw \
         tests/osu_oshm_put_mr \
	 tests/osu_oshm_atomics \
         tests/put_performance \
         tests/test_etext \
         tests/test_sheap \
         tests/test_atomics \
	 tests/test_swap_cswap \
         tests/test_mem13 \
         tests/test_lock \
         tests/test_zero_comm \
         # end

tests_barrier_performance_LDADD = libshmem.la
tests_get_performance_LDADD = libshmem.la
tests_lat_bw_LDADD = libshmem.la
tests_osu_oshm_put_mr_LDADD = libshmem.la
tests_osu_oshm_atomics_LDADD = libshmem.la
tests_put_performance_LDADD = libshmem.la
tests_test_etext_LDADD = libshmem.la
tests_test_sheap_LDADD = libshmem.la
tests_test_start_LDADD = libshmem.la
tests_test_atomics_LDADD = libshmem.la
tests_test_swap_cswap_LDADD = libshmem.la
tests_test_mem13_LDADD = libshmem.la
tests_test_lock_LDADD = libshmem.la
tests_test_zero_comm_LDADD = libshmem.la
tests_test_thread_LDADD = libshmem.la
tests_test_14_LDADD = libshmem.la
