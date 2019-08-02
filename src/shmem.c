////////////////////////////////////////////////////////////////////////////
// shmem.c - shmem module for NetPIPE
// Currently developed by Dave Turner - Kansas State University - October 2015 -
// contact DrDaveTurner@gmail.com
// NetPIPE is freely distributed under a GPL license.
//
// Usage: make shmem
//        mpirun -np 2 --hostfile mpihosts NPshmem ...
//        mpirun NPshmem --help            for complete usage information
//////////////////////////////////////////////////////////////////////////////
// TODO: I have not tested this module out, and haven't tried it with gpshmem
// TODO: Are there SHMEM gather and broadcast functions

   // Pull in module specific variables and globals

#define _GLOBAL_VAR_ extern
#include "netpipe.h"

#if defined(GPSHMEM)
  #include "gpshmem.h"
#else
  //#include <mpp/shmem.h>
  #include <shmem.h>
#endif
#include <mpi.h>

int tag = 0;


int Module_Init(int* pargc, char*** pargv)
{
   MPI_Init(pargc, pargv);
   MPI_Comm_rank(MPI_COMM_WORLD, &myproc);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
}

   // Process the module specific input arguments

int Module_ArgOpt( char *arg, char *opt )
{
   //if(        !strcmp( arg, "mtu") ) {
      //ERRCHECK( ! opt, "No MTU size was specified");
      //mtu    = atoi( opt );         // 2048 is the default
   //} else {
   //}
   PrintUsage( arg );
}

int Module_PrintUsage()
{
   mprintf("\n  NPshmem specific input arguments\n");
   //mprintf("");
}

int Module_Setup()
{
   start_pes( nprocs );     // Initialize SHMEM processes
}

int SendData()
{
   if(buflen%8==0) {
      shmem_put64(  s_ptr, s_ptr, buflen/8, mypair);
   } else {
      shmem_putmem( s_ptr, s_ptr, buflen,   mypair);
   }
}

int PrepostRecv() { }

int RecvData()
{
   tag = ++tag % 100;

   while( r_ptr[buflen-1] != tag ) {
      sched_yield();
   }
}

int Module_TestForCompletion()
{
   if( r_ptr[buflen-1] == tag ) return 1;
   else                          return 0;
}

int Sync()
{
   shmem_barrier_all();
}

   // Gather times or failures (nprocs doubles)

int Gather( double *buf)
{
   double time = buf[myproc];

   MPI_Gather( &time, 1, MPI_DOUBLE, buf, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

   // Broadcast nrepeat to all nodes

int Broadcast(int *nrepeat)
{
   MPI_Bcast( nrepeat, 1, MPI_INT, 0, MPI_COMM_WORLD);
}

int Module_CleanUp()
{
   shmem_finalize( );

   MPI_Finalize();
}

char *Module_malloc( uint64_t nbytes )
{
   return (char *) shmalloc( nbytes );
}

int Module_free( void *buf )
{
   if( buf != NULL ) shfree( buf );
}
