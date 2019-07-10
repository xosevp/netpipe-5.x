* MPI_Init() from a Fortran program must be called from Fortran

      SUBROUTINE MP_Init( i, j)

      INTEGER i, j, ierror
      INCLUDE 'mpif.h'

c     PRINT*,'Before MPI_INIT in mpif.f'
      CALL MPI_INIT(ierror)
c     PRINT*,'After MPI_INIT in mpif.f'

* Finish the initialization

      CALL MP_Init_f2c()

      END

