#!/bin/sh
# Build and run third-party OpenSHMEM tests one at a time against an OSHMPI build tree.
#
#   tests/third_party/run.sh BUILD_DIR NPES SRC_DIR [SRC_DIR ...]
#
# BUILD_DIR  a configured and built OSHMPI tree (needs src/shmemconf.h and .libs/libshmem.a
#            or libshmem.a), e.g. the directory where you ran ../configure && make
# NPES       number of PEs
# SRC_DIR    directories of .c tests (e.g. tests-sos/test/unit tests-uh/feature_tests/C)
#
# Each test is compiled separately with mpicc -std=gnu11, so a test that needs an API
# OSHMPI does not have yet is reported as NOBUILD and does not stop the run.
# Results are written to results.txt in the current directory: "<status> <test>".
# Status is the exit code (0 = pass, 124 = timeout), or NOBUILD.
#
# Environment: TIMEOUT (seconds per test, default 30), MPIEXEC (default "mpiexec"),
# EXTRA_CFLAGS, and the usual SHMEM_SYMMETRIC_HEAP_SIZE (default 100M).
set -u
build=$1; npes=$2; shift 2
top=$(cd "$(dirname "$0")/../.." && pwd)
lib=$build/.libs/libshmem.a; [ -f "$lib" ] || lib=$build/libshmem.a
[ -f "$lib" ] || { echo "no libshmem.a in $build (build with --disable-static=no or use --enable-static)"; exit 2; }
: "${TIMEOUT:=30}" "${MPIEXEC:=mpiexec}" "${SHMEM_SYMMETRIC_HEAP_SIZE:=100M}"
export SHMEM_SYMMETRIC_HEAP_SIZE
work=$(mktemp -d); : > results.txt
for dir in "$@"; do
  for f in "$dir"/*.c; do
    t=$(basename "$f" .c)
    if mpicc -g -std=gnu11 -w -I"$top/src" -I"$build/src" -I"$dir/../include" ${EXTRA_CFLAGS:-} \
         "$f" "$lib" -lm -lpthread -o "$work/$t" 2>"$work/$t.err"; then
      timeout -k 3 "$TIMEOUT" $MPIEXEC -n "$npes" "$work/$t" >"$work/$t.out" 2>&1
      echo "$? $t" >> results.txt
    else
      echo "NOBUILD $t" >> results.txt
    fi
  done
done
echo "outputs and compile errors are in $work"
awk '{c[$1]++} END {for (k in c) printf "%s: %d\n", k, c[k]}' results.txt | sort
