      program cart_test

      IMPLICIT REAL*8  (a-h,o-z)
      IMPLICIT INTEGER (i-n)

      integer dims(2), per(2), coords(2)

#include "mpif.h"

      call mpi_init(ierr)
      call mpi_comm_rank(MPI_COMM_WORLD,myproc,ierr)
      call mpi_comm_size(MPI_COMM_WORLD,nprocs,ierr)

      dims(1) = 4
      dims(2) = nprocs/4
      call mpi_cart_create( MPI_COMM_WORLD, 2, dims, 
     +                      per, 0, icart, ierr)
      call mpi_cart_coords( icart, 7, 2, coords, ierr)
      call mpi_cart_rank( icart, coords, irank, ierr)

      write(6,*) myproc, irank, coords(1), coords(2)
      call mpi_barrier(icart, ierr)

      call mpi_cart_shift( icart, 0, 1, isource, idest, ierr)
      write(6,*) myproc, isource, idest
      call mpi_barrier(icart, ierr)

      call mpi_cart_shift( icart, 1, 1, isource, idest, ierr)
      write(6,*) myproc, isource, idest
      call mpi_barrier(icart, ierr)

      call mpi_cart_get( icart, 2, dims, per, coords, ierr)
      write(6,*) myproc, dims(1), dims(2), coords(1), coords(2)


      call mpi_finalize(ierr)
      end

