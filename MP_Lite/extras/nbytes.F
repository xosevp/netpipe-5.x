      program nbytes

      IMPLICIT REAL*8  (a-h,o-z)
      IMPLICIT INTEGER (i-n)

#include "mpif.h"

      DOUBLE PRECISION s(10000), r(10000)
      INTEGER status(MPI_STATUS_SIZE), count

      call mpi_init(ierr)
      call mpi_comm_rank(MPI_COMM_WORLD,myproc,ierr)
      call mpi_comm_size(MPI_COMM_WORLD,nprocs,ierr)

      DO i=1, 10000
        s(i) = i
        r(i) = 0.0
      ENDDO

      if( myproc .eq. 0 ) then

        call MPI_Send( s, 100, MPI_DOUBLE_PRECISION, 1, 0, 
     +                 MPI_COMM_WORLD, ierr)

      else

        call MPI_Recv( r, 10000, MPI_DOUBLE_PRECISION, 0, 0, 
     +                 MPI_COMM_WORLD, status, ierr)

      endif

      call mpi_barrier(MPI_COMM_WORLD, ierr)
      print*, 'communications done'
      call mpi_barrier(MPI_COMM_WORLD, ierr)

      call MPI_Get_count( status, MPI_DOUBLE_PRECISION, count, ierr)

      print*,myproc, count, r(100), r(101)

      call mpi_finalize(ierr)
      end

