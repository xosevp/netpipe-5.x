////////////////////////////////////////////////////////////////////////////
// ibverbs.c module for NetPIPE
// Dave Turner - Kansas State University - October 2015 -
// contact DrDaveTurner@gmail.com
//
// NetPIPE is freely distributed under a GPL license.
// Some low level routines were copied from GPL licensed codes and on-line 
//    examples like rc_pingpong.c, rdma.c, rdmamojo dox
//
// The goal of this code is to measure the performance for passing messages
//    using 2-sided send/recv and 1-sided puts that are representative of what
//    would be seen in practical codes (no vendor cheating techniques).
//
// MPI is used to launch the executables, exchange handshake info, and
//    provide global bcast/gather/sync functions
//
// Usage: make ibverbs
//        mpirun -np 2 --hostfile mpihosts NPibverbs ...
//        mpirun NPibverbs --usage            for complete usage information
////////////////////////////////////////////////////////////////////////////
// TODO: Debug events - ENOMEM???
// TODO: UD option
// TODO: Compare to OpenMPI implementation
// TODO: try better sync - within a node then between nodes
// TODO: Test atomic_put latency, inline latency max_inline_data in ibv_create_qp()
// TODO: Do vendor cheating mode: pre-post all, memset buf to cache, inline small, 1-sided,
//          no work flow or ack
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
struct ibv_comp_channel *ib_send_channel, *ib_recv_channel;
struct ibv_port_attr     ib_port_attr;
struct ibv_pd           *ib_pd;
struct ibv_qp           *ib_qp;
struct ibv_cq           *ib_send_cq, *ib_recv_cq;
enum ibv_mtu             ib_mtu;
int                      ib_port;
int                      ib_sl = 0;
int                      sendflags = 0;

struct ibv_mr           *send_mr, *recv_mr;
int                      tx_depth = 1, rx_depth = 1;

      // IB connection information for local (me) and remote (mypair)
struct ib_info {
   uint32_t      lid, qpn, psn; // IB connection info
   union ibv_gid gid;           // Address info for RoCE connections
   char         *buff;          // remote buffer for rdma puts
   uint32_t      rkey;          // for remote buffer
   char          hostname[32];  // the hostname
} local, remote;

int allaccess = ( IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | 
                         IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC );

int mtu = 2048, use_events = 0, commtype = 2, pinned = 1, port;
int roce = 0, gidx = -1, tag = 0;
const char *device, *devname;

   // Module_Init() is called before any input options are read in

int Module_Init(int* pargc, char*** pargv)
{
      // Get proc info from MPI

   MPI_Init( pargc, pargv);
   MPI_Comm_rank(MPI_COMM_WORLD, &myproc);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

   device = strdup( " " );
   gethostname( local.hostname, 32);
   srand48(getpid() * time(NULL));
}

   // Process the module specific input arguments

int Module_ArgOpt( char *arg, char *opt )
{
   if(        !strcmp( arg, "mtu") ) {
      ERRCHECK( ! opt, "No MTU size was specified\n");
      mtu    = atoi( opt );         // 2048 is the default

   } else if( !strcmp( arg, "polling") ) {   // Polling is default
      use_events = 0;

   } else if( !strcmp( arg, "events") ) {
      use_events = 1;

   } else if( !strcmp( arg, "rdma") ) {     // 2-sided is default
      commtype = 1;
      mprintf("Doing 1-sided RDMA write, recv node waits on over-write of last byte\n");

   } else if( !strcmp( arg, "pinned") ) {   // pinned is default
      pinned = 1;

   } else if( !strcmp( arg, "unpinned") ) {
      pinned = 0;

   } else if( !strcmp( arg, "device") ) {
      ERRCHECK( ! opt, "No device name was specified\n");
      device = strdup( opt );

   } else if( !strcmp( arg, "port") ) {
      ERRCHECK( ! opt, "No port number was specified\n");
      port = atoi( opt );

   } else {
      PrintUsage();
   }
}

int Module_PrintUsage()
{
   mprintf("\n  NPibverbs specific input arguments\n");
   mprintf("  --mtu #        mtu size of 256, 512, 1024, 2048, 4096 (default 2048)\n");
   mprintf("  --polling      Poll for receive completion (default)\n");
   mprintf("  --events       Use event notification for receive completion\n");
   mprintf("  --rdma         use 1-sided RDMA puts (default 2-sided)\n");
   mprintf("  --pinned       (default)  or  --unpinned\n");
   mprintf("  --device name  (default is the first device found)\n");
   mprintf("  --port #       (default is the first active port on the device)\n");
}

// Module_Setup() is called after input options are read in

int Module_Setup( )
{
   int i, j, nports, max_mr_size;
   struct ibv_device      **dev_list;
   struct ibv_context      *ctx;
   struct ibv_device_attr   device_attr;

   //sendflags = IBV_SEND_FENCE | IBV_SEND_SIGNALED;
   //sendflags = IBV_SEND_FENCE;
   sendflags = 0;

   if( commtype == 2 ) async = 1;

   switch (mtu) {
      case 256:  ib_mtu = IBV_MTU_256;  break;
      case 512:  ib_mtu = IBV_MTU_512;  break;
      case 1024: ib_mtu = IBV_MTU_1024; break;
      case 2048: ib_mtu = IBV_MTU_2048; break;
      case 4096: ib_mtu = IBV_MTU_4096; break;
      default:   ERRCHECK( 1, "Invalide MTU of %d\n", mtu);
   }

      // Get the list of IB devices on each host
   
   dev_list = ibv_get_device_list(NULL);
   ERRCHECK( ! dev_list, "Failed to get InfiniBand device list\n");

      // Print the list and specs out to the screen for each device and port
      //   Choose the requested device and port or the first active non-RoCE for now

   mprintf("\n     List of InfiniBand ports (choose requested dev and port or first active)\n");

   for (i = 0; dev_list[i]; ++i) {

      devname = ibv_get_device_name(dev_list[i]);

      ctx = ibv_open_device( dev_list[i] );
      ERRCHECK( ! ctx, "Couldn't get context for %s\n", devname);

      err = ibv_query_device( ctx, &device_attr);
      ERRCHECK( err, "Couldn't query device to get attributes\n");

      nports = device_attr.phys_port_cnt;
      max_mr_size = device_attr.max_mr_size;

      for( j = 1; j <= nports; j++ ) {

         err = ibv_query_port(ctx, j, &ib_port_attr);
         ERRCHECK( err, "Couldn't get port info for probe context\n");

         mprintf(" device %10s port %d  ", devname, j);
         if( ib_port_attr.link_layer == IBV_LINK_LAYER_INFINIBAND ) mprintf(" InfiniBand      ");
         if( ib_port_attr.link_layer == IBV_LINK_LAYER_ETHERNET )   mprintf(" Ethernet (RoCE) ");

            // Check if it is the dev and port requested or the first active

         if( ib_port_attr.state != IBV_PORT_ACTIVE ) {
            mprintf(" - connection not active\n");

         } else if( (!strcmp( device, devname) && port == j ) ||  // My device and port
                    ( port == 0 && ib_port == 0 ) ) {             // First active
            
            ib_port = j;
            init_context( dev_list[i] );  // Initialize the real context now
            if( ib_port_attr.link_layer != IBV_LINK_LAYER_INFINIBAND ) {
               roce = 1;
               if( gidx < 0 ) gidx = 0;
            }
            mprintf(" - using this connection\n");

         } else mprintf("\n");
      }
      ibv_close_device( ctx );
   }

   ERRCHECK( ib_port == 0, "No active ports found\n");

      // Get the port info for send and receive contexts

   err = ibv_query_port(ib_context, ib_port, &ib_port_attr);
   ERRCHECK( err, "Couldn't get port info for context\n");
   local.lid = ib_port_attr.lid;
   local.psn = lrand48() & 0xffffff;

      // Assume same subnet for now.

   memset(&local.gid, 0, sizeof(local.gid));
   if( roce ) {    // DDT - eventually cycle thru gidx to find one that works???
      err = ibv_query_gid( ib_context, ib_port, gidx, &local.gid);
      ERRCHECK( err, "Cannot read the gid of index %d\n", gidx);
   }

   exchange_ib_settings( );       // Get remote IB information

   mprintf("\nLocal:  %s  LID %#04x  QPN %#06x  PSN %#06x  GID %lu %lu\n",
            local.hostname, local.lid, local.qpn, local.psn, 
            local.gid.global.subnet_prefix, local.gid.global.interface_id);
   mprintf("Remote: %s  LID %#04x  QPN %#06x  PSN %#06x  GID %lu %lu\n",
            remote.hostname, remote.lid, remote.qpn, remote.psn, 
            remote.gid.global.subnet_prefix, remote.gid.global.interface_id);

   ready_connection( );      // Connect to my remote pair

   ibv_free_device_list( dev_list );

   Sync( );
}


   // Send the data to my pair using 2-sided send/recv or 1-sided rdma puts

struct ibv_wc wc;

int SendData( )
{
dbprintf("%d Sending %p  %d bytes to %d\n", myproc, s_ptr, bufflen, mypair);
   // DDT - How to insure last send is completed???
   //ERRCHECK( wc.wr_id != 0, "Previous send is still pending\n");

   if( commtype == 2 ) {

      post_send( s_ptr, bufflen, send_mr->lkey );

   } else {

         // The first and last bytes of s_ptr are set to tag already

      rdma_put( s_ptr, bufflen, send_mr->lkey, r_ptr, remote.rkey );
   }
dbprintf("%d Done Sending %d bytes to %d\n", myproc, bufflen, mypair);
}


   // 2-sided must pre-post recvs to allow direct write into buffer

int PrepostRecv( )
{
   if( commtype == 1 ) return;  // Nothing to do for 1-sided rdma_puts

dbprintf("%d pre-posting %p  %d bytes from %d\n", myproc, r_ptr, bufflen, mypair);
   post_recv( r_ptr, bufflen, recv_mr->lkey );

   if( use_events ) {   // Request notification of cq event

      err = ibv_req_notify_cq( ib_recv_cq, 0);    // 0 means unsolicited
      ERRCHECK( err, "Couldn't request CQ notification in PrepostRecv()\n");
   }
dbprintf("%d Done pre-posting %d bytes from %d\n", myproc, bufflen, mypair);
}

   // 2-sided always pre-posts using PrepareToRecv()
   // Block on receive of all data from my pair
   // 1-sided will spin on over-write of last in buffer

int RecvData( )
{
dbprintf("%d Receiving %d bytes from %d\n", myproc, bufflen, mypair);
   if( commtype == 2 ) {

      if( use_events ) {   // Use events

         struct ibv_cq *event_cq;
         void          *event_context;

            // Block on cq event completion

         err = ibv_get_cq_event(ib_recv_channel, &event_cq, &event_context);
         ERRCHECK( err, "Failed to get cq_event\n");
         ERRCHECK( event_cq != ib_recv_cq, "CQ event for unknown CQ %p\n", event_cq);

         ibv_ack_cq_events(ib_recv_cq, 1);   // Ack right away

// DDT - Why is poll needed to clear recv message off queue?  Otherwise ENOMEM
         pollwait( ib_recv_cq );    // Block until polling is successful
      } else {           // Do polling

         pollwait( ib_recv_cq );    // Block until polling is successful
         //ibv_ack_cq_events(ib_recv_cq, 1);   // Ack right away

      }

   } else {    // Spin until over-write of last byte for 1-sided rdma_put

      tag = ++tag % 100;
      while( r_ptr[bufflen-1] != tag ) {
         sched_yield();
      }
   }
dbprintf("%d Done Receiving %d bytes from %d\n", myproc, bufflen, mypair);
}

   // Simple barrier sync for now.  Can try sync within a node then between nodes later.

int Sync( )
{
dbprintf("%d before MPI_Barrier()\n", myproc);
    MPI_Barrier(MPI_COMM_WORLD);
dbprintf("%d after  MPI_Barrier()\n", myproc);
}

   // Gather times or failures (nprocs doubles)

int Gather( double *buf)
{
   double time = buf[myproc];

dbprintf("%d before MPI_Gather()\n", myproc);
   MPI_Gather( &time, 1, MPI_DOUBLE, buf, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
dbprintf("%d after  MPI_Gather()\n", myproc);
}

   // Broadcast nrepeat to all nodes

int Broadcast(int *nrepeat)
{
dbprintf("%d before MPI_Bcast()\n", myproc);
   MPI_Bcast( nrepeat, 1, MPI_INT, 0, MPI_COMM_WORLD);
dbprintf("%d after  MPI_Bcast()\n", myproc);
}

   // Close context with my pair

int Module_CleanUp( )
{
   MPI_Finalize();

   close_context( ib_context );

   if( logfile ) fclose( logfile );
}


   // MallocBuffs mallocs, inits, and aligns buffers.  This does memory registration.
   //    Send and recv buffers are either same or end-to-end so only 1 MR needed.

int Module_MallocBuffs( int nbytes )
{
   recv_mr = send_mr = ibv_reg_mr(ib_pd, r_buff_orig, nbytes, allaccess);
   ERRCHECK( ! recv_mr, "Could't register MR for receive buffer\n");
dbprintf("%d MR r_buff_orig = %p  %d bytes  lkey = %u\n",
               myproc, r_buff_orig, bufflen, recv_mr->lkey);

      // Exchange remote.buff pointer and rkey with mypair for 1-sided

   if( commtype == 1 ) {
      local.buff = r_buff_orig;
      exchange_ib_settings();
   }
}

   // Deregister memory without checking for error in return

int Module_FreeBuffs( )
{
   err = ibv_dereg_mr(recv_mr);
}

int exchange_ib_settings( )
{
   MPI_Status status;

   MPI_Send( &local,  sizeof(local),  MPI_BYTE, mypair, 100, MPI_COMM_WORLD);
   MPI_Recv( &remote, sizeof(remote), MPI_BYTE, mypair, 100, MPI_COMM_WORLD, &status);
}

int ready_connection( )
{
   struct ibv_qp_attr qp_attr;
   memset( &qp_attr, 0, sizeof(qp_attr) );
   qp_attr.qp_state           = IBV_QPS_RTR;
   qp_attr.path_mtu           = ib_mtu;
   qp_attr.dest_qp_num        = remote.qpn;
   qp_attr.rq_psn             = remote.psn;
   qp_attr.max_dest_rd_atomic = 1;
   qp_attr.min_rnr_timer      = 12;
   qp_attr.ah_attr.is_global       = 0;
   qp_attr.ah_attr.dlid            = remote.lid;
   qp_attr.ah_attr.sl              = ib_sl;
   qp_attr.ah_attr.src_path_bits   = 0;
   qp_attr.ah_attr.port_num        = ib_port;

      // Set GRH info for RoCE connections

   if( remote.gid.global.interface_id ) {
      qp_attr.ah_attr.is_global = 1;
      qp_attr.ah_attr.grh.hop_limit = 1;
      qp_attr.ah_attr.grh.dgid = remote.gid;     // gid for my remote pair
      qp_attr.ah_attr.grh.sgid_index = gidx;     // Set gidx gid index
   }

   err = ibv_modify_qp(ib_qp, &qp_attr,
           IBV_QP_STATE              |
           IBV_QP_AV                 |
           IBV_QP_PATH_MTU           |
           IBV_QP_DEST_QPN           |
           IBV_QP_RQ_PSN             |
           IBV_QP_MAX_DEST_RD_ATOMIC |
           IBV_QP_MIN_RNR_TIMER );
   ERRCHECK( err == EINVAL, "EINVAL for ibv_modify_qp() - errno = %d\n", err);
   ERRCHECK( err, "Failed to modify QP to RTR - errno = %d\n", err);

   memset( &qp_attr, 0, sizeof(qp_attr) );
   qp_attr.qp_state       = IBV_QPS_RTS;
   qp_attr.sq_psn         = local.psn;
   qp_attr.timeout        = 14;
   qp_attr.retry_cnt      = 7;
   qp_attr.rnr_retry      = 7;
   qp_attr.max_rd_atomic  = 1;
   err = ibv_modify_qp(ib_qp, &qp_attr,
           IBV_QP_STATE              |
           IBV_QP_TIMEOUT            |
           IBV_QP_RETRY_CNT          |
           IBV_QP_RNR_RETRY          |
           IBV_QP_SQ_PSN             |
           IBV_QP_MAX_QP_RD_ATOMIC);
   ERRCHECK( err, "Failed to modify QP to RTS - errno = %d\n", err);
}

int init_context(struct ibv_device *ib_dev)
{
   ib_context = ibv_open_device(ib_dev);
   ERRCHECK( ! ib_context, "Couldn't get context for %s\n", ibv_get_device_name( ib_dev ) );

   if( use_events ) {
      ib_send_channel = ibv_create_comp_channel(ib_context);
      ERRCHECK( ! ib_send_channel, "Couldn't allocate send channel\n" );
      ib_recv_channel = ibv_create_comp_channel(ib_context);
      ERRCHECK( ! ib_recv_channel, "Couldn't allocate receive channel\n" );
   } else {
      ib_send_channel = ib_recv_channel = NULL;
   }

   ib_pd = ibv_alloc_pd(ib_context);
   ERRCHECK( ! ib_pd, "Couldn't allocate protection domain\n" );

   ib_send_cq = ibv_create_cq(ib_context, tx_depth, NULL, ib_send_channel, 0);
   ERRCHECK( ! ib_send_cq, "Couldn't create completion queue\n" );

   ib_recv_cq = ibv_create_cq(ib_context, rx_depth, NULL, ib_recv_channel, 0);
   ERRCHECK( ! ib_recv_cq, "Couldn't create completion queue\n" );

   struct ibv_qp_init_attr qp_init_attr;
   memset( &qp_init_attr, 0, sizeof( qp_init_attr ) );
   qp_init_attr.send_cq = ib_send_cq;
   qp_init_attr.recv_cq = ib_recv_cq;
   qp_init_attr.srq     = NULL;
   qp_init_attr.sq_sig_all = 0;    // Manually set send signals elsewhere
   qp_init_attr.cap.max_send_wr  = tx_depth;
   qp_init_attr.cap.max_recv_wr  = rx_depth;
   qp_init_attr.cap.max_send_sge = 1;
   qp_init_attr.cap.max_recv_sge = 1;
   qp_init_attr.qp_type = IBV_QPT_RC;

   ib_qp = ibv_create_qp(ib_pd, &qp_init_attr);
   ERRCHECK( ! ib_qp, "Couldn't create queue pair\n" );
   local.qpn = ib_qp->qp_num;

   struct ibv_qp_attr qp_attr;
   memset( &qp_attr, 0, sizeof(qp_attr) );
   qp_attr.qp_state    = IBV_QPS_INIT;
   qp_attr.pkey_index      = 0;
   qp_attr.port_num        = ib_port;
   qp_attr.qp_access_flags = allaccess;

   err = ibv_modify_qp(ib_qp, &qp_attr,
           IBV_QP_STATE              |
           IBV_QP_PKEY_INDEX         |
           IBV_QP_PORT               |
           IBV_QP_ACCESS_FLAGS);
   ERRCHECK( err , "Couldn't modify queue pair to INIT\n" );
}

int close_context( )
{
   err = ibv_destroy_qp( ib_qp );
   ERRCHECK( err, "Could not destroy QP\n");

   err = ibv_destroy_cq( ib_send_cq ) || ibv_destroy_cq( ib_recv_cq );
   ERRCHECK( err, "Could not destroy CQ's\n");

   err = ibv_dealloc_pd(ib_pd);
   ERRCHECK( err, "Could not deallocate PD\n");

   if( ib_send_channel ) {
      err = ibv_destroy_comp_channel(ib_send_channel) || 
            ibv_destroy_comp_channel(ib_recv_channel);
      ERRCHECK( err, "Could not destroy completion channels\n");
   }

   err = ibv_close_device(ib_context);
   ERRCHECK( err, "Could not close device\n");
}

int post_recv( void *recv_buf, uint32_t msgsize, uint32_t lkey )
{
   static uint64_t recv_count = 0;
   struct ibv_sge list = {
      .addr   = (uintptr_t) recv_buf,
      .length = msgsize,
      .lkey   = lkey
   };
   struct ibv_recv_wr wr = {
      .wr_id      = ++recv_count,
      .next       = NULL,
      .sg_list    = &list,
      .num_sge    = 1
   };
   struct ibv_recv_wr *bad_wr;

   err = ibv_post_recv(ib_qp, &wr, &bad_wr);
   ERRCHECK( err == ENOMEM, "ENOMEM wr_id = %lu\n", bad_wr->wr_id);
   ERRCHECK( err == EFAULT, "EFAULT wr_id = %lu\n", bad_wr->wr_id);
   ERRCHECK( err == EINVAL, "EINVAL wr_id = %lu\n", bad_wr->wr_id);
   ERRCHECK( err, "Couldn't post receive - %d\n", err);
}

   // 2-sided post_send will consume a matching receive request on the remote side

int post_send( void *send_buf, uint32_t msgsize, uint32_t lkey )
{
   static uint64_t send_count = 0;
   struct ibv_sge list = {
      .addr   = (uintptr_t) send_buf,
      .length = msgsize,
      .lkey   = lkey
   };
   struct ibv_send_wr wr = {
      .wr_id      = ++send_count,
      .next       = NULL,
      .sg_list    = &list,
      .num_sge    = 1,
      .opcode     = IBV_WR_SEND,
      .send_flags = sendflags,
   };
   struct ibv_send_wr *bad_wr;

   err = ibv_post_send(ib_qp, &wr, &bad_wr);
   ERRCHECK( err == ENOMEM, "ENOMEM wr_id = %lu\n", bad_wr->wr_id);
   ERRCHECK( err, "Couldn't post send - %d\n", err);
// DDT - Why is poll needed to clear msg off send queue???
   pollwait( ib_send_cq );
}

   // 1-sided rdma_put will write data directly into the remote buffer

int rdma_put( char *send_buf, uint32_t msgsize, uint32_t lkey, 
              char *recv_buf, uint32_t rkey )
{
   static uint64_t rdma_count = 0;
   struct ibv_sge list = {
      .addr   = (uintptr_t) send_buf,
      .length = msgsize,
      .lkey   = lkey
   };
   struct ibv_send_wr wr = {
      .wr.rdma.remote_addr = (uintptr_t) remote.buff + (recv_buf - r_buff_orig),
      .wr.rdma.rkey        = rkey,
      .wr_id      = ++rdma_count,
      .next       = NULL,
      .sg_list    = &list,
      .num_sge    = 1,
      .opcode     = IBV_WR_RDMA_WRITE,
      .send_flags = sendflags,
   };
   struct ibv_send_wr *bad_wr;

   err = ibv_post_send(ib_qp, &wr, &bad_wr);
   ERRCHECK( err == ENOMEM, "ENOMEM wr_id = %lu\n", bad_wr->wr_id);
// DDT - fails ENOMEM after 32
   ERRCHECK( err, "Could not RDMA put - %d\n", err);
// DDT - fails right away with this pollwait enabled
   //pollwait( ib_send_cq );
}

// Atomically increment a uint64_t on a remote node

/*
      // Atomic operations
volatile uint64_t       *atomic_buf, *work_buf, *rem_atomic_buf;
uint32_t                 rem_atomic_rkey; // will need rkey[nprocs] eventually
struct ibv_mr           *atomic_mr, *work_mr;
   
int atomic_put( uint64_t inc )
{
   static uint64_t atomic_count = 0;
   struct ibv_sge list = {   // Data atomically read from remote is put here
      .addr   = (uintptr_t) work_buf,
      .length = sizeof(uint64_t),
      .lkey   = work_mr->lkey
   };
   struct ibv_send_wr wr = {
      .wr_id      = ++atomic_count,
      .sg_list    = &list,
      .num_sge    = 1,
      .opcode     = IBV_WR_ATOMIC_FETCH_AND_ADD,
      .send_flags = sendflags,
      .wr.atomic.remote_addr = (uintptr_t) rem_atomic_buf,
      .wr.atomic.compare_add = inc,
      .wr.atomic.swap        = 0,
      .wr.atomic.rkey        = rem_atomic_rkey
   };
   struct ibv_send_wr *bad_wr;

   err = ibv_post_send(ib_qp, &wr, &bad_wr);
   ERRCHECK( err, "Couldn't post atomic put - %d\n", err);
   //pollwait( ib_send_cq );
}
      // Create and register atomic and work buffers for Sync(), Broadcast(), and Gather()

   atomic_buf = malloc( sizeof(uint64_t) );
   atomic_mr  = ibv_reg_mr(ib_pd, (void *) atomic_buf, sizeof(uint64_t), allaccess);
   ERRCHECK( ! atomic_mr, "Could not register MR for atomic buffer\n");
   *atomic_buf = 0;  // Always keep the atomic buffers at 0 when not using them

   work_buf = malloc( sizeof(uint64_t) );
   work_mr  = ibv_reg_mr(ib_pd, (void *) work_buf, sizeof(uint64_t), allaccess);
   ERRCHECK( ! work_mr, "Could not register MR for work buffer\n");
   *work_buf = -2;  // DDT - just to help with debugging for now
*/

int pollwait( struct ibv_cq *cq )     // Poll for receive completion only
{
   int sleep_count = 0, ne = 0;
   struct ibv_wc wc;

   while ( ne == 0 ) {
      ne = ibv_poll_cq(cq, 1, &wc);  // Poll for 1 completion
/*    if( sleep_count == 0 ) dbprintf("pollwait spinning ");
      else dbprintf(" ."); 
      ERRCHECK( ++sleep_count == 60, "pollwait exceeded 60 seconds\n");
      sleep(1);*/
   }
   //if( sleep_count > 0 ) dbprintf("n");

   if( cq == ib_send_cq ) dbprintf("send pollwait wr_id = %lu\n", wc.wr_id);
   else                   dbprintf("recv pollwait wr_id = %lu\n", wc.wr_id);
   ERRCHECK( ne < 0, "poll CQ failed\n");
   ERRCHECK( wc.status != IBV_WC_SUCCESS, "Failed status %s (%d) for wr_id %d\n",
             ibv_wc_status_str(wc.status), wc.status, (int) wc.wr_id);
}




