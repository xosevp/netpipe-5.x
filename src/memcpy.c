////////////////////////////////////////////////////////////////////////////
// memcpy.c module for NetPIPE
// Dave Turner - Kansas State University - October 2015 -
// contact DrDaveTurner@gmail.com
// NetPIPE is freely distributed under a GPL license.
//
// Measure the raw memory copy performance and copying through a pipe
//   Can also measure using optimized memory copy routines (see opt_memcpy.c)
//   Can measure the copy rate through pipes
//
// Usage: make memcpy
//        NPmemcpy [options]
//        NPmemcpy --help                    for complete usage information
////////////////////////////////////////////////////////////////////////////

   // Pull in global variables as externs

#define _GLOBAL_VAR_ extern
#include "netpipe.h"

#include <mpi.h>
#include <sys/shm.h>
#include <sched.h>

#ifdef USE_OPT_MEMCPY
#define memcopy(a,b,c) opt_memcpy(a,b,c)
int opt_memcpy();
#else
#define memcopy(a,b,c) memcpy(a,b,c)
#endif

int pd[2];
static char *mem_op;
static int ichar = 97;
static double dvar[8];

void Module_Init(int* pargc, char*** pargv)
{
   MPI_Init( pargc, pargv);
   MPI_Comm_rank(MPI_COMM_WORLD, &myproc);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

   stream = 1;
   module = strdup( "memcpy" );  // Used in netpipe.c to set npairs = nprocs

   mem_op = strdup( "memcpy" );  // Can be altered by input parameters
   end = 100000000;

   est_latency = 100.0e-9;       // Used to calc initial nrepeat

   memset( dvar, 111, 8*sizeof(double) );
}

   // Process the module specific input arguments

void Module_ArgOpt( char *arg, char *opt )
{
   mem_op = strdup( arg );
   if(        !strcmp( arg, "memcpy" ) ) {
      mprintf("\nCopying data using memcpy()\n");
      mprintf("    Bandwidth reported uses the amount of data copied. This data hits\n");
      mprintf("    the memory bus twice, once for the read and once for the write.\n");
   } else if( !strcmp( arg, "dcopy" ) ) {
      mprintf("\nCopying data using an unrolled copy loop with doubles.\n");
      mprintf("    Bandwidth reported uses the amount of data copied. This data hits\n");
      mprintf("    the memory bus twice, once for the read and once for the write.\n");
   } else if( !strcmp( arg, "dread" ) ) {
      mprintf("\nReading data using an unrolled copy loop with doubles.\n");
   } else if( !strcmp( arg, "dwrite" ) ) {
      mprintf("\nWriting data using an unrolled copy loop with doubles.\n");
   } else if( !strcmp( arg, "memset" ) ) {
      mprintf("\nUsing a memset()\n");
   } else if( !strcmp( arg, "pipe" ) ) {
      mprintf("\nPushing data through a non-blocking pipe()\n");
      int flags;
      err = pipe( pd );
      ERRCHECK( err, "Could not create the pipe()");

         // Make the pipe non-blocking
      flags = fcntl( pd[0], F_GETFL, 0);
      flags |= O_NONBLOCK;
      fcntl( pd[0], F_SETFL, flags);
      fcntl( pd[1], F_SETFL, flags);
   } else {
      PrintUsage( arg );
   }
}

void Module_PrintUsage()
{
   mprintf("\n  NPmemcpy specific input arguments\n");
   mprintf("      --memcpy      uses asm opt memcpy() (default)\n");
   mprintf("      --memset      uses asm opt memset()\n");
   mprintf("      --dcopy       unrolled copy  by doubles loop\n");
   mprintf("      --dread       unrolled read  by doubles loop\n");
   mprintf("      --dwrite      unrolled write by doubles loop\n");
   mprintf("      --pipe        Move data through a non-blocking pipe()\n");
}

void Module_Setup()
{
   mprintf("Testing memory using %d MPI tasks\n", nprocs);
}   

void PrepostRecv() { }


void SendData()
{
   char *rptr, *sptr;
   int i, ncopy8, nleft;
   double *sptr_double, *rptr_double;
   char   *csptr, *crptr;
   int nb, nread = 0, nwritten = 0;


      // Set the send and recv pointers to the correct buffers

   rptr = dr_ptr;
   if( cache ) sptr = ds_ptr;
   else        sptr =  s_ptr;

   sptr_double = (double *) sptr;
   rptr_double = (double *) rptr;

   if( !strcmp( mem_op, "memcpy" ) ) {  // memcpy() function

      memcopy(rptr, sptr, buflen);      // Macro to memcpy or opt_memcpy

   } else if( !strcmp( mem_op, "memset" ) ) {  // memset() function

      memset(rptr, ichar,  buflen);
      if( ++ichar > 122 ) { ichar = 97; }   // Loop through a to z

   } else if( !strcmp( mem_op, "dcopy" ) ) {   // Copy by doubles loop

      ncopy8 = buflen / (8 * sizeof(double) );  // Loop unroll
      nleft  = buflen - (8 * sizeof(double) ) * ncopy8;

      for( i = 0; i < ncopy8 ; i++ ) {
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
      }

         // Now clean up the end

      csptr = (char *) sptr_double;
      crptr = (char *) rptr_double;

      for( i = 0; i < nleft; i++ ) {
         *crptr++ = *csptr++;
      }

   } else if( !strcmp( mem_op, "dread" ) ) {  // Read by doubles loop

      ncopy8 = buflen / (8 * sizeof(double) );  // Loop unroll
      nleft  = buflen - (8 * sizeof(double) ) * ncopy8;

      for( i = 0; i < ncopy8 ; i++ ) {

         rptr_double = dvar;        // Read to the short array in cache each time

         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;

         if( i == -1 ) {    // Dummy statement to avoid compiler optimization
            printf("%lf",dvar[0]*dvar[1]*dvar[2]*dvar[3]*dvar[4]*dvar[5]*dvar[6]*dvar[7]);
         }
      }

         // Now clean up the end

      csptr = (char *) sptr_double;
      crptr = (char *) dvar;

      for( i = 0; i < nleft; i++ ) {
         *crptr++ = *csptr++;
      }

   } else if( !strcmp( mem_op, "dwrite" ) ) {   // Write by doubles loop

      ncopy8 = buflen / (8 * sizeof(double) );  // Loop unroll and allow vectorization
      nleft  = buflen - (8 * sizeof(double) ) * ncopy8;

      for( i = 0; i < ncopy8 ; i++ ) {

         sptr_double = dvar;    // Write from the short array in cache each time

         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
         *rptr_double++ = *sptr_double++;
      }

         // Now clean up the end

      csptr = (char *) dvar;
      crptr = (char *) rptr_double;

      for( i = 0; i < nleft; i++ ) {
         *crptr++ = *csptr++;
      }

   } else if( !strcmp( mem_op, "pipe" ) ) {    // Push the data thru a pipe

      while( nread < buflen ) {

            // non-blocking, so write as much as I can (~1 page)

         nb = write(pd[1], sptr, buflen - nwritten);
         sptr     += nb;
         nwritten += nb;

         nb = read( pd[0], rptr, nwritten - nread);
         rptr  += nb;
         nread += nb;
      }

   } else {
      ERRCHECK( 1, "You must choose memcpy, memset, dcopy, dread, dwrite, pipe");
   }
}

void RecvData() { }

void Sync()
{
   //if( nprocs == 1 ) return;

   MPI_Barrier(MPI_COMM_WORLD);
}

void Broadcast(int *nrepeat)
{
   MPI_Bcast( nrepeat, 1, MPI_INT, 0, MPI_COMM_WORLD);
}

void Gather(double *buf)
{
   double time = buf[myproc];

   MPI_Gather( &time, 1, MPI_DOUBLE, buf, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

void Module_CleanUp()
{
   MPI_Finalize();
}

