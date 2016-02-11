#!/bin/bash
BRANCH=v1.3.0-dev
git clone -b ${BRANCH} --depth 10 https://github.com/Sandia-OpenSHMEM/SOS sandia-openshmem
mv sandia-openshmem/test/unit/*.c .
mv sandia-openshmem/test/performance/*.c .
rm -rf sandia-openshmem
