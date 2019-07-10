* MP_Lite by Dave Turner - Ames Lab - Sept of 1997+
* This file contains some I/O commands for F77 programs
*   - MP_Get_Token() and MP_Pass_Token() provide node-order I/O
*     by passing a token that contains the file offset (shared file pointer).
*     All nodes must participate, beginning by all opening the same file.
*   * Unfortunately, most f77 compilers don't support fseek, ftell, and
*     fflush or forflush, so this is not a portable approach. F77 I/O is
*     just not flexible enough to provide easy solutions.
* ? Anyone else got any good ideas on how to make f77 I/O easier???


      SUBROUTINE MP_Get_Token(unit)

      INTEGER unit, prev, err, token, myproc, nprocs
      INTEGER FTELL

      CALL MP_Myproc(myproc)
      CALL MP_Nprocs(nprocs)

      IF( myproc .GT. 0 ) THEN
        prev = MOD(nprocs + myproc-1, nprocs)

        CALL MP_iRecv(token, 1, prev)

#if 0
        IF( unit .GT. 0 ) THEN
          CALL FSEEK(unit, token, 0)
        ENDIF
#endif
      ENDIF
      END


      SUBROUTINE MP_Pass_Token(unit)

      INTEGER unit, prev, err, token, myproc, nprocs
      INTEGER FTELL

      CALL MP_Myproc(myproc)
      CALL MP_Nprocs(nprocs)

      next = MOD(myproc+1, nprocs)
      prev = MOD(nprocs + myproc-1, nprocs)

#if 0
      IF( unit .GT. 0 ) THEN
        CALL FFLUSH(unit)
        token = FTELL(unit)
      ENDIF
#endif
      CALL MP_iSend(token, 1, next)

      IF( myproc .EQ. 0 ) THEN

        CALL MP_iRecv(token, 1, prev)

#if 0
        IF( unit .GT. 0 ) THEN
          CALL FSEEK(unit, token, 0)
        ENDIF
#endif
      ENDIF
      END


