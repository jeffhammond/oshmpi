OSHMPI: OpenSHMEM over MPI-3
============================

**This project is no longer actively developed.**

Bug fixes and specific feature requests will be addressed as time permits.  We will try to read and comment on issues and pull requests promptly.

We recommend you use [Sandia OpenSHMEM](https://github.com/Sandia-OpenSHMEM/SOS) to get the latest OpenSHMEM features.  This branch implements the OpenSHMEM 1.6 C API: the 1.3 base (including the standard shmem_malloc/shmem_calloc/shmem_free/shmem_realloc/shmem_align names), contexts, teams and team collectives, put with signal, nonblocking atomics, `wait_until`/`test` on arrays of variables, `shmem_malloc_with_hints`, scans, interleaved block transfers, `shmem_pe_quiet`, and repeated initialization.  `docs/COMPLIANCE.md` lists the deviations; `SHMEM_MINOR_VERSION` reports 6.

Test Status
===========

A C11 compiler and MPI-3 are required.  We test with MPICH and Open MPI on Ubuntu and macOS using GitHub Actions.

[![CI](https://github.com/jeffhammond/oshmpi/actions/workflows/ci.yml/badge.svg)](https://github.com/jeffhammond/oshmpi/actions/workflows/ci.yml)

Publications
============

Jeff R. Hammond, Sayan Ghosh, and Barbara M. Chapman, 
"Implementing OpenSHMEM using MPI-3 one-sided communication."  
Preprint: https://github.com/jeffhammond/oshmpi/blob/master/docs/iwosh-paper.pdf  
Workshop Proceedings: http://www.csm.ornl.gov/workshops/openshmem2013/documents/ImplementingOpenSHMEM%20UsingMPI-3.pdf  
Journal: http://dx.doi.org/10.1007/978-3-319-05215-1_4  

Platform Support
================

OSHMPI uses essentially all the new RMA features in MPI-3 and thus 
requires an MPI implementation that supports these properly.
Currently, MPICH 3.0.x and its derivatives support MPI-3.
We assume support for the UNIFIED memory model of MPI-3;
OSHMPI will abort if this is not provided by the implementation,
which should only be the case for non-cache-coherent systems.

Because SHMEM allows for communication against remote global 
variables and not just the symmetric heap, OSHMPI has an 
operating system dependency since accessing the text and data
segments is not portable.

The platforms we currently test on are:
* Mac with GCC 4.8 or LLVM 3.3+ and MPICH master (see Bugs below)
* Linux x86_64 with MPICH 3+ and MVAPICH2 1.9+

We have tested on these platforms at one point or another:
* SGI ccNUMA with MPICH 3
 
Features
========

OSHMPI attempts to use MPI-3 as effectively as possible.
To this end, we support all valid performance-related info
keys and ensure the correct semantics when they are enabled
and disabled.

Direct access to the symmetric heap is detected at run time.  The heap is always
created with `MPI_Win_allocate`; at startup OSHMPI calls `MPI_Win_shared_query`
(which MPI 4.1 allows on any allocated window) for every PE.  If every PE's heap
is mapped into the calling process, which in practice means the job runs on one
node and the MPI library supports it (Open MPI 6.0 and later does, for osc/ucx,
osc/rdma and osc/sm), all operations on the heap bypass MPI: puts, gets and strided
transfers are `memcpy`s, and every atomic is a GCC/Clang `__atomic` builtin.  Direct
access is all or nothing across the job, so hardware atomics are never mixed with
MPI atomics on the same word, and `shmem_ptr` returns usable addresses.  The data
segment still uses MPI.  With an MPI library that does not support the query,
OSHMPI uses MPI RMA for everything, as before.

Two environment variables control this: `OSHMPI_DIRECT=0` forces MPI RMA, and
`OSHMPI_VERBOSE=1` makes PE 0 report whether direct access is enabled.

Single-node results from `tools/microbench.c` with 2 PEs (one run each on an aarch64
machine, so treat them as indicative; `oshcc -O2 tools/microbench.c`):

| | Open MPI 5.0.10, MPI RMA | Open MPI 6.0, `OSHMPI_DIRECT=0` | Open MPI 6.0, direct |
|---|---|---|---|
| 8-byte put + quiet | 3.32 us | 0.12 us | 0.08 us |
| 8-byte get | 3.14 us | 0.033 us | 0.012 us |
| atomic add rate | 11.8 Mop/s | 14.5 Mop/s | 86.6 Mop/s |
| fetch-add latency | 3.19 us | 0.073 us | 0.013 us |
| 1 MiB put bandwidth | 30.2 GB/s | 35.6 GB/s | 35.3 GB/s |
| barrier | 0.46 us | 0.37 us | 0.35 us |

Future Work
===========

We look forward to patches contributing the following:

* Allow selection of tuning options at runtime.
* Eliminate all intranode MPI-RMA communication.
* Cache subcommunicators corresponding to PE subgroups.

Bugs/Omissions
==============

We look forward to patches addressing the following:

* Mac non-heap symmetric data cannot be accessed remotely reliably.
* PSHMEM interface
* OpenSHMEM 1.3 changes
