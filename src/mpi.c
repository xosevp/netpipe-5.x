////////////////////////////////////////////////////////////////////////////
// mpi.c - MPI module for NetPIPE
// Originally developed by Quinn Snell, Armin Mikler, John Gustafson at Ames Laboratory
// Currently developed by Dave Turner - Kansas State University - October 2015 -
// contact DrDaveTurner@gmail.com
// NetPIPE is freely distributed under a GPL license.
//
// Usage: make mpi
//        mpirun -np 2 --hostfile mpihosts NPmpi ...
//        mpirun NPmpi --help            for complete usage information
//////////////////////////////////////////////////////////////////////////////
// TODO: Why is OpenMPI nocache so slow???

   // Pull in module specific variables and globals

#define _GLOBAL_VAR_ extern
#include "netpipe.h"

#include <mpi.h>

int syncSend = 0, bytes_per = 1;
int datasize = 1;
static int nrepeats = 100, stag = 0, rtag = 0, btag = 0;
static MPI_Request *recvRequest;


   // Module_Init() is called before any input options are read in

void Module_Init( int* pargc, char*** pargv)
{
   MPI_Init(pargc, pargv);
   MPI_Comm_rank(MPI_COMM_WORLD, &myproc);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

   async = syncSend = source = 0;
}

   // Process the module specific input arguments

void Module_ArgOpt( char *arg, char *opt )
{
   if(        !strcmp( arg, "async") || !strcmp( arg, "asynchronous") ) {
      async = 1;
      mprintf("Preposting asynchronous receives\n");

   } else if( !strcmp( arg, "syncSend") || !strcmp( arg, "sync") ) {
      syncSend = 1;
      mprintf("Using MPI_SSend() instead of blocking sends\n");

   } else if( !strcmp( arg, "anysource") ) {
      source = MPI_ANY_SOURCE;
      mprintf("Receive using the MPI_ANY_SOURCE flag %d\n", source);

   } else if( !strcmp( arg, "doubles") || ~strcmp( arg, "double") ) {
      perturbation = 0;
      datasize = sizeof(double);
      bytes_per = 8;
      if( start % 8 > 0 ) start += (8 - start % 8);  // round up to fac of 8 B
      if( start < 16 ) start = 16;
      if( end % 8 > 0 ) end += (8 - end % 8);        // round up to fac of 8 B
      mprintf("Using type MPI_DOUBLE for testing and turning perturbations off\n");

   } else {
      PrintUsage( arg );
   }
}

void Module_PrintUsage()
{
   mprintf("\n  NPmpi specific input arguments\n");
   mprintf("  --async        use asynchronous MPI_Irecv() to pre-post\n");
   mprintf("  --syncSend     use synchronous  MPI_SSend() instead of blocking\n");
   mprintf("  --anysource    receive using the MPI_ANY_SOURCE flag\n");
   mprintf("  --doubles      Test MPI_DOUBLE instead of MPI_BYTE\n");
}

void Module_Setup()
{
   if( bidir && ! async ) {
      mprintf("MPI implementations do not have to guarantee message progress.\n");
      mprintf("Setting --async for bidirectional run to avoid locking up.\n\n");
   }
   async = 1;
   recvRequest = malloc( sizeof(MPI_Request) );
}   


void Sync()
{
   dbprintf("%d in Sync\n", myproc);
   MPI_Barrier(MPI_COMM_WORLD);
   dbprintf("%d exiting Sync\n", myproc);
}

/*
   // Sync within each host, sync between hosts, then send go to all within host
   //   Can't remember why this was useful over MPI_Barrier()

void Sync2()
{
   int myroot, i;
   double token = myproc, rtoken;
   MPI_Status status;

   dbprintf("%d in Sync\n", myproc);
   if( myproc < nprocs/2 ) myroot = 0;
   else                    myroot = nprocs/2;

      // Sync my half of the processes together first

   if( nprocs == 2 ) { // 1 proc per host so skip this step

   } else if( myproc == myroot ) {    // Collect msgs from all procs on my host

      for( i = myproc+1; i < myproc+nprocs/2; i++ ) {
         MPI_Recv( &rtoken, 1, MPI_DOUBLE, i, btag, MPI_COMM_WORLD, &status);
      }

   } else {                           // Send msg to my root

      MPI_Send( &token, 1, MPI_DOUBLE, myroot, btag, MPI_COMM_WORLD);

   }
   btag = (btag + 1) % nrepeats + nrepeats;

      // Now have the roots on each host sync

   if( myproc == 0 ) {

      MPI_Send(  &token, 1, MPI_DOUBLE, nprocs/2, btag, MPI_COMM_WORLD);
      MPI_Recv( &rtoken, 1, MPI_DOUBLE, nprocs/2, btag, MPI_COMM_WORLD, &status);

   } else if( myproc == nprocs/2 ) {

      MPI_Recv( &rtoken, 1, MPI_DOUBLE, 0, btag, MPI_COMM_WORLD, &status);
      MPI_Send(  &token, 1, MPI_DOUBLE, 0, btag, MPI_COMM_WORLD);
   }
   btag = (btag + 1) % nrepeats + nrepeats;

      // Send all nodes on my host their go message

   if( nprocs == 2 ) {           // 1 proc per host so skip this

   } else if( myproc == myroot ) {

      for( i = myproc+1; i < myproc+nprocs/2; i++ ) {
         MPI_Send( &token, 1, MPI_DOUBLE, i, btag, MPI_COMM_WORLD);
      }

   } else {

      MPI_Recv( &rtoken, 1, MPI_DOUBLE, myroot, btag, MPI_COMM_WORLD, &status);

   }
   btag = (btag + 1) % nrepeats + nrepeats;
   dbprintf("%d exiting Sync\n", myproc);
}
*/

   // Send using MPI_BYTE by default, MPI_DOUBLE if requested

void SendData()
{
   int nelements = buflen / bytes_per;
   dbprintf("%d Posting send %p %d bytes to %d with stag %d\n",
      myproc, s_ptr, buflen, mypair, stag);

   if( syncSend ) {
      if( datasize == 1 ) {
         MPI_Ssend(s_ptr, nelements, MPI_BYTE, mypair, stag, MPI_COMM_WORLD);
      } else {
         MPI_Ssend(s_ptr, nelements, MPI_DOUBLE, mypair, stag, MPI_COMM_WORLD);
      }
   } else {
      if( datasize == 1 ) {
         MPI_Send(s_ptr, nelements, MPI_BYTE, mypair, stag, MPI_COMM_WORLD);
      } else {
         MPI_Send(s_ptr, nelements, MPI_DOUBLE, mypair, stag, MPI_COMM_WORLD);
      }
   }

   stag = (stag + 1) % nrepeats;
   dbprintf("%d Done posting send\n", myproc );
}

   // PrepostRecv is only called if async is set

void PrepostRecv()
{
   int m = 0, nelements = buflen / bytes_per;
   if( burst ) m = rtag;

   dbprintf("%d Posting recv %p %d bytes from %d with rtag %d\n",
      myproc, r_ptr, buflen, source, rtag);
   if( datasize == 1 ) {
      MPI_Irecv(r_ptr, nelements, MPI_BYTE, source, rtag, MPI_COMM_WORLD, &recvRequest[m]);
   } else {
      MPI_Irecv(r_ptr, nelements, MPI_DOUBLE, source, rtag, MPI_COMM_WORLD, &recvRequest[m]);
   }

   rtag = (rtag + 1) % nrepeats;
   dbprintf("%d Done posting recv\n", myproc );
}

void RecvData()
{
   MPI_Status status;
   int nelements = buflen / bytes_per;

   dbprintf("%d RecvData %p %d bytes from %d with rtag %d\n",
      myproc, r_ptr, buflen, source, rtag);

   if( async ) {

      if( burst ) {
         MPI_Wait(&recvRequest[rtag], &status);
         rtag = (rtag + 1) % nrepeats;
      } else {
         MPI_Wait(&recvRequest[0], &status);
      }
      dbprintf("%d RecvData MPI_Wait()  done\n", myproc);

   } else {

      if( datasize == 1 ) {
         MPI_Recv(r_ptr, nelements, MPI_BYTE, source, rtag, MPI_COMM_WORLD, &status);
      } else {
         MPI_Recv(r_ptr, nelements, MPI_DOUBLE, source, rtag, MPI_COMM_WORLD, &status);
      }

      dbprintf("%d Receiveed %p %d bytes from %d with rtag %d\n",
         myproc, r_ptr, buflen, source, rtag);
      rtag = (rtag + 1) % nrepeats;
   }
}

int Module_TestForCompletion( )
{
   int complete;
   int m = 0;
   if( burst ) m = rtag;

   MPI_Test( &recvRequest[m], &complete, MPI_STATUS_IGNORE );

   return complete;
}

   // Gather the times/failures from all procs to proc 0 for output

void Gather(double *buf)
{
   double time = buf[myproc];

   MPI_Gather( &time, 1, MPI_DOUBLE, buf, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

   // Proc 0 sets nrepeats then broadcasts it to all procs

void Broadcast(int *nrepeat)
{
   MPI_Bcast(nrepeat, 1, MPI_INT, 0, MPI_COMM_WORLD);
   nrepeats = *nrepeat;

      // Set up recvRequest array for burst mode if needed

   if( burst ) {
      free( recvRequest );
      recvRequest = malloc( nrepeats * sizeof(MPI_Request) );
      ERRCHECK( recvRequest == NULL, "Could not malloc %d recvRequest", nrepeats);
   }

   stag = rtag = 0;   // Reset all tags
   btag = nrepeats;
   //dbprintf("%d Broadcast() nrepeats = %d\n", myproc, nrepeats);
}

void Module_CleanUp()
{
   MPI_Finalize();
}

