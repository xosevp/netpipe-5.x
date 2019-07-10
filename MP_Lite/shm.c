/* MP_Lite message-passing library
 * Copyright (C) 1997-2004 Dave Turner and Xuehua Chen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Contact: Dave Turner - DrDaveTurner@gmail.com
 */

/* This file contains an SMP message-passing module for the MP_Lite 
 * library.  A single large shared-memory segment is used to pass
 * all message data.  Each proc has its own segment of the pool for
 * outgoing messages, and manages all the garbage cleanup on it.
 *
 * The headers are passed through FIFO pipes which provide low latency.
 *
 * smp_send()
 *   garbage cleanup in the shared-memory segment
 *   allocated space in the segment
 *   memcpy data to the segment
 *   write the message header to the destination FIFO
 *
 * smp_recv()
 *   check the message header queue for a match
 *     --> memcpy data, mark as free for reuse, and return
 *   else
 *   ->read the FIFO for a header, blocking until one is received
 *  ^  if it matches
 *  |    --> memcpy data, mark as free for reuse, and return
 *   --else store this header in the queue and read the next from the FIFO
 */

#include "mplite.h"    /* MP_memcpy() */
#include "globals.h"   /* Globals myproc, nprocs, mylog, hostname, logfile */

#include <sys/stat.h>
#include <fcntl.h>

/* Set the size of out-of-order message queue */  
#define MSG_Q_SIZE 20 
int q_size, sync_size, fifo_size, pool_size; // Total message pool size

#define GC 2

        /* define two ways of garbage collection 
         * GC == 1, do garbage collection of all nodes first 
         *       and then allocate a new space for a new msg  
         * GC == 2, do garbage collection and allocate a  
         *       new space at the same time        */

#if GC == 1 

  typedef struct Pool_entry
  {
    struct Pool_entry *next, 
                      *dnext, 
                      *prev;
    void              *dest_ptr;        /* dest pointer for 1-sided      */
    int32_t           src;
    int32_t           dest;             /* dest (needed by smp_send      */
    int32_t           nbytes;
    int32_t           tag;
    volatile int32_t  flag;
    int32_t           pad1,pad2,pad3;
  } Pool_entry;

#elif GC == 2

  typedef struct Pool_entry
  { 
    struct Pool_entry *next,            /* pointer to the next entry     */
                      *dnext,           /* next entry with same dest     */
                      *prev;            /* pointer to the previous entry */
    void              *dest_ptr;        /* dest pointer for 1-sided      */
    int32_t           src;              /* source of the message         */
    int32_t           dest;             /* dest (needed by smp_send      */
    int32_t           nbytes;           /* number of bytes to send out   */
    int32_t           tag;
    volatile int32_t  flag;             /* flag == 0  message not ready  */
                                        /* flag == 1  message ready      */
                                        /* flag == -1 message received   */
                                        /* flag == -2 prev(dest) --> dnext */
    int32_t           pad1,pad2,pad3;
  } Pool_entry; 

  Pool_entry * nil_entry; 

#endif

typedef struct
{
  unsigned int writes;         /* One-sided ops other nodes have completed */
  unsigned int reads;          /* One-sided ops this node has processed */
  int sync[2];
} OS_Grid_entry;


typedef struct MP_SMP_msg_q_entry
{
   int src;                           // src node
   int nbytes;                        // number of bytes in the message body
   int tag;                           // the message tag
   Pool_entry *entry;
   struct MP_SMP_msg_q_entry *next;   // next header
} MP_SMP_msg_q_entry;

#if GC == 1
  static Pool_entry *head_pool, *tail_pool;
#elif GC == 2
  static Pool_entry *current; 
#endif 

static MP_SMP_msg_q_entry *head_q, *tail_q;

static int b_exit;                                                

static int  pool_id = -1;
static char *pool_shm, *fifo_shm;
uint64_t offset;
static Pool_entry **pp_dhead_shm;
static Pool_entry *p_head_pool;
static OS_Grid_entry *p_onesided_grid;
static int onesided_grid_id;
static pid_t *p_pid_shm;

/* Prototypes */

void quit(int sig);



static struct sigaction sigact1;
/* static int n_buffered; */

/* WAIT determines how spinlock() is handled.
 *  YIELD uses a sched_yield() function,
 *  SIGNAL pauses until a SIGUSR2 signal comes in, 
 *  PAUSE calls an assembly level 'pause' operation,
 *  NOP calls an assembly level 'nop'.
 *
 * Since the 1-sided operations use SIGUSR2, do not define WAIT to SIGNAL.
 */

#define YIELD    1
#define SIGNAL   2
#define PAUSE    3
#define NOP 	 4

#define WAIT YIELD

#if WAIT == SIGNAL
  static struct sigaction sigact2;
  static sigset_t   sigusr2_mask, sigusr2_umask;   /* masks to block and unblock SIGUSR2 */
  static pid_t      *pid_shm;
  static int        pid_id = -1;
#endif

void quitall()
{
  b_exit=0;
  kill(0, SIGQUIT);
  exit(0);
}

void quit(int sig)
{
  fprintf(stderr, "proc %d received SIGQUIT\n", myproc);
  if(b_exit)
    exit(0); 
}

void wakeup(int sig) { }

// wrapper function of shmat() with shm_adr always null and shmflg 0

void* myshmat(int shmid)
{
  void *ptr;

  //fprintf(stderr, "proc %d in myshmat()\n", myproc);
  if( (ptr = shmat(shmid, NULL, 0)) == (void *) -1 )
  {
    fprintf(stderr, "node %d has shmat() error %d: %s\n", 
            myproc, errno, strerror(errno));
    quitall();
  }
  //fprintf(stderr, "proc %d leaving myshmat()\n", myproc);
//printf("%d myshmat() shmat *ptr = %p\n", myproc, ptr);
  return ptr;
}

void spinlock(volatile int *lock, int held)
{
  while (*lock == held)
  {

#if WAIT == YIELD 
    sched_yield();
#elif WAIT == PAUSE 
    asm volatile (
      "pause"
    );
#elif WAIT == NOP
    asm volatile (
      "nop"
    );
#elif WAIT == SIGNAL
     // Unblock SIGUSR2 and pause(atomic action),when SIGUSR2 comes, handle
     // the signal and block the SIGUSR2 again
    if (sigsuspend(&sigusr2_umask) != -1)
    { fprintf(stderr, "sigsuspend error");  quitall();}
#endif

  }
}

// initialization of smp module 

void init_smp()
{
  int i;
  MP_SMP_msg_q_entry *hdr;


  if(nsmp < 2) return;

  if(nsmp != nprocs) {
    lprintf("shm.c doesn't currently support a mixed tcp/shm environment\n");
    lprintf("Edit tcp.c to #undef USE_SHM\n");
    exit(-1);
  }
  b_exit=1;
  sigact1.sa_handler= quit;
  
  sigaction(SIGQUIT, &sigact1, NULL);

#if WAIT == SIGNAL
  /* set up two signal masks */
  sigemptyset(&sigusr2_mask);
  sigaddset(&sigusr2_mask, SIGUSR2);
  sigfillset(&sigusr2_umask);
  sigdelset(&sigusr2_umask, SIGQUIT);
  sigdelset(&sigusr2_umask, SIGINT);
  sigdelset(&sigusr2_umask, SIGUSR2);

  /* block SIGUSR2 */
  sigprocmask(SIG_SETMASK, &sigusr2_mask, NULL);
  sigact2.sa_handler= wakeup;
  sigaction(SIGUSR2, &sigact2, NULL);
#endif

     /* Set the process group id to that of mprun, 
      * which is the parent of the process */

  if(setpgid(0,getpgid(getppid()))<0)
  {
    perror("setpgid error");
    exit(-1);
  }

  q_size    = (4000/nsmp) * 4096;
  sync_size = ( (nsmp * sizeof(int) + 4095) / 4096 ) * 4096; // Just in case it's over a page
  fifo_size = sizeofFifo();

  pool_size = nsmp * q_size + sync_size + fifo_size; // nsmp queues then sync array

  lprintf("Creating the shared pool of %d Bytes (%d %d %d)\n",pool_size,q_size,sync_size,fifo_size);

  if( myproc == 0 ) {
    pool_id = shmget(100, pool_size, IPC_CREAT | 0666 );
  } else {
    pool_id = shmget(100, pool_size, 0666 );
  }

#if WAIT == SIGNAL
  if( myproc == 0 ) {
    pid_id  = shmget(101, nsmp*sizeof(pid_t),  IPC_CREAT | 0666 );
  } else {
    pid_id  = shmget(101, nsmp*sizeof(pid_t),  0666 );
  }
#endif

  if( (pool_id < 0) ) {
    fprintf(stderr, "node %d has shmget() error %d: %s\n", 
            myproc, errno, strerror(errno));
    fprintf(stderr, "%d has pool_id=%d\n", myproc, pool_id);
    quitall();
  }
  
  lprintf("Attaching to the shared-memory segments\n");

  pool_shm = myshmat( pool_id );

#if GC == 1

  /* Each SMP proc has its own section of the segment */

  head_pool = tail_pool = (Pool_entry *)(pool_shm + q_size * smp_rank[myproc]);
  head_pool->next = NULL; 

#elif GC == 2 

  nil_entry       = (Pool_entry *) (pool_shm + q_size * smp_rank[myproc]);
  nil_entry->next = nil_entry;
  nil_entry->prev = nil_entry;
  nil_entry->flag = 0;
  current = nil_entry;

#endif 

#if WAIT == SIGNAL
  pid_shm = myshmat( pid_id);
  pid_shm[smp_rank[myproc]] = getpid();
#endif

     /* Attach the segment to sync_array used in smp_sync and initialize */

  sync_array = (int *) (pool_shm + nsmp * q_size);

  sync_array[ smp_rank[myproc] ] = 0;

    /* Initialize fifo */

  fifo_shm = pool_shm + nsmp*q_size + sync_size;

  //lprintf("pool_shm = %p  fifo_shm = %p\n", pool_shm, fifo_shm);
  //lprintf("pool_shm = %p - %p\n", pool_shm, pool_shm + pool_size);
  //fprintf(stderr,"%d before init_fifo\n",myproc);
  init_fifo( fifo_shm );
  //fprintf(stderr,"%d after init_fifo\n",myproc);

  hdr = head_q = tail_q = malloc(MSG_Q_SIZE * sizeof(MP_SMP_msg_q_entry));    

  /* head_q won't contain a valid header. It is only the head of the queue */ 
  for(i = 0;i < MSG_Q_SIZE - 1; i++) {
    hdr->next= hdr + 1;
    hdr      = hdr + 1;
  } 
  hdr->next  = head_q;       /* last entry points to the head */

  /* n_buffered = 0;*/
  lprintf(" leaving init_smp()\n");
}

void smp_cleanup( )
{
  int i, err;
  struct shmid_ds ds;            
  
  if(nsmp < 2) return;

  if(smp_rank[myproc] == 0) {

    if( (err = shmctl(pool_id, IPC_RMID, &ds)) <0)
      errorx(errno, "shmctl(pool_id,IPC_RMID) error in smp_cleanup()");

#if WAIT == SIGNAL
    if( (err = shmctl(pid_id, IPC_RMID, &ds) ) <0)
      errorx(errno, "shmctl(pid_id,IPC_RMID) error in smp_cleanup()");
#endif
  }
}

// Send out a message 

void smp_send(struct MP_msg_entry *msg)
{

#if GC == 1 
  struct Pool_entry *prev_hdr, *hdr, *new_entry;   
  int size;
  
  dbprintf(" smp_send(& %d %d %d)\n", msg->nbytes, msg->dest, msg->tag );
  /* garbage collection   */

  prev_hdr = head_pool; 
  hdr = prev_hdr->next;
  while( hdr )
  {
    if( hdr->flag == -1 )
    { 
      dbprintf("   Cleaning shm garbage\n");
      if( hdr != tail_pool ) {
        prev_hdr->next = hdr->next;
      } else {
        tail_pool = prev_hdr;
        prev_hdr->next = NULL;
      }
    }
    else prev_hdr = hdr;  
    hdr = prev_hdr->next;
  } 

  /* allocate new space from the segment, rounded up to 8 bytes  */
  if( pool_shm + q_size * (smp_rank[myproc] + 1) 
        - (char *)(tail_pool + 1) - tail_pool->nbytes  
        > msg->nbytes + sizeof(Pool_entry)) {

    /* Add to the end of the queue  */

    new_entry          = (Pool_entry *)((char *)(tail_pool + 1) + 
		                                     tail_pool->nbytes); 
    new_entry->next    = NULL;
    tail_pool->next    = new_entry;
    tail_pool          = new_entry;

  } else {

    /* Otherwise try to insert the message in the middle somewhere  */

    prev_hdr = head_pool;
    hdr = prev_hdr->next;
    while( hdr != NULL )
    {
      if( (char *)hdr - (char *)(prev_hdr + 1) - prev_hdr->nbytes 
                                     > msg->nbytes + sizeof(Pool_entry))
      { 
        new_entry       = (Pool_entry *)( (char *)(prev_hdr + 1)
                                                     + prev_hdr->nbytes);
        prev_hdr->next  = new_entry;
        new_entry->next = hdr;
        break;

      } else {

        prev_hdr = hdr;
        hdr = hdr->next;
      }
    }  

    if( hdr==NULL)
    {
      fprintf(stderr, "Not enough memory!\n"); 
      quitall();
    }
  }

#elif GC == 2
  Pool_entry *next, *prev, *begin, *new_entry;

  /* garbage collection and allocate a space for a new message   */
  next = current->next;
  prev = current->prev;
  begin  = current; 

  while (1)
  {
    while (current->flag == -1)
    {
      /* if current message is received, collect it */
      prev->next = next;
      next->prev = prev;
      current     = prev;
      prev       = prev->prev;
    }

    while ( next->flag == -1)
    {
      /* if the next message is received, collect it */
      current->next      = next->next;
      next->next->prev = current;
      next             = next->next;
    }

    if (next == nil_entry)
    {
      /* if the current entry is the last entry in the pool */
      if ((char *)(nil_entry) + q_size - 1
                   - (char *)(current + 1) - current->nbytes
                                   > sizeof(Pool_entry) + msg->nbytes)
        break;     /* find enough space after the current entry */

      /* not enough space */
      current = nil_entry;
    }
    else
    {
      /* the current entry is a middle entry in the pool */
      if ((char *)next - 1 - (char *)(current + 1) - current->nbytes
                                       > sizeof(Pool_entry) + msg->nbytes)
        break;     /* find enough space after the current entry */

      /* not enough space */
      current = next;
    }

    if (current == begin)
    {
      /* looked through all the entry and cannot find an adequate space */
      fprintf(stderr, "Not enough memory to store the new message!");
      quitall();
    }
  }

  new_entry = (Pool_entry *) ((char *)(current + 1) + current->nbytes);
  current->next     = new_entry;
  new_entry->prev   = current;
  new_entry->next   = next;
  next->prev      = new_entry;
  current           = new_entry;
#endif 

  new_entry->flag   = 0;
  new_entry->nbytes = msg->nbytes;
  new_entry->src    = msg->src;
  new_entry->tag    = msg->tag;

  MP_memcpy(new_entry + 1, msg->buf, msg->nbytes);

  offset = (uint64_t)((char *)new_entry - pool_shm);
  FIFOwrite(msg->dest, offset); // Write the offset to the appropriate FIFO

  new_entry->flag   = 1;

#if WAIT == SIGNAL
  kill(pid_shm[smp_rank[msg->dest]], SIGUSR2);
#endif

  msg->nleft = 0;

}   

// Receive a message 

void smp_recv( struct MP_msg_entry *msg )
{
  MP_SMP_msg_q_entry *prev_hdr, *hdr ;
  Pool_entry         *recv_entry;
  static int n_buffered = 0; 
  int i;

  /* dbprintf(" smp_recv(& %d %d %d)\n", msg->nbytes, msg->src, msg->tag );*/
  
  for (i = 0; i < n_buffered; i++) /* Search the hdr queue for a match */
  {
    if (i==0)
    { 
      prev_hdr = head_q; 
      hdr = prev_hdr->next;
    }

    if( hdr->nbytes <= msg->nbytes &&
        hdr->src == msg->src &&
        (msg->tag < 0 || hdr->tag == msg->tag ) ) {

          /* Found a match in the queue, copy the data and do cleanup */

      dbprintf("   Found a match, copying %d bytes\n", hdr->nbytes);

      MP_memcpy(msg->buf, hdr->entry + 1, hdr->nbytes);

/* DDT - This still worries me, since n_buffered is also in the conditional
 * to end the loop, but Xuehua says this works.
 */
      n_buffered-- ;

      last_src = msg->src;
      last_tag = hdr->tag;
      last_size = hdr->nbytes;

      if ( hdr == tail_q ) tail_q = prev_hdr;
      else
      {
        prev_hdr->next = hdr->next;
        hdr->next = tail_q->next;
        tail_q->next = hdr;
      }

      /* Mark the segment section for garbage cleanup */
      hdr->entry->flag = -1;

      msg->nleft = 0;
      return;
    }
    else
    {
      prev_hdr = hdr;
      hdr = hdr->next;
    }
  }     

   // No match found in the queue, read the FIFO and push headers to
   // the queue until a match is found.
 
//  for (i = 0; i < n_buffered; i++)
  for( ; ; )
  {

    offset = FIFOread( msg->src );
    recv_entry = (Pool_entry *) (pool_shm + offset);

    if( recv_entry->nbytes <= msg->nbytes &&
        (msg->tag < 0 || recv_entry->tag == msg->tag ) ) {

      dbprintf("matched - copying %d bytes\n", recv_entry->nbytes);

      spinlock(&recv_entry->flag, 0);  /* wait until the message is ready */

      MP_memcpy(msg->buf, recv_entry + 1, recv_entry->nbytes);

      last_src  = msg->src;
      last_tag  = recv_entry->tag;
      last_size = recv_entry->nbytes;

         /* Mark the segment section for garbage cleanup by the source proc */

      recv_entry->flag = -1;

      msg->nleft = 0;

      return;

    } else {
      if (n_buffered == MSG_Q_SIZE)
      {
        fprintf(stderr, "Allocate larger queue in shm.c! myproc=%d\n", myproc);
        quitall();
      }
      tail_q = hdr;
      tail_q->tag    = recv_entry->tag;
      tail_q->nbytes = recv_entry->nbytes;
      tail_q->src    = recv_entry->src;
      tail_q->entry  = recv_entry;
      dbprintf("adding hdr(%d %d %d) to the queue\n",
                recv_entry->nbytes, msg->src, recv_entry->tag);
      n_buffered ++;
    }
  }
}


/*************************************/
/* One-sided additions by Adam Oline */
/*************************************/
void recv_shm_put_request(Pool_entry* p_hdr)
{
  int size;
  dbprintf("  processing one-sided put request\n");
  size = ((p_hdr->nbytes%8==0)?0:(8-p_hdr->nbytes%8)) + p_hdr->nbytes;
  MP_memcpy( p_hdr->dest_ptr, (char*)p_hdr - size, p_hdr->nbytes );

}

void recv_shm_get_request(Pool_entry* p_hdr, int src)
{
  int size;
  struct MP_os_get_hdr get_hdr;
  struct MP_msg_entry* reply;

  dbprintf("  processing one-sided get request\n");
  size = ((p_hdr->nbytes%8==0)?0:(8-p_hdr->nbytes%8)) + p_hdr->nbytes;
  MP_memcpy( &get_hdr, (char*)p_hdr - size, p_hdr->nbytes );

     /* Create new msg containing requested data, send back */

  reply = create_os_msg_entry(NULL, get_hdr.src_ptr, get_hdr.nbytes, 
                              smp_rank[myproc], src, -4, get_hdr.win_id);
  reply->trace_id = trace_start( get_hdr.nbytes, src );

  smp_os_send( reply );
  add_msgid( get_hdr.win_id, reply->id );
  MP_Wait( &reply->id ); /* This is OK in shm implementation, as opposed to
                            tcp, because data is never pushed to a buffer
                            since it is guaranteed to be put in shared memory
                            right away through smp_os_send. */

  dbprintf("  sent get reply\n");
}

void recv_shm_get_reply(Pool_entry* p_hdr, int src)
{
  int size;
  struct MP_get_complete* gc;
 
  dbprintf("  Processing one-sided get reply\n");

  size = ((p_hdr->nbytes%8==0)?0:(8-p_hdr->nbytes%8)) + p_hdr->nbytes;
  MP_memcpy( get_buf_addr_q[src][ get_buf_addr_q_head[src] ], 
             (char*)p_hdr - size, p_hdr->nbytes );  
 
  for(gc=get_complete_head[src];gc->val==1;gc=gc->next) ;
  dbprintf("  Setting get_completed_head[%d]->val=1\n", src);
  gc->val = 1;
  
  /* remove local get buffer addr from q */
  get_buf_addr_q_head[src] = (get_buf_addr_q_head[src] + 1) % GET_BUF_Q_SIZE;

}

void recv_shm_accum_request(Pool_entry* p_hdr, int src)
{
  int op, size;
  
  dbprintf("  Processing one-sided accum request(%d)\n", p_hdr->tag);

  op = -5 - p_hdr->tag; /* Accum ops should range from 0 to 11 */

  size = ((p_hdr->nbytes%8==0)?0:(8-p_hdr->nbytes%8)) + p_hdr->nbytes;
  do_accum_op( p_hdr->dest_ptr, (char*)p_hdr - size, op, p_hdr->nbytes );
}

void recv_shm_lock_request(Pool_entry* p_hdr, int src)
{
  int size;
  struct MP_lock_hdr lock_hdr;
  dbprintf("  Processing one-sided lock request\n");

  size = ((p_hdr->nbytes%8==0)?0:(8-p_hdr->nbytes%8)) + p_hdr->nbytes;
  MP_memcpy(&lock_hdr, (char*)p_hdr - size, p_hdr->nbytes);

  do_lock(src, lock_hdr.lock_type, lock_hdr.win_id, lock_hdr.lock_flag);
}

void recv_shm_active_target_sync(Pool_entry* p_hdr, int src)
{
  int active_targ_hdr[2];
  int size;

  dbprintf(  "  Processing one-sided active target synchronization\n");
  size = ((p_hdr->nbytes%8==0)?0:(8-p_hdr->nbytes%8)) + p_hdr->nbytes; 
  MP_memcpy(active_targ_hdr, (char*)p_hdr - size, p_hdr->nbytes);

  if(active_targ_hdr[1]==0) {
    dbprintf("    Got all of message, doing post\n");
    windows[active_targ_hdr[0]]->post_received[src] = 1;
    
  } else {
    dbprintf("    Got all of message, doing complete\n");
    windows[active_targ_hdr[0]]->post_sent[src] = 0;
  }
}

void service_os_request(int node)
{
  Pool_entry **pp_dhead, *p_hdr, *p_hdr_prev;
  OS_Grid_entry *entry;
  int serviced;

  dbprintf(" >service_os_request(%d)\n", node);

  if(node > smp_rank[myproc]) {
     pp_dhead = pp_dhead_shm    + node*(nsmp - 1) + smp_rank[myproc];
     entry    = p_onesided_grid + node*(nsmp - 1) + smp_rank[myproc];
  } else {
     pp_dhead = pp_dhead_shm    + node*(nsmp - 1) + smp_rank[myproc] - 1;
     entry    = p_onesided_grid + node*(nsmp - 1) + smp_rank[myproc] - 1;
  }

  p_hdr_prev = NULL;
  p_hdr      = *pp_dhead;
     
  while( p_hdr != NULL ) {
    serviced = 0;

    /* Call appropiate function depdending on type of one-sided request */
    if(p_hdr->tag == -2) { /* Incoming put request */
      recv_shm_put_request(p_hdr);
      serviced = 1;
    } else if(p_hdr->tag == -3) { /* Incoming get request */
      recv_shm_get_request(p_hdr, node);
      serviced = 1;
    } else if(p_hdr->tag == -4) { /* Incoming get reply */
      recv_shm_get_reply(p_hdr, node);
      serviced = 1;
    } else if(p_hdr->tag < -4 && p_hdr->tag > -17) {
      recv_shm_accum_request(p_hdr, node);
      serviced = 1;
    } else if(p_hdr->tag == -17) {
      recv_shm_lock_request(p_hdr, node);
      serviced = 1;
    } else if(p_hdr->tag == -18) {
      recv_shm_active_target_sync(p_hdr, node);
      serviced = 1;
    }

    /* If we serviced a one-sided request, then clean up message */
    if(serviced) {
      /* Update read count at src node */
      entry->reads++;

      /* Tell src I'm changing dnext */
      p_hdr->flag = -2;
      
      /* Remove msg from my destination list */
      if(p_hdr_prev)
        p_hdr_prev->dnext = p_hdr->dnext;
      else
        *pp_dhead = p_hdr->dnext;

      /* Mark as garbage */
      p_hdr->flag = -1;

    }

    /* Cycle through all messages in list */
    p_hdr_prev = p_hdr;
    p_hdr      = p_hdr->dnext;

  }

  dbprintf(" <service_os_request()\n");
}

void check_onesided_grid()
{
  int i;
  OS_Grid_entry* entry;
 
  for(i=0; i<nsmp; i++) if(i != smp_rank[myproc]) {

    if(smp_rank[myproc] < smp_rank[i])
      entry = p_onesided_grid + smp_rank[i]*(nsmp-1) + smp_rank[myproc];
    else
      entry = p_onesided_grid + smp_rank[i]*(nsmp-1) + smp_rank[myproc] - 1;

    if(entry->reads < entry->writes) {
      service_os_request( i );
    }
  }
}

void handle_onesided_sig(int sig)
{
#if ! defined (POSIX_SIGNALS)    /* POSIX does this automatically */
   Block_Signals();
#else
   already_blocking++; /* Protect from other functions unblocking signals */   
#endif

  dbprintf("*** Received SIGUSR2 signal\n");
  check_onesided_grid();

#if ! defined (POSIX_SIGNALS)    /* POSIX does this automatically */
   Unblock_Signals();
#else
   already_blocking--; /* ok to unblock now */
#endif
  dbprintf("*** Signals reenabled in SIGUSR2 signal handler\n");
}

void shm_onesided_init()
{
  int i;
  struct sigaction sigact22;

  dbprintf("  Doing one-sided shared memory initializtion\n");

     /* Register signal handler for one-sided messages */

  sigaddset( &sig_mask, SIGUSR2);
  sigact22.sa_handler = handle_onesided_sig;
  sigaction(SIGUSR2, &sigact22, NULL);

     /* Create nsmp x nsmp-1 grid of pair of integers */

  if( myproc == 0 ) {
    onesided_grid_id = 
       shmget(103, nsmp*(nsmp-1)*sizeof(OS_Grid_entry), 0660 | IPC_CREAT );
  } else {
    onesided_grid_id = 
       shmget(103, nsmp*(nsmp-1)*sizeof(OS_Grid_entry), 0660);
  }

  if( onesided_grid_id < 0 ) {
    fprintf(stderr, "shmget() error\n");
    quitall();
  }

  p_onesided_grid = (OS_Grid_entry*) myshmat(onesided_grid_id);


     /* Initialize one-sided grid */

  for(i=0;i<nsmp*(nsmp-1);i++)
    p_onesided_grid[i].sync[0] = p_onesided_grid[i].sync[1] = 
      p_onesided_grid[i].writes = p_onesided_grid[i].reads = 0;
              
  smp_sync(); /* Make sure we sync here through tcp so the smp sync
                 doesn't get messed up (e.g. one process calls MP_Sync before
                 the other has initialized one-sided grid data */

  dbprintf("  Done with one-sided init\n");
}

void shm_onesided_cleanup()
{
  int err;
  struct shmid_ds ds;

  if(smp_rank[myproc] == 0)
  {
    if( (err = shmdt(p_onesided_grid)) < 0 )   
      errorx(errno, "shmdt() error in smp_cleanup()");
    if( (err = shmctl(onesided_grid_id, IPC_RMID, &ds) ) < 0 )
      errorx(errno, "shmctl(IPC_RMID) error in smp_cleanup()");
  }
}

/* Signals must be blocked while in smp_os_send to insure atomic access to
   'reads' count variable of grid entry, because the SIGUSR2 signal handler
   may also update the 'reads' variable.  Currently smp_os_send is only 
   called by One-sided ops in tcp.c, so we are safe since these block sigs. */
void smp_os_send(struct MP_msg_entry* msg)
{
  OS_Grid_entry* entry;
 
  dbprintf(" >smp_os_send\n");

  smp_send(msg);

  /* Set entry to src node's row, dest node's column */
  if(msg->dest < smp_rank[myproc])
    entry = p_onesided_grid + smp_rank[myproc]*(nsmp-1) + smp_rank[msg->dest];
  else
    entry = p_onesided_grid + smp_rank[myproc]*(nsmp-1) + smp_rank[msg->dest] - 1;

  if(entry->writes == entry->reads)
    entry->writes = entry->reads = 0; /* This assignment must be atomic */
  else if(entry->writes == UINT_MAX) {
    /* We should block here until dest proc catches up, then reset to 0 */
    while(entry->writes > entry->reads) sched_yield();
    entry->writes = entry->reads = 0; /* This assignment must be atomic */
  }

  entry->writes++;

  /* Send signal to notify dest that a new message is in shared mem,
     unless we're sending a reply to a get request.  In that case, the dest 
     node will spin through check_onesided_grid() when MP_Wait is called, 
     until it picks up our reply by itself */
  
  if(msg->tag != -4)
    kill(p_pid_shm[smp_rank[msg->dest]], SIGUSR2);

  dbprintf(" <smp_os_send\n");
}

void smp_os_fence()
{
  OS_Grid_entry* entry;
  int i;

  
  dbprintf(" >smp_os_fence\n");
  smp_sync();

  for(i=0; i<nsmp; i++) if(i != smp_rank[myproc]) {

    if(smp_rank[myproc] < smp_rank[i])
      entry = p_onesided_grid + smp_rank[i]*(nsmp-1) + smp_rank[myproc];
    else
      entry = p_onesided_grid + smp_rank[i]*(nsmp-1) + smp_rank[myproc] - 1;
    
    /* Wait for myself to complete all incoming one-sided msgs from proc i */
    while(entry->reads < entry->writes)  sched_yield();
      
  }

  dbprintf(" <smp_os_fence\n");

}


/* Flip the byte for my proc to let others know I'm in the sync routine.
 * Then block until all others have flipped their bytes.
 */

void smp_sync()
{
   static int sync_byte = 0;
   int i;

   if(nsmp < 2) return;

   dbprintf(" >smp_sync\n");

      /* Flip my sync_byte to let other procs know that I am in the sync */

   sync_byte = (sync_byte+1) % 10;

   sync_array[ smp_rank[myproc] ] = sync_byte;


      /* Now block until all procs have flipped their sync_bytes.
       * If two subsequent syncs are done, it is possible for the
       * second to have some procs flip again, so check for that
       * as well.  */

   for( i=0; i<nsmp; i++) {

      while( sync_array[ i ] !=  sync_byte        &&
             sync_array[ i ] != (sync_byte+1)%10 ) sched_yield();

   }

   dbprintf(" <smp_sync\n");
}

