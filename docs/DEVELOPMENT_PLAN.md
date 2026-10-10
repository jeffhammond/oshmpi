# OSHMPI development plan: OpenSHMEM 1.3 and 1.4 compliance

The 1.3 work is on branch `osh13-compliance` (started from `osh1.3-candidate`, rebased onto `master`);
the 1.4 work is on `osh14-compliance`, which is based on it.  See the 1.4 section at the end.
Developed and tested on macOS (arm64), MPICH 5.0.2 (`ch4:ofi`), 2-4 PEs only.

## Done

- **API coverage.** 215 of the 216 1.3 entry points checked were already present; `shmem_pcontrol` (no-op) was the one added.
  The list was written from memory of the spec, not generated from it, so recheck against the spec text.
- **Standard memory management names.** `shmem_malloc`, `shmem_free`, `shmem_realloc`, `shmem_align`, and `shmem_calloc`
  (formally new in 1.4 but added here with the others);
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

---

# OpenSHMEM 1.4 (branch `osh14-compliance`)

Reference: the Sandia OpenSHMEM type tables and release notes (the 1.4 spec text was not available locally).
The 1.4 additions are thread safety, contexts, `test` and `sync`, `calloc`, bitwise atomics, the standard type names
for RMA/AMO routines, and updated C11 generic bindings.  An older `origin/openshmem-1.4` branch (July 2021) only had
`shmem_init_thread`/`shmem_query_thread`; that was reimplemented here rather than cherry-picked because its header edits
conflicted with the 1.3 work.

## Done

- **Thread support.** `shmem_init_thread` (returns int) and `shmem_query_thread`, `SHMEM_THREAD_*`.  Also fixed the thread-level
  check in `oshmpi_initialize`, which aborted when MPI provided *more* than requested, and the symmetric-heap mspace is
  locked when `MPI_THREAD_MULTIPLE` is provided.
- **Contexts.** `shmem_ctx_t`, `SHMEM_CTX_DEFAULT`/`INVALID`, `SHMEM_CTX_{SERIALIZED,PRIVATE,NOSTORE}`,
  `shmem_ctx_create/destroy/fence/quiet`, and context versions of all RMA and atomic routines.  All contexts share the PE's
  MPI windows, so a context's fence/quiet completes everything the PE has outstanding.  That is stronger than required and
  therefore correct, but gives no per-context performance isolation.
- **Standard-named RMA** for all 24 types (`shmem_TYPE_{put,get,p,g,iput,iget,put_nbi,get_nbi}`), plus `shmem_iput8/16`, `shmem_iget8/16`.
- **Atomics:** `shmem_TYPE_atomic_{fetch,set,swap,compare_swap,fetch_inc,inc,fetch_add,add}` and bitwise
  `shmem_TYPE_atomic_{fetch_,}{and,or,xor}`, each with a context form.
- **`shmem_TYPE_test`, `shmem_TYPE_wait_until`** for the 1.4 synchronization types, **`shmem_sync`/`shmem_sync_all`.**
- **C11 generic bindings** (`tools/gen-c11-generics.py` -> `src/shmem14-generic.h`), with optional leading context argument.
  They select on the basic C types (fixed-width, `size_t` and `ptrdiff_t` alias them).  Not visible under `-std=gnu99`.
- **Environment variables:** `SHMEM_SYMMETRIC_SIZE` (read first), `SHMEM_VERSION`, `SHMEM_INFO`.  `SHMEM_DEBUG` is accepted and ignored.
- `SHMEM_ALLTOALLS_SYNC_SIZE`; version macros report 1.4.
- Tests added to `make check`: `test_thread`, `test_14`, `test_generic` (skips without C11).  Fixed `tests/Makefile.mk`, which
  was not running the new tests at all.

## To do (1.4)

1. **Spec check.** Verify the 1.4 routines against the spec text (argument types, return values, which types each routine takes).
   Found one such mismatch from an external test: `shmem_init_thread` must return `int`.
2. **Deprecations.** Mark the 1.4-deprecated routines (`shmem_wait*`, `start_pes`, old-style atomics such as `shmem_int_fadd`,
   cache routines, ...) with `OSHMPI_DEPRECATED` as the spec's Annex requires, without breaking the existing tests.
3. **Contexts:** real per-context ordering/completion (separate windows or request tracking) if performance isolation matters;
   `SHMEM_CTX_PRIVATE`/`NOSTORE` options are accepted but have no effect.
4. **Thread safety.** Audit the internal state for `SHMEM_THREAD_MULTIPLE`: the comm cache (`ENABLE_COMM_CACHING`), the lock
   queue, and `shmem_malloc` collectives called from several threads.  Only a single-threaded test exists.
5. **Bitwise atomics** use the MPI path only (the shared-memory fast path in `shmem-internals.c` is not used) and mix with the
   `__sync` atomics the SMP path uses for `add`/`swap`; verify atomicity across the two.
6. Test with more PEs/threads and on Linux; the same macOS-only caveats as 1.3 apply.
