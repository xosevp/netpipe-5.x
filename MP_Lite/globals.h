/* MP_Lite message-passing library
 * Copyright (C) 1998-2004 Dave Turner
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 * Contact: Dave Turner - turner@ameslab.gov
 */

/* globals.h - header file for using mplite stuff */

#ifndef MP_GLOBALS_H
#define MP_GLOBALS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#if defined (DARWIN)
#include <sys/malloc.h>
#else
/*#include <malloc.h>*/
#endif

#include <math.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#if defined (sgi)
#include <sys/resource.h>
#endif

#include <sys/types.h>

#include <signal.h>

#include <sys/uio.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/* OSF 4.0 version only works with POSIX signals
 * most other systems work with either POSIX or BSD
 */
#define POSIX_SIGNALS
/*#define _BSD_SIGNALS*/

#define PANIC_TIME 20.0     /* seconds to wait before logging a warning */
#define BAIL_TIME  120.0    /* seconds to wait before aborting */
#define USER_FRIENDLY
#define MAX_PROCS 1024
#define MAX_NICS 4
#define MAX_WINS 10000
#define MIN_SPLIT 1500
#define MTU_MIN 8192
#define GET_BUF_Q_SIZE 10000

#define TCP_PORT 5800
#define TCP_PORT_MAX 5999

#define MAX_MSGS 10000

#if ! defined( MIN )
#define MIN(a,b) a < b ? a : b
#endif

#define HDRSIZE (int)(sizeof(struct MP_msg_header))

#if defined (_CRAYMPP)
#define TIMER_RESOLUTION 0.000001

#else
#define TIMER_RESOLUTION 0.001

#endif

struct MP_msg_entry
{
   int nbytes;              /* Total number of bytes to send or receive */
   int src;                 /* source node number (0..nprocs-1) */
   int tag;                 /* Message tag for sorting incoming messages */
   char* dest_ptr;          /* pointer for one-sided operations */ 
   int dest;                /* destination node number (0..nprocs-1) */
   void *next;              /* pointer to the next hdr in send_q/recv_q list */
   void *mnext;             /* pointer to the next hdr in msg_q linked list */
   char *buf;               /* Beginning of the buffer */
   char *buffer[MAX_NICS];  /* Send buffer pointers for each NIC, if needed */
   char *cur_ptr;           /* Next data to send/recv */
   char *ptr[MAX_NICS];     /* current pointers for each NIC */
   char *phdr;              /* Current header pointer */
   int nleft;               /* Total number of bytes left to send/recv */
   int nbl[MAX_NICS];       /* Number of bytes left to send for each NIC */
   int nhdr;                /* Number of bytes left to send/recv in the hdr */
   int mtu;                 /* Packet size for splitting across multi-nics */
   int send_flag;           /* 1 ==> send, 2 ==> buffer me, 3 ==> buffered */
                            /* 4 ==> user will not call wait, needs cleanup */
   int recv_flag;           /* 1 ==> recv, 2 ==> do not check msg_q */
   int one_sided;           /* 0 ==> not one-sided msg, 1 ==> one-sided msg */
   int window_id;           /* Window id this 1-sided msg is associated with */
   int trace_id;
   int id;
   int stats;
#if defined (INFINIBAND)
   int ib_flag;             /* Flag for use in Inifiniband module */
   void* ib_ptr;            /* Pointer for use in Infiniband module */
   int ib_index;            /* Index of eager buffer used for message */
   int ib_key;              /* access key for IB memory regions */
#endif
};

struct MP_msg_header  /* The order must match the start of MP_msg_entry */
{
   int nbytes;        /* number of bytes in the message body */
   int src;           /* source node number (0.. nprocs-1) */
   int tag;           /* the message tag */
   char* dest_ptr;    /* pointer for one-sided operations */
};

struct recv_buf_type {
   char *base;
   char *ptr;
   int  len;
};

struct trace_element {
   int node;
   int nbytes;
   int npackets;
   double start;
   double end;
};

/* Structure definitions needed for both tcp.c and shm.c */

struct MP_os_get_hdr {
  void* src_ptr;
  int nbytes;
  int win_id;
};

struct MP_get_complete {
  int val; /* val = 0 -> not completed; val = 1 -> completed */
  int mid; /* message id */
  struct MP_get_complete* next;
};

struct MP_lock_hdr {
  int win_id;
  int lock_type;
  void* lock_flag;
};

struct win_lock_q_node {
  int src;         /* proc requesting lock                                 */
  int type;        /* type of lock (0=>shared, 1=>exclusive)               */
  void* lock_flag; /* address of flag to write to when lock req is granted */
};

struct MP_Win_elt {
  void* base;
  int size;
  int disp_unit;
};

#define LOCK_Q_SIZE 1000

struct MP_Window {

  /* Used for MP_Win_start,MP_Win_complete,MP_Win_post,MP_Win_wait  */
  int* post_sent; 
  int* post_received;

  /* Used for MP_Win_lock, MP_Win_unlock */
  int locked;   /* 0 => not locked, 1 => shared lock, 2 => exclusive lock */

  /* Used for both of above synchronizations */
  int count;  /* count of locks (limit: shared => nprocs, exclusive => 1)
                 or count of number of procs either posted or started */

  /* queue for lock requests, used when window already locked */
  struct win_lock_q_node lock_q[LOCK_Q_SIZE];
  int lock_q_head;
  int lock_q_tail;

  struct MP_Win_elt* node;
};

#if defined MPLITE_MAIN     /* Global variables set in mplite.c */

  int myproc=-1 ,nprocs, nsmp = 0;
  int true = 1, false = 0;
  FILE *mylog = NULL;  /* .mplite.mymachine file to log the current state */
  char *interface[MAX_PROCS][MAX_NICS], hostname[MAX_PROCS][100];
  char myhostname[100], filename[100], prog_name[100];
  int buffer_size = 4194304; /* buf size for regular and one-sided sockets */
  int small_buffer_size = 1500; /* buf size for sync sockets */
  int *MP_all_mask;
  int smp_rank[MAX_PROCS];
/*  struct shm_entry_type shm_cleanup_list = { NULL, NULL };*/
  struct recv_buf_type **recv_buf;
  int last_tag = 0, last_src = 0, last_size = 0;
  int n_send = 0, n_recv = 0, n_send_packets = 0, n_recv_packets = 0;
  int nb_send = 0, nb_recv = 0, nb_send_buffered = 0, nb_recv_buffered = 0;
  int n_recv_buffered = 0, n_send_buffered = 0;
  int n_interrupts = 0, n_sockets_serviced = 0;
  int buffer_orig = 0;
  int nics;
  int salen = sizeof(struct sockaddr_in);
  struct sockaddr_in serv_addr;
  char cbuf[100];
  struct MP_msg_entry  *msg_list[MAX_MSGS*2];
  struct MP_msg_header **hdr;
  struct MP_msg_entry msg_null = {
         0,                                   /* nbytes */
         -1,                                  /* src */
         -1,                                  /* tag */
         NULL,                                /* dest_ptr */
         -1,                                  /* dest */
         NULL,                                /* *next */
         NULL,                                /* *mnext */
         NULL,                                /* *buf */
         {NULL, NULL, NULL, NULL},            /* *buffer[MAX_NICS=4] */
         NULL,                                /* *cur_ptr */
         {NULL, NULL, NULL, NULL},            /* *ptr[MAX_NICS=4] */
         NULL,                                /* *phdr */
         0,                                   /* nleft */
         {0, 0, 0, 0},                        /* nbl[MAX_NICS=4] */
         0,                                   /* nhdr */
         0,                                   /* mtu */
         0,                                   /* send_flag */
         0,                                   /* recv_flag */
         0,                                   /* one_sided */
         -1,                                  /* window_id */
         -1,                                  /* trace_id */
         -1,                                  /* id */
         0,                                   /* stats */
#if defined (INFINIBAND)
         0,                                   /* ib_flag */
         NULL,                                /* *ib_ptr */
         -1,                                  /* ib_index */
         -1,                                  /* ib_key */
#endif
         };

/************************* added by Adam Oline **********************/
  void* get_buf_addr_q[MAX_PROCS][GET_BUF_Q_SIZE];
  int get_buf_addr_q_head[MAX_PROCS];
  int get_buf_addr_q_tail[MAX_PROCS];
  int already_blocking = 0;
  struct MP_Window* windows[MAX_WINS];
  struct MP_get_complete* get_complete_head[MAX_PROCS];/* linked-list head ptr */
  struct MP_get_complete* get_complete_last[MAX_PROCS];/* linked-list last ptr */
#if defined (POSIX_SIGNALS)
  struct sigaction sigact;
  sigset_t sig_mask, sigusr1_mask;
#endif
/********************************************************************/

#if defined (DEBUG1)
  int debug_level=1;
#elif defined (DEBUG2)
  int debug_level=2;
#elif defined (DEBUG3)
  int debug_level=3;
#else
  int debug_level=0;
#endif


#else

  extern int myproc ,nprocs, nsmp;
  extern int true, false;
  extern FILE *mylog;
  extern char *interface[MAX_PROCS][MAX_NICS], hostname[MAX_PROCS][100];
  extern char myhostname[100], filename[100], prog_name[100];
  extern int buffer_size;
  extern int small_buffer_size;
  extern int *MP_all_mask;
/*  extern struct shm_entry_type shm_cleanup_list;*/
  extern struct MP_msg_entry msg_null;
  extern int smp_rank[MAX_PROCS];
  extern struct recv_buf_type **recv_buf;
  extern int last_tag, last_src, last_size;
  extern int n_send, n_recv, n_send_packets, n_recv_packets;
  extern int nb_send, nb_recv, nb_send_buffered, nb_recv_buffered;
  extern int n_recv_buffered, n_send_buffered;
  extern int n_interrupts, n_sockets_serviced;
  extern int buffer_orig;
  extern int nics;
  extern int salen;
  extern struct sockaddr_in serv_addr;
  extern char cbuf[100];
#ifndef MP_MPI_C_C
  extern struct MP_msg_entry  *msg_list[MAX_MSGS*2];
#endif
  extern struct MP_msg_header **hdr;
  extern int debug_level;

/************************* added by Adam Oline **********************/
  extern void* get_buf_addr_q[MAX_PROCS][GET_BUF_Q_SIZE];
  extern int get_buf_addr_q_head[MAX_PROCS];
  extern int get_buf_addr_q_tail[MAX_PROCS];
  extern int already_blocking;
  extern struct MP_Window* windows[MAX_WINS];
  extern struct MP_get_complete* get_complete_head[MAX_PROCS];/* linked-list head ptr */
  extern struct MP_get_complete* get_complete_last[MAX_PROCS];/* linked-list last ptr */
#if defined (POSIX_SIGNALS)
  extern struct sigaction sigact;
  extern sigset_t sig_mask, sigusr1_mask;
#endif

/********************************************************************/

#endif


/*********************/
/* Global prototypes */
/*********************/

void errorw(int err_num, char* text);
void errorx(int err_num, char* text);
void errorxs(int err_num, char* format_statement, char* string_ptr);

void range_check( int nbytes, int node);

void stall_check( double *t0, int nbytes, int src, int tag);
void set_status(int abort, char *status, char *message);
void check_status();
void initialize_status_file();

void dump_and_quit();
void dump_msg_state();

int trace_start( int nbytes, int dest);
void trace_end( int trace_id, int stats, int send_flag);
void trace_set0();
void trace_dump();

void lprintf(char *fmt, ...);
void dbprintf(char *fmt, ...);
void ddbprintf(char *fmt, ...);

void start_protect_from_sig_handler(void);
void stop_protect_from_sig_handler(void);

/* SMP Prototypes - used in shm_*.c and tcp.c */

void smp_send( struct MP_msg_entry *msg);
void smp_recv( struct MP_msg_entry *msg);
void smp_cleanup( );
void init_smp( );
int sizeofFifo( );
uint64_t FIFOread( int );
void FIFOwrite( int, uint64_t);
void init_fifo( char * );
void freefifo( );
void quitall( );


void do_accum_op(void*, void*, int, int);
void do_lock(int, int, int, int*);
void smp_os_fence();

void Read(int src, void ** address);
void Write(int dest, void * address);

pid_t getpgid( pid_t pid );
int setpgid( pid_t pid, pid_t pgid );
pid_t getppid(void);

static int *sync_array;

/* Prototypes for functions in utils.c */

int accept_a_connection_from(int node, int listen_socket, int nic);
void check_os_header( struct MP_msg_entry **msg, int src);
int connect_to(int node, int port, int nic, int bsize);
int create_listen_socket(int port, int bsize);
struct MP_msg_entry *create_msg_entry(void *buf, int nbytes,
                                      int src,int dest, int tag);
struct MP_msg_header *create_msg_header(int nbytes, int src, int tag);
struct MP_msg_entry *create_os_msg_entry(void* dest_ptr,void *buf,int nbytes,
                                         int src,int dest, int tag, int wid);
struct MP_msg_header *create_os_msg_header(void* dest_ptr, int nbytes, 
                                           int src, int tag);
int create_socket(int bsize);
void push_to_buffer(struct MP_msg_entry *msg);

void set_message( struct MP_msg_entry *msg, int nbytes, int src, int tag);
void set_os_message( struct MP_msg_entry *msg, void* dest_ptr, 
                     int nbytes, int src, int tag);
void twiddle( double t );

/* Utility functions used by MP_Win_fence, MP_Win_unlock, MP_APut, MP_Get,
 * MP_Accumulate:
 */

void add_msgid(int, int);
void rem_msgid(int, int);
int show_head_msgid(int);
int pop_head_msgid(int);

/* Prototypes for locking functions */

void myshmget(int *id, key_t key, int size, int shmflg);
void* myshmat(int shmid);
int sched_yield(void);


/****************************************************************************/
/* Note: the contents of this file from here to the end may be copied to    */
/* the beginning of one of shm_*.c source files if a single shm module is   */
/* chosen, and removed from this file                                       */
/****************************************************************************/

/* Structure and union definitions for shm_*.c */

union sem_union {                           /* define union for semctl() */
  int              val;
  struct semid_ds *buf;
  unsigned short  *array;
};

struct MP_key {
   int nbytes;
   int token;
   int tag;
};

struct MP_key_msg {
   void *next;
   int nbytes;
   int token;
   int tag;
};

/* Prototypes for use in shm_*.c */

void put_token( int token, int nbytes, int dest, int tag);
void get_token( int *token, int nbytes, int src, int tag);
void add_shmid( int shmid, void *shmptr);
int  get_shmid( void *shmptr );
void post_key_to_q( int nbytes, int token, int tag, struct MP_key_msg *mptr);
void service_os_request(int node);
void check_onesided_grid();
void handle_onesided_sig(int sig);
void shm_onesided_init();
void shm_onesided_cleanup();
void smp_os_send(struct MP_msg_entry* msg);
void smp_sync();
struct MP_msg_entry *create_os_msg_entry(void* dest_ptr, void *buf, int nbytes,
                                         int src,int dest, int tag, int wid);

void MP_Global_gather(void *vec, int n, int op_type, int *mask);

void Block_Signals();
void Unblock_Signals();
void Block_SIGUSR1();
void Unblock_SIGUSR1();

#endif /* MP_GLOBALS_H */
