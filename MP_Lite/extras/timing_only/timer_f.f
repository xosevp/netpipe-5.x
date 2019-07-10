* Fortran MP_Enter() function that adds a '|' symbol to manually terminate 
*   the string before calling the C MP_Enter() routine

      SUBROUTINE MP_Enter(name)

      CHARACTER*(*) name
      CHARACTER*101 newname

      CALL MP_Terminate(name, newname)

      CALL mp_enter_c(newname)
      END


      SUBROUTINE MP_Time_Report(name)

      CHARACTER*(*) name
      CHARACTER*101 newname
      
      CALL MP_Terminate(name, newname)

      CALL mp_time_report_c(newname)
      END


      SUBROUTINE MP_Terminate( name, newname)

      INTEGER i, length
      CHARACTER*(*) name
      CHARACTER*101 newname

      length = LEN(name)
      IF ( length .GT. 100) THEN
         PRINT*,'WARNING: Character string too long in MP_Terminate()'
      ENDIF
c     PRINT*,'Length = ',length,' Name = ',name

      DO i = 1, length
        newname(i:i) = name(i:i)
      ENDDO
      IF ( name(length:length) .NE. '|' ) THEN 
        newname(length+1:length+1) = '|'
      ENDIF

      END

