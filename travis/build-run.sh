#! /bin/sh

# Exit on error
set -ev

# Environment variables
export CFLAGS="-std=c99"
export MPICC=mpicc

SMP_OPT="$1"

# Configure and build
./autogen.sh

case "$SMP_OPT" in
    0)
        ./configure CC=mpicc --prefix=$TRAVIS_INSTALL --disable-shared
        ;;
    1)
        ./configure CC=mpicc --prefix=$TRAVIS_INSTALL --disable-shared --enable-smp-optimizations
        ;;
esac

# Run unit tests
export SHMEM_SYMMETRIC_HEAP_SIZE=500M
make
make check
make install
