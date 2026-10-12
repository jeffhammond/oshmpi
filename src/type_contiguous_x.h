/* BSD-2 License.  Written by Jeff Hammond. */

#ifndef TYPE_CONTIGUOUS_X_H
#define TYPE_CONTIGUOUS_X_H

/* This code was taken from the BigMPI project.
 * Please see https://github.com/jeffhammond/BigMPI
 * for all technical details. */

#include <limits.h>
#include <stddef.h>
#include <mpi.h>
/*
 * Synopsis
 *
 * int MPIX_Type_contiguous_x(size_t         count,
 *                            MPI_Datatype   oldtype,
 *                            MPI_Datatype * newtype)
 *
 *  Input Parameters
 *
 *   count             replication count (nonnegative integer)
 *   oldtype           old datatype (handle)
 *
 * Output Parameter
 *
 *   newtype           new datatype (handle)
 *
 */
static inline int MPIX_Type_contiguous_x(size_t count, MPI_Datatype oldtype, MPI_Datatype * newtype)
{
    int c = count/(size_t)INT_MAX;
    int r = count%(size_t)INT_MAX;

    MPI_Datatype chunks;
    MPI_Type_vector(c, INT_MAX, INT_MAX, oldtype, &chunks);

    MPI_Datatype remainder;
    MPI_Type_contiguous(r, oldtype, &remainder);

    MPI_Aint lb /* unused */, extent;
    MPI_Type_get_extent(oldtype, &lb, &extent);

    MPI_Aint remdisp          = (MPI_Aint)c*(size_t)INT_MAX*extent;
    int blocklengths[2]       = {1,1};
    MPI_Aint displacements[2] = {0,remdisp};
    MPI_Datatype types[2]     = {chunks,remainder};
    MPI_Type_create_struct(2, blocklengths, displacements, types, newtype);

    MPI_Type_free(&chunks);
    MPI_Type_free(&remainder);

    return MPI_SUCCESS;
}

/* 'count' elements of oldtype, 'stride' elements apart (stride may be negative or exceed an
 * int).  Equivalent to MPI_Type_vector(count, 1, stride, oldtype) but with no limit on count
 * or stride.  The extent is that of the vector; callers who need a different one resize it. */
static inline int OSHMPI_Type_strided_x(size_t count, ptrdiff_t stride, MPI_Datatype oldtype,
                                        MPI_Datatype * newtype)
{
    const size_t M = (size_t) 1 << 30;
    MPI_Aint lb /* unused */, extent;
    MPI_Type_get_extent(oldtype, &lb, &extent);
    MPI_Aint sb = (MPI_Aint) stride * extent;
    size_t q = count / M, r = count % M;

    MPI_Datatype rem = MPI_DATATYPE_NULL, outer = MPI_DATATYPE_NULL;
    if (r > 0 || q == 0) MPI_Type_create_hvector((int) r, 1, sb, oldtype, &rem);
    if (q > 0) {
        MPI_Datatype inner;
        MPI_Type_create_hvector((int) M, 1, sb, oldtype, &inner);
        MPI_Type_create_hvector((int) q, 1, (MPI_Aint) M * sb, inner, &outer);
        MPI_Type_free(&inner);
    }
    if (q == 0) { *newtype = rem; return MPI_SUCCESS; }
    if (r == 0) { *newtype = outer; return MPI_SUCCESS; }

    int blocklengths[2] = {1, 1};
    MPI_Aint displacements[2] = {0, (MPI_Aint) q * (MPI_Aint) M * sb};
    MPI_Datatype types[2] = {outer, rem};
    MPI_Type_create_struct(2, blocklengths, displacements, types, newtype);
    MPI_Type_free(&outer);
    MPI_Type_free(&rem);
    return MPI_SUCCESS;
}

#endif // TYPE_CONTIGUOUS_X_H
