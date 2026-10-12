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
| `SHMEM_SYMMETRIC_SIZE`, `SHMEM_VERSION`, `SHMEM_INFO` | 1.4 | Size accepts floating-point values and k/m/g/t suffixes (powers of 1024) as in 1.5, rounds up, and an invalid value aborts; the heap holds at least that many bytes. `SHMEM_DEBUG` accepted and ignored; `SMA_*` and other vendor names still read, with the same syntax |
| `shmem_pcontrol` | 1.5 | no-op |
| `shmem_<T>_wait_until_{all,any,some}` and `_vector` forms, `shmem_<T>_test_{all,any,some}` and `_vector` forms | 1.5 | `tools/gen-shmem15-sync.py` generates the C11 bindings; the any/some scans rotate their start so repeated `test_any` calls reach every satisfied element |
| Nonblocking atomics `shmem_<T>_atomic_{fetch,swap,compare_swap,fetch_inc,fetch_add,fetch_and,fetch_or,fetch_xor}_nbi`, with context versions and C11 bindings | 1.5 | Complete inside the call, which the specification allows |
| `shmem_malloc_with_hints`, `SHMEM_MALLOC_ATOMICS_REMOTE`, `SHMEM_MALLOC_SIGNAL_REMOTE` | 1.5 | Hints are advisory and ignored |
| `shmem_ctx_session_start/stop`, `shmem_ctx_session_config_t` | 1.6 | No-ops, which the specification permits |
| Put with signal: `shmem_<T>_put_signal(_nbi)`, `shmem_putmem_signal(_nbi)`, `shmem_put{8..128}_signal(_nbi)` and context versions; `shmem_signal_add/set/fetch`, `shmem_signal_wait_until`, `SHMEM_SIGNAL_SET/ADD` | 1.5/1.6 | Data, then a fence (and a flush of the target on the MPI path), then the atomic signal update; also the `shmemx_signal_add/set` names the Sandia tests use |
| Teams: `SHMEM_TEAM_WORLD`, `SHMEM_TEAM_SHARED`, `shmem_team_my_pe/n_pes/get_config/translate_pe/split_strided/split_2d/destroy/ptr/sync`, `shmem_team_create_ctx`, `shmem_ctx_get_team` | 1.5 | Teams wrap MPI communicators; the predefined teams are created in `shmem_init` |
| Team collectives: `shmem_<T>_{broadcast,collect,fcollect,alltoall,alltoalls}` and `*mem` forms, `shmem_<T>_{and,or,xor,max,min,sum,prod}_reduce`, with C11 bindings | 1.5 | The 1.3 active-set routines remain |
| Direct load/store access to the symmetric heap, detected at run time with `MPI_Win_shared_query` | (implementation) | See the README; `OSHMPI_DIRECT=0` turns it off |
| Per-variable MCS locks | (implementation) | Each lock variable is an independent lock; locks may be nested |
| `<mpp/shmem.h>`, `<mpp/shmemx.h>` | 1.1 (deprecated) | forwarding headers |
| `extern "C"` linkage in the headers; `_Complex` instead of `complex` in the reduction prototypes | 1.4 | the library can be called from C++ |
| deprecation attributes on the pre-1.4 AMO and `shmem_wait` routines; `SMA_*` variables | 1.4 | |

## Not implemented

Scans (`shmem_*_scan`), re-initialization after finalize (1.6), and the vendor extensions the Sandia and Cray
suites use beyond the specification: `shmemx_team_*` (Cray), `shmemx_am_*` (active messages) and `SHMEMX_*` malloc flags, and
the C++ overloaded names (`shmem_g(...)` and so on) that Sandia OpenSHMEM provides.  The tests that need these do not compile;
the list is in `tests/third_party/STATUS.md`.

## Known deviations

- `shmem_global_exit(0)` terminates the job with exit status **1**, not 0 (the spec says the status is one of the values
  passed).  `MPI_Abort` with code 0 does not reliably end the other PEs.
- Concurrent `shmem_malloc`-family calls from several threads must be serialized by the application (they are collective;
  concurrent collectives on one team are undefined in the spec).
- Threads: with MPI_THREAD_MULTIPLE every MPI RMA, flush and sync call is serialized by one mutex, because Open MPI's osc/ucx
  and osc/rdma hang with concurrent RMA from several threads.  Operations on the symmetric heap with direct access enabled do
  not touch MPI at all.  Open MPI 5.x osc/ucx can still crash when a thread exits while others issue RMA (fixed or rarer in 6.0).
- `shmem_quiet`/`shmem_fence` flush all outstanding RMA on the PE, which is stronger than the spec requires.
- Counts and sizes are not limited to an MPI `int`.  Contiguous RMA, strided RMA (including strides over 2^31 elements),
  data-segment windows over 2 GiB and every collective, team or legacy, move more than 2^31-1 elements or bytes, using
  derived datatypes or pieces.  Opt-in tests (`OSHMPI_TEST_LARGE=1` or `2`): `test_bigcount`, `test_bigcoll`, `test_bigbss`.
- `shmem_init` and `shmem_init_thread` may be called more than once, each matched by a `shmem_finalize`; the library is
  torn down by the last one.  It cannot be initialized again after that (MPI cannot be restarted): the call fails, and
  `shmem_init_thread` returns nonzero.
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
