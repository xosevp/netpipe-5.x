////////////////////////////////////////////////////////////////////////////
// ibverbs.c module for NetPIPE
// NetPIPE is freely distributed under a GPL license.
// Dave Turner - Kansas State University - October 2015 -
// contact DrDaveTurner@gmail.com
// I looked at rc_pingpong.c and ibv.c (Cisco) while developing this module.
//
// The goal of this code is to measure the performance for passing messages
//    using 2-sided send/recv and 1-sided puts that are representative of what
//    would be seen in practical codes (no vendor cheating techniques).
// MPI is used to launch the executables, exchange handshake info, and
//    provide global bcast/gather/sync functions
//
// Usage: make ibverbs
//        mpirun -np 2 --hostfile mpihosts NPibverbs ...
//        mpirun NPibverbs --help            for complete usage information
//        mpirun -np 2 NPibverbs --device mlx4_0 --port 1 --rdma
//
// inline max is 512 bytes for RoCE - doesn't make much difference
// immediate mode only allows 4 bytes so it is not tested here
// UD only allows a single MTU packet, so break messages into 
//    linked lists of work requests
////////////////////////////////////////////////////////////////////////////
// TODO: RC - Why does IB take extra latency hit at 1 MTU???
//            Look for any other optimizations.

// TODO: RDMA - nocache burst segfaults

// TODO: UD - RoCE fails to pass 1st message, same on QDR IB
// TODO: UD - Try 2-4 QP striping to optimize bandwidth
// TODO: UD - check pre-burst preposting of many recv buffers
// TODO: UD - does copy buffer need to be registered?
//
// TODO: Compare to OpenMPI implementation - UD, inline small, striping, polling?, RC too
// TODO: Test with non-pinned memory?
// TODO: TestForCompletion() needed for workload measurements
//          Test workload polling vs events

   // Pull in global variables as externs

#define _GLOBAL_VAR_ extern
#include "netpipe.h"

#include <mpi.h>
#include <infiniband/verbs.h>


   // Global variables for this module

struct ibv_context      *ib_context;
struct ibv_comp_channel *ib_channel = NULL;
struct ibv_port_attr     ib_port_attr;
struct ibv_pd           *ib_pd;
struct ibv_qp           **ib_qp;
struct ibv_cq           *ib_recv_cq, *ib_send_cq;
enum ibv_mtu             ib_mtu;
struct ibv_ah           *ud_address_handle;
int                      ib_port;
int                      ib_qkey = 0x11111111;
int                      ib_sl = 0;

struct ibv_mr           *send_mr, *recv_mr, *ud_mr;
int                      tx_depth = 1, rx_depth = 1;     // Set higher for burst or stream

enum completion_types {    // Completion types
   POLLING,
   EVENTS
} comp_type;

enum communication_types {    // Communication types
   SEND_RECV,
   RDMA,
   ATOMIC
} comm_type;

      // IB connection information for local (me) and remote (mypair)
struct ib_info {
   uint32_t      lid, qpn, psn; // IB connection info
   union ibv_gid gid;           // Address info for RoCE connections
   char         *buf;           // remote buffer for rdma puts
   uint32_t      rkey;          // for remote buffer
   char          hostname[32];  // the hostname
} local, remote;

int allaccess = ( IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | 
                  IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC );

int mtu = 2048, nacks = 1, pinned = 1, port = -1;
int unreliable = 0, ud_memcpy = 0, ud_bufsize = 0, sendqps = 1;
int send_posts = 0, recv_posts = 0, inline_data = 0, max_inline_data = 0;
char *ud_buf;
int roce = 0, gidx = -1;
const char *device, *devname;

   // Internal function prototypes

void exchange_ib_settings( );
void ready_connection( );
void init_context(struct ibv_device *ib_dev);
void close_context( );
void post_recv_RC( char *recv_buf, uint32_t msgsize, uint32_t lkey );
void post_recv_UD( char *recv_buf, uint32_t msgsize, uint32_t lkey );
void post_send_RC( char *send_buf, uint32_t msgsize, uint32_t lkey );
void post_send_UD( char *send_buf, uint32_t msgsize, uint32_t lkey );
void post_send_UD2( char *send_buf, uint32_t msgsize, uint32_t lkey );
void rdma_put( char *send_buf, uint32_t msgsize, uint32_t lkey, 
               char *recv_buf, uint32_t rkey );
static inline void pollwait( int s_posts, int r_posts ); // static to match ibv_poll_cq

   // Module_Init() is called before any input options are read in

void Module_Init(int* pargc, char*** pargv)
{
      // Get proc info from MPI

   MPI_Init( pargc, pargv);
   MPI_Comm_rank(MPI_COMM_WORLD, &myproc);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

   device = NULL;
   gethostname( local.hostname, 32);
   srand48(getpid() * time(NULL));

      // Set the defaults for testing

   comm_type = SEND_RECV;
   comp_type = POLLING;
}

   // Process the module specific input arguments

void Module_ArgOpt( char *arg, char *opt )
{
   if(        !strcmp( arg, "async") || !strcmp( arg, "asynchronous") ) {
      async = 1;
      mprintf("Preposting asynchronous receives\n");

   } else if( !strcmp( arg, "mtu") ) {
      ERRCHECK( ! opt, "No MTU size was specified");
      mtu    = atoi( opt );         // 2048 is the default

   } else if( !strcmp( arg, "polling") ) {   // Polling is default
      comp_type = POLLING;

   } else if( !strcmp( arg, "events") ) {
      comp_type = EVENTS;
      mprintf("Using Events completion\n");

   } else if( !strcmp( arg, "acks") || !strcmp( arg, "ack") ) {
      ERRCHECK( ! opt, "You did not provide the number of acks to send at once");
      nacks = atoi( opt );

   } else if( !strcmp( arg, "rdma") ) {     // 2-sided is default
      comm_type = RDMA;
      mprintf("Doing 1-sided RDMA write, recv node waits on overwrite of last byte\n");
   } else if( !strcmp(arg,"unreliable") || !strcmp(arg,"UD") || !strcmp(arg,"ud") ) {
      unreliable = 1;
      mprintf("Using unreliable datagrams UD\n");

   } else if( !strcmp(arg,"qps") || !strcmp(arg,"sendqps") ) {
      ERRCHECK( ! opt, "You did not specify the number of send QP's");
      sendqps = atoi( opt );
      mprintf("Unreliable datagrams will sent striped across %d QP's\n", sendqps);

   } else if( !strcmp(arg,"memcpy") || !strcmp(arg,"copy") || !strcmp(arg,"memcopy") ) {
      ud_memcpy = 1;
      mprintf("Unreliable datagrams will use an extra memory copy\n");

   } else if( !strcmp( arg, "inline") ) {   // Pass small messages inline
      inline_data= 1;
      mprintf("Passing small messages inline\n");

   } else if( !strcmp( arg, "pinned") ) {   // pinned is default
      pinned = 1;

   } else if( !strcmp( arg, "unpinned") ) {
      pinned = 0;

   } else if( !strcmp( arg, "device") ) {
      ERRCHECK( ! opt, "No device name was specified");
      device = strdup( opt );

   } else if( !strcmp( arg, "port") ) {
      ERRCHECK( ! opt, "No port number was specified");
      port = atoi( opt );

   } else {
      PrintUsage( arg );
   }
}

void Module_PrintUsage()
{
   mprintf("\n  NPibverbs specific input arguments (default 2-sided polling 2048 MTU)\n");
   mprintf("  --mtu #        mtu size of 256, 512, 1024, 2048, 4096\n");
   mprintf("  --polling      Poll for receive completion\n");
   mprintf("  --events       Use event notification for receive completion\n");
   mprintf("    --acks #     For events, number of acks to do at once (default 1)\n");
   mprintf("  --ud           Use Unreliable Datagrams instead of a Reliable Connection\n");
   mprintf("    --sendqps #  Number of send QP's to strip UD MTU's accross (default 1)\n");
   mprintf("    --memcpy     For UD, do an extra memory copy (default)\n");
   mprintf("  --rdma         use 1-sided RDMA puts\n");
   mprintf("  --inline       Pass small messages inline\n");
   mprintf("  --pinned       (default)  or  --unpinned\n");
   mprintf("  --device name --port #\n");
   mprintf("                 (default is the first device and active port found)\n");
}

   // Module_Setup() is called after input options are read in

void Module_Setup( )
{
   int i, j, nports, max_mr_size = 0, max_device_wr = 0;
   struct ibv_device      **dev_list;
   struct ibv_context      *ctx;
   struct ibv_device_attr   device_attr;

   if( !async ) mprintf("Setting --async as required for InfiniBand\n");
   async = 1;  // InfiniBand requires asynchronus receives

   ERRCHECK( stream && comm_type == RDMA, 
      "No flow control for RDMA writes so you cannot test in streaming mode");

   ERRCHECK( bidir && comm_type == RDMA, 
      "No flow control for RDMA writes so you cannot test in bi-directional mode");
   // Would need to prepost the next recv before SendData as well to allow this

   ERRCHECK( burst && comm_type == RDMA, 
      "burst mode for RDMA writes currently segfaults");

      // For multiple uni-directional streams in --event mode, the ibverbs comms
      // interfere with the ibverbs in MPI_Broadcast() for some reason.  Multiple
      // streams should always be run with --bidir anyway, so just dis-allow

   ERRCHECK( comp_type == EVENTS && nprocs > 2 && ! bidir, 
      "Multi-stream IBverbs --events only works with --bidir mode");

   if( comm_type == RDMA && burst ) {
      if( cache ) mprintf("Setting --nocache as required for burst mode using RDMA\n");
      cache = 0;
   }

   switch (mtu) {
      case 256:  ib_mtu = IBV_MTU_256;  break;
      case 512:  ib_mtu = IBV_MTU_512;  break;
      case 1024: ib_mtu = IBV_MTU_1024; break;
      case 2048: ib_mtu = IBV_MTU_2048; break;
      case 4096: ib_mtu = IBV_MTU_4096; break;
      default:   ERRCHECK( 1, "Invalide MTU of %d", mtu);
   }

      // Allocate space for 1 or more QP pointers

   ib_qp = malloc( sendqps * sizeof( struct ibv_qp * ) );

      // Check the input parameters

   ERRCHECK( ud_memcpy && ! unreliable, "--memcpy only works with UD");
   ERRCHECK( nacks > 1 && (comp_type != EVENTS), "--acks only works when using events");
   //if( unreliable ) {
      //end = mtu;
      //mprintf("UD - setting the end to the MTU size of %d bytes\n", mtu);
   //}

      // Get the list of IB devices on each host
   
   dev_list = ibv_get_device_list(NULL);
   ERRCHECK( ! dev_list, "Failed to get InfiniBand device list");

      // Print the list and specs out to the screen for each device and port
      //   Choose the requested device and port or the first active

   mprintf("\n     List of InfiniBand ports (choose requested dev and port or first active)\n");

   for (i = 0; dev_list[i]; ++i) {

      devname = ibv_get_device_name(dev_list[i]);

      ctx = ibv_open_device( dev_list[i] );
      ERRCHECK( ! ctx, "Couldn't get context for %s", devname);

      err = ibv_query_device( ctx, &device_attr);
      ERRCHECK( err, "Couldn't query device to get attributes");

      nports = device_attr.phys_port_cnt;

      for( j = 1; j <= nports; j++ ) {

         err = ibv_query_port(ctx, j, &ib_port_attr);
         ERRCHECK( err, "Could not get port info for probe context");

         mprintf(" device  %s:%d  ", devname, j);
         if( ib_port_attr.link_layer == IBV_LINK_LAYER_INFINIBAND ) mprintf(" InfiniBand      ");
         if( ib_port_attr.link_layer == IBV_LINK_LAYER_ETHERNET )   mprintf(" Ethernet (RoCE) ");

            // Check if it is the dev and port requested or the first active

         if( ib_port_attr.state != IBV_PORT_ACTIVE ) {
            mprintf(" - connection not active\n");

         } else if ( !device ||     // No device and port requested, take the first active
              (device && (!strcmp( device, devname)) && port == j ) ) { // Req dev & port

            ib_port = j;
            max_device_wr = device_attr.max_qp_wr;
            max_mr_size   = device_attr.max_mr_size;
            tx_depth = max_device_wr / 2;    // Overkill unless streaming or burst mode
            rx_depth = max_device_wr / 2;    // Normally 1 is enough for both

            init_context( dev_list[i] );  // Initialize the context, QP, and CQ channel
            if( ib_port_attr.link_layer != IBV_LINK_LAYER_INFINIBAND ) {
               roce = 1;
               if( gidx < 0 ) gidx = 0;
            }
            mprintf(" - using this connection\n");
            device = strdup( devname );

         } else mprintf("\n");
      }
      ibv_close_device( ctx );
   }

   ERRCHECK( ib_port == 0, "No active ports found");

   mprintf("\nMaximum posts for %s:%d is %d, max memory registration is %d bytes\n", 
      device, port, max_device_wr, max_mr_size);
   if( stream ) {
      mprintf("Streaming limited to %d tests per data point\n", tx_depth);
      cache = 0;
      mprintf("No flow control for InfiniBand so streaming requires walking\n");
      mprintf("the send/recv pointers through larger buffers to prevent overwrites.\n");
   }
   if( burst ) mprintf("Pre-burst limited to %d tests per data point\n", rx_depth);

      // Get the port info for send and receive contexts

   err = ibv_query_port(ib_context, ib_port, &ib_port_attr);
   ERRCHECK( err, "Couldn't get port info for context");
   local.lid = ib_port_attr.lid;
   local.psn = lrand48() & 0xffffff;

      // Assume same subnet for now.

   memset(&local.gid, 0, sizeof(local.gid));
   if( roce ) {    // DDT - eventually cycle thru gidx to find one that works???
      err = ibv_query_gid( ib_context, ib_port, gidx, &local.gid);
      ERRCHECK( err, "Cannot read the gid of index %d", gidx);
   }

   exchange_ib_settings( );       // Get remote IB information

   char gid_address[33];
//DDT - gid_address is coming back as '::'
   inet_ntop( AF_INET6, &local.gid, gid_address, sizeof(gid_address) );
   mprintf("\nLocal:  %s  LID %#04x  QPN %#06x  PSN %#06x  GID %s\n",
            local.hostname, local.lid, local.qpn, local.psn, gid_address);
   inet_ntop( AF_INET6, &remote.gid, gid_address, sizeof(gid_address) );
   mprintf(  "Remote: %s  LID %#04x  QPN %#06x  PSN %#06x  GID %s\n",
            remote.hostname, remote.lid, remote.qpn, remote.psn, gid_address);

   ready_connection( );      // Connect to my remote pair

   ibv_free_device_list( dev_list );

   Sync( );
}


   // Send the data to my pair using 2-sided send/recv or 1-sided rdma put

struct ibv_wc wc;

void SendData( )
{
   dbprintf("%d Sending %p  %d bytes to %d\n", myproc, s_ptr, buflen, mypair);

   if( stream && send_posts > 0 ) pollwait( send_posts, 0 ); // Clear previous send

   if( comm_type == SEND_RECV ) {

      if( unreliable ) {    // UD
         post_send_UD( s_ptr, buflen, send_mr->lkey );
      } else {              // RC
         post_send_RC( s_ptr, buflen, send_mr->lkey );
      }

   } else if( comm_type == RDMA ) {

      if( send_posts > 0 ) pollwait( send_posts, 0 ); // Clear previous send

         // The first and last bytes of s_ptr are set to stag already

      rdma_put( s_ptr, buflen, send_mr->lkey, dr_ptr, remote.rkey );

   } else {
      ERRCHECK(0, "ATOMIC not implemented yet");
   }

   dbprintf("%d Done Sending %d bytes to %d\n", myproc, buflen, mypair);
}


   // 2-sided must pre-post recvs to allow direct write into buffer

void PrepostRecv( )
{
   dbprintf("%d pre-posting %p  %d bytes from %d\n", myproc, r_ptr, buflen, mypair);

   if( comm_type == RDMA ) {

      r_ptr[0] = r_ptr[buflen-1] = -1;  // Set first and last to -1 for overwrite test
      dbprintf("%d Set r_prt[0] at %p & r_ptr[%d] at %p to -1\n", 
         myproc, &r_ptr[0], buflen-1, &r_ptr[buflen-1]);

   } else if( unreliable ) {    // Prepare the buffer for UD if needed

      int nbytes = sizeof( struct ibv_grh ); // 40 byte GRH segment
      if( ud_memcpy ) nbytes += buflen;      // GRH + buflen

      if( nbytes != ud_bufsize ) {
         if( ud_buf ) {
            err = ibv_dereg_mr( ud_mr );
            free( ud_buf );
         }
         ud_bufsize = nbytes;
         ud_buf = malloc( ud_bufsize );
         ud_mr = ibv_reg_mr(ib_pd, ud_buf, ud_bufsize, allaccess);
         ERRCHECK( ! ud_mr, "Could not register MR for UD buffer");
      }

      post_recv_UD( ud_buf, ud_bufsize, ud_mr->lkey );

   } else {    // RC

      post_recv_RC( r_ptr, buflen, recv_mr->lkey );

   }

   if( comp_type == EVENTS ) {   // Request notification of cq event

      err = ibv_req_notify_cq( ib_recv_cq, 0);    // 0 means unsolicited
      ERRCHECK( err, "Couldn't request CQ notification in PrepostRecv()");
   }
   dbprintf("%d Done pre-posting %d bytes from %d\n", myproc, buflen, mypair);
}

   // 2-sided always pre-posts using PrepostRecv()
   // Block on receive of all data from my pair
   // 1-sided will spin on over-write of last in buffer

static int nevents = 0;

void RecvData( )
{
   static int rtag = 0;
   //volatile char* v_ptr = &r_ptr[buflen-1];

   dbprintf("%d Receiving %d bytes from %d\n", myproc, buflen, mypair);
   if( comm_type == SEND_RECV ) {

      if( comp_type == EVENTS ) {   // Use events

         struct ibv_cq *event_cq;
         void          *event_context;

            // Block on cq event completion

         nevents++;
         err = ibv_get_cq_event(ib_channel, &event_cq, &event_context);
         ERRCHECK( err, "Failed to get cq_event");
         ERRCHECK( event_cq != ib_recv_cq, "CQ event for unknown CQ %p", event_cq);

         pollwait( send_posts, 1 );  // Poll block on recv and clear send if needed

            // Every ibv_get_cq_event() must be acked, but you can do many at once
            //   running with --acks #   will set nacks to more than 1

         if( nevents == nacks ) {
            ibv_ack_cq_events(ib_recv_cq, nacks);   // Ack right away
            nevents = 0;
         }

      } else if( comp_type == POLLING ) {     // Do polling

         pollwait( send_posts, 1 );  // Poll block on recv and clear send if needed

      }

   } else {    // Spin until over-write of last byte for 1-sided rdma_put

      rtag = (rtag + 1) % 100;
      dbprintf("%d spinning on r_ptr[%d] (%p) = %d to equal %d\n", 
         myproc, buflen-1, &r_ptr[buflen-1], r_ptr[buflen-1], rtag);

      while( r_ptr[buflen-1] != rtag ) {
      //while( *v_ptr != rtag ) {
         sched_yield();
      }

   }

      // For UD with --memcpy, copy the message from the buffer to r_ptr

   //for( i=0; i<ud_bufsize; i++ ) fprintf(stderr,"%d %d\n", i, (int) ud_buf[i]);
   if( ud_memcpy ) {
      memcpy( r_ptr, ud_buf + 40, buflen);
   }
   dbprintf("%d Done Receiving %d bytes from %d\n", myproc, buflen, mypair);
}

   // Simple barrier sync for now.  Can try sync within a node then between nodes later.

void Sync( )
{
dbprintf("%d before MPI_Barrier()\n", myproc);
   MPI_Barrier(MPI_COMM_WORLD);
dbprintf("%d after  MPI_Barrier()\n", myproc);
}

   // Gather times or failures (nprocs doubles)

void Gather( double *buf)
{
   double time = buf[myproc];

dbprintf("%d before MPI_Gather()\n", myproc);
   MPI_Gather( &time, 1, MPI_DOUBLE, buf, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
dbprintf("%d after  MPI_Gather()\n", myproc);
}

   // Broadcast nrepeat to all nodes

void Broadcast(int *nrepeat)
{
dbprintf("%d before MPI_Bcast()\n", myproc);
   MPI_Bcast( nrepeat, 1, MPI_INT, 0, MPI_COMM_WORLD);

      // Limit nrepeats in stream and burst modes

   if( burst ) *nrepeat = MIN( *nrepeat, rx_depth );
   if( stream && myproc <  nprocs/2 ) *nrepeat = MIN( *nrepeat, tx_depth );
   if( stream && myproc >= nprocs/2 ) *nrepeat = MIN( *nrepeat, rx_depth );

dbprintf("%d after  MPI_Bcast()\n", myproc);
}

   // Close context with my pair

void Module_CleanUp( )
{
   MPI_Finalize();

   if( ud_mr ) err = ibv_dereg_mr(ud_mr);
   ERRCHECK( err, "Could not deregister memory");

   if( ib_context ) close_context( ib_context );
}


   // MallocBufs mallocs, inits, and aligns buffers.  This does memory registration.
   //    Send and recv buffers are either same or end-to-end so only 1 MR needed.

char * Module_malloc( uint64_t nbytes )
{
   void *buf;

   int err = posix_memalign( &buf, PAGESIZE, nbytes );
   ERRCHECK( err, "Could not malloc %d bytes", (int)nbytes);

   recv_mr = send_mr = ibv_reg_mr(ib_pd, buf, nbytes, allaccess);
   ERRCHECK( ! recv_mr, "Could't register MR for receive buffer");

      // Get remote.buf pointer and rkey from mypair for rdma_put

   if( comm_type == RDMA ) {
      local.rkey = recv_mr->rkey;
      local.buf  = recv_mr->addr;
      exchange_ib_settings();

      dsr_buf = remote.buf;   // Let MallocBufs in netpipe.c finish the setup
      dbprintf("%d Module_malloc( %d ) dsr_buf = %p  rkey = %u\n",
               myproc, (int)nbytes, dsr_buf, recv_mr->rkey);
   }

   dbprintf("%d Module_malloc( %d ) registered sr_buf = %p  lkey = %u\n",
             myproc, (int)nbytes, buf, recv_mr->lkey);

   return buf;
}

void Module_Reset( )
{
   if( send_posts + recv_posts > 0 ) {      // Clean up any remaining
      pollwait( send_posts, recv_posts );
   }
}

   // Deregister memory - send is already last half of receive buffer
   //    so don't do anything for it.

void Module_free( void * buf )
{
   if( buf == sr_buf ) {
      err = ibv_dereg_mr(send_mr);
      ERRCHECK( err, "Could not deregister memory");

      free( buf );
   }
}

void exchange_ib_settings( )
{
   MPI_Status status;

   MPI_Sendrecv( &local,  sizeof(local),  MPI_BYTE, mypair, 100,
                 &remote, sizeof(remote), MPI_BYTE, mypair, 100,
                 MPI_COMM_WORLD, &status);
}

void ready_connection( )
{
      // Modify my queue pair to the Ready To Receive state

   int i, attributes;

      // Set up the address handle

   struct ibv_ah_attr ah_attr;
   memset( &ah_attr, 0, sizeof(ah_attr) );
   if( roce ) {                            // Set up GRH
      ah_attr.is_global      = 1;
      ah_attr.grh.dgid       = remote.gid;
      ah_attr.grh.sgid_index = gidx;
      ah_attr.grh.hop_limit   = 1;
   } else {
      ah_attr.is_global      = 0;          // No GRH
   }
   ah_attr.dlid           = remote.lid;    // lid 0 for all RoCE
   ah_attr.sl             = ib_sl;
   ah_attr.port_num       = ib_port;

   struct ibv_qp_attr qp_attr;
   memset( &qp_attr, 0, sizeof(qp_attr) );
   qp_attr.qp_state           = IBV_QPS_RTR;

   if( unreliable ) {    // UD

      ud_address_handle = ibv_create_ah( ib_pd, &ah_attr );
      ERRCHECK( ! ud_address_handle, "Could not create UD address handle");

      attributes = IBV_QP_STATE;

   } else {              // RC

      attributes = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                   IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
      qp_attr.path_mtu           = ib_mtu;
      qp_attr.dest_qp_num        = remote.qpn;
      qp_attr.rq_psn             = remote.psn;
      qp_attr.max_dest_rd_atomic = 1;
      qp_attr.min_rnr_timer      = 12;   // ibv.c uses 5

      qp_attr.ah_attr = ah_attr;
   }
   err = ibv_modify_qp(ib_qp[0], &qp_attr, attributes);  // Only the first QP recvs
   ERRCHECK( err, "Failed to modify QP to RTR" );

      // Modify my queue pair to the Ready To Send state

   memset( &qp_attr, 0, sizeof(qp_attr) );
   qp_attr.qp_state       = IBV_QPS_RTS;
   qp_attr.sq_psn         = local.psn;

   if( unreliable ) {    // UD
      attributes = IBV_QP_STATE | IBV_QP_SQ_PSN;
   } else {              // RC
      attributes = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                   IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
      qp_attr.sq_psn         = local.psn;
      qp_attr.timeout        = 14;    // ibv.c uses 31
      qp_attr.retry_cnt      = 7;     // ibv.c uses 1
      qp_attr.rnr_retry      = 7;     // ibv.c uses 1
      qp_attr.max_rd_atomic  = 1;
   }
   for( i = 0; i < sendqps; i++ ) {  // UD - Stripe sends over multiple QP if requested
      err = ibv_modify_qp(ib_qp[i], &qp_attr, attributes);
      ERRCHECK( err, "Failed to modify QP %d to RTS", i);
   }
}

void init_context(struct ibv_device *ib_dev)
{
   int i;

      // Create the context

   ib_context = ibv_open_device(ib_dev);
   ERRCHECK( ! ib_context, "Couldn't get context for %s", ibv_get_device_name( ib_dev ) );

      // Create completion channel if using events

   if( comp_type == EVENTS ) {
      ib_channel = ibv_create_comp_channel(ib_context);
      ERRCHECK( ! ib_channel, "Could not allocate send/recv channel" );
   }

      // Allocate the protection domain

   ib_pd = ibv_alloc_pd(ib_context);
   ERRCHECK( ! ib_pd, "Couldn't allocate protection domain" );

      // Create send and receive completion queues as one

   ib_send_cq = ibv_create_cq(ib_context, tx_depth, NULL, ib_channel, 0);
   ERRCHECK( ! ib_send_cq, "Could not create send completion queue" );

   ib_recv_cq = ibv_create_cq(ib_context, rx_depth, NULL, ib_channel, 0);
   ERRCHECK( ! ib_recv_cq, "Could not create recv completion queue" );

      // Create my queue pair   (multiple if UD and striping across send QP)

   struct ibv_qp_init_attr qp_init_attr;
   memset( &qp_init_attr, 0, sizeof( qp_init_attr ) );
   qp_init_attr.send_cq = ib_send_cq;
   qp_init_attr.recv_cq = ib_recv_cq;
   qp_init_attr.srq     = NULL;
   qp_init_attr.sq_sig_all = 0;    // Manually set send signals elsewhere
   qp_init_attr.cap.max_send_sge = 10;
   qp_init_attr.cap.max_recv_sge = 10;

   qp_init_attr.qp_type = IBV_QPT_RC;
   if( unreliable ) qp_init_attr.qp_type = IBV_QPT_UD;   // Unreliable datagram

      // Determine the max number of outstanding work requests

   qp_init_attr.cap.max_send_wr = tx_depth;
   qp_init_attr.cap.max_recv_wr = rx_depth;
   do {
      ib_qp[0] = ibv_create_qp(ib_pd, &qp_init_attr);

      if( ib_qp[0] != NULL ) break;

      --qp_init_attr.cap.max_send_wr;
      --qp_init_attr.cap.max_recv_wr;
   } while( qp_init_attr.cap.max_send_wr >= 1 );
   if( ib_qp[0] ) ibv_destroy_qp( ib_qp[0] );
   mprintf("  max reqs %d  ", qp_init_attr.cap.max_send_wr);


      // Determine the max inline data size

   if (inline_data)
   {
      qp_init_attr.cap.max_inline_data = max_inline_data = 1 << 23;
      do {
         ib_qp[0] = ibv_create_qp(ib_pd, &qp_init_attr);

         if( ib_qp[0] != NULL ) break;

         if( max_inline_data > 0 ) max_inline_data >>= 1;
         qp_init_attr.cap.max_inline_data = max_inline_data;
      } while( max_inline_data >= 1 );
      if( ib_qp[0] ) ibv_destroy_qp( ib_qp[0] );
      mprintf("  inline %d  ", max_inline_data);
   } else {
      max_inline_data = 0;
   }

      // Now create 1 or more QP's

   qp_init_attr.cap.max_inline_data = max_inline_data;

   for( i = 0; i < sendqps; i++ ) {

      ib_qp[i] = ibv_create_qp(ib_pd, &qp_init_attr);

      ERRCHECK( ! ib_qp[i], "Could not create QP %d", i );
   }

   local.qpn = ib_qp[0]->qp_num;    // First QP receives

      // Modify the queue pair to put it into the INIT state

   int attributes;
   struct ibv_qp_attr qp_attr;
   memset( &qp_attr, 0, sizeof(qp_attr) );
   qp_attr.qp_state        = IBV_QPS_INIT;
   qp_attr.pkey_index      = 0;
   qp_attr.port_num        = ib_port;

   if( unreliable ) {    // UD
      //qp_attr.qkey = myproc;  // DDT - Not sure what this will be used for yet
      qp_attr.qkey = ib_qkey;
      attributes = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY;
   } else {              // RC & RDMA
      qp_attr.qp_access_flags = allaccess;
      attributes = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
   }

   for( i = 0; i < sendqps; i++ ) {
      err = ibv_modify_qp(ib_qp[i], &qp_attr, attributes);
      ERRCHECK( err , "Could not modify queue pair %d to INIT", i);
   }
}

void close_context( )
{
   int i;

   for( i = 0; i < sendqps; i++ ) {
      err = ibv_destroy_qp( ib_qp[i] );
      ERRCHECK( err, "Could not destroy QP %d", i);
   }

   err = ibv_destroy_cq( ib_send_cq );
   ERRCHECK( err, "Could not destroy send CQ");

   if( nevents > 0 ) {     // Clean up any remaining acks
      ibv_ack_cq_events(ib_recv_cq, nevents);
   }
   err = ibv_destroy_cq( ib_recv_cq );
   ERRCHECK( err, "Could not destroy recv CQ");

   err = ibv_dealloc_pd(ib_pd);
   ERRCHECK( err, "Could not deallocate PD");

   if( ib_channel ) {
      err = ibv_destroy_comp_channel(ib_channel);
      ERRCHECK( err, "Could not destroy completion channel");
   }

   err = ibv_close_device(ib_context);
   ERRCHECK( err, "Could not close device");
}


   // Post a receive for a Reliable Connection requiring 1 WR

void post_recv_RC( char *recv_buf, uint32_t msgsize, uint32_t lkey )
{
   static uint64_t recv_count = 1000;
   struct ibv_sge list;
   list.addr   = (uintptr_t) recv_buf;
   list.length = msgsize;
   list.lkey   = lkey;

   struct ibv_recv_wr wr, *bad_wr;
   wr.wr_id      = ++recv_count;
   wr.next       = NULL;
   wr.sg_list    = &list;
   wr.num_sge    = 1;

   err = ibv_post_recv(ib_qp[0], &wr, &bad_wr);
   recv_posts++;

   ERRCHECK( err, "Could not post receive - wr_id = %d", (int)bad_wr->wr_id);
}

   // Post a receive for an Unreliable Datagram which requires
   //    many WR each of MTU size maximum.  First 40 bytes is GRH.

void post_recv_UD( char *recv_buf, uint32_t msgsize, uint32_t lkey )
{
   static uint64_t recv_count = 0;
   int i, nleft = msgsize;
   int npackets = (msgsize + mtu - 1) / mtu;

   struct ibv_sge list;
   list.lkey   = lkey;

   struct ibv_recv_wr wr, *bad_wr;
   wr.sg_list    = &list;
   wr.num_sge    = 1;
   wr.next       = NULL;

   for( i = 0; i < npackets; i++ ) {
      list.addr   = (uintptr_t) &recv_buf[i*mtu];
      list.length = MIN( nleft, mtu);
      nleft      -= MIN( nleft, mtu);

      wr.wr_id      = ++recv_count;

      err = ibv_post_recv(ib_qp[0], &wr, &bad_wr);  // Always recv on first QP
      recv_posts++;

      ERRCHECK( err, "Could not post receive - wr_id = %d", (int)bad_wr->wr_id);
   }
}

   // 2-sided post_send will consume a matching receive request on the remote side
   //    RC sends one message
   //    UD limited to a single MTU - break the message into mtu sized packets

void post_send_RC( char *send_buf, uint32_t msgsize, uint32_t lkey )
{
   static uint64_t send_count = 0;
   struct ibv_sge list;
   list.addr   = (uintptr_t) send_buf;
   list.length = msgsize;
   list.lkey   = lkey;

   struct ibv_send_wr wr, *bad_wr;
   wr.wr_id      = ++send_count;
   wr.next       = NULL;
   wr.sg_list    = &list;
   wr.num_sge    = 1;
   wr.opcode     = IBV_WR_SEND;
   wr.send_flags = IBV_SEND_SIGNALED;

   if( msgsize <= max_inline_data ) wr.send_flags |= IBV_SEND_INLINE; // Inline data

   err = ibv_post_send(ib_qp[0], &wr, &bad_wr);
   send_posts++;

   ERRCHECK( err, "Could not post send - wr_id = %d", (int)bad_wr->wr_id);
}

   // For this try, use separate WR and send posts for each packet
   //    This is bad since each packet will have 40 Byte GRH
   //    or is it the same for linked WR list???

void post_send_UD( char *send_buf, uint32_t msgsize, uint32_t lkey )
{
   static uint64_t send_count = 0;
   int i, nleft = msgsize;
   int npackets = (msgsize + mtu - 1) / mtu;

   struct ibv_sge list;
   list.lkey   = lkey;

   struct ibv_send_wr wr, *bad_wr;
   wr.next       = NULL;
   wr.sg_list    = &list;
   wr.num_sge    = 1;
   wr.opcode     = IBV_WR_SEND;
   wr.send_flags = IBV_SEND_SIGNALED;
   wr.wr.ud.ah          = ud_address_handle;
   wr.wr.ud.remote_qpn  = remote.qpn;
   wr.wr.ud.remote_qkey = ib_qkey;

   for( i = 0; i < npackets; i++ ) {

      list.addr   = (uintptr_t) send_buf[i*mtu];
      list.length = MIN( nleft, mtu );
      nleft      -= MIN( nleft, mtu );

      wr.wr_id      = ++send_count;

// DDT - Change this to strip across 'sendqps' QP's eventually
      err = ibv_post_send(ib_qp[0], &wr, &bad_wr);
      send_posts++;
   }

   ERRCHECK( err, "Could not post send - wr_id = %d", (int)bad_wr->wr_id);
}

   // This tries to use a linked list of WR which I've never gotten to work

void post_send_UD2( char *send_buf, uint32_t msgsize, uint32_t lkey )
{
   static uint64_t send_count = 0;
   int i, nleft = msgsize;
   int npackets = (msgsize + mtu - 1) / mtu;

   struct ibv_sge *list;
   list = malloc( npackets * sizeof(struct ibv_sge) );

   struct ibv_send_wr *wr, *bad_wr;
   wr = malloc( npackets * sizeof(struct ibv_send_wr) );

   for( i = 0; i < npackets; i++ ) {
      list[i].addr   = (uintptr_t) &send_buf[i*mtu];
      list[i].length = MIN( nleft, mtu);       // UD
      nleft         -= MIN( nleft, mtu);
      list[i].lkey   = lkey;

      wr[i].wr_id      = ++send_count;
      if( i < npackets-1 ) wr[i].next = &wr[i+1];
      else                 wr[i].next = NULL;
      wr[i].sg_list    = &list[i];
      wr[i].num_sge    = 1;
      wr[i].opcode     = IBV_WR_SEND;
      wr[i].send_flags = 0;
      //wr[i].send_flags = IBV_SEND_SIGNALED;

      wr[i].wr.ud.ah          = ud_address_handle;
      wr[i].wr.ud.remote_qpn  = remote.qpn;
      wr[i].wr.ud.remote_qkey = ib_qkey;
   }

   if( msgsize <= max_inline_data ) wr[0].send_flags |= IBV_SEND_INLINE; // Inline data

   err = ibv_post_send(ib_qp[0], wr, &bad_wr);
   send_posts++;
   ERRCHECK( err, "Could not post send - wr_id = %d", (int)bad_wr->wr_id);
}

   // rdma_put will write data directly into the remote buffer

void rdma_put( char *send_buf, uint32_t msgsize, uint32_t lkey, 
               char *recv_buf, uint32_t rkey )
{
   static uint64_t rdma_count = 0;
   struct ibv_sge list = {
      .addr   = (uintptr_t) send_buf,
      .length = msgsize,
      .lkey   = lkey
   };
   struct ibv_send_wr wr = {
      .wr.rdma.remote_addr = (uintptr_t) recv_buf,
      .wr.rdma.rkey        = rkey,
      .wr_id      = ++rdma_count,
      .next       = NULL,
      .sg_list    = &list,
      .num_sge    = 1,
      .opcode     = IBV_WR_RDMA_WRITE,
      .send_flags = IBV_SEND_SIGNALED,
   };
   struct ibv_send_wr *bad_wr;

   dbprintf("%d rdma_put to %p with rkey = %u\n", myproc, recv_buf, rkey);
   err = ibv_post_send(ib_qp[0], &wr, &bad_wr);
   send_posts++;
   ERRCHECK( err, "Could not RDMA put - wr_id = %d", (int)bad_wr->wr_id);
}

   // RC - poll for a signaled receive and clear the unsignaled send
      // Using separate send and recv cq's does not affect the time
   // UD - poll for a series of signaled MTU sized recvs and unsignaled sends

static inline void pollwait( int s_posts, int r_posts )
{
   int ne = 0;
   struct ibv_wc *wc;
   wc = malloc( MAX( s_posts, r_posts) * sizeof( struct ibv_wc ) );

   dbprintf("%d pollwait for %d send posts & %d recv posts\n", myproc, s_posts, r_posts);

      // Clear off any send posts requested

   while( ne >= 0 && s_posts > 0 ) {   // ne < 0 means an error
      ne = ibv_poll_cq(ib_send_cq, s_posts, wc);
      if( ne > 0 ) {
         dbprintf("%d pollwait cleared %d of %d send posts %d\n",
            myproc, ne, s_posts, (int)wc[0].wr_id);
         s_posts -= ne;
         send_posts -= ne;
      }
   }

      // Now block on the message coming in then clear the CQ

   while( ne >= 0 && r_posts > 0 ) {   // ne < 0 means an error
      ne = ibv_poll_cq(ib_recv_cq, r_posts, wc);
      if( ne > 0 ) {
         dbprintf("%d pollwait found %d of %d recv posts %d\n",
            myproc, ne, r_posts, (int)wc[0].wr_id);
         r_posts -= ne;
         recv_posts -= ne;
      }
      //sched_yield();
   }

   dbprintf("pollwait wr_id = %d\n", (int)wc[0].wr_id);
   ERRCHECK( ne < 0, "poll CQ failed");
   ERRCHECK( wc[0].status != IBV_WC_SUCCESS, "%d Failed status %s (%d) for wr_id %d\n",
             myproc, ibv_wc_status_str(wc[0].status), wc[0].status, (int) wc[0].wr_id);
   free( wc );
}


