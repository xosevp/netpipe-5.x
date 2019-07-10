/*    Xuehua Chen and Dave Turner, Summer of 2002, Ames Lab - ISU
 *
 * This file contains an SMP message-passing module for the MP_Lite 
 * library.  A single large shared-memory segment is used to pass
 * all message data.  A doubly linked list of messages and data are
 * spread through the segment.  Semaphores are used to lock other
 * processes out only when the linked list needs to be changed.
 *
 */

#include "mplite.h"
#include "globals.h"   /* Globals myproc, nprocs, mylog, hostname, logfile */

/* For system fifo */ 
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#define POLL     1 
#define SIGNAL   2

#define WAIT POLL 

#define POOL_SIZE 10000000

#define GC       2          /* 
			     * Define two ways of garbage collection 
                             * GC == 1, do garbage collection of all nodes first
                             *       and then allocate a new space for a new msg
                             * GC == 2, do garbage collection and allcate a
                             *       new space at the same time
                             */


#if GC == 1
  typedef struct Pool_entry
  {
    struct Pool_entry *next;
    int               nbytes;
    int               tag;
    int               flag;
    int               src;
  } Pool_entry;
  static Pool_entry *head_pool, *tail_pool;

#elif GC == 2

  typedef struct Pool_entry
  {
    int    tag;         
    struct Pool_entry *next;            /* point to the next entry       */
    struct Pool_entry *prev;            /* point to the previous entry   */
    int    nbytes;                      /* number of bytes to send out   */    
    int    flag;                        /* flag == 0  message not ready  */ 
                                        /* flag == 1  message ready      */  
                                        /* flag == -1 message received   */ 
    int    src;                         /* source of the message         */ 
  } Pool_entry;
  struct Pool_entry *head_pool;
  static Pool_entry **current; 
#endif

typedef struct MP_SMP_msg_q_entry
{
  int src;                              /* src node                      */
  int nbytes;                           /* size of the message           */
  int tag;                              /* the tag  of the message       */
  Pool_entry *entry;
  struct MP_SMP_msg_q_entry *next;      /* the next header               */
} MP_SMP_msg_q_entry;


static MP_SMP_msg_q_entry *head_q, *tail_q;



static int b_exit;
static int pool_id, 
	   curr_id;  


static Pool_entry *pool_shm;

/**************** signal related **********************/
static struct sigaction sigact1;

#if WAIT == SIGNAL
  static struct sigaction sigact2;
  static sigset_t   sigusr2_mask, sigusr2_umask;   /* masks to block and unblock SIGUSR2 */ 
  static pid_t      *pid_shm;
  static int        pid_id;  
#endif

void print_pool();
void print_queue();

#if GC == 1 

void print_pool()
{
  Pool_entry *entry; 

  entry = head_pool;
  fprintf(stderr, "proc %d: pool head %d, next %d\n", 
		    myproc, entry, entry->next); 

  for (entry = head_pool->next; entry != NULL; entry = entry->next)  
  {
    fprintf(stderr, "proc %d: pool entry %d, src %d, size %d, tag %d, flag %d, next %d\n", 
		    myproc, entry, entry->src, entry->nbytes, entry->tag, entry->flag, entry->next); 
  }
}

#elif GC == 2

void print_pool()
{
  Pool_entry *entry;
  fprintf(stderr, "proc %d: pool current entry is %d\n", myproc, *current);
  fprintf(stderr, "proc %d: pool head %d\n", myproc, head_pool);

  for (entry = head_pool->next; entry != head_pool; entry = entry->next)     
  { 
    fprintf(stderr, "proc %d: pool entry %d, src %d, size %d, tag %d, flag %d, next %d, prev %d,\n", 
          myproc, entry, entry->src, entry->nbytes, entry->tag, entry->flag, entry->next, entry->prev); 
  }
}
#endif 

void print_queue()
{
  MP_SMP_msg_q_entry *hdr;
  fprintf(stderr, "proc %d: queue head %d, next %d\n", myproc, head_q, head_q->next);
  for (hdr = head_q->next; hdr != tail_q->next; hdr = hdr->next)
  {
    fprintf(stderr, "proc %d: queue entry %d, address %d, src %d, nbytes %d, tag %d, next %d \n",
                                 myproc, hdr, hdr->entry, hdr->src, hdr->nbytes, hdr->tag, hdr->next);
  }
}

double Time()
{
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return ((double) tp.tv_sec + (double) tp.tv_usec * 1e-6);
}

void quitall()
{
  b_exit=0;
  fprintf(stderr,"\nquitall\n");
  kill(0, SIGQUIT);
  exit(0);
}

void quit(int sig)
{
  fprintf(stderr, "proc %d received SIGQUIT\n", myproc);
  print_pool();
  print_queue(); 
  smp_cleanup();
  if(b_exit) exit(0); 
}

void wakeup(int sig)
{
  ;
}

/*
 * wrapper function of shmget
 */
void MP_shmget(int *id, key_t key, int size, int shmflg)
{
  if( (*id = shmget(key, size, shmflg)) < 0 )
  {
    fprintf(stderr, "node %d has shmget() error at key %d: %s", myproc, key, strerror(errno));
    quitall();
  }
}

/*
 * wrapper function of shmat
 */
void MP_shmat(void **ptr, int shmid, const void *shmaddr, int shmflg)
{
  if( (*ptr = shmat(shmid, shmaddr, 0)) == (void *) -1 )
  {
    fprintf(stderr, "node %d has shmat() error: %s", myproc, strerror(errno));
    quitall();
  }
}

/*
 * wrapper function for sem_get
 */
void MP_semget(int *id, key_t key, int size, int semflg)
{
  if( (*id = semget(key, size, semflg)) < 0 )
  {
    fprintf(stderr, "node %d has semget() error: %s", myproc, strerror(errno));
    quitall();
  }
}

void  init_smp()
{
  MP_SMP_msg_q_entry *hdr;
  int i;
  double t1, t2; 

  if(nsmp < 2) return;

  if(nsmp != nprocs) {
    lprintf("shm.c doesn't currently support a mixed tcp/shm environment\n");
    lprintf("Edit tcp.c to #undef USE_SHM\n");
    exit(-1);
  }


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

  b_exit=1;
  sigact1.sa_handler= quit;
  sigaction(SIGQUIT, &sigact1, NULL);

  MP_Sync();

   /* Set the process group id to that of mprun, which is the parent of the process */ 
  if(setpgid(0,getpgid(getppid()))<0) 
  {
    perror("setpgid error");
    exit(-1);
  }

  MP_Sync();


  if(smp_rank[myproc]==0)
  {
    
    MP_shmget(&pool_id, 500, POOL_SIZE,            0660 | IPC_CREAT | IPC_EXCL);
    MP_shmget(&curr_id, 600, sizeof(Pool_entry *), 0660 | IPC_CREAT | IPC_EXCL); 

#if WAIT == SIGNAL 
    MP_shmget(&pid_id,  700, nsmp*sizeof(pid_t), 0660 | IPC_CREAT | IPC_EXCL);
#endif

    allocate_fifo();
    allocate_lock();  
/*    allocate_lock(0, nsmp);  */
  }

  MP_Sync();  

  if(smp_rank[myproc]!=0)
  {
    MP_shmget(&pool_id, 500, POOL_SIZE,              0660 | IPC_CREAT);
    MP_shmget(&curr_id, 600, sizeof(Pool_entry *),   0660 | IPC_CREAT); 

#if WAIT == SIGNAL
    MP_shmget(&pid_id,  700, nsmp*sizeof(pid_t), 0660 | IPC_CREAT);
#endif 

    allocate_fifo();
    allocate_lock();
/*    allocate_lock(smp_rank[myproc], nsmp);*/
  }

  MP_shmat((void **)&pool_shm, pool_id, NULL, 0);

#if GC == 2 
  MP_shmat((void **)&current,  curr_id, NULL, 0); 
#endif

#if WAIT == SIGNAL 
  MP_shmat((void **)&pid_shm , pid_id,  NULL, 0); 
  pid_shm[smp_rank[myproc]] = getpid();
#endif

  init_lock();
  init_fifo();


#if GC == 1

  /* Each SMP proc has its own section of the segment */

  head_pool = tail_pool = pool_shm;
  head_pool->next = NULL;

#elif  GC == 2
      
  head_pool       = pool_shm;      
  head_pool->next = pool_shm;
  head_pool->prev = pool_shm; 
  head_pool->flag = 0;       
  *current        = pool_shm;

#endif

  /* initialize out of order buffer queue */ 
  hdr = head_q = tail_q = malloc(100*sizeof(MP_SMP_msg_q_entry));
  /* head_q won't contain a valid header. It is only the head of the queue */
  for(i=0;i<99;i++)
  {
    hdr->next=hdr+1;
    hdr=hdr+1;
  }
  hdr->next=head_q;
  
  puts("smp_init done");

  if ( myproc == 0 )
  {
    t1 = Time();
    for ( i = 0; i < 1000; i ++ )
    {
      lock_pool();
      unlock_pool();
    }
    t2 = Time();
    fprintf(stderr, "lock and unlock takes %lf us ", (t2-t1)/1000*1.0e6);
  }
}

void smp_cleanup()
{
  int i, err;
  struct shmid_ds ds;

  if( nsmp < 2 ) return;

  if(smp_rank[myproc] == 0)
  {
    if( (err = shmctl(pool_id, IPC_RMID, &ds)) <0)
      errorx(errno, "shmctl(IPC_RMID) error in smp_cleanup()");
    if( (err = shmctl(curr_id, IPC_RMID, &ds)) <0) 
      errorx(errno, "semctl(IPC_RMID) error in smp_cleanup()");

#if WAIT == SIGNAL
    if( (err = shmctl(pid_id, IPC_RMID, &ds) ) <0)
      errorx(errno, "shmctl(IPC_RMID) error in smp_cleanup()");
#endif 

    free_lock();
  }

  freefifo();
  exit(0);
}

void spinlock(volatile int *lock, int held)
{
  int i; 
  while (*lock == held)
  {
#if WAIT == POLL 
    short_wait();
#elif WAIT == SIGNAL
    /* Unblock SIGUSR2 and pause(atomic action),when SIGUSR2 comes, handle
              the signal and block the SIGUSR2 again       */
    if (sigsuspend(&sigusr2_umask) != -1)
    { fprintf(stderr, "sigsuspend error");  quitall();}
#endif
  } 
}

void smp_recv( struct MP_msg_entry *msg )
{
  int i;
  Pool_entry * recv_entry;
  MP_SMP_msg_q_entry *prev_hdr, *hdr ;

  /* check through the buffered messages first  */
  dbprintf("smp_recv: src %d, size %d, tag %d\n", msg->src, msg->nbytes, msg->tag);
  prev_hdr = head_q;
  hdr = prev_hdr->next;

  while( hdr != tail_q->next ) /* Search the hdr queue for a match  */
  {
    if( hdr->nbytes <= msg->nbytes &&
        hdr->src == msg->src &&
        (msg->tag < 0 || hdr->tag == msg->tag ) ) {

      /* Found a match in the queue, copy the data and do cleanup  */

      dbprintf("   Found a match in the header queue, copying %d bytes\n", hdr->nbytes);

      MP_memcpy(msg->buf, (char *)(hdr->entry) + 128, hdr->nbytes);

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

      /* Mark the segment section for garbage cleanup  */
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

  for ( i = 0; i < 1000; i ++ )
  {
    Read(msg->src, (void **) &recv_entry);
    if( recv_entry->nbytes <= msg->nbytes &&
        recv_entry->src == msg->src && 
        (msg->tag < 0 || recv_entry->tag == msg->tag ) ) {

      dbprintf("matched - copying %d bytes\n", recv_entry->nbytes);

      spinlock(&recv_entry->flag, 0);  /* wait until the message is ready  */

      MP_memcpy(msg->buf, (char *)recv_entry + 128, recv_entry->nbytes);

      last_src  = msg->src;
      last_tag  = recv_entry->tag;
      last_size = recv_entry->nbytes; 

      /* Mark the segment section for garbage cleanup by the source proc  */

      recv_entry->flag = -1;

      msg->nleft = 0;

      return;

    } else {
      
      if ((hdr = tail_q->next) == head_q) 
      {
        fprintf(stderr, "Need to allocate a larger queue in shm.c! myproc=%d\n", myproc);
        quitall(); 
      } 
      tail_q         = tail_q->next;
      tail_q->tag    = recv_entry->tag;
      tail_q->nbytes = recv_entry->nbytes;
      tail_q->src    = recv_entry->src;
      tail_q->entry  = recv_entry;
      dbprintf("adding hdr(%d %d %d) to the queue\n",
                recv_entry->nbytes, msg->src, recv_entry->tag);
    }
  }
}

#if GC == 1
void smp_send(struct MP_msg_entry *msg)
{
  struct Pool_entry *prev_hdr, *hdr, *new_entry;
  int size;

  dbprintf("smp_send: dest %d, size %d, tag %d\n", msg->dest, msg->nbytes, msg->tag );

  prev_hdr = head_pool;

  lock_pool();

  /* garbage collection */
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

  /* allocate new space from the segment, rounded up to 8 bytes */
  if( (char *)pool_shm + POOL_SIZE
              - (char *)(tail_pool + 1) - tail_pool->nbytes
		        > msg->nbytes + sizeof(Pool_entry)) 
  {
    /* Add to the end of the queue */
    new_entry          = (Pool_entry *)((char *)(tail_pool + 1) +
                                                       tail_pool->nbytes);
    new_entry->next    = NULL;
    tail_pool->next    = new_entry;
    tail_pool          = new_entry;

  } 
  else
  {

    /* Otherwise try to insert the message in the middle somewhere */
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

      } 
      else 
      {
        prev_hdr = hdr;
	hdr = hdr->next;
      }
    }

    if( hdr==NULL)
    {
      fprintf(stderr, "Not enough memory!\n");
      print_pool();
      quitall();
    }
  }

  new_entry->flag  = 0; 
  new_entry->nbytes = msg->nbytes;  /* Needs to be locked */

  /* Unlock the pool   */
  unlock_pool();  

  new_entry->src    = msg->src;
  new_entry->tag    = msg->tag;

  Write(msg->dest, new_entry);

  MP_memcpy((char *)new_entry + 128, msg->buf, msg->nbytes);

  new_entry->flag   = 1;

#if WAIT == SIGNAL
  kill(pid_shm[smp_rank[msg->dest]], SIGUSR2);
#endif

  msg->nleft  = 0;
}

#elif GC == 2

void smp_send(struct MP_msg_entry *msg)
{
  Pool_entry *p_next, *p_prev, *begin, *new_entry;
  char *tmp;

  /* lock the pool  */
  dbprintf("smp_send: dest %d, size %d, tag %d\n", msg->dest, msg->nbytes, msg->tag );
  lock_pool();


  /* garbage collection and allocate a space for a new message   */
  p_next = (*current)->next;
  p_prev = (*current)->prev;
  begin  = *current;

  while (1)
  {
    while ((*current)->flag == -1)
    {
      /* if current message is received, recollect it  */
      p_prev->next = p_next;
      p_next->prev = p_prev;
      *current     = p_prev;
      p_prev       = p_prev->prev;
    }

    while ( p_next->flag == -1)
    {
      /* if the next message is received, recollect it    */
      (*current)->next   = p_next->next;
      p_next->next->prev = *current;
      p_next             = p_next->next;
    }

    tmp = (char *) ((((int) ((char *)(*current) + 128 + (*current)->nbytes)) + 127) & -128);
    if (p_next == head_pool)
    {
      /* if the current entry is the last entry in the pool  */
      if ((char *)(head_pool) + POOL_SIZE
	                     - tmp > 128 + msg->nbytes)
	break;    /* find enough space after the current entry    */

      /* not enough space   */
      *current = head_pool;
    }
    else
    {
      /* the current entry is a middle entry in the pool  */
      if ((char *)p_next - tmp > 128 + msg->nbytes)
        break;     /* find enough space after the current entry    */

      /* not enough space  */
      *current = p_next;
    }

    if (*current == begin)
    {
      /* looked through all the entry and cannot find an adequate space  */
      fprintf(stderr, "Not enough memory to store the new message!");
      quitall();
    }
  }

  new_entry        = (Pool_entry *)tmp;
  (*current)->next = new_entry;
  new_entry->prev  = *current;
  new_entry->next  = p_next;
  p_next->prev     = new_entry;
  *current         = new_entry;

  new_entry->flag  = 0; 
  new_entry->nbytes = msg->nbytes;  /* Needs to be locked */

  /* Unlock the pool   */
  unlock_pool();  

  new_entry->src    = msg->src;
  new_entry->tag    = msg->tag;

  Write(msg->dest, new_entry);

  MP_memcpy((char *)new_entry + 128, msg->buf, msg->nbytes);

  new_entry->flag   = 1;

#if WAIT == SIGNAL
  kill(pid_shm[smp_rank[msg->dest]], SIGUSR2);
#endif

  msg->nleft  = 0;

}
#endif
