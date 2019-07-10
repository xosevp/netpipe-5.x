c-----------------------------------------------------------------------
c     Pong.f was written by Bogdan Vasiliu of Ames Lab.
c     It was converted from a C version by Dave Turner.
c-----------------------------------------------------------------------
      PROGRAM Pong

      IMPLICIT NONE

      INTEGER myproc, nprocs, tag
      INTEGER size, other_node, i, last
      REAL*8 t1, t2, time
      REAL*8 max_rate, min_latency
      REAL*8 a(132000), b(132000)
      REAL*8 mp_time
      INTEGER irid, irid_a, irid_b


c-----------------------------------------------------------------------
c       Init MP_Lite
c-----------------------------------------------------------------------


      CALL MP_Init(0,0)
      CALL MP_Myproc( myproc )
      CALL MP_Nprocs( nprocs )

      min_latency = 10e6
      max_rate = 0.0

      DO i = 1,132000
        a(i) = i
        b(i) = 0.0d0
      ENDDO

      IF( nprocs .NE. 2) STOP'You must choose 2 processors'

      other_node = MOD(myproc + 1, 2)

c     PRINT*,'Hello from ',myproc,' of ',nprocs
      CALL MP_Sync()

c-----------------------------------------------------------------------
c       Timer accuracy test       
c-----------------------------------------------------------------------

      t1 = MP_Time()
   10 t2 = MP_Time()
      IF(t2 .EQ. t1) GOTO 10

      IF(myproc .EQ. 0)THEN
        PRINT*,'Timer accuracy of ',(t2-t1)*1000000, 'usecs'
      ENDIF

c-----------------------------------------------------------------------
c Communications between nodes
c   - Blocking sends and recvs
c   - No guarantee of prepost, so data might pass through the comm buffer
c-----------------------------------------------------------------------

      CALL MP_Enter('Blocking')

      size = 8
      tag = 0

   20 DO i = 1, size/8
        a(i) = i
        b(i) = 0.0
      ENDDO

      last = size/8
      CALL MP_Sync()
      t1 = MP_Time()

      IF(myproc .EQ. 0)THEN
        CALL MP_Send(a, size, other_node, tag)
        tag = tag + 1
        CALL MP_Recv(b, size, other_node, tag)
      ELSE
        CALL MP_Recv(b, size, other_node, tag)
        b(1) = b(1) + 1.0
        IF( last .NE. 1) b(last) = b(last) + 1.0
        tag = tag + 1
        CALL MP_Send(b, size, other_node, tag)
      ENDIF

      t2 = MP_Time()
      time = 1e6 * (t2 - t1)
      CALL MP_Sync()

      IF( (b(1) .NE. 2.0) .OR. (b(last) .NE. last + 1) )THEN
        PRINT*,'ERROR - b[1] = ',b(1),' b[',last,'] = ',b(last)
        STOP
      ENDIF
      DO i = 2, last - 1
        IF( b(i) .NE. i) PRINT*,'ERROR - b[',i,'] = ',b(i)
      ENDDO

      IF( (myproc .EQ. 0) .AND. (time .GT. 0.000001) )THEN

        PRINT 1000,size,' bytes took ',time,' usec (',2.0*size/time,
     &              ' MB/sec)'
 1000   FORMAT(I8, A, F10.1, A, F8.3, A )

        IF(2*size/time .GT. max_rate) max_rate = 2 * size / time
        IF(time / 2 .LT. min_latency) min_latency = time / 2

      ELSE

        IF( myproc .EQ. 0 )THEN
          PRINT*,size,' bytes took less than the timer accuracy'
        ENDIF

      ENDIF

      size = size * 2
      IF(size .LE. 1048576) GOTO 20

      CALL MP_Leave(0.0d0)


c-----------------------------------------------------------------------
c Async communications
c   - Prepost receives to guarantee bypassing the comm buffer
c-----------------------------------------------------------------------

      CALL MP_Enter('Asynchronous ping-pong')

      IF( myproc .EQ. 0 )THEN
        PRINT*,' '
        PRINT*,'Asynchronous ping-pong'
        PRINT*,' '
      ENDIF

      size = 8
   30 DO i = 1, size/8
        a(i) = i
        b(i) = 0.0
      ENDDO
      last = size/8
      tag = tag + 1

      CALL MP_Arecv(b, size, other_node, tag, irid)
      CALL MP_Sync()
      t1 = MP_Time()

      IF(myproc .EQ. 0)THEN
        CALL MP_Send(a, size, other_node, tag)
        CALL MP_Wait(irid)
      ELSE
        CALL MP_Wait(irid)
        b(1) = b(1) + 1.0
        IF( last .NE. 1) b(last) = b(last) + 1.0
        CALL MP_Send(b, size, other_node, tag)
      ENDIF

      t2 = MP_Time()
      time = 1e6 * (t2 - t1)
      CALL MP_Sync()

      IF( (b(1) .NE. 2.0) .OR. (b(last) .NE. last + 1) )THEN
        PRINT*,'ERROR - b[1] = ',b(1),' b[',last,'] = ',b(last)
        STOP
      ENDIF
      DO i = 2, last - 1
        IF( b(i) .NE. i) PRINT*,'ERROR - b[',i,'] = ',b(i)
      ENDDO

      IF( (myproc .EQ. 0) .AND. (time .GT. 0.000001) )THEN
        PRINT 1000,size,' bytes took ',time,' usec (',2.0*size/time,
     &              ' MB/sec)'
        IF(2*size/time .GT. max_rate) max_rate = 2 * size / time

        IF(time / 2 .LT. min_latency) min_latency = time / 2
      ELSE
        IF( myproc .EQ. 0 )THEN
          PRINT*,size,' bytes took less than the timer accuracy'
        ENDIF
      ENDIF

      size = size * 2
      IF(size .LE. 1048576) GOTO 30

      CALL MP_Leave(0.0d0)


c-----------------------------------------------------------------------
c         Bi-directional asynchronous ping-pong
c-----------------------------------------------------------------------

      IF( myproc .EQ. 0 )THEN
        PRINT*,' '
        PRINT*,'Bi-directional asynchronous ping-pong'
        PRINT*,' '
      ENDIF
      CALL MP_Enter('Bi-directional')
      size = 8
      tag = tag + 1

   40 DO i = 1, size/8
        a(i) = i
        b(i) = 0.0
      ENDDO

      last = size/8
      CALL MP_Arecv(b, size, other_node, tag, irid_b)
      CALL MP_Arecv(a, size, other_node, tag+1, irid_a)
      CALL MP_Sync()
      t1 = MP_Time()


      CALL MP_Send(a, size, other_node, tag)
      CALL MP_Wait(irid_b)

      b(1) = b(1) + 1.0
      IF( last .NE. 1) b(last) = b(last) + 1.0
      tag = tag + 1

      CALL MP_Send(b, size, other_node, tag)
      CALL MP_Wait(irid_a)

      t2 = MP_Time()
      time = 1e6 * (t2 - t1)
      CALL MP_Sync()

      IF( (b(1) .NE. 2.0) .OR. (b(last) .NE. last + 1) )THEN
        PRINT*,'ERROR - b[1] = ',b(1),' b[',last,'] = ',b(last)
        STOP
      ENDIF
      DO i = 2, last - 1
        IF( b(i) .NE. i) PRINT*,'ERROR - b[',i,'] = ',b(i)
      ENDDO

      IF( (myproc .EQ. 0) .AND. (time .GT. 0.000001) )THEN

        PRINT 1000,size,' bytes took ',time,' usec (',2.0*size/time,
     &              ' MB/sec)'

        IF(2*size/time .GT. max_rate) max_rate = 2 * size / time
        IF(time / 2 .LT. min_latency) min_latency = time / 2

      ELSE

        IF( myproc .EQ. 0 )THEN
          PRINT*,size,' bytes took less than the timer accuracy'
        ENDIF

      ENDIF

      size = size * 2
      IF(size .LE. 1048576) GOTO 40

      CALL MP_Leave(0.0d0)


c-----------------------------------------------------------------------
c       Max rate, Min latency
c-----------------------------------------------------------------------

      IF( myproc .EQ. 0 )THEN
        PRINT*,' '
        PRINT 1030,'Max rate = ',max_rate,' MB/sec Min latency = ',
     &             min_latency
 1030   FORMAT(A, F10.7, A, F25.12)
        PRINT*,' '
      ENDIF

c-----------------------------------------------------------------------
c       Leave MP_LITE
c-----------------------------------------------------------------------

      CALL MP_Time_Report('pong_f.out')
      CALL MP_Finalize()

      END

