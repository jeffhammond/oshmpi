#!/bin/sh
# Build and run the Mellanox OpenSHMEM verifier (openshmem-org/tests-mellanox/verifier)
# against an OSHMPI build tree, without autotools.
#
#   tests/third_party/mellanox.sh BUILD_DIR VERIFIER_DIR NPES
#
# Environment: WORKDIR (default: a fresh mktemp -d), MPIEXEC (default "mpiexec"), TIMEOUT per
# case in seconds (default 300).  Prints one line per test case: "<status> <suite:case>".
set -u
build=$1; ver=$(cd "$2" && pwd); npes=$3
top=$(cd "$(dirname "$0")/../.." && pwd)
lib=$build/.libs/libshmem.a; [ -f "$lib" ] || lib=$build/libshmem.a
: "${MPIEXEC:=mpiexec}" "${TIMEOUT:=300}"
if [ -n "${WORKDIR:-}" ]; then work=$WORKDIR; mkdir -p "$work"; else work=$(mktemp -d); fi
[ -w "$work" ] || { echo "no work directory (set WORKDIR)"; exit 2; }
mkdir -p "$work/inc"
{
  echo '#define HAVE_UNISTD_H 1'; echo '#define CHECK_ACTIVE_SET 1'; echo '#define VERSION "1"'
  echo '#define QUICK_TEST 1'; echo '#define HAVE_ALLTOALL 1'
  for t in UINT ULONG ULONGLONG; do for o in AND OR XOR FETCH_AND FETCH_OR FETCH_XOR; do
    echo "#define HAVE_DECL_SHMEM_${t}_ATOMIC_$o 1"; done; done
} > "$work/inc/config.h"
cd "$ver" || exit 2
# every .c except the two template files that are #included by other sources
for f in $(find . -name '*.c' | grep -v 'wait_template\|wait_until_template' | sed 's|^\./||'); do
  mpicc -std=gnu11 -w -DHAVE_NBI -c -I"$work/inc" -I. -Icmn -I"$top/src" -I"$build/src" "$f" \
        -o "$work/$(echo "$f" | tr / _).o" || { echo "NOBUILD $f"; }
done
mpicc "$work"/*.o "$lib" -lm -o "$work/oshmem_test" || exit 2
# list every case, then run them one at a time so that one failure does not hide the rest
"$work/oshmem_test" exec --info=case 2>&1 | sed 's/\x1b\[[0-9;]*m//g' > "$work/cases.txt"
python3 - "$work/cases.txt" > "$work/tasks.txt" <<'PY'
import re, sys
suite = None
for l in open(sys.argv[1]):
    m = re.match(r'^   (\S+) \((\w+)\)', l)
    if m: suite = m.group(2); continue
    m = re.match(r'^      (\S+)\s*\((tc\d+)', l)
    if m: print(f"{suite}:{m.group(2)}")
PY
: > results.txt
for t in $(cat "$work/tasks.txt"); do
  timeout -k 3 "$TIMEOUT" $MPIEXEC -n "$npes" "$work/oshmem_test" exec --task="$t" > "$work/$(echo "$t" | tr : _).out" 2>&1
  rc=$?
  r=$(sed 's/\x1b\[[0-9;]*m//g' "$work/$(echo "$t" | tr : _).out" | grep -E '^(PASS|FAIL|SKIP|IGNORE)' | awk '{print $1}' | sort -u | tr '\n' ',' | sed 's/,$//')
  echo "${r:-CRASH($rc)} $t" >> results.txt
done
awk '{c[$1]++} END {for (k in c) printf "%s: %d\n", k, c[k]}' results.txt | sort
