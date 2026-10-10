# OpenSHMEM compliance status

OSHMPI is **not** a fully conforming OpenSHMEM implementation of any version beyond 1.2/1.3, and does not aim to be.
The goal is: every easy feature, and every harder feature that a third-party test validates.  A feature counts as done
only when a third-party test exercises it and passes (`tests/third_party/STATUS.md`).

`SHMEM_MINOR_VERSION` is therefore still **1.3**; the features beyond 1.3 listed below are implemented but the version
macro is not bumped because the implementation is partial.  `docs/DEVELOPMENT_PLAN.md` has the history and the remaining
work; the reference text is the OpenSHMEM specification (`content/*.tex` in the specification repository).

Requirements: a C11 compiler (checked by `configure`) and MPI-3.

## Implemented beyond 1.3 (validated by third-party tests)

| Feature | Spec | Notes |
|---|---|---|
| `shmem_calloc` | 1.4 | |
| `shmem_init_thread` (returns `int`), `shmem_query_thread`, `SHMEM_THREAD_*` | 1.4 | Reports what MPI provides; the symmetric heap is locked at `MPI_THREAD_MULTIPLE` |
| Contexts: `shmem_ctx_create/destroy/fence/quiet` and context versions of all RMA/AMO routines | 1.4 | All contexts share the PE's windows, so a context's fence/quiet completes everything outstanding (stronger than required). `SHMEM_CTX_{SERIALIZED,PRIVATE,NOSTORE}` are accepted and ignored |
| Standard type names for RMA, AMO, `wait_until`, `test` | 1.4 | Tables match `rma_intro.tex`, `atomics_intro.tex`, `p2p_sync_intro.tex` |
| `shmem_TYPE_atomic_*` including bitwise `and/or/xor` | 1.4 | Old AMO names remain |
| `shmem_TYPE_test`, `shmem_sync`, `shmem_sync_all`, `SHMEM_SYNC_SIZE` | 1.4 | `sync` publishes local stores but does not complete remote updates, as specified |
| C11 type-generic bindings (RMA, AMO, `wait_until`, `test`, with optional context argument) | 1.3/1.4 | `tools/gen-c11-generics.py`; selects on basic C types |
| `SHMEM_SYMMETRIC_SIZE`, `SHMEM_VERSION`, `SHMEM_INFO` | 1.4 | `SHMEM_DEBUG` accepted and ignored; `SMA_*` and other vendor names still read |
| `shmem_pcontrol` | 1.5 | no-op |
| `<mpp/shmem.h>`, `<mpp/shmemx.h>` | 1.1 (deprecated) | forwarding headers |
| `extern "C"` linkage in the headers; `_Complex` instead of `complex` in the reduction prototypes | 1.4 | the library can be called from C++ |
| deprecation attributes on the pre-1.4 AMO and `shmem_wait` routines; `SMA_*` variables | 1.4 | |

## Not implemented (no third-party test can validate it here, or not yet needed)

Teams, put-with-signal and signals, `shmem_*_wait_until_{all,any,some}` and `test_*`, nonblocking AMOs,
`shmem_malloc_with_hints`, team-based collectives and scans, sessions, re-initialization after finalize (1.6).
The Sandia tests that need these do not compile yet; the list is in `tests/third_party/STATUS.md`.

## Known deviations

- `shmem_global_exit(0)` terminates the job with exit status **1**, not 0 (the spec says the status is one of the values
  passed).  `MPI_Abort` with code 0 does not reliably end the other PEs.
- Concurrent `shmem_malloc`-family calls from several threads must be serialized by the application (they are collective;
  concurrent collectives on one team are undefined in the spec).
- Locks: all locks share one global MCS queue regardless of the lock variable.  Correct, but over-serialized, and holding
  one lock while setting another can deadlock.  Rewrite planned (`docs/DEVELOPMENT_PLAN.md`).
- `shmem_quiet`/`shmem_fence` flush all outstanding RMA on the PE, which is stronger than the spec requires.
- Static/global symmetric data on macOS uses the main executable's `__DATA` segment.

## Known test failures (not believed to be OSHMPI bugs)

See `tests/third_party/STATUS.md`.  The UH put/get tests assume a 16-byte `long double` (test bug), the osa-testcodes
programs are deliberately buggy, and the Mellanox `basic:shmalloc` case reallocates a freed pointer (undefined behavior;
dlmalloc aborts).  With MPICH, use `FI_PROVIDER=tcp`: the libfabric `sockets` provider makes many programs hang in
`MPI_Finalize`, which is a provider problem, not an OSHMPI one.

## Bugs found and fixed by the third-party suites

`and_to_all` used a logical instead of a bitwise AND; `PE_root` of the broadcasts was treated as a world rank instead of an
ordinal in the active set; `shmem_alltoalls32` moved 8-byte elements and the strided datatypes had the wrong extent;
the `alltoall*` routines initialized a `pSync` array larger than the one the spec gives them; `shmem_global_exit` aborted a
duplicated communicator, which MPICH did not propagate; `shmem_finalize` finalized an MPI that the user had initialized.
