////////////////////////////////////////////////////////////////////////////
// tcp.c - TCP module for NetPIPE
// Originally developed by Quinn Snell, Armin Mikler, John Gustafson at Ames Laboratory
// Currently developed by Dave Turner - Kansas State University - October 2015 -
// contact DrDaveTurner@gmail.com
// NetPIPE is freely distributed under a GPL license.
//
// Usage: make tcp
//        mpirun -np 2 --hostfile mpihosts NPtcp ...
//        mpirun NPtcp --help            for complete usage information
//////////////////////////////////////////////////////////////////////////////

   // Pull in global variables as externs

#define _GLOBAL_VAR_ extern
#include "netpipe.h"

#include <mpi.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define DEFPORT 5002

int reset_conn = 0, port = DEFPORT, serv_sd, comm_sd, nodelay, tcpbuffersize = 0;
char myhost[32], remotehost[32];
struct sockaddr_in *sdout;

   // Internal function prototypes

int np_create_socket();
void np_connect_to( );
void accept_from( );
void establish();
int readOnce();
void readFully( char *ptr, int nbytes );
void writeFully( char *ptr, int nbytes);


void Module_Init(int* pargc, char*** pargv)
{
   MPI_Init(pargc, pargv);
   MPI_Comm_rank(MPI_COMM_WORLD, &myproc);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
}

   // Process the module specific input arguments

void Module_ArgOpt( char *arg, char *opt )
{
   if(        !strcmp( arg, "reset") ) {
      reset_conn = 1;
      mprintf("Resetting connection after every trial\n");

   } else if( !strcmp( arg, "buffersize") ) {
      tcpbuffersize = atoi( opt );
      mprintf("Increasing the TCP buffer sizes to %d\n", tcpbuffersize);

   } else {
      PrintUsage( arg );
   }
}

void Module_PrintUsage()
{
   mprintf("\n  NPtcp specific input arguments\n");
   mprintf("  --reset          reset the socket between trials\n");
   mprintf("  --buffersize #   try to set the TCP send and recv socket buffer sizes\n");
}

void Module_Setup()
{
   int bound;
   MPI_Status status;

   if( stream ) reset_conn = 1;

   if( myproc < nprocs/2 ) port = DEFPORT + myproc;
   else                    port = DEFPORT + mypair;

   gethostname( myhost, 32);

      // Handle name change for Beocat's firewall

   if( !strcmp( myhost, "helios" ) ) { sprintf( myhost, "ns1.beocat.ksu.edu" ); }
   if( !strcmp( myhost, "moira"  ) ) { sprintf( myhost, "ns1.beocat.ksu.edu" ); }

   MPI_Sendrecv( myhost,     sizeof(myhost),     MPI_BYTE, mypair, 0,
                 remotehost, sizeof(remotehost), MPI_BYTE, mypair, 0,
                 MPI_COMM_WORLD, &status);

      // Create the listen socket, for any interface on my port

   sdout = (struct sockaddr_in *) malloc( sizeof( *sdout ) );
   memset( sdout, 0, sizeof(*sdout) );
   sdout->sin_family      = AF_INET;
   sdout->sin_addr.s_addr = htonl( INADDR_ANY );
   sdout->sin_port        = htons( port );

   serv_sd = np_create_socket( );  // This will be the listening socket

   bound = bind(serv_sd, (const struct sockaddr *) sdout, sizeof(*sdout) );
   ERRCHECK( bound, "%d bind on local address failed!", myproc);

   listen(serv_sd, 5); // Set the socket to listen for incoming connections

   establish();        // Establish connections

   mprintf("The socket buffer sizes may limit the maximum test size.\n");
   if( stream ) {
      mprintf("Socket buffers are being reset between trials to prevent\n");
      mprintf("   window collapse.\n");
   }
}   

   // Create a generic socket

int np_create_socket()
{
   int sd, one=1, sizeofint=sizeof(int), send_size, recv_size;
   static int print_once = 1;
   struct protoent *proto;

   sd = socket(AF_INET, SOCK_STREAM, 0);
   ERRCHECK( sd < 0, "%d can't open the stream socket!", myproc);

   proto = getprotobyname("tcp");
   ERRCHECK( ! proto, "%d protocol tcp unknown!", myproc);

       // Attempt to set TCP_NODELAY

   err = setsockopt(sd, proto->p_proto, TCP_NODELAY, (const void *) &one, sizeof(one));
   ERRCHECK( err < 0, "%d setsockopt: TCP_NODELAY failed!", myproc);

   err = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (const void *) &one, sizeof(one));
   ERRCHECK( err < 0, "%d setsockopt: SO_REUSEADDR failed!", myproc);

      // If requested, set the send and receive buffer sizes

   if( tcpbuffersize > 0 ) {

      err = setsockopt(sd, SOL_SOCKET, SO_SNDBUF, 
          (const void *) &(tcpbuffersize), sizeof(tcpbuffersize));
      ERRCHECK( err < 0, 
          "%d setsockopt: SO_SNDBUF failed (larger than actual buffer)!", myproc);
      err = setsockopt(sd, SOL_SOCKET, SO_RCVBUF, 
          (const void *) &(tcpbuffersize), sizeof(tcpbuffersize));
      ERRCHECK( err < 0, 
          "%d setsockopt: SO_RCVBUF failed (larger than actual buffer)!", myproc);
   }

   getsockopt(sd, SOL_SOCKET, SO_SNDBUF,
                   (char *) &send_size, (void *) &sizeofint);
   getsockopt(sd, SOL_SOCKET, SO_RCVBUF,
                   (char *) &recv_size, (void *) &sizeofint);

   if( print_once ) {
      mprintf("Send and receive buffers are %d and %d bytes\n", send_size, recv_size);
      print_once = 0;
   }

   return sd;
} 

   // Create a socket and connect it to the node specified

static char buf[] = "      ";

void np_connect_to( )
{
   struct sockaddr_in *sdin;
   struct hostent *addr;
   double t0 = myclock();

      // Set the socket info for initiating connections 

   sdin = (struct sockaddr_in *) malloc( sizeof(struct sockaddr_in) );
   memset( sdin, 0, sizeof( *sdin ) );

   addr = gethostbyname(remotehost);
   ERRCHECK( !addr, "%d invalid remote hostname '%s'", myproc, remotehost);
printf("addr->h_name = %s\n", addr->h_name);
printf("addr->h_addrtype = %d\n", addr->h_addrtype);
printf("addr->h_length = %d\n", addr->h_length);

   sdin->sin_family = addr->h_addrtype;
   bcopy(addr->h_addr, (char*) &(sdin->sin_addr.s_addr), addr->h_length);

   sdin->sin_port = htons(port);
printf("sdin->sin_port = %d\n", sdin->sin_port);

   comm_sd = np_create_socket( );

   errno = -1;
   while( connect(comm_sd, (const struct sockaddr *) sdin, sizeof(*sdin)) < 0 ) {

         // Keep trying until mypair has gotten to its accept() function

      ERRCHECK( (!reset_conn || errno != ECONNREFUSED) && myclock()-t0 > 20.0,
               "%d Cannot connect for 20 seconds!", myproc);
      sleep(1);
   }

      // Do a read here to make sure the socket is completely connected

   readFully( (char *) buf, strlen(buf)); 
   ERRCHECK( strcmp( buf, "SyncMe"), "Error reading sync buffer in np_connect_to()");
}

   // Accept a connect from mypair on the listen socket

void accept_from( )
{
   int one = 1;
   socklen_t clen;
   struct protoent *proto;
   char buf[] = "SyncMe";
   
   clen = (socklen_t) sizeof(sdout);

   comm_sd = accept(serv_sd, (struct sockaddr *) sdout, &clen);
   ERRCHECK( comm_sd < 0, "%d Accept Failed!", myproc);

      // Attempt to set TCP_NODELAY. TCP_NODELAY may or may not be propagated
      // to accepted sockets.

   proto = getprotobyname("tcp");
   ERRCHECK( ! proto, "%d Unknown protocol!", myproc);

   err = setsockopt(comm_sd, proto->p_proto, TCP_NODELAY,
                 (const void *) &one, sizeof(one));
   ERRCHECK( err < 0, "%d TCP_NODELAY Failed!", myproc);

   err = setsockopt(comm_sd, SOL_SOCKET, SO_REUSEADDR, 
                    (const void *) &one, sizeof(one));
   ERRCHECK( err < 0, "%d SO_REUSEADDR Failed!", myproc);

      // If requested, set the send and receive buffer sizes

   if( tcpbuffersize > 0 ) {

      err = setsockopt(comm_sd, SOL_SOCKET, SO_SNDBUF, 
          (const void *) &(tcpbuffersize), sizeof(tcpbuffersize));
      ERRCHECK( err < 0, 
          "%d setsockopt: SO_SNDBUF failed (larger than actual buffer)!", myproc);
      err = setsockopt(comm_sd, SOL_SOCKET, SO_RCVBUF, 
          (const void *) &(tcpbuffersize), sizeof(tcpbuffersize));
      ERRCHECK( err < 0, 
          "%d setsockopt: SO_RCVBUF failed (larger than actual buffer)!", myproc);
   }

   writeFully( (char *) buf, strlen(buf));  // Write to mypair to check the connection
}

     // Establish a connection to mypair

void establish()
{
   long socket_flags;

   if( myproc < nprocs/2 ){

      np_connect_to( );       // Connect to mypair

   } else {

      accept_from( );

   }

      // Make sockets non-blocking for CPU workload measurements

   if( workload ) {

      socket_flags = fcntl(comm_sd, F_GETFL, 0);
#if defined (FNONBLK)
      socket_flags = socket_flags + FNONBLK;
#elif defined (FNONBLOCK)
      socket_flags = socket_flags + FNONBLOCK;
#else
      socket_flags = socket_flags + O_NONBLOCK;
#endif
      err = fcntl(comm_sd, F_SETFL, socket_flags);
      ERRCHECK( err, "%d fcntl failed!", myproc);
   }
}

   // Read once from the socket (used only for TestForCompletion)

int readOnce()
{
   int bytesRead, errno;

   bytesRead = read(comm_sd, (void *) r_ptr, bytesLeft);

   if( bytesRead < 0 && errno == EWOULDBLOCK ) bytesRead = 0;
   ERRCHECK( bytesRead < 0, "%d read %d of %d", myproc, buflen - bytesLeft, buflen);

   bytesLeft -= bytesRead;    // Global var: running total of bytes left
   r_ptr     += bytesRead;    // r_ptr will be reset after completion

   return bytesRead;
}


   // Read buflen from the socket until complete

void readFully( char *ptr, int nbytes )
{
   int bytesRead, errno;

   while( nbytes > 0 ) { 

      bytesRead = read(comm_sd, (void *) ptr, nbytes);

      if( bytesRead < 0 && errno == EWOULDBLOCK ) bytesRead = 0;
      ERRCHECK( bytesRead < 0, "%d read %d of %d", myproc, buflen - nbytes, buflen);

      nbytes -= bytesRead;
      ptr    += bytesRead;
   }
}

   // Write nbytes to the socket until complete

void writeFully( char *ptr, int nbytes)
{
   int bytesSent, errno;

   while( nbytes > 0 ) { 

      bytesSent = write(comm_sd, (void *) ptr, nbytes);

      if( bytesSent < 0 && errno == EWOULDBLOCK ) bytesSent = 0;
      ERRCHECK( bytesSent < 0, "%d write %d of %d", myproc, buflen - nbytes, buflen);

      nbytes -= bytesSent;
      ptr    += bytesSent;
   }
}

   // Send the data to mypair

void SendData()
{
   writeFully( (char *) s_ptr, buflen);
}

void PrepostRecv() { }

void RecvData()
{
   if( ! workload ) {  // For workload all reading is done in TestForCompletion()
      readFully( (char *) r_ptr, buflen);
   }
}

   // This function reads the socket buffers once then tests for message
   // completion.  It may need to be adapted to read multiple times until
   // 0 bytes is read.

int Module_TestForCompletion( )
{
   readOnce( );

   return bytesLeft == 0 ? 1 : 0;   // Is message complete
}

void Sync()
{
    MPI_Barrier(MPI_COMM_WORLD);
}

   // Gather the times/failures from all procs to proc 0 for output

void Gather(double *buf)
{
   double time = buf[myproc];

   MPI_Gather( &time, 1, MPI_DOUBLE, buf, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

   // Proc 0 sets nrepeats then broadcasts it to all procs

void Broadcast( int *nrepeat )
{
   MPI_Bcast(nrepeat, 1, MPI_INT, 0, MPI_COMM_WORLD);
}

void Module_CleanUp()
{
   MPI_Finalize();

   close(comm_sd);     // Close the current sockets
   close(serv_sd);
}

   // Reset the sockets between trials

void Module_Reset()
{
  if( reset_conn ) {

   close(comm_sd);     // Close the current sockets
   close(serv_sd);

    Module_Setup();    // Now open and connect new sockets
  }
}


