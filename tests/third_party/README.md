# Third-party tests

OSHMPI uses third-party suites to decide when a feature is done (see `docs/COMPLIANCE.md`).
A feature counts only when a test from one of these suites exercises it and passes.

Suites (clone each into a fresh directory outside this tree; do not run their scripts from inside it):

- Sandia OpenSHMEM tests: https://github.com/openshmem-org/tests-sos (`test/unit` is the main target)
- University of Houston tests: https://github.com/openshmem-org/tests-uh (`feature_tests/C`, `error_tests`, `performance_tests`)
- Cray vendor tests: https://github.com/openshmem-org/tests-cray (`sma1`, `sma2`)
- Mellanox verifier: https://github.com/openshmem-org/tests-mellanox (`verifier`; use `mellanox.sh`)
- OpenSHMEM Analyzer fixtures: https://github.com/openshmem-org/osa-testcodes (deliberately buggy programs; many are expected to crash)
- AM microbenchmarks https://github.com/openshmem-org/openshmem-am-testsuite use vendor `shmemx_am_*` calls and do not build
- first-party spec examples: `example_code/*.c` in the OpenSHMEM specification repository

## Reproducing

```sh
# build OSHMPI out of tree with a static library so the tests can link it
mkdir build && cd build
../autogen.sh   # once, from the source directory
../configure CC=mpicc CFLAGS="-g -std=gnu11" --enable-static
make

# run every unit test on 2 PEs
../tests/third_party/run.sh . 2 /path/to/tests-sos/test/unit /path/to/tests-uh/feature_tests/C

# the Mellanox verifier is built and run case by case by its own script
../tests/third_party/mellanox.sh . /path/to/tests-mellanox/verifier 2
```

`run.sh` compiles each test separately, so tests that need an API we do not have are reported as `NOBUILD`
instead of stopping the run.  Some launchers need extra flags: on macOS use `FI_PROVIDER=tcp` and `MPIEXEC="mpiexec -iface lo0"`
(loopback only, so the firewall does not prompt).  Do not set `FI_TCP_IFACE` alone; MPICH then falls back to the
libfabric `sockets` provider, where many programs hang in `MPI_Finalize`.  Set `WORKDIR` if `mktemp -d` is not
available.

Status of the last run: `STATUS.md`.
