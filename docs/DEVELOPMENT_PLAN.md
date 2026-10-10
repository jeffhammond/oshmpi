# OSHMPI development plan: OpenSHMEM 1.3 compliance

Status as of branch `osh13-compliance` (started from `osh1.3-candidate`, rebased onto `master`).
Developed and tested on macOS (arm64), MPICH 5.0.2 (`ch4:ofi`), 2-4 PEs only.

## Done

- **API coverage.** 215 of the 216 1.3 entry points checked were already present; `shmem_pcontrol` (no-op) was the one added.
  The list was written from memory of the spec, not generated from it, so recheck against the spec text.
- **Standard memory management names.** `shmem_malloc`, `shmem_calloc`, `shmem_free`, `shmem_realloc`, `shmem_align`;
  the legacy `shmalloc`, `shfree`, `shrealloc`, `shmemalign` forward to them.
  Zero-size requests return NULL; the routines are collective with the required barriers.
  (The `--enable-sheap-barriers` configure option is unused; barriers are unconditional.)
- **Complex reductions** (`shmem_complex{f,d}_{sum,prod}_to_all`) were declared but not implemented.
- **Version** macros report 1.3.
- **Static/global symmetric data on macOS.** Uses the runtime `__DATA` segment of the main executable (`getsegmentdata`)
  instead of the deprecated `get_etext`/`get_end`.
- **`put64/get64/put128/get128`** (and strided/nbi variants) move data as `MPI_INT64_T`, not `MPI_DOUBLE`.
- **Zero-length transfers** with NULL pointers are no-ops.
- **`shmem_global_exit(0)`** aborts with code 1 (MPI does not reliably end the job on code 0).
- **MCS lock bugs:** hand-off used `MPI_Probe` (message never consumed), unlock never reset `next`/tail, trylock re-queued after
  winning, and `shmem_test_lock` had an inverted return value.
- **Regression tests** added to `make check`: `tests/test_mem13`, `tests/test_lock`, `tests/test_zero_comm`.

## Test status

- Repo `make check`: all pass.
- External suites (copies kept in `~/Work/PGAS/oshmpi-testing`): `openshmem-org/tests-sos` unit tests and `openshmem-org/tests-uh`
  feature tests; about 72 of 76 that compile pass on 2 and 3 PEs. 67 more do not compile because they use 1.4/1.5 APIs (teams,
  contexts, signals, `shmem_*_atomic_*`, `*_reduce`), which is expected.

## Known failures (not fixed, not believed to be OSHMPI correctness bugs)

| Test | Symptom | Cause |
|---|---|---|
| `tests-uh` `test_shmem_put`, `_put_shmem_malloc`, `_get`, `_get_shmem_malloc` | abort at exit / `shmem_get64: Failed` | Test bugs: assume a 16-byte `long double` (it is 8 bytes on arm64), and `dest8` allocated 4 bytes but written with 7. Pass with those fixed. |
| SOS `global_exit` | hangs; peers blocked in a barrier are not reaped after `MPI_Abort` | Seen with MPICH + libfabric `sockets`; SOS disables this test for MPI launchers. Retest on a Linux system. |
| Occasional `MPI_Finalize` hangs on freshly linked binaries | both ranks in the libfabric progress loop | Appears tied to the local network/firewall environment (plain MPI programs and re-linked copies of the same source differ run to run). Retest on a better system. |

## To do

1. **Locks.** All locks share one global MCS queue regardless of the lock variable passed to `shmem_set_lock`; this
   over-serialises unrelated locks and a PE holding one lock while setting another can deadlock. Needs a per-lock queue
   (tail and next kept in the user's symmetric `long`, as the spec's lock variable allows).
2. **Verify behaviour against the 1.3 spec text,** routine by routine. Not yet verified:
   - `alltoall32/64`, `alltoalls32/64` (original author wrote "not sure if correct"); needs a test using the 1.3 (non-team) API.
   - `wait`/`wait_until` comparison operators and all supported types.
   - `shmem_ptr` and `shmem_addr_accessible` semantics, `shmem_info_get_name`.
   - Collectives with non-power-of-two and non-zero-start active sets.
   - Reductions for every type/operation, including the new complex ones, which are only compiled, not exercised.
3. **Scale testing.** Everything so far ran on one Mac with 2-4 PEs. Test on Linux (MPICH and Open MPI), larger PE counts and multiple nodes.
4. **Retest the known failures** above on a better system and file upstream issues for the `tests-uh` bugs.
5. **PRK.** Confirm the PRK SHMEM kernels (`common/par-res-kern_shmem.h`) now build and run; this was the original motivation
   (the standard `shmem_malloc`/`shmem_free`/`shmem_align`/`shmem_realloc` names were missing).
6. **Housekeeping.** Remove or implement the unused `--enable-sheap-barriers` option; update `README.md` and `.travis.yml` for the 1.3 claim.
   Autogen output (`configure`, `Makefile.in`, ...) is untracked and should stay out of commits.
