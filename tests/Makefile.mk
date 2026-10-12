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
                  tests/test_generic \
                  tests/test_coll_semantics \
                  tests/test_spin \
                  tests/test_mt_rma \
                  tests/test_lock_multi \
                  tests/test_direct \
                  tests/test_sync15 \
                  tests/test_amo_nbi \
                  tests/test_signal \
                  tests/test_team \
                  tests/test_team_coll \
                  tests/test_ctx \
                  tests/test_ctx_signal \
                  tests/test_bigcount \
                  tests/test_bigcoll \
                  tests/test_bigbss \
                  tests/test_env_parse \
                  tests/test_symsize_k \
                  tests/test_symsize_frac \
                  tests/test_symsize_odd \
                  tests/test_symsize_legacy \
                  tests/test_legacy \
                  tests/test_init_nested \
                  tests/test_shmem16 \
                  tests/test_reinit \
                  tests/test_init_thread_single \
                  tests/test_init_thread_funneled \
                  tests/test_init_thread_serialized \
                  tests/test_init_thread_multiple \
                  tests/test_symsize_full \
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
         tests/test_thread \
         tests/test_14 \
         tests/test_generic \
         tests/test_coll_semantics \
         tests/test_spin \
         tests/test_mt_rma \
         tests/test_lock_multi \
         tests/test_direct \
         tests/test_sync15 \
         tests/test_amo_nbi \
         tests/test_signal \
         tests/test_team \
         tests/test_team_coll \
         tests/test_ctx \
         tests/test_ctx_signal \
         tests/test_bigcount \
         tests/test_bigcoll \
         tests/test_bigbss \
         tests/test_env_parse \
         tests/test_symsize_k \
         tests/test_symsize_frac \
         tests/test_symsize_odd \
         tests/test_symsize_legacy \
         tests/test_legacy \
         tests/test_init_nested \
         tests/test_shmem16 \
         tests/test_reinit \
         tests/test_init_thread_single \
         tests/test_init_thread_funneled \
         tests/test_init_thread_serialized \
         tests/test_init_thread_multiple \
         tests/test_symsize_full \
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
tests_test_generic_LDADD = libshmem.la
tests_test_coll_semantics_LDADD = libshmem.la
tests_test_spin_LDADD = libshmem.la
tests_test_mt_rma_LDADD = libshmem.la
tests_test_mt_rma_LDFLAGS = -pthread
tests_test_lock_multi_LDADD = libshmem.la
tests_test_direct_LDADD = libshmem.la
tests_test_sync15_LDADD = libshmem.la
tests_test_amo_nbi_LDADD = libshmem.la
tests_test_signal_LDADD = libshmem.la
tests_test_team_LDADD = libshmem.la
tests_test_team_coll_LDADD = libshmem.la

tests_test_bigcount_LDADD = libshmem.la
tests_test_bigcoll_LDADD = libshmem.la
tests_test_bigbss_LDADD = libshmem.la
tests_test_ctx_LDADD = libshmem.la
tests_test_ctx_signal_LDADD = libshmem.la
tests_test_env_parse_SOURCES = tests/test_env_parse.c
tests_test_symsize_k_SOURCES = tests/test_symsize.c
tests_test_symsize_k_CPPFLAGS = -DSZ='"300k"' -DWANT=307200
tests_test_symsize_k_LDADD = libshmem.la
tests_test_symsize_frac_SOURCES = tests/test_symsize.c
tests_test_symsize_frac_CPPFLAGS = -DSZ='"1.5m"' -DWANT=1572864
tests_test_symsize_frac_LDADD = libshmem.la
tests_test_symsize_odd_SOURCES = tests/test_symsize.c
tests_test_symsize_odd_CPPFLAGS = -DSZ='"100003"' -DWANT=100003
tests_test_symsize_odd_LDADD = libshmem.la
tests_test_symsize_legacy_SOURCES = tests/test_symsize.c
tests_test_symsize_legacy_CPPFLAGS = -DENVNAME='"SMA_SYMMETRIC_SIZE"' -DSZ='"2M"' -DWANT=2097152
tests_test_symsize_legacy_LDADD = libshmem.la
tests_test_symsize_full_SOURCES = tests/test_symsize.c
tests_test_symsize_full_CPPFLAGS = -DSZ='"2m"' -DWANT=2097152 -DEXHAUST
tests_test_symsize_full_LDADD = libshmem.la


tests_test_legacy_LDADD = libshmem.la -lm
tests_test_init_nested_LDADD = libshmem.la
tests_test_shmem16_LDADD = libshmem.la
tests_test_reinit_LDADD = libshmem.la
tests_test_init_thread_single_SOURCES = tests/test_init_thread.c
tests_test_init_thread_single_CPPFLAGS = -DREQ=0
tests_test_init_thread_single_LDFLAGS = -pthread
tests_test_init_thread_single_LDADD = libshmem.la
tests_test_init_thread_funneled_SOURCES = tests/test_init_thread.c
tests_test_init_thread_funneled_CPPFLAGS = -DREQ=1
tests_test_init_thread_funneled_LDFLAGS = -pthread
tests_test_init_thread_funneled_LDADD = libshmem.la
tests_test_init_thread_serialized_SOURCES = tests/test_init_thread.c
tests_test_init_thread_serialized_CPPFLAGS = -DREQ=2
tests_test_init_thread_serialized_LDFLAGS = -pthread
tests_test_init_thread_serialized_LDADD = libshmem.la
tests_test_init_thread_multiple_SOURCES = tests/test_init_thread.c
tests_test_init_thread_multiple_CPPFLAGS = -DREQ=3
tests_test_init_thread_multiple_LDFLAGS = -pthread
tests_test_init_thread_multiple_LDADD = libshmem.la
