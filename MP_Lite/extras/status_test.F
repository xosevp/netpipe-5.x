      program status_test

      IMPLICIT REAL*8  (a-h,o-z)
      IMPLICIT INTEGER (i-n)

#include "mpif.h"

      integer a(100), b(100), nbor, ierr, status(MPI_STATUS_SIZE)
      integer msg_id, count

      call mpi_init(ierr)
      call mpi_comm_rank(MPI_COMM_WORLD,myproc,ierr)
      call mpi_comm_size(MPI_COMM_WORLD,nprocs,ierr)

      IF( MOD(nprocs, 2) .NE. 0 ) THEN
        STOP'You must use an even number of processors'
      ENDIF

      nbor = myproc + 1
      if( nbor/2 .NE. myproc/2 ) nbor = myproc - 1

      call mpi_send( a, 10, MPI_INTEGER, nbor, 1,
     +               MPI_COMM_WORLD, ierr)
      call mpi_recv( b, 10, MPI_INTEGER, -1, -1,
     +               MPI_COMM_WORLD, status, ierr)

      call mpi_get_count( status, MPI_INTEGER, count, ierr)
      call mpi_get_count( status, MPI_BYTE, nbytes, ierr)

      write(6,1000) myproc, nbor, count, nbytes, status(MPI_SOURCE),
     +              status(MPI_TAG)
 1000 FORMAT(i2,"<-->",i2,i6," integers = ",i6," bytes from node ",
     +       i2," with tag = ",i5)

      call mpi_barrier(MPI_COMM_WORLD, ierr)
      write(6,*)' Starting the MPI_Irecv() test'
      call mpi_barrier(MPI_COMM_WORLD, ierr)

      call mpi_irecv( b, 20, MPI_INTEGER, -1, -1,
     +               MPI_COMM_WORLD, msg_id, ierr)
      call mpi_barrier(MPI_COMM_WORLD, ierr)
      call mpi_send( a, 20, MPI_INTEGER, nbor, 2,
     +               MPI_COMM_WORLD, ierr)
      call mpi_wait( msg_id, status, ierr)

      call mpi_get_count( status, MPI_INTEGER, count, ierr)
      call mpi_get_count( status, MPI_BYTE, nbytes, ierr)

      write(6,1000) myproc, nbor, count, nbytes, status(MPI_SOURCE),
     +              status(MPI_TAG)


      call mpi_finalize(ierr)
      end

