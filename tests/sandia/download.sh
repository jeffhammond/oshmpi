#!/bin/bash
git clone https://github.com/Sandia-OpenSHMEM/SOS sandia-openshmem
mv sandia-openshmem/test/unit/*.c .
mv sandia-openshmem/test/performance/*.c .
rm -rf sandia-openshmem
