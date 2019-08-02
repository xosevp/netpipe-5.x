////////////////////////////////////////////////////////////////////////////
// netpipe.h include file for netpipe.c and all module files for NetPIPE
// Dave Turner - Kansas State University - October 2015 -
// contact DrDaveTurner@gmail.com
//
// NetPIPE is freely distributed under a GPL license.
////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>   // getrusage()
#include <sys/time.h>       // struct timeval
#include <time.h>           // struct timespec
#include <unistd.h>         // getopt, read, write, ...
#include <inttypes.h>       // uint64_t
#include <fcntl.h>
//#include <asm-generic/fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#define __USE_GNU
#include <sched.h>

#if defined(MPLITE)
#include "mplite.h" // Included for the malloc wrapper
#endif


   // MEMSIZE should be twice the largest message size and larger than the cache size
#define  MEMSIZE            200000000

#define  LONGTIME           1e99
#define  PAGESIZE           sysconf(_SC_PAGESIZE)

   // Workspace size for CPU load measurement
#define WORKSIZE 131072       /* 132 kB will fit into any cache */
#define NELEMENTS (WORKSIZE/16)

#define     ABS(x)     (((x) < 0)?(-(x)):(x))
#define     MIN(x,y)   (((x) < (y))?(x):(y))
#define     MAX(x,y)   (((x) > (y))?(x):(y))

   // Define font colors for printing

#if defined( NOCOLOR )

#define RED     ""
#define GREEN   ""
#define YELLOW  ""
#define BLUE    ""
#define MAGENTA ""
#define CYAN    ""
#define ONRED   ""
#define RESET   ""

#else

#define RED     "\e[31m"
#define GREEN   "\e[32m"
#define YELLOW  "\e[33m"
#define BLUE    "\e[34m"
#define MAGENTA "\e[35m"
#define CYAN    "\e[36m"
#define ONRED   "\e[41m"
#define RESET   "\e[0m"

#endif

   // Set up a dump file for each proc if compiled with -DDUMPTOLOG

#if defined(DEBUG)

#define dbprintf(_format, _aa...) do { \
   if(        myproc == 0 ) { \
      fprintf(stderr, "%s: " YELLOW  _format RESET, __FUNCTION__, ##_aa); \
   } else if( myproc == 1 ) { \
      fprintf(stderr, "%s: " RED     _format RESET, __FUNCTION__, ##_aa); \
   } else if( myproc == 2 ) { \
      fprintf(stderr, "%s: " GREEN   _format RESET, __FUNCTION__, ##_aa); \
   } else if( myproc == 3 ) { \
      fprintf(stderr, "%s: " CYAN    _format RESET, __FUNCTION__, ##_aa); \
   } else { \
      fprintf(stderr, "%s: "         _format RESET, __FUNCTION__, ##_aa); \
   } \
   fflush( stderr ); \
} while(0)

#else
#define dbprintf(_format, _aa...)
#endif

#define mprintf if(myproc==0)printf

   // return error check variable and macro

int err;

#define ERRCHECK( _ltrue, _format, _args...) do {         \
   if( _ltrue ) {                                   \
      fprintf(stderr, "\nERROR in %s: " _format, __FUNCTION__, ##_args); \
      if( errno > 0 ) { \
         fprintf(stderr, " -- error %d: %s\n", errno, strerror(errno) ); \
      } \
      printf( "\n" ); \
      exit(0); \
   } \
} while(0)


   // Global variables for core and all modules (modules define _GLOBAL_VAR_ as extern)

_GLOBAL_VAR_ int     nprocs, myproc, master, transmitter, mypair, source, dest, 
                     buflen, bytesLeft;

_GLOBAL_VAR_ int     cache,         // Cache flag, 0 => limit cache, 1=> use cache
                     bidir,         // Bi-directional flag
                     stream,        // Streaming mode
                     rwstream,      // stream read/writes for memcpy and disk
                     workload,      // Measure the CPU workload of the comm system
                     burst,         // Prepost burst mode
                     async,         // pre-post receives
                     perturbation,
                     nrepeat_const,
                     ntrials,
                     start, end,    // Lower and upper limits to message size
                     finished,      // Set to 1 when main loop is finished
                     soffset,       // Send buffer offsets
                     roffset,       // Recv buffer offsets
                     disk;          // disk.c module requires no buffer check

_GLOBAL_VAR_ double  stoptime,      // Stop testing if a trial exceeds this time
                     est_latency,   // Estimated latency for each module (inits tlast)
                     clock_res;     // Clock resolution

_GLOBAL_VAR_ char    *hostfile,     // Name of the hostfile and other host names
                     **host,
                     *module;       // Name of the module being tested

_GLOBAL_VAR_ char    *sr_buf,       // Original unaligned send/recv buffer
                                    // First half is send buf, second half recv

                     *s_buf,        // Aligned send buffer
                     *s_ptr,        // Pointer to current location in send buffer

                     *r_buf,        // Aligned receive buffer
                     *r_ptr,        // Pointer to current location in recv buffer

                     *dsr_buf,      // Pointer to remote send/recv buffer for RDMA
                     *ds_buf,       // Send pointer base on dest proc for kernel_copy.c
                     *dr_buf,       // Recv pointer base on dest proc for kernel_copy.c
                     *ds_ptr,       // Send pointer on dest proc for kernel_copy.c
                     *dr_ptr;       // Recv pointer on dest proc for kernel_copy.c


   // Define global variables for each module

#if defined(THEO)
  double Theo_Time();
#endif

double myclock();


   // Function prototypes for netpipe.c

void flushcache(int *ptr, uint64_t n);
void PrintUsage( char *arg);
void SavePtrs( char *s, char *r );
void RestorePtrs( char **s, char **r );
void AdvancePtrs( char **s_cache, char **r_cache, char **s_no, char **r_no, int size );
void InitBufferData(uint64_t nbytes);
void SetupBufferData();
void CheckBufferData();
void MallocBufs( uint64_t size );
void FreeBufs( );
void Reset();
char *mymalloc( uint64_t nbytes );
void myfree( char *ptr );
int TestForCompletion( );

double DoWork( char *type, int nelem, int count, int calibration );
char * bytestring( int nbytes );
char * bitstring( double gbps );
char * timestring( double secs );
double AverageBandwidth( double rate );
double Theo_Time( );

   // Function prototypes for module routines

void Module_Init( int* pargc, char*** pargv);
void Module_ArgOpt( char *arg, char *opt );
void Module_PrintUsage();
void Module_Setup();
void Sync();
void SendData();
void PrepostRecv();
int Module_TestForCompletion();
void RecvData();
void Gather(double *buf);
void Broadcast(int *nrepeat);
void Module_CleanUp();
void Module_Reset();
char *Module_malloc( uint64_t nbytes );
void Module_free( void *buf);

