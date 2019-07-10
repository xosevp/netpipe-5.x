      program glob_test

      IMPLICIT REAL*8  (a-h,o-z)
      IMPLICIT INTEGER (i-n)

      DOUBLE PRECISION s, r

#include "mpif.h"

      call mpi_init(ierr)
      call mpi_comm_rank(MPI_COMM_WORLD,myproc,ierr)
      call mpi_comm_size(MPI_COMM_WORLD,nprocs,ierr)

      s = 10.0**myproc

      call mpi_allreduce( s, r, 1, MPI_DOUBLE_PRECISION, 
     +                    MPI_SUM, MPI_COMM_WOLRD, ierr)

      call mpi_barrier(MPI_COMM_WORLD, ierr)
      write(6,*) myproc, s, r

      if( myproc .eq. 0) s = 22.0

      call mpi_bcast( s, 1, MPI_DOUBLE_PRECISION, 0, 
     +                MPI_COMM_WORLD, ierr)

      call mpi_barrier(MPI_COMM_WORLD, ierr)
      write(6,*) myproc, s

      call mpi_finalize(ierr)
      end

