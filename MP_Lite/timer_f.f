c#if defined sgi
c     CHARACTER *(*) oldname
c     CHARACTER *101 newname
c#else
c#endif
* Fortran MP_Enter() function that adds a '|' symbol to manually terminate 
*   the string before calling the C MP_Enter() routine

      SUBROUTINE MP_Enter(oldname)

      CHARACTER (*) oldname
      CHARACTER newname*101

      CALL MP_Terminate(oldname, newname)

      CALL mp_enter_c(newname)
      END


      SUBROUTINE MP_Get_Time(oldname)

      CHARACTER (*) oldname
      CHARACTER newname*101
      
      CALL MP_Terminate(oldname, newname)

      CALL mp_get_time_c(newname)
      END

      SUBROUTINE MP_Get_Flops(oldname)

      CHARACTER (*) oldname
      CHARACTER newname*101
      
      CALL MP_Terminate(oldname, newname)

      CALL mp_get_flops_c(newname)
      END

      SUBROUTINE MP_Time_Report(oldname)

      CHARACTER (*) oldname
      CHARACTER newname*101

      CALL MP_Terminate(oldname, newname)

      CALL mp_time_report_c(newname)
      END

      SUBROUTINE MP_Set(oldname, value)

      CHARACTER (*) oldname
      INTEGER value

      IF( oldname .EQ. 'debug' .OR.
     +   oldname .EQ. 'DEBUG' .OR.
     +   oldname .EQ. 'Debug' ) THEN
         CALL mp_set_debug(value)
      ELSE
         PRINT*,'timer_f.f: MP_Set() could not match ',oldname, value
      ENDIF

      END



      SUBROUTINE MP_Terminate( oldname, newname)

      INTEGER i, length
      CHARACTER (*) oldname
      CHARACTER newname*101

      length = LEN(oldname)
      IF ( length .GT. 100) THEN
         PRINT*,'WARNING: Character string too long in MP_Terminate()'
      ENDIF
c     PRINT*,'Length = ',length,' Name = ',oldname

      DO i = 1, length
        newname(i:i) = oldname(i:i)
      ENDDO
      IF ( oldname(length:length) .NE. '|' ) THEN 
        newname(length+1:length+1) = '|'
      ENDIF

      END

