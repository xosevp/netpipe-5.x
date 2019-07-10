C-----------------------------------------------------------------------
C     Filename: pong.f
C-----------------------------------------------------------------------
C-----------------------------------------------------------------------

	PROGRAM PONG

        IMPLICIT NONE

        INTEGER ierr, myproc, nprocs 
        INTEGER end_node, size, other_node, i, last
        DOUBLE PRECISION t1, t2, time
        DOUBLE PRECISION max_rate, min_latency
        DOUBLE PRECISION a(132000), b(132000)


C-----------------------------------------------------------------------
C       Init MPI
C-----------------------------------------------------------------------
        include "mpif.h"

        INTEGER status(MPI_STATUS_SIZE)
        INTEGER request, request_a, request_b

        call mpi_init(ierr)
        call mpi_comm_rank(MPI_COMM_WORLD,myproc,ierr)
        call mpi_comm_size(MPI_COMM_WORLD,nprocs,ierr)

C-----------------------------------------------------------------------
C       
C-----------------------------------------------------------------------
        min_latency = 10e6
        max_rate = 0.0

        DO i = 1,132000
           a(i) = i
           b(i) = .0
        END DO

        if ( nprocs .NE. 2) then
           stop
        end if

        end_node = nprocs - 1
        other_node = MOD (myproc + 1, 2)

        PRINT*,'Hello from ',myproc,' of ',nprocs
        call mpi_barrier(MPI_COMM_WORLD, ierr)

C-----------------------------------------------------------------------
C       Timer accuracy test       
C-----------------------------------------------------------------------

        t1 = mpi_wtime()
1       t2 = mpi_wtime()

         if (t2 .EQ. t1) then 
            go to 1
         end if

         if (myproc .EQ. 0) then
            PRINT*,'Timer accuracy of ',(t2-t1)*1000000, 'usecs'
         end if

C-----------------------------------------------------------------------
C Communications between nodes
C   - Blocking sends and recvs
C   - No guarantee of prepost, so might pass through comm buffer
C-----------------------------------------------------------------------

          size = 8
         
2         DO i = 1, size/8
             a(i) = i
             b(i) = 0.0
          END DO

          last = size/8
          call mpi_barrier (MPI_COMM_WORLD, ierr)
          t1 = mpi_wtime()
          
          if (myproc .EQ. 0) then
             call mpi_send(a, size/8, MPI_DOUBLE_PRECISION, other_node, 
     &                     0, MPI_COMM_WORLD, ierr)
             call mpi_recv(b, size/8, MPI_DOUBLE_PRECISION, other_node, 
     &                     0, MPI_COMM_WORLD, status, ierr)
          else
             call mpi_recv(b, size/8, MPI_DOUBLE_PRECISION, other_node,
     &                     0, MPI_COMM_WORLD, status, ierr)
             b(1) = b(1) + 1.0
             if ( last .NE. 1) then
                b(last) = b(last) + 1.0
             end if
             call mpi_send(b, size/8, MPI_DOUBLE_PRECISION, other_node, 
     &                     0, MPI_COMM_WORLD, ierr)
          end if

          t2 = mpi_wtime()
          time = 1e6 * (t2 - t1)
          call mpi_barrier (MPI_COMM_WORLD, ierr)
         
          if ( (b(1) .NE. 2.0) .OR. (b(last) .NE. last + 1) ) then
             PRINT*,'ERROR - b[1] = ',b(1),' b[',last,'] = ',b(last)
             STOP
          end if
          DO i = 2, last - 1
             if ( b(i) .NE. i) then
                PRINT*,'ERROR - b[',i,'] = ',b(i)
             end if
          END DO

          if ( (myproc .EQ. 0) .AND. (time .GT. 0.000001) ) then
             PRINT 100,size,' bytes took ',time,' usec (',2.0*size/time,
     &              ' MB/sec)'
100          FORMAT(I8, A, F15.2, A, F10.7, A )
             if (2*size/time .GT. max_rate) then
                max_rate = 2 * size / time
             end if
             if (time / 2 .LT. min_latency) then
                min_latency = time / 2
             end if
           else
               if ( myproc .EQ. 0 ) then
                  PRINT*,size,' bytes took less than the timer accuracy'
               end if
           end if
   
          size = size * 2
          if (size .LE. 1048576) then
             go to 2
          end if


C-----------------------------------------------------------------------
C Async communications
C   - Prepost receives to guarantee bypassing the comm buffer
C-----------------------------------------------------------------------
          call mpi_barrier(MPI_COMM_WORLD, ierr)
          
          if ( myproc .EQ. 0 ) then
             PRINT*,' '
             PRINT*,'Asynchronous ping-pong'
             PRINT*,' '
          end if

          size = 8
         
3         DO i = 1, size/8
             a(i) = i
             b(i) = 0.0
          END DO

          last = size/8
          call mpi_irecv (b, size/8, MPI_DOUBLE_PRECISION, other_node, 
     &                    0, MPI_COMM_WORLD, request)
          call mpi_barrier (MPI_COMM_WORLD, ierr)
          t1 = mpi_wtime()

          if (myproc .EQ. 0) then
             call mpi_send(a, size/8, MPI_DOUBLE_PRECISION, other_node,
     &                     0, MPI_COMM_WORLD, ierr)
             call mpi_wait(request, status, ierr)
          else
             call mpi_wait(request, status, ierr)
             b(1) = b(1) + 1.0
             if ( last .NE. 1) then
                b(last) = b(last) + 1.0
             end if
             call mpi_send(b, size/8, MPI_DOUBLE_PRECISION, other_node,
     &                     0, MPI_COMM_WORLD, ierr)
          end if

          t2 = mpi_wtime()
          time = 1e6 * (t2 - t1)
          call mpi_barrier (MPI_COMM_WORLD, ierr)

          if ( (b(1) .NE. 2.0) .OR. (b(last) .NE. last + 1) ) then
             PRINT*,'ERROR - b[1] = ',b(1),' b[',last,'] = ',b(last)
             STOP
          end if
          DO i = 2, last - 1
             if ( b(i) .NE. i) then
                PRINT*,'ERROR - b[',i,'] = ',b(i)
             end if
          END DO

          if ( (myproc .EQ. 0) .AND. (time .GT. 0.000001) ) then
             PRINT 200,size,' bytes took ',time,' usec (',2.0*size/time,
     &              ' MB/sec)'
200          FORMAT(I8, A, F15.2, A, F10.7, A )
             if (2*size/time .GT. max_rate) then
                max_rate = 2 * size / time
             end if

             if (time / 2 .LT. min_latency) then
                min_latency = time / 2
             end if
           else
               if ( myproc .EQ. 0 ) then
                  PRINT*,size,' bytes took less than the timer accuracy'
               end if
           end if


          size = size * 2
          if (size .LE. 1048576) then
             go to 3
          end if

C-----------------------------------------------------------------------
C         Bi-directional asynchronous ping-pong
C-----------------------------------------------------------------------

          if ( myproc .EQ. 0 ) then
             PRINT*,' '
             PRINT*,'Bi-directional asynchronous ping-pong'
             PRINT*,' '
          end if

          size = 8
         
4         DO i = 1, size/8
             a(i) = i
             b(i) = 0.0
          END DO

          last = size/8
          call mpi_irecv (b, size/8, MPI_DOUBLE_PRECISION, other_node, 
     &                    0, MPI_COMM_WORLD, request_b)
          call mpi_irecv (a, size/8, MPI_DOUBLE_PRECISION, other_node, 
     &                    0, MPI_COMM_WORLD, request_a)
          call mpi_barrier (MPI_COMM_WORLD, ierr)
          t1 = mpi_wtime()


          call mpi_send(a, size/8, MPI_DOUBLE_PRECISION, other_node,
     &                     0, MPI_COMM_WORLD, ierr)
          call mpi_wait(request_b, status, ierr)

          b(1) = b(1) + 1.0
          if ( last .NE. 1) then
             b(last) = b(last) + 1.0
          end if

          call mpi_send(b, size/8, MPI_DOUBLE_PRECISION, other_node,
     &                     0, MPI_COMM_WORLD, ierr)
          call mpi_wait(request_a, status, ierr)

          t2 = mpi_wtime()
          time = 1e6 * (t2 - t1)
          call mpi_barrier (MPI_COMM_WORLD, ierr)

          if ( (b(1) .NE. 2.0) .OR. (b(last) .NE. last + 1) ) then
             PRINT*,'ERROR - b[1] = ',b(1),' b[',last,'] = ',b(last)
             STOP
          end if
          DO i = 2, last - 1
             if ( b(i) .NE. i) then
                PRINT*,'ERROR - b[',i,'] = ',b(i)
             end if
          END DO

          if ( (myproc .EQ. 0) .AND. (time .GT. 0.000001) ) then
             PRINT 300,size,' bytes took ',time,' usec (',2.0*size/time,
     &              ' MB/sec)'
300          FORMAT(I8, A, F15.2, A, F10.7, A )
             if (2*size/time .GT. max_rate) then
                max_rate = 2 * size / time
             end if

             if (time / 2 .LT. min_latency) then
                min_latency = time / 2
             end if
           else
               if ( myproc .EQ. 0 ) then
                  PRINT*,size,' bytes took less than the timer accuracy'
               end if
           end if

          size = size * 2
          if (size .LE. 1048576) then
             go to 4
          end if
C-----------------------------------------------------------------------
C       Max rate, Min latency
C-----------------------------------------------------------------------

          if ( myproc .EQ. 0 ) then
             PRINT*,' '
             PRINT 400,'Max rate = ',max_rate,' MB/sec Min latency = ',
     &             min_latency
400          FORMAT(A, F10.7, A, F25.12)   
             PRINT*,' '
          end if
C-----------------------------------------------------------------------
C       Leave MPI
C-----------------------------------------------------------------------

        call mpi_finalize(ierr)

C-----------------------------------------------------------------------
C     End Program
C-----------------------------------------------------------------------

        END 

C-----------------------------------------------------------------------
