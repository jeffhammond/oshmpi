# Third-party tests

OSHMPI uses third-party suites to decide when a feature is done (see `docs/COMPLIANCE.md`).
A feature counts only when a test from one of these suites exercises it and passes.

Suites (clone each into a fresh directory outside this tree; do not run their scripts from inside it):

- Sandia OpenSHMEM tests: https://github.com/openshmem-org/tests-sos (`test/unit` is the main target)
- University of Houston tests: https://github.com/openshmem-org/tests-uh (`feature_tests/C`)
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
```

`run.sh` compiles each test separately, so tests that need an API we do not have are reported as `NOBUILD`
instead of stopping the run.  Some launchers need extra flags (for example `MPIEXEC="mpiexec -iface lo0"` on a
machine whose firewall prompts for network access); set `MPIEXEC` accordingly.

Status of the last run: `STATUS.md`.
