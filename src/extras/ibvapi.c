
// The Mellanox VAPI is deprecated

/*****************************************************************************/
/* "NetPIPE" -- Network Protocol Independent Performance Evaluator.          */
/* Copyright 1997, 1998 Iowa State University Research Foundation, Inc.      */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation.  You should have received a copy of the     */
/* GNU General Public License along with this program; if not, write to the  */
/* Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   */
/*                                                                           */
/*       ib.c              ---- InfiniBand module for the Mellanox VAPI      */
/*                              Parts of this code were taken from           */
/*                              Mellanox example codes.                      */
/*                                                                           */
/*      This module was programmed by Adam Oline and Dave Turner             */
/*                                                                           */
/*****************************************************************************/
#include    "netpipe.h"
/*#include    <getopt.h>*/

/* Macros -- do { ... } while(0) enclosure is used to give
 * each macro the same semantics as a real C function; the compiler will
 * complain if there's no semicolon afterwards, an if statement will
 * execute all lines of the macro, rather than just the first line, etc */

FILE* logfile;

#undef DUMPTOLOG
#ifdef DUMPTOLOG
#define LOGPRINTF(_format, _aa...) do { \
   fprintf(logfile, "%s: " _format, __FUNCTION__, ##_aa); \
   fflush(logfile); \
} while(0)
#else
#define LOGPRINTF(_format, _aa...)
#endif

/* Macros to check return values from VAPI functions */

#define CHECK_RET(a, s) do {                  \
   if(a != VAPI_OK && a != VAPI_CQ_EMPTY) {   \
      fprintf(stderr, s": %s\n",              \
              VAPI_strerror(ret));            \
      exit(-1);                               \
   }                                          \
} while(0)

#define CHECK_WC(a, s) do {                    \
   if(a.status != VAPI_SUCCESS) {              \
      fprintf(stderr, s": %s\n",               \
              VAPI_wc_status_sym(a.status));   \
      exit(-1);                                \
   }                                           \
} while(0)

/* Global vars */

static VAPI_hca_hndl_t     hca_hndl=VAPI_INVAL_HNDL;
static VAPI_hca_port_t     hca_port;
static int                 port_num;
static VAPI_cqe_num_t      num_cqe;
static VAPI_cqe_num_t      act_num_cqe;
static EVAPI_compl_handler_hndl_t ceh_hndl=VAPI_INVAL_HNDL;
static VAPI_mrw_t          mr_in;
static VAPI_mrw_t          s_mr_out;
static VAPI_mrw_t          r_mr_out;

     /* mr_hndl is used in MyMalloc/MyFree, which only comms with my dest pair */

static VAPI_mr_hndl_t      s_mr_hndl=VAPI_INVAL_HNDL;
static VAPI_mr_hndl_t      r_mr_hndl=VAPI_INVAL_HNDL;

static VAPI_qp_init_attr_t qp_init_attr;
static VAPI_qp_prop_t      qp_prop;
static VAPI_qp_attr_mask_t qp_attr_mask;
static VAPI_qp_attr_t      qp_attr;
static VAPI_qp_cap_t       qp_cap;
static VAPI_wc_desc_t      wc;
static int                 max_wq=50000;
static void*               remote_address;
static VAPI_rkey_t         remote_key;
static volatile int        receive_complete=0;
VAPI_ret_t                 ret;       /* Return code */

      /* Sync-over-IB specific vars */

static VAPI_mr_hndl_t      sync_mr_hndl;
static char*               sync_buf;
static VAPI_mrw_t          sync_mr_out;

/* Local structures */

typedef struct {
   void* buf;
   int nbytes;
   VAPI_mr_hndl_t mr_hndl;
} NP_ib_buf_hndl_t;

/* Local prototypes */

void event_handler(VAPI_hca_hndl_t, VAPI_cq_hndl_t, void*);
void writeFully(ArgStruct *p, int proc, void *buf, int nbytes);
void readFully( ArgStruct *p, int proc, void *buf, int nbytes);
void file_sync( ArgStruct *p, int proc );
void sync_over_ib(ArgStruct *p, int proc);

/* Function definitions */

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
      /* Set defaults */

   p->prot.ib_mtu = MTU1024;             /* 1024 Byte MTU                    */
   p->prot.commtype = NP_COMM_SENDRECV;  /* Use Send/Receive communications  */
   p->prot.comptype = NP_COMP_LOCALPOLL; /* Use local polling for completion */
}

void Setup(ArgStruct *p)
{
   int i, j, qp1, qp2, lc, mylc;
   FILE *fd, *pd;
   char logfilename[80], filename[80], command[80];
   char *myhostname, *remotehost, *dot;

      /* Sanity check */

   if( p->prot.commtype == NP_COMM_RDMAWRITE && 
       p->prot.comptype != NP_COMP_LOCALPOLL ) {
      fprintf(stderr, "Error, RDMA Write may only be used with local polling.\n");
      fprintf(stderr, "Try using RDMA Write With Immediate Data with vapi polling\n");
      fprintf(stderr, "or event completion\n");
      exit(-1);
   }
 
   myhostname = (char *) malloc( 100 );
   gethostname( myhostname, 100);
   if( (dot=strstr( myhostname, "." )) != NULL ) *dot = '\0';


      /* allocate space for the list of hosts */

   p->host = (char **) malloc( p->nprocs * sizeof(char *) );
   for( i=0; i<p->nprocs; i++ ) 
      p->host[i] = (char *) malloc( 100 * sizeof(char) );


   if( p->hostfile == NULL ) {             /* No hostfile */

      fprintf(stderr, "You must use <-H hostfile> to specify a hostfile\n");
      exit(0);

   } else {                                /* Read the hostfile */

      if( (fd = fopen( p->hostfile, "r")) == NULL ) {
         fprintf(stderr, "%d Could not open the hostfile %s, (errno=%d)\n",
                 p->myproc, p->hostfile, errno );
         exit(0);
      }

      for( i=0; i<p->nprocs; i++ ) {

         fscanf( fd, "%s", p->host[i] );

         if( p->myproc < 0 ) {      /* myproc not set yet, hope this is not SMP */

            if( (dot=strstr( p->host[i], "." )) != NULL ) *dot = '\0';

               /* Set myproc if this is my host and it was not set by nplaunch */

            if( ! strcmp( myhostname, p->host[i] ) ) p->myproc = i;
         }
      }
      fclose( fd );
   }

   if( p->myproc < 0 || p-> myproc >= p->nprocs ) {
      fprintf(stderr, "NetPIPE: myproc=%d was not set properly\n", p->myproc);
      exit(0);
   }

   if( p->myproc == 0 ) p->master = 1;

      /* Open the log files if needed */

#ifdef DUMPTOLOG
   sprintf(logfilename, ".iblog%d", p->myproc);
   logfile = fopen(logfilename, "w");
#endif

      /* Set the first half to be transmitters */

   p->tr = p->rcv = 0;
   if( p->myproc < p->nprocs/2 ) p->tr = 1;
   else p->rcv = 1;

   p->dest = p->source = p->nprocs - 1 - p->myproc;


      /* Initialize the local Mellanox InfiniBand card. */

   p->prot.lid = (IB_lid_t *) malloc( p->nprocs * sizeof( IB_lid_t ) );

   init_hca( p );

      /* malloc space for all the queue handles */

   p->prot.s_cq_hndl = (VAPI_cq_hndl_t *) malloc( p->nprocs * sizeof(VAPI_cq_hndl_t) );
   p->prot.r_cq_hndl = (VAPI_cq_hndl_t *) malloc( p->nprocs * sizeof(VAPI_cq_hndl_t) );
   p->prot.qp_hndl   = (VAPI_qp_hndl_t *) malloc( p->nprocs * sizeof(VAPI_qp_hndl_t) );

      /* Allocate Protection Domain */

   ret = VAPI_alloc_pd(hca_hndl, &p->prot.pd_hndl);
   CHECK_RET(ret, "Error allocating PD");
   LOGPRINTF("Allocated Protection Domain\n");

      /* Initialize my qp_nums for communications with proc 0 and my pair */

   p->prot.qp_num      = (VAPI_qp_num_t *) malloc( p->nprocs * sizeof( VAPI_qp_num_t ) );
   p->prot.dest_qp_num = (VAPI_qp_num_t *) malloc( p->nprocs * sizeof( VAPI_qp_num_t ) );

   if( p->master ) {

      for( i=1; i<p->nprocs; i++ ) init_queue_pair( p, i);

   } else {

      init_queue_pair( p, 0 );

      if( p->dest != 0 ) init_queue_pair( p, p->dest );
   }

      /* Initialize memory regions and handles for synchronization over Infiniband.
       * We'll use two bytes for each proc, so we can flip-flop between the byte
       * we are spinning on. This provides a simple way to avoid deadlock if one
       * proc should try to sync twice in the time it takes the other proc to sync
       * once.
       */

   p->prot.r_key  = (VAPI_rkey_t *) malloc( p->nprocs * sizeof(VAPI_rkey_t) );
   p->prot.r_addr =       (void **) malloc( p->nprocs * sizeof(void*) );
   sync_buf = (char*) malloc( p->nprocs * 2 * sizeof(char) );
   memset(sync_buf, 0, p->nprocs * 2 * sizeof(char));
   memset(sync_buf + p->myproc *2, 1, 2 * sizeof(char) );

   mr_in.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
   mr_in.l_key = 0;
   mr_in.pd_hndl = p->prot.pd_hndl;
   mr_in.r_key = 0;
   mr_in.size = p->nprocs * 2 * sizeof(char);
   mr_in.start = (VAPI_virt_addr_t)(MT_virt_addr_t)sync_buf;
   mr_in.type = VAPI_MR;

   ret = VAPI_register_mr(hca_hndl, &mr_in, &sync_mr_hndl, &sync_mr_out);
   CHECK_RET(ret, "Error registering sync buffer");
   LOGPRINTF("Registered Sync Buffer\n");

      /* Now exchange the HCA lids and qp_nums using a file .npX.
       * Proc 0 will dump its lid then qp_nums for each proc, 1 per line.
       * Each other proc will dump lid, qp_num]0], and qp_num[dest] in 1 line.
       */

   if( p->master ) {

      system( "rm -f .np*");  /* Clean up old files. nplaunch also does this */

         /* Open .np0 and write my comm info */

      fd = fopen( ".np0.lock", "w" );

      fprintf(fd, "%hu\n", p->prot.lid[p->myproc]);  /* my HCA ID */
      
         /* my remote key and buffer address for RDMA access */
      
      fprintf(fd, "%u %p\n", sync_mr_out.r_key, sync_buf); 

      for( i=1; i<p->nprocs; i++ ) {

         fprintf(fd, "%u\n", p->prot.qp_num[i]);  /* qp_nums for comm with 0 */

      }

      fclose( fd );

      system("mv .np0.lock .np0");   /* Give read access to all other procs */

         /* Now read lid and qp_num info from .npX files from other procs */

      for( i=1; i<p->nprocs; i++ ) {

         sprintf(filename, ".np%d", i);

         LOGPRINTF("Opening %s for reading\n", filename);

         while( (fd = fopen( filename, "r" )) == NULL ) { 
            usleep( 1000000 ); 
            
            /* This hack seems to force NFS to update its cache of the local directory.
             * Without it, I have seen the fopen function stall for up to 30 seconds until
             * the cache is updated. */

            system("ls . > /dev/null");
         }

         fscanf( fd, "%hu %u %u %u %p\n", &p->prot.lid[i],
                      &p->prot.dest_qp_num[i], &qp2, &p->prot.r_key[i],
                      &p->prot.r_addr[i]);

         fclose( fd );

         LOGPRINTF("Proc %d has lid %hu, qp_num[0]=%u, r_key=%u, and r_addr=%p\n",
                    i, p->prot.lid[i], p->prot.dest_qp_num[i], p->prot.r_key[i],
                    p->prot.r_addr[i]);
      }

   } else {

         /* Read proc 0 data from .np0 */

      LOGPRINTF("Opening .np0 for reading\n");

      while( (fd = fopen( ".np0", "r" )) == NULL ) { 
         usleep( 1000000 ); 
         system("ls . > /dev/null");
      }

      fscanf( fd, "%hu\n", &p->prot.lid[0]);
      
      fscanf( fd, "%u %p\n", &p->prot.r_key[0], &p->prot.r_addr[0]);

      for( i=1; i<p->nprocs; i++ ) {

         fscanf( fd, "%u\n", &qp1);
   
            /* Throw out all but my qp_num for comm with proc 0 */

         if( i == p->myproc ) p->prot.dest_qp_num[0] = qp1;

      }

      fclose( fd );

      LOGPRINTF("Proc 0 has lid %hu, qp_num[myproc]=%u, r_key=%u, and r_addr=%p\n",
                 p->prot.lid[0], p->prot.dest_qp_num[0], p->prot.r_key[0],
                 p->prot.r_addr[0]);

         /* Write my lid, qp_nums, r_key and r_addr */

      sprintf(filename, ".np%d.lock", p->myproc);
      fd = fopen( filename, "w" );

      fprintf(fd, "%hu %u %u %u %p\n", p->prot.lid[p->myproc], p->prot.qp_num[0], 
            p->prot.qp_num[p->dest], sync_mr_out.r_key, sync_buf );

      fclose( fd );

      sprintf(command, "mv .np%d.lock .np%d", p->myproc, p->myproc);
      system( command );

      if( p->dest != 0 ) {   /* My comm pair is not proc 0 */

            /* Now read the lid and qp_num info from my comm pair */

         sprintf(filename, ".np%d", p->dest);

         LOGPRINTF("Opening %s for reading\n", filename);

         while( (fd = fopen( filename, "r" )) == NULL ) { 
            usleep( 1000000 );
            system("ls . > /dev/null");
         }

         fscanf( fd, "%hu %u %u %u %p\n", &p->prot.lid[p->dest], &qp1, 
                             &p->prot.dest_qp_num[p->dest], 
                             &p->prot.r_key[p->dest], 
                             &p->prot.r_addr[p->dest]);
         fclose( fd );

         LOGPRINTF("Proc %d has lid %hu, qp_num[myproc]=%u, r_key=%u, r_addr=%p\n", 
               p->dest, p->prot.lid[p->dest], p->prot.dest_qp_num[p->dest], 
               p->prot.r_key[p->dest], p->prot.r_addr[p->dest]);
      }
   }

   LOGPRINTF("\nAfter file exchange\n\n");

      /* Establish the connections with the other procs */

   if( p->master ) {

      for( i=1; i<p->nprocs; i++) {

         connect_queue_pair( p, i );

      }

   } else {   /* Connect to proc 0 and my destination pair */

      connect_queue_pair( p, 0 );

      if( p->myproc != p->nprocs - 1 ) connect_queue_pair( p, p->dest );

   }

   Sync( p );

   if( p->master) system("rm -f .np*");    /* Remove the exchange files */

   /* If doing event completion, set event handler here */

   if( p->prot.comptype == NP_COMP_EVENT ) {
      
      LOGPRINTF("Setting completion handler\n");

      ret = EVAPI_set_comp_eventh(hca_hndl, p->prot.r_cq_hndl[p->dest], 
                                  event_handler, NULL, &ceh_hndl);
      CHECK_RET(ret, "Error setting completion event handler");
      
      ret = VAPI_req_comp_notif(hca_hndl, p->prot.r_cq_hndl[p->dest],
                                VAPI_NEXT_COMP);
      CHECK_RET(ret, "Error requesting notification for next completion event");

   }

}   

   /* Initialize the HCA adaptor */

int init_hca( ArgStruct *p )
{

      /* Open HCA just in case it was not opened by system earlier.
         Do not check the return value since it will come back as
         'device busy' if it has already been opened. */

   ret = VAPI_open_hca("InfiniHost0", &hca_hndl); 

   ret = EVAPI_get_hca_hndl("InfiniHost0", &hca_hndl);
   CHECK_RET(ret, "Error getting the HCA handle");
   LOGPRINTF("Got HCA handle\n");

      /* Get HCA properties */

   port_num=1;
   ret = VAPI_query_hca_port_prop(hca_hndl, (IB_port_t)port_num, 
                                  (VAPI_hca_port_t *)&hca_port);
   CHECK_RET(ret, "Error querying HCA");
   p->prot.lid[p->myproc] = hca_port.lid;
   LOGPRINTF("Queried HCA: lid = %d\n", hca_port.lid);
}

   /* Create a Queue Pair */

int init_queue_pair( ArgStruct *p, int proc )
{

      /* Create send completion queue */
  
   num_cqe = 30000; /* Requested number of completion q elements */
   ret = VAPI_create_cq(hca_hndl, num_cqe, &p->prot.s_cq_hndl[proc], &act_num_cqe);
   CHECK_RET(ret, "Error creating send completion queue");
   LOGPRINTF("Created send completion queue with %d elements\n", act_num_cqe);


      /* Create recv completion queue */
  
   num_cqe = 20000; /* Requested number of completion q elements */
   ret = VAPI_create_cq(hca_hndl, num_cqe, &p->prot.r_cq_hndl[proc], &act_num_cqe);
   CHECK_RET(ret, "Error creating recv completion queue");
   LOGPRINTF("Created receive completion queue with %d elements\n", act_num_cqe);

   qp_init_attr.cap.max_oust_wr_rq = max_wq; /* Max outstanding WR on RQ      */
   qp_init_attr.cap.max_oust_wr_sq = max_wq; /* Max outstanding WR on SQ      */
   qp_init_attr.cap.max_sg_size_rq = 1; /* Max scatter/gather entries on RQ   */
   qp_init_attr.cap.max_sg_size_sq = 1; /* Max scatter/gather entries on SQ   */
   qp_init_attr.rdd_hndl           = 0; /* Reliable datagram domain handle    */
   qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR; /* Signalling type   */
   qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR; /* Signalling type   */
   qp_init_attr.ts_type            = IB_TS_RC; /* Transmission type           */
   qp_init_attr.pd_hndl            = p->prot.pd_hndl; /* Protect domain hndl  */
   qp_init_attr.rq_cq_hndl         = p->prot.r_cq_hndl[proc]; /* recv CQ hndl */
   qp_init_attr.sq_cq_hndl         = p->prot.s_cq_hndl[proc]; /* send CQ hndl */

   ret = VAPI_create_qp(hca_hndl, &qp_init_attr, &p->prot.qp_hndl[proc], 
                        &qp_prop);
   CHECK_RET(ret, "Error creating queue pair");
   p->prot.qp_num[proc] = qp_prop.qp_num;
   LOGPRINTF("Created queue pair for proc %d; qp_num = %d\n", 
             proc, qp_prop.qp_num);
}

void file_sync( ArgStruct *p, int proc )
{
   char command[80], filename[80];
   FILE *fd;


   LOGPRINTF("file_sync started with %d\n", proc);

   sprintf( filename, ".np.%dsync%d",       p->myproc, proc );
   
      /* Each proc creates a sync file */

   while( (fd = fopen( filename, "r")) != NULL ) {
      fclose( fd );
      usleep(1000000);
   }

   sprintf( command,  "touch .np.%dsync%d", p->myproc, proc );
   system( command );

      /* Block for proc to create its file */
   
   sprintf( filename, ".np.%dsync%d",       proc, p->myproc );

   while( (fd = fopen( filename, "r")) == NULL ) {
      usleep(1000000);
      system("ls . > /dev/null");
   }
   fclose( fd );

      /* Remove this file */

   sprintf( command,  "rm .np.%dsync%d",    proc, p->myproc );
   system( command );

   LOGPRINTF("file_sync completed with %d\n", proc);
}


      /* Bring up the Queue Pair */
 
int connect_queue_pair( ArgStruct *p, int proc )
{ 
   int i;

     /******* INIT state ******/

   QP_ATTR_MASK_CLR_ALL(qp_attr_mask);

   qp_attr.qp_state = VAPI_INIT;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);

   qp_attr.pkey_ix = 0;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PKEY_IX);

   qp_attr.port = port_num;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PORT);

   qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE | VAPI_EN_REM_READ;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);

   ret = VAPI_modify_qp(hca_hndl, p->prot.qp_hndl[proc],
                        &qp_attr, &qp_attr_mask, &qp_cap);
   CHECK_RET(ret, "Error modifying QP to INIT");
   LOGPRINTF("Modified QP to INIT\n");

      /******* RTR (Ready-To-Receive) state *******/

   QP_ATTR_MASK_CLR_ALL(qp_attr_mask);

   qp_attr.qp_state = VAPI_RTR;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);

   qp_attr.qp_ous_rd_atom = 1;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_OUS_RD_ATOM);

      /* Set the destination qp_num here */

   qp_attr.dest_qp_num = p->prot.dest_qp_num[proc];
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_DEST_QP_NUM);

   qp_attr.av.sl = 0;
   qp_attr.av.grh_flag = FALSE;

      /* Set the lid of the destination proc */

   qp_attr.av.dlid = p->prot.lid[proc];

   qp_attr.av.static_rate = 0;
   qp_attr.av.src_path_bits = 0;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_AV);

   qp_attr.path_mtu = p->prot.ib_mtu;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PATH_MTU);

   qp_attr.rq_psn = 0;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RQ_PSN);

   qp_attr.pkey_ix = 0;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PKEY_IX);

   qp_attr.min_rnr_timer = 5;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_MIN_RNR_TIMER);

      /* This sets the dest lid and qp_num for this qp_hndl */
 
   ret = VAPI_modify_qp(hca_hndl, p->prot.qp_hndl[proc],
                        &qp_attr, &qp_attr_mask, &qp_cap);
   CHECK_RET(ret, "Error modifying QP to RTR");
   LOGPRINTF("Modified QP to RTR\n");

      /* Sync before going to RTS state to make sure both processes
       * have reached the RTR state. This is necessary to prevent
       * a process from racing ahead and attempting to send data over
       * VAPI before the receiving process has entered the Ready To
       * Receive state. */

   file_sync( p, proc );   /* Using lock files for now */

      /******* RTS (Ready-to-Send) state *******/

   QP_ATTR_MASK_CLR_ALL(qp_attr_mask);

   qp_attr.qp_state = VAPI_RTS;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);

   qp_attr.sq_psn = 0;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_SQ_PSN);

   qp_attr.timeout = 31;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_TIMEOUT);

   qp_attr.retry_count = 1;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RETRY_COUNT);

   qp_attr.rnr_retry = 1;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RNR_RETRY);

   qp_attr.ous_dst_rd_atom = 1;
   QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_OUS_DST_RD_ATOM);

   ret = VAPI_modify_qp(hca_hndl, p->prot.qp_hndl[proc],
                        &qp_attr, &qp_attr_mask, &qp_cap);
   CHECK_RET(ret, "Error modifying QP to RTS");
   LOGPRINTF("Modified QP to RTS\n");

}

int finalizeIB(ArgStruct *p)
{
   int i;

   LOGPRINTF("Finalizing IB stuff\n");

      /* Clear completion event handler */

   if(p->prot.comptype == NP_COMP_EVENT ) {
      LOGPRINTF("Clearing comp handler\n");
      ret = EVAPI_clear_comp_eventh(hca_hndl, ceh_hndl);
      CHECK_RET(ret, "Error clearing event handler");
      LOGPRINTF("Cleared event handler\n");
   }

      /* Destroy all handles */

   for( i=0; i<p->nprocs; i++) 
      if( i != p->myproc && (p->master || (!p->master && 
                  (i == 0 || i == p->nprocs-1-p->myproc)))) {

      if(p->prot.qp_hndl[i] != VAPI_INVAL_HNDL) {
         LOGPRINTF("Destroying QP\n");
         ret = VAPI_destroy_qp(hca_hndl, p->prot.qp_hndl[i]);
         CHECK_RET(ret, "Error destroying Queue Pair");
      }

      if(p->prot.r_cq_hndl[i] != VAPI_INVAL_HNDL) {
         LOGPRINTF("Destroying Recv CQ\n");
         ret = VAPI_destroy_cq(hca_hndl, p->prot.r_cq_hndl[i]);
         CHECK_RET(ret, "Error destroying recv CQ");
      }

      if(p->prot.s_cq_hndl[i] != VAPI_INVAL_HNDL) {
         LOGPRINTF("Destroying Send CQ\n");
         ret = VAPI_destroy_cq(hca_hndl, p->prot.s_cq_hndl[i]);
         CHECK_RET(ret, "Error destroying send CQ");
      }

   }

      /* Check memory registrations just in case the user bailed out */

   if(s_mr_hndl != VAPI_INVAL_HNDL) {
      LOGPRINTF("Deregistering send buffer\n");
      ret = VAPI_deregister_mr(hca_hndl, s_mr_hndl);
      CHECK_RET(ret, "Error deregistering send mr");
   }

   if(r_mr_hndl != VAPI_INVAL_HNDL) {
      LOGPRINTF("Deregistering recv buffer\n");
      ret = VAPI_deregister_mr(hca_hndl, r_mr_hndl);
      CHECK_RET(ret, "Error deregistering recv mr");
   }

      /* Deregister synchronization memory region */
   
   if(sync_mr_hndl != VAPI_INVAL_HNDL) {
      LOGPRINTF("Deregistering sync buffer\n");
      ret = VAPI_deregister_mr(hca_hndl, sync_mr_hndl);
      CHECK_RET(ret, "Error deregistering sync mr");
   }

      /* Deallocate protection domain */

   if(p->prot.pd_hndl != VAPI_INVAL_HNDL) {
      LOGPRINTF("Deallocating PD\n");
      ret = VAPI_dealloc_pd(hca_hndl, p->prot.pd_hndl);
      CHECK_RET(ret, "Error deallocating PD");
   }

      /* Application code should not close HCA, just release handle */

   if(hca_hndl != VAPI_INVAL_HNDL) {
      LOGPRINTF("Releasing HCA\n");
      ret = EVAPI_release_hca_hndl(hca_hndl);
      CHECK_RET(ret, "Error releasing HCA");
   }

   return 0;
}

void event_handler(VAPI_hca_hndl_t hca, VAPI_cq_hndl_t cq, void* data)
{
   VAPI_ret_t ret;
   VAPI_wc_desc_t wc;

   LOGPRINTF("Entered event_handler()\n");

   /* We should always poll the completion queue until it's empty.
    * Even if we're only expecting one entry, errors will also
    * generate completion entries. */

   while(1) {
            
      ret = VAPI_poll_cq(hca, cq, &wc);
      CHECK_RET(ret, "Error polling for completion in event handler");
      CHECK_WC(wc, "Error in status of work completion in event handler");

      if(ret == VAPI_CQ_EMPTY) {
         LOGPRINTF("Empty completion queue, requesting next notification\n");
         ret = VAPI_req_comp_notif(hca_hndl, cq, VAPI_NEXT_COMP);
         CHECK_RET(ret, "Error requesting completion notification");
         return;
      }
                              
      LOGPRINTF("Retrieved work completion\n");

      /* If receive_complete is already 1, and we're in bi-directional burst
       * mode, then the main thread must not have reset receive_complete to 0
       * yet, so we wait until that happens. If we're not in bi-directional
       * burst mode, then receive_complete should always be set to 0 by this
       * point. We'll leave the code in for now to catch bugs in that case */

      while( receive_complete == 1 ) { sched_yield(); }

      receive_complete = 1;
   }
}

   /* Send all the message to a remote proc via InfiniBand. This function
    * is synchronous and will block until a corresponding readFully() is
    * called at the other node */

void writeFully(ArgStruct *p, int proc, void *buf, int nbytes)
{
   VAPI_sr_desc_t      sr;        /* Send request */
   VAPI_sg_lst_entry_t sg_entry;  /* Scatter/Gather list - holds buff addr */
   VAPI_mrw_t          mr_out;
   VAPI_mr_hndl_t      mr_hndl;
   
   /* Register buffer with Infiniband */

   mr_in.acl = 0; /* Read access is always implied */
   mr_in.pd_hndl = p->prot.pd_hndl;
   mr_in.size = nbytes;
   mr_in.start = (VAPI_virt_addr_t)(MT_virt_addr_t)buf;
   mr_in.type = VAPI_MR;

   ret = VAPI_register_mr(hca_hndl, &mr_in, &mr_hndl, &mr_out);
   CHECK_RET(ret, "Error registering send buffer");
   LOGPRINTF("Registered send buffer\n");

   /* Post a signaled send for the supplied buffer */

   sr.opcode = VAPI_SEND;
   sr.comp_type = VAPI_SIGNALED;
   sr.sg_lst_len = 1;
   sr.sg_lst_p = &sg_entry;

   sg_entry.lkey = mr_out.l_key;
   sg_entry.len = nbytes;
   sg_entry.addr = (VAPI_virt_addr_t)(MT_virt_addr_t)buf; 

   /* Sync up with other node so that we know the corresponding receive
    * is already posted */

   sync_over_ib(p, proc);

   ret = VAPI_post_sr(hca_hndl, p->prot.qp_hndl[proc], &sr);
   CHECK_RET(ret, "Error posting send request");
   LOGPRINTF("Posted send request\n");

   /* Poll completion queue for send completion */
   
   ret = VAPI_CQ_EMPTY;
   while( ret == VAPI_CQ_EMPTY )
      ret = VAPI_poll_cq(hca_hndl, p->prot.s_cq_hndl[proc], &wc);
   CHECK_RET(ret, "Error polling for completion");
   CHECK_WC(wc, "Error in status of work completion");
   LOGPRINTF("Got completion for receive\n");

   /* Deregister the buffer with Infiniband */

   ret = VAPI_deregister_mr(hca_hndl, mr_hndl);
   CHECK_RET(ret, "Error deregistering mr handle");
   LOGPRINTF("Deregistered send buffer\n");
   
}

   /* Receive the complete message across the InfiniBand network. This
    * function is synchronous and will block until a corresponding
    * writeFully() is called at the other node */

void readFully(ArgStruct *p, int proc, void *buf, int nbytes)
{
   VAPI_rr_desc_t      rr;        /* Receive request */
   VAPI_sg_lst_entry_t sg_entry;  /* Scatter/Gather list - holds buff addr */
   VAPI_mrw_t          mr_out;
   VAPI_mr_hndl_t      mr_hndl;
   VAPI_wc_desc_t      wc;

   /* Register buffer with Infiniband. Store memory region handle in the 
    * supplied pointer to a handle. */

   mr_in.acl = VAPI_EN_LOCAL_WRITE;
   mr_in.pd_hndl = p->prot.pd_hndl;
   mr_in.size = nbytes;
   mr_in.start = (VAPI_virt_addr_t)(MT_virt_addr_t)buf;
   mr_in.type = VAPI_MR;

   ret = VAPI_register_mr(hca_hndl, &mr_in, &mr_hndl, &mr_out);
   CHECK_RET(ret, "Error registering receive buffer");
   LOGPRINTF("Registered recveive buffer\n");

   /* Post a signaled receive for the supplied buffer */

   rr.opcode = VAPI_RECEIVE;
   rr.comp_type = VAPI_SIGNALED;
   rr.sg_lst_len = 1;
   rr.sg_lst_p = &sg_entry;

   sg_entry.lkey = mr_out.l_key;
   sg_entry.len = nbytes;
   sg_entry.addr = (VAPI_virt_addr_t)(MT_virt_addr_t)buf; 

   ret = VAPI_post_rr(hca_hndl, p->prot.qp_hndl[proc], &rr);
   CHECK_RET(ret, "Error posting receive request");
   LOGPRINTF("Posted receive request\n");

   /* Sync to tell other node to go ahead with the send */

   sync_over_ib(p, proc);
   
   if( p->prot.comptype == NP_COMP_EVENT && proc == p->dest ) {

      /* If we're using event completion, then every receive
       * from p->dest will cause the event handler to be called.
       * Instead of polling on cq here, wait for event handler
       * to flip receive_complete flag. */

      while( receive_complete == 0 ) { /* BUSY WAIT */ }

      receive_complete = 0;
      
   } else {

      /* Poll completion queue for the completion of the previously
       * posted receive */

      ret = VAPI_CQ_EMPTY;
      while( ret == VAPI_CQ_EMPTY )
         ret = VAPI_poll_cq(hca_hndl, p->prot.r_cq_hndl[proc], &wc);
      CHECK_RET(ret, "Error polling for completion");
      CHECK_WC(wc, "Received completion with bad status");
      LOGPRINTF("Got completion for receive\n");

   }

   /* Deregister the buffer with Infiniband */

   ret = VAPI_deregister_mr(hca_hndl, mr_hndl);
   CHECK_RET(ret, "Error deregistering mr handle");
   LOGPRINTF("Deregistered receive buffer\n");

}

   /* Use Infiniband VAPI communications to perform sync with another node */

void sync_over_ib(ArgStruct *p, int proc)
{
   static int* flip = NULL;
   VAPI_sr_desc_t      sr;        /* Send request */
   VAPI_sg_lst_entry_t sg_entry;  /* Scatter/Gather list - holds buff addr */
   VAPI_wc_desc_t      wc;        /* Work completion descriptor */

      /* Initialize flip if this is first call to Sync */

   if(flip == NULL) {
      flip = malloc( p->nprocs * sizeof(int) );
      memset(flip, 0, p->nprocs * sizeof(int) );
   }

   /* Fill in send request struct */

   sr.opcode = VAPI_RDMA_WRITE;
   sr.remote_addr = (VAPI_virt_addr_t)(MT_virt_addr_t)
      (p->prot.r_addr[proc] + p->myproc*2 + flip[proc]);
   sr.r_key = p->prot.r_key[proc];
   sr.comp_type = VAPI_SIGNALED;

   sr.sg_lst_len = 1;
   sr.sg_lst_p = &sg_entry;

   sg_entry.lkey = sync_mr_out.l_key; /* Local memory region key */
   sg_entry.len = sizeof(char);
   sg_entry.addr = (VAPI_virt_addr_t)(MT_virt_addr_t)sync_buf + p->myproc*2 + flip[proc];

      /* Post the send request */

   ret = VAPI_post_sr(hca_hndl, p->prot.qp_hndl[proc], &sr);
   CHECK_RET(ret, "Error posting send request");
   LOGPRINTF("Posted sync rdma write request\n");

      /* Poll for completion */

   ret = VAPI_CQ_EMPTY;
   while(ret == VAPI_CQ_EMPTY) {
      ret = VAPI_poll_cq(hca_hndl, p->prot.s_cq_hndl[proc], &wc);
   }
   CHECK_RET(ret, "Error polling for completion");
   CHECK_WC(wc, "Error in status of work completion");
   LOGPRINTF("Got completion for sync rdma write request\n");

      /* Poll for incoming '1' */

   while( sync_buf[ proc*2 + flip[proc] ] != 1 ) sched_yield();
   sync_buf[ proc*2 + flip[proc] ] = 0;

      /* Flip flip */

   flip[proc] = 1 - flip[proc];
}

   /* Global sync with the master proc, then local sync with your comm pair */

void Sync(ArgStruct *p)
{
   int i;

      /* First do a global synchronization with the master proc 0
       * if there are multiple pairs. */

   if( p->master && p->nprocs > 2 ) {

      for( i=1; i<p->nprocs; i++ ) {

         sync_over_ib(p, i);
         
      }

   } else if( p->nprocs > 2 ) {

      sync_over_ib(p, 0);
      
   }

      /* Now do a local sync with your comm pair. */

   sync_over_ib(p, p->dest);
}


/* Reset is used after a trial to empty the work request queues so we
 * have enough room for the next trial to run.  The signaled sends
 * done in Reset() will flush the unsignaled communications done by
 * SendData/RecvData. This allows unsignaled communications to poll
 * on an overwrite of the last byte rather than using other methods
 * that could add extra latency. */

void Reset(ArgStruct *p)
{
   VAPI_sr_desc_t sr;
   VAPI_rr_desc_t rr;

      /* Prepost signaled receive on communication channel with
       * my partner. */

   rr.opcode = VAPI_RECEIVE;
   rr.comp_type = VAPI_SIGNALED;
   rr.sg_lst_len = 0;

   ret = VAPI_post_rr(hca_hndl, p->prot.qp_hndl[p->dest], &rr);
   CHECK_RET(ret, "Error posting receive request");
   LOGPRINTF("Posted receive request to %d\n", p->dest);

   sync_over_ib(p, p->dest);

      /* Do corresponding signaled send on communication channel
       * with my partner. */

   sr.opcode = VAPI_SEND;
   sr.comp_type = VAPI_SIGNALED;
   sr.sg_lst_len = 0;

   ret = VAPI_post_sr(hca_hndl, p->prot.qp_hndl[p->dest], &sr);
   CHECK_RET(ret, "Error posting send request in Reset");
   LOGPRINTF("Posted send request to %d\n", p->dest);

   ret = VAPI_CQ_EMPTY;
   while(ret == VAPI_CQ_EMPTY)
      ret = VAPI_poll_cq(hca_hndl, p->prot.s_cq_hndl[p->dest], &wc);
   CHECK_RET(ret, "Error polling CQ for send");
   CHECK_WC(wc, "Error in status of work completion for send");
   LOGPRINTF("Got completion for send request\n");

   if( p->prot.comptype == NP_COMP_EVENT ) {

      /* If we're doing event completion, the event handler will be
       * called when the receive comes in, so instead of polling on the
       * cq, poll on the receive_complete flag, which the event handler
       * will flip. */

      while( receive_complete == 0 ) { /* BUSY WAIT */ }

      receive_complete = 0;

   } else {
      
         /* Poll completion queue for the completion of the receive
          * request. */

      ret = VAPI_CQ_EMPTY;
      while(ret == VAPI_CQ_EMPTY) 
         ret = VAPI_poll_cq(hca_hndl, p->prot.r_cq_hndl[p->dest], &wc);
      CHECK_RET(ret, "Error polling CQ for receive");
      CHECK_WC(wc, "Error in status of work completion for receive");
      LOGPRINTF("Got completion for receive request\n");
   
   }

   sync_over_ib(p, p->dest);
   
   LOGPRINTF("Done with Reset\n");
}


void CleanUp(ArgStruct *p)
{
   char *quit="QUIT";

   finalizeIB(p);
}

   /* Send the data segment to the proc I am paired with.  */

void SendData(ArgStruct *p)
{

   VAPI_sr_desc_t      sr;        /* Send request */
   VAPI_sg_lst_entry_t sg_entry;  /* Scatter/Gather list - holds buff addr */

      /* Fill in send request struct */

   if(p->prot.commtype == NP_COMM_SENDRECV) {
      sr.opcode = VAPI_SEND;
      LOGPRINTF("Doing regular send\n");
   } else if(p->prot.commtype == NP_COMM_SENDRECV_WITH_IMM) {
      sr.opcode = VAPI_SEND_WITH_IMM;
      LOGPRINTF("Doing regular send with imm\n");
   } else if(p->prot.commtype == NP_COMM_RDMAWRITE) {
      sr.opcode = VAPI_RDMA_WRITE;
      sr.remote_addr = (VAPI_virt_addr_t)(MT_virt_addr_t)(p->prot.r_addr_rbuf 
            + ((p->s_ptr - p->soffset) - p->s_buff) + p->roffset);
      sr.r_key = p->prot.r_key_rbuf;
      LOGPRINTF("Doing RDMA write (raddr=%p)\n", sr.remote_addr);
   } else if(p->prot.commtype == NP_COMM_RDMAWRITE_WITH_IMM) {
      sr.opcode = VAPI_RDMA_WRITE_WITH_IMM;
      sr.remote_addr = (VAPI_virt_addr_t)(MT_virt_addr_t)(p->prot.r_addr_rbuf 
            + ((p->s_ptr-p->soffset) - p->s_buff) + p->roffset);
      sr.r_key = p->prot.r_key_rbuf;
      LOGPRINTF("Doing RDMA write with imm (raddr=%p)\n", sr.remote_addr);
   } else {
      fprintf(stderr, "Error, invalid communication type in SendData\n");
      exit(-1);
   }
  
   /* According to Mellanox Verbs API reference, an early revision of hardware
    * (A-0) has a bug such that the comp_type flag will be ignored if the
    * set_se (solicited events) flag is set to TRUE (1).  To insure that we
    * have unsignaled completions here, we must make sure set_se is set to
    * FALSE (0). */
   
   sr.comp_type = VAPI_SIGNALED;
   sr.set_se = FALSE;  /* Set solicited event flag, not using events */

   sr.sg_lst_len = 1;
   sr.sg_lst_p = &sg_entry;

   sg_entry.lkey = p->prot.l_key_sbuf; /* Local memory region key */
   sg_entry.len = p->bufflen;
   sg_entry.addr = (VAPI_virt_addr_t)(MT_virt_addr_t) p->s_ptr;

      /* Post the send request */

   ret = VAPI_post_sr(hca_hndl, p->prot.qp_hndl[p->dest], &sr);
   CHECK_RET(ret, "Error posting send request");
   LOGPRINTF("Posted send request\n");

      /* Wait for send request to complete */

   do {
      ret = VAPI_poll_cq(hca_hndl, p->prot.s_cq_hndl[p->dest], &wc);
   } while(ret == VAPI_CQ_EMPTY);
   CHECK_RET(ret, "Error polling completion queue");
   CHECK_WC(wc, "Error in status of work completion");
   
}

   /* netpipe.c will call PrepareToReceive() before each RecvData() call */

void PrepareToReceive(ArgStruct *p)
{
   VAPI_rr_desc_t      rr;        /* Receive request */
   VAPI_sg_lst_entry_t sg_entry;  /* Scatter/Gather list - holds buff addr */

      /* We don't need to post a receive if doing RDMA write with local polling */

   if( p->prot.commtype == NP_COMM_RDMAWRITE &&
       p->prot.comptype == NP_COMP_LOCALPOLL ) {
      return;
   }
  
   rr.opcode = VAPI_RECEIVE;

      /* We only need signaled completions if using VAPI
       * completion methods. */

   if( p->prot.comptype == NP_COMP_LOCALPOLL )
      rr.comp_type = VAPI_UNSIGNALED;
   else
      rr.comp_type = VAPI_SIGNALED;

   rr.sg_lst_len = 1;
   rr.sg_lst_p = &sg_entry;

   sg_entry.lkey = p->prot.l_key_rbuf;
   sg_entry.len = p->bufflen;
   sg_entry.addr = (VAPI_virt_addr_t)(MT_virt_addr_t) p->r_ptr;

   ret = VAPI_post_rr(hca_hndl, p->prot.qp_hndl[p->source], &rr);
   CHECK_RET(ret, "Error posting receive request");
   LOGPRINTF("Posted receive request\n");

}


int TestForCompletion(ArgStruct *p)
{
   volatile unsigned char *cbuf = (unsigned char *) p->r_ptr;
   int complete = 0;

   if( p->prot.comptype == NP_COMP_LOCALPOLL ) {

         /* Poll for receive completion locally on the receive data */

      if( cbuf[p->bufflen-1] == (p->tr ? p->expected+1 : p->expected-1) ) 
         complete = 1;

   } else if( p->prot.comptype == NP_COMP_VAPIPOLL ) {
      
         /* Poll for receive completion using VAPI poll function */

      ret = VAPI_CQ_EMPTY;

      ret = VAPI_poll_cq(hca_hndl, p->prot.r_cq_hndl[p->source], &wc);

      if( ret != VAPI_CQ_EMPTY ) complete = 1;

      CHECK_RET(ret, "Error polling for completion of receive");
      CHECK_WC(wc, "Error in status of returned completion for receive");
     
   } else if( p->prot.comptype == NP_COMP_EVENT ) {

         /* Instead of polling directly on data or VAPI completion queue,
          * let the VAPI event completion handler set a flag when the receive
          * completes, and poll on that instead. Could try using semaphore here
          * as well to eliminate busy polling
          */

      if( receive_complete != 0 ) complete = 1;

      /* Reset receive_complete to 0. It might make more sense to
       * always do this in PrepareToReceive, but we *have* to do it
       * here for bi-directional prepost burst mode, so to keep things
       * simple, we'll always reset it here. */

      receive_complete = 0;
   }

   return complete;
}

   /* RecvData may just test for an overwrite of the last byte which
    * does not fully clean up the completion queue.  This is the fastest
    * approach, providing the lowest latency, but does require cleanup
    * at some point which is done in NetPIPE by doing a signaled send
    * in the Reset() function.
    */

void RecvData(ArgStruct *p)
{
   volatile unsigned char *cbuf = (unsigned char *) p->r_ptr;

      /* Busy wait for incoming data */

   LOGPRINTF("Receiving at buffer address %p\n", cbuf);

   if( p->prot.comptype == NP_COMP_LOCALPOLL ) {

         /* Poll for receive completion locally on the receive data */

      LOGPRINTF("Waiting for last byte of data to arrive\n");
     
      while( cbuf[p->bufflen-1] != (p->tr ? p->expected+1 : p->expected-1) ) {
         /* BUSY WAIT */
      }

      LOGPRINTF("Received all of data\n");

   } else if( p->prot.comptype == NP_COMP_VAPIPOLL ) {
      
         /* Poll for receive completion using VAPI poll function */

      ret = VAPI_CQ_EMPTY;
      while(ret == VAPI_CQ_EMPTY)
         ret = VAPI_poll_cq(hca_hndl, p->prot.r_cq_hndl[p->source], &wc);
      CHECK_RET(ret, "Error polling for completion of receive");
      CHECK_WC(wc, "Error in status of returned completion for receive");
      LOGPRINTF("Got completion for receive\n");
     
   } else if( p->prot.comptype == NP_COMP_EVENT ) {

         /* Instead of polling directly on data or VAPI completion queue,
          * let the VAPI event completion handler set a flag when the receive
          * completes, and poll on that instead. Could try using semaphore here
          * as well to eliminate busy polling
          */

      LOGPRINTF("Polling receive flag\n");
     
      while( receive_complete == 0 ) { sched_yield(); }

      LOGPRINTF("Receive completed\n");

      /* Reset receive_complete to 0. It might make more sense to
       * always do this in PrepareToReceive, but we *have* to do it
       * here for bi-directional prepost burst mode, so to keep things
       * simple, we'll always reset it here. */

      receive_complete = 0;
   }
}

   /* Generic Gather routine (used to gather times) */

void Gather( ArgStruct *p, double *buf)
{
   int i, nbytes = sizeof(double);
   char *ptr = (char *) buf;

   if( p->master ) {   /* Gather the data from the other procs */

      for( i=1; i<p->nprocs; i++ ) {

         ptr += nbytes;

         readFully(p, i, ptr, nbytes);

      }

   } else {   /* Write my section to the master proc 0 */

      ptr += p->myproc * nbytes;
         
      writeFully(p, 0, ptr, nbytes);

   }
}

   /* Broadcast from master 0 to all other procs (used for nrepeat) */

void Broadcast(ArgStruct *p, unsigned int *buf)
{
   int i, nbytes = sizeof(int);

   if( p->master ) {

      for( i=1; i<p->nprocs; i++ ) {

         writeFully(p, i, buf, nbytes);

      }

   } else {

      readFully(p, 0, buf, nbytes);

   }
}


void AfterAlignmentInit(ArgStruct *p)
{
      /* Exchange buffer pointers and remote InfiniBand keys if doing rdma. Do
       * the exchange in this function because this will happen after any
       * memory alignment is done, which is important for getting the 
       * correct remote address.
      */

   if( p->prot.commtype == NP_COMM_RDMAWRITE || 
       p->prot.commtype == NP_COMM_RDMAWRITE_WITH_IMM ) {
     
         /* Exchnage receive buffer addresses */

      if( p->tr ) {
         writeFully( p, p->dest, (void *)&p->r_buff, sizeof(void*) );
         LOGPRINTF("Sent buffer address: %p\n", p->r_buff);
         readFully( p, p->dest, (void *)&p->prot.r_addr_rbuf, sizeof(void*) );
         LOGPRINTF("Received remote address from other proc: %p\n", p->prot.r_addr_rbuf);

            /* If cache and bi-directional mode, we'll be swapping send and recv buffers
             * every trial, so we need to have the remote key and addresses for the send
             * buffer as well. */

         if( p->cache && p->bidir ) {
            writeFully( p, p->dest, (void *)&p->s_buff, sizeof(void*) );
            LOGPRINTF("Sent buffer address: %p\n", p->s_buff);
            readFully( p, p->dest, (void *)&p->prot.r_addr_sbuf, sizeof(void*) );
            LOGPRINTF("Received remote address from other proc: %p\n", p->prot.r_addr_sbuf);
         }

      } else {
         readFully( p, p->dest, (void *)&p->prot.r_addr_rbuf, sizeof(void*) );
         LOGPRINTF("Received remote address from other proc: %p\n", p->prot.r_addr_rbuf);
         writeFully( p, p->dest, (void *)&p->r_buff, sizeof(void*) );
         LOGPRINTF("Sent buffer address: %p\n", p->r_buff);

         if( p->cache && p->bidir ) {
            readFully( p, p->dest, (void *)&p->prot.r_addr_sbuf, sizeof(void*) );
            LOGPRINTF("Received remote address from other proc: %p\n", p->prot.r_addr_sbuf);
            writeFully( p, p->dest, (void *)&p->s_buff, sizeof(void*) );
            LOGPRINTF("Sent buffer address: %p\n", p->s_buff);
         }
      }

         /* Exchange remote keys for accessing
          * remote buffers via IB RDMA */

      if( p->tr ) {
         writeFully( p, p->dest, (void *)&r_mr_out.r_key, sizeof(VAPI_rkey_t) );
         LOGPRINTF("Sent remote key: %d\n", r_mr_out.r_key);
         readFully( p, p->dest, (void *)&p->prot.r_key_rbuf, sizeof(VAPI_rkey_t) );
         LOGPRINTF("Received remote key from other proc: %d\n", p->prot.r_key_rbuf);

         if( p->cache && p->bidir ) {
            writeFully( p, p->dest, (void *)&s_mr_out.r_key, sizeof(VAPI_rkey_t) );
            LOGPRINTF("Sent remote key: %d\n", s_mr_out.r_key);
            readFully( p, p->dest, (void *)&p->prot.r_key_sbuf, sizeof(VAPI_rkey_t) );
            LOGPRINTF("Received remote key from other proc: %d\n", p->prot.r_key_sbuf);
         }

      } else {
         readFully( p, p->dest, (void *)&p->prot.r_key_rbuf, sizeof(VAPI_rkey_t) );
         LOGPRINTF("Received remote key from other proc: %d\n", p->prot.r_key_rbuf);
         writeFully( p, p->dest, (void *)&r_mr_out.r_key, sizeof(VAPI_rkey_t) );
         LOGPRINTF("Sent remote key: %d\n", r_mr_out.r_key);

         if( p->cache && p->bidir ) {
            readFully( p, p->dest, (void *)&p->prot.r_key_sbuf, sizeof(VAPI_rkey_t) );
            LOGPRINTF("Received remote key from other proc: %d\n", p->prot.r_key_sbuf);
            writeFully( p, p->dest, (void *)&s_mr_out.r_key, sizeof(VAPI_rkey_t) );
            LOGPRINTF("Sent remote key: %d\n", s_mr_out.r_key);
         }
      }
   }
}

   /* This only mallocs the data buffers for comms to my dest pair */

void MyMalloc(ArgStruct *p, int bufflen)
{

      /* Allocate buffers -- we always allocate the receive buffer.
       * If we're doing uni-directional cache mode, then we just use
       * the one buffer for sends and receives. If we're doing 
       * bi-directional cache mode, or cache-invalidation mode, then
       * we allocate a second buffer. */

   p->r_buff = malloc( bufflen + MAX(p->soffset,p->roffset) );
   if( p->r_buff == NULL ) {
      fprintf(stderr, "Error malloc'ing buffer\n");
      exit(-1);
   }

   if(p->cache && !p->bidir) {

         /* InfiniBand spec says we can register same memory region
          * more than once, so just copy buffer address. We will register
          * the same buffer twice with InfiniBand.  */

       p->s_buff = p->r_buff;

   } else {

      p->s_buff = malloc( bufflen + p->soffset );
      if( p->s_buff == NULL ) {
         fprintf(stderr, "Error malloc'ing buffer\n");
         exit(-1);
      }
   }

      /* Register buffers with InfiniBand */

   mr_in.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
   mr_in.l_key = 0;

   mr_in.pd_hndl = p->prot.pd_hndl;

   mr_in.r_key = 0;
   mr_in.size = bufflen+MAX(p->soffset,p->roffset);
   mr_in.start = (VAPI_virt_addr_t)(MT_virt_addr_t)p->r_buff;
   mr_in.type = VAPI_MR;

   ret = VAPI_register_mr(hca_hndl, &mr_in, &r_mr_hndl, &r_mr_out);
   CHECK_RET(ret, "Error registering receive buffer");
   LOGPRINTF("Registered receive buffer\n");
   
   p->prot.l_key_rbuf = r_mr_out.l_key;

      /* enable remote write in send buffer region, because
       * we will be writing to it if we're using bi-directional
       * and cache modes (swapping send and receive buffers). */

   if(p->cache && p->bidir)
      mr_in.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
   else
      mr_in.acl = VAPI_EN_LOCAL_WRITE;

   mr_in.l_key = 0;

   mr_in.pd_hndl = p->prot.pd_hndl;

   mr_in.r_key = 0;
   mr_in.size = bufflen+p->soffset;
   mr_in.start = (VAPI_virt_addr_t)(MT_virt_addr_t)p->s_buff;
   mr_in.type = VAPI_MR;

   ret = VAPI_register_mr(hca_hndl, &mr_in, &s_mr_hndl, &s_mr_out);
   CHECK_RET(ret, "Error registering send buffer");
   LOGPRINTF("Registered send buffer\n");

   p->prot.l_key_sbuf = s_mr_out.l_key;
}


void FreeBuff(char *buff1, char *buff2)
{

   if(s_mr_hndl != VAPI_INVAL_HNDL) {
      LOGPRINTF("Deregistering send buffer\n");
      ret = VAPI_deregister_mr(hca_hndl, s_mr_hndl);
      CHECK_RET(ret, "Error deregistering send mr");
      s_mr_hndl = VAPI_INVAL_HNDL;
   }

   if(r_mr_hndl != VAPI_INVAL_HNDL) {
      LOGPRINTF("Deregistering recv buffer\n");
      ret = VAPI_deregister_mr(hca_hndl, r_mr_hndl);
      CHECK_RET(ret, "Error deregistering recv mr");
      r_mr_hndl = VAPI_INVAL_HNDL;
   }

   if(buff1 != NULL) free(buff1);

   if(buff2 != NULL) free(buff2);
}

void ModuleSwapPtrs(ArgStruct *p)
{
   VAPI_lkey_t tmp_lkey;
   VAPI_rkey_t tmp_rkey;
   void* tmp_ptr;

      /* Need to swap local access keys */

   tmp_lkey = p->prot.l_key_rbuf;
   p->prot.l_key_rbuf = p->prot.l_key_sbuf;
   p->prot.l_key_sbuf = tmp_lkey;

      /* Need to swap remote access keys */

   tmp_rkey = p->prot.r_key_rbuf;
   p->prot.r_key_rbuf = p->prot.r_key_sbuf;
   p->prot.r_key_sbuf = tmp_rkey;

      /* Need to swap remote buffer addresses */

   tmp_ptr = p->prot.r_addr_rbuf;
   p->prot.r_addr_rbuf = p->prot.r_addr_sbuf;
   p->prot.r_addr_sbuf = tmp_ptr;
}
