       PROGRAM TRANPHOT
       implicit real*8 (a-h,o-z)
       implicit integer (i-n)
       parameter(ixymax=100)

       complex*16 work1(4*ixymax)

       call MP_Init(0,0)
       call MP_Nprocs(nproc)
       call MP_Myproc(iproc)

       do i=1,4*ixymax
         work1(i)=(1.0, 1.0)
       enddo

       call MP_Sync()
       call MP_dSum(work1,8*ixymax)
c      call MP_Sync()

       print*, iproc,work1(1),'=iproc,work1(1)'

       call MP_Finalize()
    
       end
