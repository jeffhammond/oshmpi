# Third-party test status

Last run: 2 PEs, macOS (arm64), MPICH 5.0.2 (`FI_PROVIDER=tcp`, `mpiexec -iface lo0`), `-std=gnu11` / `-std=c++11`,
branch `osh14-compliance`.  See `README.md` for how to reproduce.

## Result by repository (openshmem-org)

| Repository / directory | Pass | Fail | Do not compile |
|---|---|---|---|
| tests-sos `test/unit` | 71 | 1 (`global_exit`: exits 1 as intended; the runner counts non-zero as a fail) | 88 |
| tests-sos `test/shmemx` | 0 | 0 | 13 |
| tests-sos `test/spec-example` | 2 | 0 | 17 |
| tests-sos `test/apps` | 2 | 0 | 1 |
| tests-sos `test/performance` | 0 | 0 | 16 |
| tests-uh `feature_tests/C` | 16 | 3 | 0 |
| tests-uh `performance_tests/micro_benchmarks` | 6 | 0 | 0 |
| tests-cray `sma1` | 27 | 0 | 5 |
| tests-cray `sma2` (C++) | 7 | 0 | 0 |
| osa-testcodes | 10 | 6 | 4 |
| openshmem-am-testsuite | 0 | 0 | 17 |
| tests-mellanox `verifier` (`oshmem_test`, run by `mellanox.sh`) | 182 of 184 cases | 2 | 0 |
| test-suites | a meta repository with no tests of its own (git submodules of the above) | | |
| tests-sgi | empty | | |
| shmemx-cray-tests | only `usage` documentation for the Cray teams extension, no programs | | |

## What the failures are

| Test | Result | Cause |
|---|---|---|
| tests-uh `test_shmem_global_exit` | exit 99 | exits 99 on purpose; the test checks the exit status |
| tests-uh `test_shmem_put`, `_put_shmem_malloc`, `_get`, `_get_shmem_malloc` | exit 6 / prints `shmem_get64: Failed` | test bug: assumes a 16-byte `long double` (8 bytes on arm64) and writes past a 4-byte buffer |
| osa-testcodes `badget`, `badput`, `test-alias`, `test-bounds`, `test-init`, `test-init-interproc`, `test-types`, `test-shmem_heap*`, `test-symmetric`, `test-barrier-matching1`, `mypefail`, `test-startpes-ipa` | abort, segfault or hang | these are static-analysis fixtures with deliberate bugs (out-of-bounds accesses, calls before `shmem_init`, mismatched barriers); a crash is the expected outcome |
| tests-mellanox `basic:shmalloc` | abort | the test frees a pointer and then reallocates it, expecting NULL (undefined behavior in the spec); dlmalloc aborts on it. All other items of the case pass, up to alignment |
| tests-mellanox `basic:shmalloc` item 7 | NULL expected for a non-power-of-two alignment | undefined in the spec; OSHMPI rounds up, as tests-cray `align` expects |

## Do-not-compile groups

None of these are 1.4 features.

| Missing feature | Spec | Tests |
|---|---|---|
| teams (`shmem_team_*`, `SHMEM_TEAM_WORLD`) | 1.5 | 32, in tests-sos unit and spec-example |
| nonblocking AMOs `shmem_atomic_*_nbi` | 1.5 | `atomic_nbi`, `fadd_nbi`, `c11_test_shmem_atomic_*` |
| typed team collectives and reductions (`shmem_*_reduce`, `shmem_*_broadcast`, `shmem_*_collect`) | 1.5 | 21 |
| `wait_until`/`test` `_all`/`_any`/`_some` and `_vector` | 1.5 | 9 |
| signals and put-with-signal | 1.5 | 8 |
| `shmem_malloc_with_hints` | 1.5 | 2 |
| SOS extensions `shmemx_*` (counting puts, ibput/ibget, scans, `shmemx_am_*`) | vendor | 25 |
| C++ overloads `shmem_put`, `shmem_atomic_add`, ... | vendor (not in any spec version) | 15 |
| helper sources without `main`, and the bandwidth tests that need a second file | n/a | `mt_lock`, `mt_membar`, `bw_common.h`, `latency_common.h` users (14) |
| pre-1.0 names (`my_pe()`) | n/a | `test-bounds-constprog`, `test-startpes`, `test-symmetric2` |

## Features and the tests that cover them

| Feature | Spec | Result |
|---|---|---|
| thread support: `shmem_init_thread`, `shmem_query_thread` | 1.4 | 7 of 7 SOS tests pass |
| contexts | 1.4 | pass |
| `shmem_TYPE_atomic_*`, including bitwise | 1.4 | 16 SOS tests plus the 18 Mellanox bitwise-atomic cases pass |
| `shmem_test`, `shmem_wait_until` (new types) | 1.4 | pass |
| `shmem_sync`, `shmem_sync_all` | 1.4 | pass |
| `shmem_calloc` | 1.4 | pass |
| C11 generic bindings | 1.4 | 7 SOS tests pass |
| `<mpp/shmem.h>` (deprecated) | 1.1 | tests-cray and osa-testcodes build |
| nonblocking put/get | 1.3 | pass |
| `shmem_alltoall`, `shmem_alltoalls` (non-team) | 1.3 | Mellanox `coll` cases pass at 2 and 3 PEs |
| reductions, broadcast on active sets | 1.0 | Mellanox `reduce` (44) and `coll` cases pass |

## Provider note

With `FI_TCP_IFACE=lo0` alone, MPICH 5.0.2 falls back to the libfabric `sockets` provider, and many binaries hang in
`MPI_Finalize` (deterministic per binary).  `FI_PROVIDER=tcp` with `mpiexec -iface lo0` avoids it.  With the `tcp`
provider, ping-pong tests that send 16 KiB messages and spin on the last word (`tests/lat_bw`) stall; the `sockets`
provider runs them.  Both are provider issues outside OSHMPI.
