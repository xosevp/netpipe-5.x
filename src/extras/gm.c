/* This is the GM module for NetPIPE 
 * Originally written by Xuehua Chen (I think)
 * Modified for multiple pairwise interactions by Dave Turner
 */

#include "netpipe.h"

struct gm_port *gm_p;
int sizeof_workspace;
char *workspace;
int *r, *s;            /* incoming and outgoing node numbers */
int stokens;


void Init(ArgStruct *p, int* pargc, char*** pargv)
{
}

void Setup(ArgStruct *p)
{
   FILE *fd;
   int i;
   char *myhostname, *short_hostname, *dot;

      /* Open port 2 on the Myrinet card */

   if( gm_open( &gm_p, 0, 5, "port2", (enum gm_api_version) GM_API_VERSION ) 
       != GM_SUCCESS) {
      printf(" Couldn't open board 0 port 2\n");
      exit(-1);
   } else {
/*      printf("Opened board 0 port2\n");*/
   }


      /* Open the hostfile, read in the host names and get the host_ids */

   myhostname = (char *) malloc( 100 );
   gethostname( myhostname, 100);
   if( (dot=strstr( myhostname, "." )) != NULL ) *dot = '\0';

   if( (fd = fopen( p->hostfile, "r")) == NULL ) {
      fprintf(stderr, "%d Could not open the hostfile %s, (errno=%d)\n",
              p->myproc, p->hostfile, errno );
      exit(0);
   }

      /* allocate space for the list of hosts and their host id's */

   p->prot.host_id = (short int *) malloc( p->nprocs * sizeof(short int) );

   p->host = (char **) malloc( p->nprocs * sizeof(char *) );

   for( i=0; i<p->nprocs; i++ ) 
      p->host[i] = (char *) malloc( 100 * sizeof(char) );

   for( i=0; i<p->nprocs; i++ ) {

      fscanf( fd, "%s", p->host[i] );

      if( (dot=strstr( p->host[i], "." )) != NULL ) *dot = '\0';

      p->prot.host_id[i] = gm_host_name_to_node_id(gm_p, p->host[i]);

      if( p->prot.host_id[i] == GM_NO_SUCH_NODE_ID ) {
         fprintf(stderr,"GM ERROR: %d failed to connect to host %s\n",
                 p->myproc, p->host[i]);
         exit(0);
      }

         /* Set myproc if this is my host and it was not set by nplaunch */

      if( p->myproc < 0 && !strcmp(p->host[i],myhostname) ) p->myproc = i;
   }


      /* The first half will pair up with the second half in overlapping fashion */

   p->tr = p->rcv = 0;
   if( p->myproc < p->nprocs/2 ) p->tr  = 1;
   else                          p->rcv = 1;

   p->dest = p->source = p->nprocs - 1 - p->myproc;
   if( p->myproc == 0 ) p->master = 1;

   gm_free_send_tokens(gm_p, GM_LOW_PRIORITY,  gm_num_send_tokens(gm_p)/2);

   gm_free_send_tokens(gm_p, GM_HIGH_PRIORITY, gm_num_send_tokens(gm_p)/2);


      /* Set up pinned workspace for Sync, Broadcast and Gather */

   r  = (int *) gm_dma_malloc( gm_p, 3*sizeof(int) );
   s  = (int *) gm_dma_malloc( gm_p, 3*sizeof(int) );
   for(i=0; i<3; i++) s[i] = p->myproc + 1000;

   sizeof_workspace = p->nprocs * ( sizeof( double ) + sizeof(int) );
   workspace = (char *) gm_dma_malloc( gm_p, sizeof_workspace );


   Sync(p);   /* Just does a sync of all procs to test */

   p->prot.num_stokens = stokens = gm_num_send_tokens(gm_p);
}   

void my_low_send_callback(struct gm_port *port, void *context, 
                          gm_status_t status)
{
   if (status != GM_SUCCESS && status != GM_SEND_DROPPED) {
      gm_perror("low send completed with error", status);
   } else {
      gm_free_send_token( port, GM_LOW_PRIORITY);
      stokens++;
   }
}

void my_high_send_callback(struct gm_port *port, void *context, 
                          gm_status_t status)
{
   if (status != GM_SUCCESS && status != GM_SEND_DROPPED) {
      gm_perror("high send completed with error", status);
   } else {
      gm_free_send_token( port, GM_HIGH_PRIORITY);
      stokens++;
   }
}


   /* Write all the data to the remote node.  buf must be DMA-able. */

int writeFully(ArgStruct *p, int node, void *buf, int nbytes,
                             unsigned int priority)
{
   stokens--;

/*fprintf(stderr, "%d has %d send tokens\n", p->myproc, stokens);*/

   if( priority == GM_LOW_PRIORITY ) {

      gm_send_to_peer_with_callback(gm_p, buf, 
              gm_min_size_for_length( nbytes ), nbytes, GM_LOW_PRIORITY, 
              p->prot.host_id[node], my_low_send_callback, NULL);

   } else {

      gm_send_to_peer_with_callback(gm_p, buf, 
              gm_min_size_for_length( nbytes ), nbytes, GM_HIGH_PRIORITY, 
              p->prot.host_id[node], my_high_send_callback, NULL);

   }

   return nbytes;
}
 
   /* Read all the data from the remote node.  buf must be DMA-able.
    * NOTE: readFully() will accept any message matching the size
    * and priority regardless of the source.  
    */

int readFully(ArgStruct *p, int node, void *buf, int nbytes, 
                            unsigned int priority)
{
   int bytesRead = 0, bytesLeft = nbytes;
   char *ptr = buf;
   gm_recv_event_t *e;


   gm_provide_receive_buffer(gm_p, buf, 
                             gm_min_size_for_length(nbytes), priority); 

   while( bytesLeft > 0 ){

      e = gm_receive(gm_p);

      switch( gm_ntoh_u8(e->recv.type) ){

         case GM_FAST_RECV_EVENT:
         case GM_FAST_HIGH_RECV_EVENT:
         case GM_FAST_PEER_RECV_EVENT:
         case GM_FAST_HIGH_PEER_RECV_EVENT:

            /* DDT - This mode is used for messages below 128 bytes */

            bytesRead = (int) gm_ntoh_u32 (e->recv.length);

            bcopy( gm_ntohp(e->recv.message), ptr, bytesRead);

            bytesLeft -= bytesRead; 
            ptr       += bytesRead;

            break; 

         case GM_RECV_EVENT:
         case GM_PEER_RECV_EVENT:
         case GM_HIGH_RECV_EVENT:
         case GM_HIGH_PEER_RECV_EVENT:

               /* DDT - This mode is used for large messages above 127 bytes */

            bytesRead = (int) gm_ntoh_u32 (e->recv.length);
            bytesLeft -= bytesRead;

            break;

         case GM_NO_RECV_EVENT:
            break;

         default:
            gm_unknown(gm_p,e);
      }
   }
   return nbytes;
}   

int readOnce(ArgStruct *p)
{
   int bytesRead = 0;
   gm_recv_event_t *e;


   e = gm_receive(gm_p);

   switch( gm_ntoh_u8(e->recv.type) ){

      case GM_FAST_RECV_EVENT:
      case GM_FAST_HIGH_RECV_EVENT:
      case GM_FAST_PEER_RECV_EVENT:
      case GM_FAST_HIGH_PEER_RECV_EVENT:

            /* DDT - This mode is used for messages below 128 bytes */

         bytesRead = (int) gm_ntoh_u32 (e->recv.length);

         bcopy( gm_ntohp(e->recv.message), p->r_ptr, bytesRead);

         p->bytesLeft -= bytesRead; 
         p->r_ptr     += bytesRead;

         break; 

      case GM_RECV_EVENT:
      case GM_PEER_RECV_EVENT:
      case GM_HIGH_RECV_EVENT:
      case GM_HIGH_PEER_RECV_EVENT:

            /* DDT - This mode is used for large messages above 127 bytes */

         bytesRead = (int) gm_ntoh_u32 (e->recv.length);

         p->bytesLeft -= bytesRead;
         p->r_ptr     += bytesRead;

         break;

      case GM_NO_RECV_EVENT:
         break;

      default:
         gm_unknown(gm_p,e);
   }

   return bytesRead;
}   

   /* Global sync with the master node, then local sync with your comm pair.
	* The global sync is done at GM_HIGH_PRIORITY to prevent confusion
	* with ping-pong traffic, then the local sync and following ping-pong
	* traffic is done at GM_LOW_PRIORITY.
	*/

void Sync(ArgStruct *p)
{
   int i;
   static int* sa;               /* sync array */


   if( sa == NULL ) sa = (int *) malloc( p->nprocs * sizeof(int) );

   for( i=0; i<p->nprocs; i++ ) sa[i] = 0;


      /* First do a global synchronization with the master proc 0 */

   if( p->master ) {

         /* Read from procs in the order that they arrive */

      for( i=1; i<p->nprocs; i++ ) {

         readFully( p, i, r, sizeof(int), GM_HIGH_PRIORITY );

         *r -= 1000;

         if( *r < 1 || *r > p->nprocs || sa[*r] != 0 ) {
            fprintf(stderr,"%d NetPIPE: error reading global sync value"
                           " from proc %d (%d)\n", p->myproc, i, *r);
            exit(0);
         }

         sa[*r]++;     /* mark that node as having responded */
      }

         /* Write back to each proc */

      for( i=1; i<p->nprocs; i++ ) {

         writeFully(p, i, s, sizeof(int), GM_HIGH_PRIORITY);

      }

   } else {

      writeFully(p, 0, s, sizeof(int), GM_HIGH_PRIORITY);
      readFully( p, 0, r, sizeof(int), GM_HIGH_PRIORITY);

      if( *r-1000 != 0 ) {
         fprintf(stderr,"%d NetPIPE: error reading global sync value"
                        " from proc 0 (%d)\n", p->myproc, *r);
         exit(0);
      }
   }

      /* Now do a local sync with your comm pair. The size of 12 bytes
       * is chosen to seperate these messages from the global syncs. */

   if( p->myproc != 0 && p->myproc != p->nprocs-1 ) {

      writeFully(p, p->dest, s, 3*sizeof(int), GM_HIGH_PRIORITY);
      readFully( p, p->dest, r, 3*sizeof(int), GM_HIGH_PRIORITY);

      if( *r-1000 != p->dest ) {
         fprintf(stderr,"%d NetPIPE: error reading pair sync value"
                        " from proc %d (%d)\n", p->myproc, p->dest, *r);
         exit(0);
      }
   }
}
    
/* This is used only for CPU workload measurements.  PrepareToReceive sets
 * up the receive buffer, then readOnce receives data in chunks.
 */

void PrepareToReceive(ArgStruct *p)
{
   gm_provide_receive_buffer(gm_p, p->r_ptr, 
                             gm_min_size_for_length(p->bufflen), GM_LOW_PRIORITY); 
}

void SendData(ArgStruct *p)
{
   writeFully( p, p->dest, p->s_ptr, p->bufflen, GM_LOW_PRIORITY);
}

/* This function reads the GM buffer once then tests for message
 * completion.  It may need to be adapted to read multiple times until
 * 0 bytes is read.
 */

int TestForCompletion( ArgStruct *p )
{
   readOnce( p );

   if( p->bytesLeft == 0 ) return 1;   /* The message is complete */
   else                    return 0;   /* The message is incomplete */
}
 
void RecvData(ArgStruct *p)
{
      /* Note: readFully will accept any message matching the size,
       * regardless of the source */

   if( p->bytesLeft > 0 )   /* protect against re-read in CPU workload measurements */
      readFully( p, p->source, p->r_ptr, p->bufflen, GM_LOW_PRIORITY);
   else
      p->r_ptr = p->r_ptr_saved;

} 

   /* Pass myproc with the data to guarantee node order */

void Gather( ArgStruct *p, double *buf)
{
   int i, proc, nbytes = sizeof(double);
   char *pbuf = (char *) buf;
   char *pwork = (char *) workspace;

   Sync( p );  /* Needed to seperate from ping-pong traffic */

   if( p->nprocs * (nbytes+sizeof(int)) > sizeof_workspace ) {
      fprintf(stderr,"%d pinned workspace is too small in Gather\n",p->myproc);
      exit(-1);
   }

   if( p->master ) {   /* Gather the data from the other procs */

      bcopy( &p->myproc, pwork, sizeof(int) );

      bcopy( pbuf, pwork+sizeof(int), nbytes );     /* copy proc 0's data */

      for( proc=1; proc<p->nprocs; proc++ ) {

         pwork += (sizeof(int) + nbytes);

         readFully( p, proc, pwork, sizeof(int) + nbytes, GM_LOW_PRIORITY);
      }

      pwork = workspace;

      for( i=0; i<p->nprocs; i++ ) {

         bcopy( pwork, &proc, sizeof(int));

         bcopy( pwork+sizeof(int), buf + proc * nbytes, nbytes );

         pwork += sizeof(int) + nbytes;
      }

   } else {   /* Write my section to the master proc 0 */

      pwork += p->myproc * (sizeof(int) + nbytes);
      pbuf  += p->myproc * (sizeof(int) + nbytes);

      bcopy( &p->myproc, pwork, sizeof(int) );

      bcopy( pbuf, pwork+sizeof(int), nbytes );  /* copy to pinned memory */

      writeFully( p, 0, pwork, sizeof(int)+nbytes, GM_LOW_PRIORITY);
   }
}


void Broadcast(ArgStruct *p, unsigned int *buf)
{
   int i, nbytes = sizeof(int);

   Sync( p );  /* Needed to seperate from ping-pong traffic */

   if( nbytes > sizeof_workspace ) {
      fprintf(stderr,"%d pinned workspace is too small in Broadcast\n",p->myproc);
      exit(-1);
   }

   if( p->master ) {

      bcopy( buf, workspace, nbytes );  /* copy to pinned memory */

      for( i=1; i<p->nprocs; i++ ) {

         writeFully( p, i, workspace, nbytes, GM_LOW_PRIORITY);

      }

   } else {

      readFully( p, 0, workspace, nbytes, GM_LOW_PRIORITY);

      bcopy( workspace, buf, nbytes );  /* copy from pinned memory */

   }
}


void CleanUp(ArgStruct *p)
{
   sleep(2); 
   gm_close(gm_p);
   gm_exit(GM_SUCCESS);
   gm_finalize();
}


void Reset(ArgStruct *p) { }

void MyMalloc(ArgStruct *p, int bufflen)
{  
  if((p->r_buff = (char *)gm_dma_malloc(gm_p, bufflen+MAX(p->soffset,p->roffset)))==(char *)NULL)
  {
      fprintf(stderr,"couldn't allocate memory\n");
      exit(-1);
  } 

  if(!p->cache)
    if((p->s_buff = (char *)gm_dma_malloc(gm_p, bufflen+p->soffset))==(char *)NULL)
    {
        fprintf(stderr,"Couldn't allocate memory\n");
        exit(-1);
    } 
}

void FreeBuff(char *buff1, char *buff2)
{
  if(buff1 != NULL)
    gm_dma_free(gm_p, buff1);

  if(buff2 != NULL)
    gm_dma_free(gm_p, buff2);
}

