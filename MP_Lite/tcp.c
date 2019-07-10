/* MP_Lite message-passing library
 * Copyright (C) 1997-2004 Dave Turner
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
 * Contact: Dave Turner - drdaveturner@gmail.com
 */

/* There are 3 different 'modes' or implementations of TCP-based MP_Lite.
 * This file is for the TCP signals version, but there are two more files:
 * tcp_nosignals.c and tcp_pthreads.c.  A short description of each file:
 *
 * tcp.c
 *   - SIGIO or interrupt driven: Works asynchronously using an interrupt 
 *     handler to keep data flowing through the TCP buffers.
 *
 * tcp_nosignals.c
 *   - Synchronous: Uses very large TCP buffers and warns if messages are 
 *     getting too large for the TCP buffers.  This method is very efficient,
 *     provide performance similar to raw TCP performance, but only works
 *     when the user can guarantee that the message sizes are small than than
 *     the TCP buffer sizes.
 *
 * tcp_pthreads.c
 *   - pThreads-based controller:  This is experimental controller based on
 *     posix threads that I have not yet gotten to work well enough.  It would
 *     be the slickest method since it would provide the most control through
 *     a light-weight thread, but I have found that pThreads is still somewhat
 *     lacking at this point.
 *
 *     All versions use a 3 integer header (number of bytes, destination, tag)
 *  for each message.  A writev() (gather-write) sends the header then the
 *  message body in one step.  On the other end, a readv() (scatter-read)
 *  separates the header off preventing any extra buffering.
 *     The synchronous version uses blocking sockets while the SIGIO and 
 *  pThreads versions use asynchronous sockets.  All versions use separate 
 *  sockets for the MP_Sync() function to allow this to operate indepent of 
 *  other message traffic.  
 *
 *
 * SIGIO VERSION
 *
 *    An MP_ASend() creates a message header then initiates the send by 
 * calling do_send(), which uses writev() once then lets the sigio_handler()
 * finish the send.  This initial writev() puts as much data as will fit into
 * the TCP send buffer.  When some of this data is transfered to the 
 * destination node, leaving free space in the TCP send buffer, a SIGIO 
 * interrupt is generated and the sigio_handler() is called.  This routine 
 * services all active transfers then returns control.  An MP_Wait() on the 
 * msg_id of the MP_ASend() will push the remaining data to a send buffer to
 * avoid any possibility of a lock-up condition.  An MP_Send() is then just an
 * MP_ASend() followed by an MP_Wait().
 *    An MP_ARecv() from a known source creates a message header then lets 
 * do_recv() initiate the receive.  It first looks in the receive message q
 * for a message that has been buffered, and if not found it does a readv() 
 * once then lets sigio_handler() finish the receive if necessary. If there
 * is already a receive in progress from the source node, it just adds the
 * header to the end of a receive message q rmsg_q[] instead.  For an unknown
 * source, sigio_handler() must activate all TCP buffers and keep messages
 * flowing by buffering them to the message q if they don't match.  This 
 * again is much less efficient than when the sources are known, but can
 * be a convenience when in an area where performance is not critical.
 * An MP_Wait() on the MP_ARecv() msg_id simply blocks until sigio_handler()
 * has completed the receive.  MP_Recv() is then simply an MP_ARecv() followed
 * by an MP_Wait().
 *
 */

/* PERFORMANCE
 *
 *    The synchronous version pretty much tracks the raw TCP performance,
 * providing a peek of 90 Mbps for Fast Ethernet and 250 Mbps for Gigabit
 * Ethernet (Packet Engines G-NIC I or II), with latencies in the 
 * 60-70 usec range (TCP-GE-FE).  A loss of < 2% was incurred when I 
 * incorporated the ability to use message tags, and the buffer that it
 * entails.  Using an SMP kernel increases the latency to ~100 usec.
 *    The SIGIO version which is more robust suffers more due to its
 * interrupt driven nature.  For Fast Ethernet, a loss of only a few percent
 * is seen in the midrange, but for Gigabit Ethernet a more substantial loss
 * of up to 18% is seen.  
 *    For fast networks, the best solution is to try to increase the TCP 
 * buffer sizes enough to handle the message traffic for your application.
 * Then you can use the more efficient synchronous version.  MP_Lite 
 * already tries to increase these buffers, but most systems
 * have hard limits set.  If you have control over the cluster you are 
 * running on, you can increase these limits.  For Linux, you can
 * increase the maximum TCP send and receive buffer sizes to 512 KB
 * by adding the following lines to the /etc/sysctl.conf file.

# Increase max socket buffer sizes
net.core.rmem_max = 524288
net.core.wmem_max = 524288
# Increase the max shm segment memory
kernel.shmmax = 512000000

 *    For further performance information, check the MP_Lite webpage at:
 *
 *        http://www.scl.ameslab.gov/Projects/MP_Lite/
 *
 * MEMORY USAGE ~ (MAX_MSGS) * 252 bytes ==> 2.5 MBytes for MAX_MSGS == 10000
 */


/* Undefine USE_SHM to turn off shared memory support for message-passing
 * between processes on the same node, defaulting back to TCP based comms.
 * SHM is not supported for a distributed set of SMP nodes yet, so the code
 * will automatically default back to using TCP.
 */

#undef USE_SHM
//#define USE_SHM


#define GOT_HDR  (msg->nhdr == 0)
#define HDR_SENT (msg->nhdr == 0)

#include "globals.h"   /* Globals myproc, nprocs, mylog, interface, hostname */
#include "mplite.h"

#if defined (sun)
#include <sys/file.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <ctype.h>      /* isdigit() */
#include <stddef.h>
#include <sched.h>

#include <signal.h>
#include <fcntl.h>


/***********************************/
/* Structure definitions for tcp.c */
/***********************************/

struct window_msgid_q_node {
  int mid;
  struct window_msgid_q_node* next;
};

struct window_msgid_q_node* win_msgid_q_head[MAX_WINS];
struct window_msgid_q_node* win_msgid_q_tail[MAX_WINS];

/**************************/
/* tcp.c global variables */
/**************************/

int sd[MAX_PROCS*2][MAX_NICS], sd_prev, sd_next;   /* socket descriptors */
int portnum[MAX_PROCS], mypid;
int nics;
int smp_mode = 0;   /* Use sockets everywhere by default */
int exit_thread = 0, socket_blocking, socket_nonblocking;


void **recv_q, **send_q, **msg_q;
int *ractive, *wactive, active=1, inactive=0;
int protect_from_sig_handler = 0;
int need_to_block = 0;
int received_signal = 0;

struct MP_Window* windows[MAX_WINS];
int** procs;
int* procs_win;

/*********************************/
/* Function prototypes for tcp.c */
/*********************************/

void tcp_asend( struct MP_msg_entry *msg );
void tcp_arecv( struct MP_msg_entry *msg );
void tcp_block(struct MP_msg_entry *msg);
void do_send(int node);
void do_recv(int node);
void sigio_handler();
void *msg_q_thread();
void nanny();
void send_to_q( struct MP_msg_entry *msg );
void recv_from_q( struct MP_msg_entry *msg );
void post( struct MP_msg_entry *msg, void **q);
void check_header( struct MP_msg_entry **msg, int src);
void update_msg_progress( struct MP_msg_entry *msg, int i, int *n );
void send_to_self( struct MP_msg_entry *msg );
void socket_handshake( int buffer_size, int sd_offset );
void service_os_socket( int );
void win_check( int, int, int );
void create_msgid_q(int);
void remove_msgid_q(int);
void add_msgid(int, int);
void rem_msgid(int, int);
int pop_head_msgid(int);
void try_lock(int, int, int*);
void signal_safe_put(void*, void*, int, int);


#if defined (POSIX_SIGNALS)

/* already_blocking == 0 => We are not already blocking signals
 * already_blocking  > 0 => We are already blocking signals
 *
 * Situations arise where we may have nested pairs of Block_Signals()
 * and Unblock_Signals() calls. We keep a count of how many times we've 
 * called Block_Signals() so that we don't prematurely unblock signals
 * each time we call Unblock_Signals().
 */

void Block_Signals()
{
  if( ! smp_mode ) { 
    if(already_blocking++==0) {
      sigprocmask(SIG_BLOCK, &sig_mask, NULL); 
    }
  }
}
  
void Unblock_Signals()
{ 
  if( ! smp_mode ) { 
    if(--already_blocking==0) {
      sigprocmask(SIG_UNBLOCK, &sig_mask, NULL); 
    }
  }
}

void Block_SIGUSR1()
{ 
   sigprocmask(SIG_BLOCK, &sigusr1_mask, NULL); 
}
  
void Unblock_SIGUSR1()
{ 
   sigprocmask(SIG_UNBLOCK, &sigusr1_mask, NULL); 
}

#elif defined (_BSD_SIGNALS)    /* BSD signals package */

void Block_Signals()
{ 
  if( ! smp_mode ) { 
     if(already_blocking++==0) {
        sigblock(sigmask(SIGIO));
        sigblock(sigmask(SIGUSR1));
     }
  }
}

void Unblock_Signals()
{
  if( ! smp_mode ) { 
     if(--already_blocking==0) sigblock(0);
  }
}

void Block_SIGUSR1()
{ 
   sigblock(sigmask(SIGUSR1));
}

void Unblock_SIGUSR1()
{
   sigblock(0);
}

#endif



void MP_Init(int *argc, char ***argv)
{
   FILE *fd, *pd;
   int iproc, i, j, p, n, r, node, next, prev, tmp, my_smp_rank, pid;
   int nic, one = 1, socket_flags, is_ip, ndots = 0;
   int listen_socket;
   int irtn, msg_id;
   char *ptr, *srtn, garb[100], command[100];
   struct hostent *host_entry;
   struct in_addr ipadr;

   errno = -1;

   gethostname(myhostname,100);
   ptr = strstr(myhostname,".");
   if (ptr != NULL && ptr != myhostname) strcpy(ptr,"\0");
   mypid = getpid();


      /* Open a .node.myhostname.mypid log file for each node.  After node 
       * numbers are assigned, this will be moved to .node# .
       */

   sprintf(filename, ".node.%s.%d", myhostname, mypid);
   mylog = fopen( filename, "w" );
   if( !mylog ) errorxs(0,"Couldn't open the log file %s",filename);

      /* Open .mplite.config and read in nprocs and hostnames.
       * Set myproc if possible, interface[0..nprocs-1][0..nics-1]
       */

   fd = fopen(".mplite.config","r");
   if( !fd ) errorx(0,"Couldn't open the .mplite.config file");

   srtn = fgets(garb, 90, fd);
   sscanf(garb,"%d",&nprocs);
   srtn = fgets(garb, 90, fd);
   sscanf(garb,"%d",&nics);
   srtn = fgets(garb, 90, fd);
   sscanf(garb, "%s", prog_name);

   for(i=0; i<nprocs; i++) {

      irtn = fscanf(fd,"%d",&iproc);

      for(nic=0; nic<nics; nic++) {

         interface[i][nic] = malloc(100*sizeof(char));

         irtn = fscanf(fd,"%s", interface[i][nic]);

      /* If these are IP numbers, convert into dot.style.interface.addresses.
       * If it is an alias, asign the real hostname instead.
       */

         is_ip = true;
         for( j=0; j<strlen(interface[i][nic]); j++) {
            if( ! isdigit(interface[i][nic][j]) && interface[i][nic][j] != '.' )
               is_ip = false;
            if( interface[i][nic][j] == '.' ) ndots++;
         }
         if( ndots < 3 ) is_ip = false;

         if( is_ip ) {

            inet_aton( interface[i][nic], &ipadr );
            host_entry = gethostbyaddr( (const void*) &ipadr, 4, AF_INET );
            strcpy( interface[i][nic], host_entry->h_name );

         } else {   /* Convert any aliases */

// The Intel Phi cannot handle gethostbyname so commenting out for now

            //host_entry = gethostbyname( interface[i][0] );
            //strcpy( hostname[i], host_entry->h_name );

         }
      }

         /* Cut the hostname down */

      strcpy(hostname[i], interface[i][0]);
      ptr = strstr(hostname[i],".");
      if (ptr != NULL && ptr != hostname[i]) strcpy(ptr,"\0");

         /* Is this proc on an SMP node? */

      if( !strcmp( hostname[i], myhostname) ) {
         nsmp++;
         myproc = i;   /* This will set myproc accurately for non-SMP procs */
      }

         /* Set the smp_rank for each proc */

      smp_rank[i] = 0;

      for(j=0; j<myproc; j++) {

         if( !strcmp( hostname[j], hostname[i]) ) smp_rank[i]++;

      }
   }
   fclose(fd);

      /* If on an SMP node, what rank am I? */

   if( nsmp > 1 ) {

      lprintf("Blocking until %d .node.%s.pid# files exist\n", nsmp, myhostname);

      n = 0;
      sprintf( command, "ls -a1 | grep .node.%s. | wc -l", myhostname);

      while( n != nsmp ) {

         pd = popen( command, "r");

         irtn = fscanf( pd, "%d", &n);

         pclose( pd );
      }

      lprintf("Done blocking, now reading PID numbers\n");

      sprintf( command, "ls -a1 | grep .node.%s | cut -d '.' -f 4", myhostname);

      pd = popen( command, "r");

      for( i=0; i<nsmp; i++ ) {

         irtn = fscanf( pd, "%d", &pid );

         lprintf("PID %d has smp_rank = %d\n", pid, i);

         if( pid == mypid ) my_smp_rank = i;   /* Save the smp_rank */

      }
      fclose( pd );

      nsmp = 0;
      myproc = -1;

      for(i=0; i<nprocs; i++) {

         if( !strcmp( hostname[i], myhostname) ) {
            if( nsmp == my_smp_rank ) myproc = i;
            nsmp++;
         }
      }
      smp_rank[myproc] = my_smp_rank;
   }

   if( myproc < 0 ) {
      errorxs(myproc,"%s matched no entry in .mplite.config",myhostname);
   }

#if defined (USE_SHM)
   if( nsmp == nprocs ) {        /* All procs are on a single SMP node */
      smp_mode = 1;
      lprintf("\nAll %d procs are on the same SMP node\n\n", nsmp);
   }
#endif
   if( nprocs == 1 ) smp_mode = 1;

   initialize_status_file();


        /* Initialize for SMP message-passing */

   if( smp_mode ) {

      if( nprocs > 1 ) init_smp( );

           /* TCP mode, initialize all sockets */

   } else {

         /* Set the port numbers for each node.  TCP_PORT is the default,
          * but may be incremented for additional SMP processes
          * (once mixed-mode is supported :). */

      for( i=0; i<nprocs; i++ ) {

         portnum[i] = TCP_PORT;

         for( j=0; j<i; j++ ) {

            if( !strcmp( hostname[j], hostname[i]) ) portnum[i]++;

         }
      }

      lprintf("\n%s PID %d is node %d of %d connected to port %d\n\n", 
               myhostname, mypid, myproc, nprocs, portnum[myproc]);
      for(i=0; i<nprocs; i++) {
         lprintf("   %d ==> ", i);
         for( nic=0; nic<nics; nic++) { lprintf(" %s ", interface[i][nic]); }
         lprintf(" port %d\n", portnum[i]);
      }


         /* Handle first contact */

      socket_handshake( buffer_size, 0);


      i = sizeof(int);
      j = (myproc+1) % nprocs;
      getsockopt(sd[j][0], SOL_SOCKET, SO_RCVBUF, (char *) &tmp, (void *) &i);
      lprintf("\nSend/Receive Buffers: %d ==> %d bytes\n\n", buffer_orig,tmp);

         /* Set up blocking sockets to the next/previous nodes for MP_Sync() */

      lprintf("\n   Setting up special sockets for MP_Sync()\n");

      next = (myproc+1)%nprocs;
      prev = (myproc+nprocs-1)%nprocs;

      listen_socket = create_listen_socket( portnum[myproc]+nsmp, small_buffer_size );

      if( myproc == 0 ) {
         sd_next = connect_to(next, portnum[next]+nsmp, 0, small_buffer_size);
         if( nprocs == 2 ) {
            sd_prev = sd_next;
         } else {
            sd_prev = accept_a_connection_from(prev, listen_socket, 0);
         }
      } else {
         sd_prev = accept_a_connection_from(prev, listen_socket, 0);
         if( nprocs == 2 ) {
            sd_next = sd_prev;
         } else {
            sd_next = connect_to(next, portnum[next]+nsmp, 0, small_buffer_size);
         }
      }
      lprintf("%d sockets created for MP_Sync()\n\n", nprocs == 2 ? 1 : 2);
      close(listen_socket);

         /* Set up socket buffers for one-sided communications */

      lprintf("Setting up 1-sided socket buffers\n");

      socket_handshake( buffer_size, nprocs);

      lprintf("1-sided sockets done\n");

         /* Enable asynchronous I/O now on all sockets */

      socket_flags = socket_blocking = fcntl(sd[0][0], F_GETFL, 0);
#if defined (FNONBLK)
      socket_flags = socket_flags + FNONBLK;
#elif defined (FNONBLOCK)
      socket_flags = socket_flags + FNONBLOCK;
#else
      socket_flags = socket_flags + O_NONBLOCK;
#endif
      socket_nonblocking = socket_flags;

      socket_flags = socket_flags + FASYNC;

      for( node=0; node<nprocs; node++) if( node != myproc ) {
         for( nic=0; nic<nics; nic++) {

            r = fcntl(sd[node][nic], F_SETOWN, getpid());
            r = fcntl(sd[node+nprocs][nic], F_SETOWN, getpid());
            dbprintf("  initial fcntl flags = %d\n",socket_flags);

            r = fcntl(sd[node][nic], F_SETFL, socket_flags);
            r = fcntl(sd[node+nprocs][nic], F_SETFL, socket_flags);
            dbprintf("  final fcntl flags = %d (%d %d)\n",socket_flags, 
                     O_NONBLOCK, FASYNC);
      }  }

   }  /* End of TCP socket setup */

      /* Now we all know myproc, so move .node.myhostname.mypid to .node#
       * where # is myproc. */

   dbprintf("MP_Sync after socket or smp initialization\n");

   MP_Sync();

   sprintf(command, "mv .node.%s.%d .node%d", myhostname, mypid, myproc);
   irtn = system( command );

   trace_set0();   /* Sync then set t_0 to the current time */


      /* Initialize windows array and window msgid queue */

   for(i=0;i<MAX_WINS;i++) {
     windows[i]=NULL; /* Set pointer to null, indicating free window slot */
     win_msgid_q_head[i]=NULL;
     win_msgid_q_tail[i]=NULL;
   }


      /* Set up the message queues and recv headers for each source node.
       *   Posted receives go to the end of the recv_q[src]  queue.
       *   Posted sends    go to the end of the send_q[dest] queue.
       *   Buffered receives go to the msg_q[src] queue.
       */

   for( i=0; i<MAX_MSGS; i++) {
      msg_list[i] = malloc(sizeof(struct MP_msg_entry));
      if( ! msg_list[i] ) errorx(0,"tcp.c MP_msg_entry() - malloc() failed");
      *msg_list[i] = msg_null;
   }

      /* wactive, recv_q, send_q, and hdr are allocated extra memory for 
       * one-sided msgs */

   msg_q  = (void **) malloc( (nprocs+1)*sizeof(void *) ) + 1;
   ractive = (int *) malloc( (nprocs+1)*sizeof(int) ) + 1;  /* [-1:nprocs-1] */
   wactive = (int *) malloc( ((nprocs*2)+1)*sizeof(int) ) + 1; 
   hdr = (struct MP_msg_header **) malloc( ((nprocs*2)+1)*sizeof(void *) ) + 1;
   recv_q = (void **) malloc( ((nprocs*2)+1)*sizeof(void *) ) + 1;
   send_q = (void **) malloc( ((nprocs*2)+1)*sizeof(void *) ) + 1;

   for( i = -1; i<nprocs; i++) {
      ractive[i] = wactive[i] = inactive;
      recv_q[i] = send_q[i] = msg_q[i] = NULL;
      hdr[i] = create_msg_header( -2, -2, -2);
   }

      /* Initialize one-sided data */

   for( i = 0; i<nprocs; i++) {
     wactive[i+nprocs] = inactive;
     recv_q[i+nprocs] = send_q[i+nprocs] = NULL;
     hdr[i+nprocs] = create_os_msg_header(NULL, -2, -2, -2);
     get_complete_head[i] = NULL;
     get_buf_addr_q_head[i] = 0;
     get_buf_addr_q_tail[i] = 0;
   }

   /* Cntl-c or kill -HUP will dump the message queue state then exit.
    * sigio_handler() will handle SIGIO interrupts.
    */

#if defined (POSIX_SIGNALS)

   sigaddset( &sig_mask, SIGUSR1);  /* These are used in [Un]block_Signals() */
   sigaddset( &sig_mask, SIGIO);
   sigaddset( &sigusr1_mask, SIGUSR1);  /* Used in [Un]block_SIGUSR1() */

   sigact.sa_handler = nanny;
   sigaction(SIGUSR1, &sigact, NULL);

   sigact.sa_handler = dump_and_quit;
   sigaction(SIGINT, &sigact, NULL);
   sigaction(SIGHUP, &sigact, NULL);

   sigact.sa_handler = sigio_handler;
   sigaction(SIGIO,  &sigact, NULL);

#elif defined (_BSD_SIGNALS)

   signal(SIGINT, dump_and_quit);
   signal(SIGHUP, dump_and_quit);

   signal(SIGUSR1, nanny);

   signal(SIGIO, sigio_handler);

#endif


      /* Handshake with all, just to be polite */

   MP_Sync();

   lprintf("Handshake between all nodes - ");

   for(node=0; node<nprocs; node++) {

      MP_iARecv(&j, 1, node, 0, &msg_id);
      MP_iSend(&myproc,  1, node, 0);
      MP_Wait( &msg_id );

      if( j != node ) {
         lprintf("Comm test to node %d in MP_Init() came across as %d\n",
                 node, j); 
         exit(-1);
   }  }

   MP_Sync();

   lprintf("Completed successfully\n");


   /* Set up a default mask for the global operations that include all procs */

   MP_all_mask = malloc(nprocs*sizeof(int));
   if( ! MP_all_mask ) errorx(0,"tcp.c - malloc(MP_all_mask) failed");
   for( i=0; i<nprocs; i++) MP_all_mask[i] = 1;

   twiddle( 0.0 );   /* Initialize the twiddle increment */

   n_send = n_send_buffered = nb_send_buffered = n_send_packets = 0;
   n_recv = n_recv_buffered = nb_recv_buffered = n_recv_packets = 0;
   n_interrupts = n_sockets_serviced = 0;


   /* Set up one-sided fence flags and windows */

   procs = malloc(sizeof(int*)*nprocs);
   procs_win = malloc(sizeof(int)*nprocs);
   for(i=0;i<nprocs;i++) {
     procs[i] = malloc(sizeof(int)*2);
     procs[i][0] = -1;
     procs[i][1] = -1;
     MP_Win_create(procs[i], sizeof(int)*2, 1, &procs_win[i]);
   }

   if( myproc == 0 ) 
      fprintf(stderr, "----------  Done handshaking  ----------\n");
   lprintf("Done with all the handshaking, exiting MP_Init()\n");
   lprintf("------------------------------------------------\n");
   set_status(0, "running", "");
}

void MP_Finalize()
{
   int nmsgs, nic, node;
   int i;

   MP_Sync();

   for(i=nmsgs=0;i<MAX_MSGS;i++) if(msg_list[i]->buf != NULL) nmsgs++;
   if( nmsgs > 0 ) lprintf("%d messages stil in use\n", nmsgs);

      /* Free windows for one-sided fence calls */

   for(i=0;i<nprocs;i++)
     MP_Win_free(&procs_win[i]);

   if( smp_mode ) {

      smp_cleanup();

   } else {

         /* Close all sockets.  The side with the dynamic port must be closed
          * first, leaving it in a TIMEWAIT state for an ack for ~30 seconds.
          * Otherwise, the known ports would be tied up, forcing negotiation
          * for the next mprun if it was done within ~30 seconds.
          */

      for( node=myproc+1; node<nprocs; node++) {
         for( nic=0; nic<nics; nic++) {
            close(sd[node][nic]);
            close(sd[node+nprocs][nic]);
      }  }

      MP_Sync();

      if( nprocs > 1 ) close( sd_next );

      for( node=0; node<myproc; node++) {
         for( nic=0; nic<nics; nic++) {
            close(sd[node][nic]);
            close(sd[node+nprocs][nic]);
      }  }

      sleep(1);

      if( nprocs > 2 ) close( sd_prev );
   }

   set_status(0, "completed", "");

   lprintf("------------------------------------------------\n");
   dump_msg_state();
   lprintf("************************************************\n");

   lprintf("%d sends with %d partially buffered\n"
           "   (%f%% of traffic buffered, %d packets)\n", n_send,
           n_send_buffered, (100.0*nb_send_buffered)/nb_send, n_send_packets);
   lprintf("%d recvs with %d partially buffered\n"
           "   (%f%% of traffic buffered, %d packets)\n", n_recv, 
           n_recv_buffered, (100.0*nb_recv_buffered)/nb_recv, n_recv_packets);
   lprintf("%d interrupts handled servicing %d sockets\n",
           n_interrupts, n_sockets_serviced);

   lprintf("************************************************\n");
   fclose(mylog);
   trace_dump();         /* Only active if compiled with -DTRACE */
}


/**********************************************************************/
/*        SIGIO interrupt based communications                        */
/**********************************************************************/

/* sigio_handler() Handles SIGIO interrupts when data arrives or is sent
 *              on each socket.  sigio_handler() services these by retrying the
 *              active sends and recvs on each socket until they are completed.
 * msg_q_thread() Posix thread that services the message queue on each socket
 *
 * MP_ASend() - Asynchronous send - Make message header and initiate send.
 * MP_Send()  - MP_ASend() then MP_Wait().
 * MP_ARecv() - Asynchronous recv - Make message header and initiate recv.
 * MP_Recv()  - MP_ARecv() then MP_Wait().
 * MP_Wait()  - ARecv: block until full message has arrived.
 *            - ASend: Dump to send buffer if part of message is still left.
 * MP_Block() - Block on an ASend(), but avoid pushing data to a send buffer.
 */


void MP_ASend(void *buf, int nbytes, int dest, int tag, int *msg_id)
{
   struct MP_msg_entry *msg;

   Block_Signals();
      dbprintf("\n>MP_ASend(& %d %d %d)\n", nbytes, dest, tag );
   range_check( nbytes, dest );

   msg = create_msg_entry( buf, nbytes, myproc, dest, tag);
   *msg_id = msg->id;
   msg->trace_id = trace_start( nbytes, dest);   /* -DTRACE --> active */

   if( dest == myproc ) {           /* Handle sending to self separately */

      send_to_q( msg );

   } else if( smp_mode ) {
      smp_send( msg );

   } else {               /* Add to the end of the queue for this socket */

      tcp_asend( msg );

   }

   Unblock_Signals();
   n_send++; nb_send += nbytes;
   dbprintf("<MP_ASend()\n");
}

void MP_Send(void *buf, int nbytes, int dest, int tag)
{
   int msg_id;

   MP_ASend(buf, nbytes, dest, tag, &msg_id);
   MP_Wait(&msg_id);
}


void MP_ARecv(void *buf, int nbytes, int src, int tag, int *msg_id)
{
   struct MP_msg_entry *msg;

   Block_Signals();
      dbprintf("\n>MP_ARecv(& %d %d %d)\n", nbytes, src, tag);
   if( src < 0 ) src = -1;
   range_check( nbytes, src);

   msg = create_msg_entry( buf, nbytes, src, -1, tag);
   *msg_id = msg->id;
   msg->trace_id = trace_start( -nbytes, src);   /* -DTRACE --> active */

   if( src == myproc ) {                   /* Let MP_Wait() handle it */

   } else if( smp_mode ) { /* Let MP_Wait() handle it */

   } else {       /* Add to the end of the recv queue for this socket */

      tcp_arecv( msg );
   }

   Unblock_Signals();
   n_recv++; nb_recv += nbytes;
   dbprintf("<MP_ARecv()\n");
}

void MP_Recv(void *buf, int nbytes, int src, int tag)
{
   int msg_id;

   MP_ARecv(buf, nbytes, src, tag, &msg_id);
   MP_Wait(&msg_id);
}


void MP_Wait(int *msg_id)
{
   struct MP_msg_entry *msg;
   struct MP_get_complete* gc;
   struct MP_get_complete* p;

   dbprintf(">MP_Wait(%d)\n", *msg_id);
   if( *msg_id == -1 ) return;

   msg = msg_list[*msg_id];
   if( msg->buf == NULL ) errorx(*msg_id,"MP_Wait() has NULL msg");

   if( msg->nleft == 0 ) {                        /* Send() or Recv() done */

   } else if ( msg->send_flag == 1 ) {            /* ASend() not completed */

     dbprintf("  push the rest to a send buffer\n");
     push_to_buffer(msg);

   } else if( msg->src == myproc ) {              /* Recv from self */

      recv_from_q( msg );

/* PROBLEM - In a mixed smp/tcp environ, we can't assume one or the other */

   } else if( smp_mode ) {  /* SMP recv using shm */

      smp_recv( msg );

   } else {                                         /* Recv from other */

      dbprintf("  going into a wait loop for %d bytes\n", msg->nleft);
      tcp_block( msg );
      dbprintf("  wait loop done\n");
   }

   if ( msg->one_sided == 1 && (msg->tag == 1 || msg->tag == -3) && 
        (msg->dest!=myproc)) { 

        /* Waiting for reply from get request */

     dbprintf("  waiting for one-sided get to complete\n");    

     /* Need to find the node in the linked-list corresponding to this
        get, since we could wait on gets in a different order that they
        were requested */

     for(p = NULL, gc = get_complete_head[msg->dest]; 
         gc != NULL && gc->mid != *msg_id; 
         p=gc, gc=gc->next);
     
     if(gc == NULL) {
       fprintf(stderr, "Error - MP_Wait(): Couldn't find corresponding get\n");
       exit(-1);
     }

     while( gc->val == 0 ) { /* Busy wait */

        if( smp_mode ) {

          /* Loop through check_onesided_grid until we pick up the get reply
             from the target node.  This allows us to avoid waiting for a signal
             to propagate before we read in the data.  */

          check_onesided_grid();

        } else {

          sched_yield();

        }
     }

     if(p == NULL)  /* Remove head node */
       get_complete_head[msg->dest] = get_complete_head[msg->dest]->next;
     else  /* Remove non-head node */
       p->next = gc->next;
     free(gc);

   }

      /* These can be retrieved using the MP_Get_Last_Source()/Tag()/Size() 
       * functions. */

   last_src = msg->src;
   last_tag = msg->tag;
   last_size = msg->nbytes;

   if( msg->send_flag > 0 ) n_send_packets += msg->stats;
   else                     n_recv_packets += msg->stats;

   if( msg->send_flag != 3 ) {
      msg->buf = NULL;       /* Mark the msg header as free for reuse */
   }

   if( msg->one_sided == 1 ) { /* Remove msg id from one-sided q */
     rem_msgid( msg_list[*msg_id]->window_id, *msg_id );
   }
   
   *msg_id = -1;
   trace_end( msg->trace_id, msg->stats, msg->send_flag);
   dbprintf("<MP_Wait()\n");
}

int MP_Test(int *msg_id, int *flag)
{
   struct MP_msg_entry *msg;

   Block_Signals();
   dbprintf(">MP_Test(%d)\n", *msg_id);
   msg = msg_list[*msg_id];
   if( msg->buf == NULL ) errorx(*msg_id,"MP_Test() has NULL msg");

   if (msg->nleft != 0) {      /* Not Done       */

      *flag = 0;

   } else {                     /* Done            */

      *flag = 1;

      MP_Wait(msg_id);       /* Clean things up */

   }

   dbprintf("<MP_Test(%d)\n", *msg_id);
   Unblock_Signals();
   return *flag;
}

int MP_Testall(int count, int *msg_ids, int *flag)
{
  struct MP_msg_entry *msg;
  int i;

  Block_Signals();

  *flag = 1;
  for(i=0;i<count;i++) {
    msg = msg_list[msg_ids[i]];
    
    if( msg->buf == NULL ) errorx(msg_ids[i],"MP_Test() has NULL msg");
    
    if (msg->nleft != 0)      
      *flag = 0;

  }

  if(*flag == 1) {
    for(i=0;i<count;i++) {
      msg = msg_list[msg_ids[i]];

      MP_Wait(&msg_ids[i]);
    }
  }
  return 0;
}

int MP_AProbe(int nbytes, int source, int tag, int *flag)
{
   struct MP_msg_entry *p, *msg;
   struct MP_msg_header *h;

   int i, n, num_qs = 1, tsrc = 0;
   void *buf;

   Block_Signals();
   dbprintf("\n>MP_AProbe() trying to match (%d, %d, %d)\n", nbytes, source, tag);
  
   if ( !(source >= 0 && msg_q[source] == NULL) ) /* Message maybe in the msq_q */
   { 
      if ( source < 0) num_qs = nprocs;
      else tsrc = source;
   
      for( i=0; i<num_qs; i++) {

         p = msg_q[tsrc];
         while( p != NULL ) {

            if ( p->nbytes <= nbytes &&
               ( tag < 0 || p->tag == tag ) ) { /* Found a completed or partial  match */

               dbprintf("  found a match in msg_q[%d]\n", tsrc);

               last_src = tsrc;
               last_size = p->nbytes;
               last_tag = p->tag;
               *flag = 1;
               Unblock_Signals();
               return 1;
            }
            p = p->next;
         }
         tsrc++;
      }
   }

   num_qs = 1;
   tsrc = 0;

   if (source < 0) num_qs = nprocs;
   else tsrc = source;

   for(i=0; i<num_qs; i++) if ( tsrc != myproc ) {
      if ( ractive[tsrc] != 1 ) { /* inactive - peek at the buffer to check for the message */

         h = hdr[tsrc];

         do {
            n = recv( sd[tsrc][0], h, HDRSIZE, MSG_PEEK );
            if ( n != HDRSIZE ) { /* It is not a valid header */

               if ( source == -1) break;
               else {
                  *flag = 0;
                  Unblock_Signals();
                  return 0;
               }
            }

            /* Check the header for a match */

            if ( ( tag < 0 || h->tag == tag ) && /* It matches */
                 ( h->nbytes <= nbytes ) ) {

               dbprintf("  Peeked in the buffer and found a matching header"
                        " (%d %d %d)\n", h->nbytes, tsrc, h->tag);

               last_src = h->src;
               last_size = h->nbytes;
               last_tag = h->tag;

               *flag = 1;
               Unblock_Signals();
               return 1; 
            }
            else { /* Header Mismatch */

               buf = malloc(h->nbytes);
               if( !buf ) errorx(0,"tcp.c MP_AProbe() - malloc() failed");

               msg = create_msg_entry( buf , h->nbytes, h->src, -1, h->tag );

               dbprintf("  Peeked in buffer %d but found mismatch (%d %d %d)\n",
                           tsrc, h->nbytes, h->src, h->tag);

               msg->recv_flag = 2;     /* Do not check msg_q */
               post(msg, msg_q);       /* post it to msg_q */
               post(msg, recv_q);      /* post it to recv_q */

               do_recv(h->src);

               if ( msg->nleft != 0) { /* not completed */

                  if ( source == -1) break;
                  else {
                     *flag = 0;
                     Unblock_Signals();
                     return 0;
                  }
               }
            }
         } while (1);
      }
      tsrc++;
   }

   *flag = 0;
   Unblock_Signals();
   return 0;
}

int MP_Probe(int nbytes, int source,int tag)
{
      /* This function was programmed by Shoba Selvarajan :) */

   return 0;
}

/* Block until an MP_ASend() completes, avoiding a push to a send buffer */

void MP_Block(int *msg_id)
{
   struct MP_msg_entry *msg;

   msg = msg_list[*msg_id];
   tcp_block( msg );
}

void read_msg(struct MP_msg_entry* msg, int src, int* m)
{
  int nic, n;

  for( nic=0; nic<nics; nic++) {

    if( nic == 0 && ! GOT_HDR ) {  /* Read the header separately */
      dbprintf("    reading %d bytes of header from node %d <--", 
               msg->nhdr, msg->src);

      n = read(sd[src][0], msg->phdr, msg->nhdr);

      update_msg_progress( msg, 0, &n);

      if( GOT_HDR ) check_header( &msg, src );

    }

    if( GOT_HDR && msg->nbl[nic] > 0 ) {
      dbprintf("    reading %d bytes of data from node %d nic %d <--", 
               msg->nbl[nic], msg->src, nic);

      n = read(sd[src][nic], msg->ptr[nic], msg->nbl[nic]);

      update_msg_progress( msg, nic, &n);
    }
    *m += n;
  }

}

void write_msg(struct MP_msg_entry* msg, int dest, int* m)
{
  int nic, n, nb;
  
  for( nic=0; nic<nics; nic++) {

    if( nic == 0 && ! HDR_SENT ) {
      dbprintf("    sending %d bytes of header to node %d -->", 
               msg->nhdr, msg->dest);
            
      n = write(sd[dest][0], msg->phdr, msg->nhdr);

      update_msg_progress( msg, 0, &n);

    }

    if( HDR_SENT && msg->nbl[nic] > 0 ) {
      dbprintf("    sending %d bytes of data to node %d nic %d -->", 
               msg->nbl[nic], msg->dest, nic);

      nb = MIN(msg->nbl[nic], msg->mtu);

      n = write(sd[dest][nic], msg->ptr[nic], nb);

      update_msg_progress( msg, nic, &n);
    }
    *m += n;
  }
}

void do_send(int dest)
{
   int m=1, cat_nap = 1, nic, idle;
   struct MP_msg_entry *msg;

   msg = send_q[dest];
   dbprintf("  do_send(& %d %d %d) - %d bytes left\n", 
            msg->nbytes, msg->dest, msg->tag, msg->nleft );

   while( m > 0 && msg->nleft > 0 ) {
      m = 0;
      write_msg(msg, dest, &m);

           /* increase the packet size on a sliding scale to decrease
              the effects of latency while keeping both NICs active */

      if( nics > 1 && msg->mtu < 50000 ) msg->mtu *= 2;

           /* The first time we write 0 bytes, try napping to let
            * the send buffer clear and retry.  This avoids pushing
            * data to a send buffer for messages larger than the
            * TCP buffer sizes. */

      if( m == 0 && cat_nap ) {
         twiddle( 0.001500 );
         cat_nap = 0;
         m = 1;
      } else {
         cat_nap = 1;
      }
   }

   if (msg->nleft > 0) {
      wactive[dest] = active;
   } else {
      send_q[dest] = msg->next;   /* detach msg header from the q */
      if( msg->send_flag == 3 ) {     /* free buffer and msg header */
         for( nic=0; nic<nics; nic++) {
            if( msg->buffer[nic] != NULL ) {
               free( msg->buffer[nic] );
               msg->buffer[nic] = NULL;
         }  }
         msg->buf = NULL;       /* Mark the msg header as free for reuse */
      } else if( msg->send_flag == 4) { /* free msg header only */
         msg->buf = NULL;
         /* Update stats here since MP_Wait will never be called */
         n_send++; nb_send += msg->nbytes; n_send_packets += msg->stats;
         trace_end( msg->trace_id, msg->stats, msg->send_flag);
      }
      if( send_q[dest] == NULL ) wactive[dest] = inactive;
      else do_send(dest);
   }
}


/* For an existing message, continue reading from the TCP buffer.
 * For new msg with a known source, check msg_q[] & TCP buffers.
 * do_recv() should never be called if the source is unknown (-1).
 */

void do_recv(int src)
{
   struct MP_msg_entry *msg;
   int m = 1, vec;

   msg = recv_q[src];       /* Get the current message */
   dbprintf("  do_recv(& %d %d %d) - %d bytes left\n", 
            msg->nbytes, msg->src, msg->tag, msg->nleft );

   if( ! GOT_HDR && msg->recv_flag != 2 ) { /* New msg, check the msg_q first */
      recv_from_q( msg );
   }

/*   if( msg->nleft > 0 ) {*/

   while( m > 0 && msg->nleft > 0 ) {     /* Didn't find it in the msg_q[] */
     m = 0;
     read_msg(msg, src, &m);
   }

      /* If not done, keep the msg active and wait for sigio_handler().
       * If done, go to next message and call do_recv() to initiate. */ 

   if (msg->nleft > 0 ) {
      ractive[src] = active;
   } else {
      if( msg->nleft == 0 ) recv_q[src] = msg->next;   /* detach msg header from the queue */
      if( recv_q[src] == NULL ) ractive[src] = inactive;
      else do_recv(src);
   }
}

/**************************************
 * One-Sided Communications functions *
 **************************************
 *   MP_Win_create()    Create window, return handle to user
 *   MP_shMalloc()      Allocate memory, create window, return handle to user
 *   MP_Win_free()      Free window
 *   MP_Win_fence()     All one-sided communications (incoming/outgoing) 
 *                      issued before fence will have completed locally 
 *                      when fence returns.
 *   MP_Win_lock()      Lock a window for reading or reading/writing
 *   MP_Win_unlock()    Unlock window, all one-sided communications issued
 *                      while window was locked will have completed locally 
 *                      and remotely when unlock returns.
 *
 *   MP_Win_start()     Notifies dest procs that you are about to access window
 *                      (Blocks until post called on dest procs).
 *   MP_Win_complete()  Notifies procs from start call that you are done 
 *                      accessing window.
 *   MP_Win_post()      Notifies dest procs that you are making window 
 *                      available for access.
 *   MP_Win_wait()      Notifies procs from post call that the window is no
 *                      longer available for access (Blocks until completes
 *                      are called).
 *   MP_Win_test()      Non-blocking version of wait, if wait would have
 *                      returned right away, test acts exactly like wait. If
 *                      wait would not have returned right away, then test
 *                      returns with flag set to 0, and no other actions are
 *                      taken.
 *
 *   MP_APut()          Asynchronous put
 *   MP_Get()           Asynchronous get
 *   MP_Accumulate()    Asynchronous accumulate
 */

int MP_Win_create(void* base, int size, int disp_unit, int* wid)
{
  /* Since MP_Win_create is a collective communications call, and the window
     id pool begins in the same state on each proc, corresponding windows on 
     each proc will have the same window id */
  struct MP_Window* win;
  int i;
  int wid_start;
  static int current_wid = 0;

  dbprintf(">MP_Win_create()\n");
  
  wid_start = current_wid++;
  while( windows[current_wid%MAX_WINS] != NULL) {

    if( current_wid%MAX_WINS == wid_start ) {
      lprintf("You have more than MAX_WINS windows in use\n");
      lprintf("Edit tcp.c to increase MAX_WINS then recompile and relink\n");
      errorx(MAX_WINS,"You have more than MAX_WINS windows in use\n");
    }
    current_wid++;
  }
  current_wid = current_wid % MAX_WINS;

  *wid = current_wid; /* Set win to the handle for this window */

  win = windows[current_wid] = malloc( sizeof(struct MP_Window) );
  if(windows[current_wid] == NULL)
    errorx(errno, "MP_Win_create() - MP_Window malloc failed");

  win->node = malloc(sizeof(struct MP_Win_elt)*nprocs);
  if(win->node == NULL)
    errorx(errno, "MP_Win_create() - MP_Win_elt malloc failed");

  win->post_sent      = malloc(nprocs*sizeof(int));
  win->post_received  = malloc(nprocs*sizeof(int));
  for(i=0;i<nprocs;i++)
    win->post_received[i] = win->post_sent[i] = 0;

  win->locked  = 0;
  win->count = 0;
  win->lock_q_head = 0;
  win->lock_q_tail = 0;

  win->node[myproc].base = base;
  win->node[myproc].size = size;
  win->node[myproc].disp_unit = disp_unit;

  MP_Gather(win->node, sizeof(struct MP_Win_elt));

  dbprintf("<MP_Win_create(%d)\n", *wid);
  return 0;
}

int MP_shMalloc(void** base, int size, int disp_unit, int* win)
{
  int rc;

  dbprintf(">MP_shMalloc()\n");
  *base = malloc(size);
  if(*base == NULL) 
    errorx(errno, "MP_shMalloc() - malloc for 'base' failed\n");

  rc = MP_Win_create(*base, size, disp_unit, win);

  dbprintf("<MP_shMalloc()\n");
  return rc;

}

int MP_Win_free(int* wid)
{
  dbprintf(">MP_Win_free()\n");
  MP_Sync();

  /* Free window memory */

  free(windows[*wid]->node);
  free(windows[*wid]);

  windows[*wid] = NULL;

  *wid = -1;

  dbprintf("<MP_Win_free()\n");
  return 0;
}

void MP_Win_fence(int wid)
{
  int i, mid;
  static int fence_index=0;

  dbprintf(">MP_Win_fence()\n");

  /* Wait on msg ids associated with win */
  mid = show_head_msgid( wid );
  while(mid != -1) {
    MP_Wait(&mid);
    mid = show_head_msgid( wid );
  }

  if( smp_mode ) smp_os_fence();


  /* For synchronization purposes, each processor does a put to every other
     processor to assure all outstanding put calls have completed at the 
     target (this essentially flushes the one-sided buffers) */

  /* Note: We use alternating indices of the 2-element-per-proc procs array 
     to prevent a race condition with a single-element-per-proc array.  If
     we were doing MP_APut()s to the same element of the array every time, 
     it's possible for two puts to arrive before we've checked the array 
     once, and the process hangs in an infinite while loop. */

  for(i=0;i<nprocs;i++) if(i!=myproc) {
    MP_APut(&myproc, sizeof(int), i, sizeof(int)*fence_index, 
            procs_win[myproc], &mid);
    MP_Wait(&mid);
  }
    
  for(i=0;i<nprocs;i++) if(i!=myproc) {
    while(procs[i][fence_index]!=i) /* Busy wait for put from proc i */
      if(i%2==3) printf(""); /* invalidate cache */
    procs[i][fence_index]=-1; /* Reset flag */
  }
  fence_index = 1 - fence_index; /* Flip between 0 and 1 */


     /* All incoming put calls have now completed */

  dbprintf("<MP_Win_fence()\n");
}


    /* Add msg id (mid) to end of queue for window id (wid) */

void add_msgid(int wid, int mid)
{
  struct window_msgid_q_node* p;
  
  start_protect_from_sig_handler();

  dbprintf(" >add_msgid(%d %d)\n", wid, mid);

  /* Allocate memory for mid entry at tail end of queue */

  if( win_msgid_q_head[wid] == NULL ) { /* Queue for this wid is empty */

    p = win_msgid_q_tail[wid] = win_msgid_q_head[wid] = 
      malloc( sizeof(struct window_msgid_q_node) );

  } else { /* Queue for this wid is non-empty */

    p = win_msgid_q_tail[wid]->next = 
      malloc( sizeof(struct window_msgid_q_node) );
    win_msgid_q_tail[wid] = p; /* Set tail to new node */
  }

  /* Check for malloc failure */
  
  if( p == NULL ) {
    fprintf(stderr, "Error in add_msgid(): malloc failed\n");
    exit(-1);
  }

  /* Copy mid to allocated entry */

  p->mid = mid;
  p->next = NULL;

  dbprintf(" <add_msgid()\n");

  stop_protect_from_sig_handler();
}

/* Remove a specific message id from queue.
 */
void rem_msgid(int wid, int mid)
{
  struct window_msgid_q_node* c; /* Current pointer */
  struct window_msgid_q_node* p; /* Previous pointer */

  start_protect_from_sig_handler();

  dbprintf(" >rem_msgid(%d %d)\n", wid, mid);

  for(p=NULL, c=win_msgid_q_head[wid]; c != NULL; p=c, c=c->next) { 

    if(c->mid == mid) { /* Found matching msg id */
      
      if(p == NULL) { /* Remove head node */

        win_msgid_q_head[wid] = c->next;

      } else { /* Remove non-head node */

        p->next = c->next; /* Remove node from linked-list */

      }
      
      free(c); /* Free memory allocated for node */
      
      break;
    }
    
  }

  if(c == NULL) { /* We didn't find msg id in queue */
    fprintf(stderr, "Error in rem_msgid(): Coulnd't find mid\n");
    exit(-1);
  }

  dbprintf(" <rem_msgid()\n");

  stop_protect_from_sig_handler();

}

/* Show the head msg id in the queue for window with id == wid 
 * (Don't remove from list).
 */
int show_head_msgid(int wid)
{
  struct window_msgid_q_node* p;
  int mid;

  start_protect_from_sig_handler();

  dbprintf(" >show_head_msgid(%d)\n", wid);

  if( win_msgid_q_head[wid] == NULL ) { /* Queue for this wid is empty */
    
    mid = -1;

  } else { /* Queue for this wid is non-empty */

    mid = win_msgid_q_head[wid]->mid; 

  }

  dbprintf(" <show_head_msgid()\n");

  stop_protect_from_sig_handler();

  return mid;
}

/* Pop head msg id in queue for window with id == wid. Return -1
 * if queue is empty. 
 */
int pop_head_msgid(int wid)
{
  void* p; /* Generic pointer */
  int mid;

  start_protect_from_sig_handler();

  dbprintf(" >pop_head_msgid(%d)\n", wid);

  if( win_msgid_q_head[wid] == NULL ) { /* Queue for this wid is empty */
    
    mid = -1;

  } else { /* Queue for this wid is non-empty */

    mid = win_msgid_q_head[wid]->mid; 
    p = win_msgid_q_head[wid]; /* Save address of node */
    win_msgid_q_head[wid] = win_msgid_q_head[wid]->next; /* Unlink node */
    free(p); /* Free memory allocated for node */

  }

  dbprintf(" <pop_head_msgid()\n");

  stop_protect_from_sig_handler();

  return mid;
}

/* MP_Win_start simply waits for the elements of the win->posted array
 * corresponding to group to be flipped from 0 to 1 before proceeding 
 *
 * Note: in MP_Win_start and MP_Win_post, group is simply an integer array
 * of size nprocs.  If group[i] == 1, then proc i is included in the group,
 * otherwise proc i is not included in the group 
 */
void MP_Win_start(int* group, int wid)
{
  struct MP_Window* win = windows[wid];
  int i,j;
 
  dbprintf(">MP_Win_start(%d)\n", wid);

  for(i=j=0;i<nprocs;i++) if(group[i] == 1) {

    j++;

    while(win->post_received[i] == 0)
      if(i%2==3) printf("");

  }

  win->count = j;

  dbprintf("<MP_Win_start\n");
}

void MP_Win_complete(int wid)
{
  struct MP_Window* win = windows[wid];
  struct MP_msg_entry** msgs;
  int* mids;
  int active_targ_hdr[2], i, j, mid;

  dbprintf(">MP_Win_complete(%d)\n", wid);

  mid = show_head_msgid(wid);
  while(mid != -1) {
    MP_Wait(&mid);
    mid = show_head_msgid(wid);
  }

  msgs = malloc(sizeof(struct MP_msg_entry*)*win->count);
  if(msgs == NULL) errorx(win->count, "msgs malloc failed\n");
  mids = malloc(sizeof(int)*win->count);
  if(mids == NULL) errorx(win->count, "mids malloc failed\n");

  /* Send 'complete' msg to everyone who sent a post to me */
  for(i=j=0; i<nprocs; i++) if(win->post_received[i] == 1) {

    if( i == myproc ) {


      win->post_sent[i] = 0;

    } else {

      active_targ_hdr[0] = wid; /* Window ID */
      active_targ_hdr[1] = 1;   /* Type of active target request (Complete) */

      msgs[j] = create_os_msg_entry(NULL, &active_targ_hdr, 2*sizeof(int), 
                                    myproc, i, 16, wid);

      mids[j] = msgs[j]->id;

      msgs[j]->trace_id = trace_start(sizeof(int)*2, i); /* DTRACE -> active */

      if( smp_mode ) {
         msgs[j]->tag = -18;
         smp_os_send( msgs[j] );
      } else {
         tcp_asend( msgs[j] );
      }
      j++;
    }

    /* Reset post_received for proc i */
    win->post_received[i] = 0;

  }

  for(i=0;i<j;i++) {
    add_msgid(wid, mids[i]);
    MP_Wait( &mids[i] );
  }

  win->count = 0;
  
  dbprintf("<MP_Win_complete(%d)\n", wid);
}

void MP_Win_post(int* group, int wid)
{
  struct MP_Window* win = windows[wid];
  struct MP_msg_entry** msgs;
  int* mids;
  int active_targ_hdr[2], i, j;

  dbprintf(">MP_Win_post(%d)\n", wid);

  for(i=j=0;i<nprocs;i++) if(group[i]==1) j++;
  win->count = j;

  msgs = malloc(sizeof(struct MP_msg_entry*)*win->count);
  mids = malloc(sizeof(int)*win->count);

  for(i=j=0; i<nprocs; i++) if(group[i] == 1) {

    if( i == myproc ) {
      
      win->post_received[i] = 1;
      
    } else {
  
      active_targ_hdr[0] = wid; /* Window ID */
      active_targ_hdr[1] = 0;   /* Type of active target request (Post) */

      msgs[j] = create_os_msg_entry(NULL, &active_targ_hdr, 2*sizeof(int), 
                                    myproc, i, 16, wid);
      mids[j] = msgs[j]->id;
      msgs[j]->trace_id = trace_start(sizeof(int)*2, i); /* DTRACE -> active */

      if( smp_mode ) {
         msgs[j]->tag = -18;
         smp_os_send( msgs[j] );
      } else {
         tcp_asend( msgs[j] );
      }
      j++;

    }

    win->post_sent[i] = 1;

  }

  for(i=0;i<j;i++) {
    add_msgid(wid, mids[i]);
    MP_Wait( &mids[i] );
  }

  dbprintf("<MP_Win_post()\n");
}

void MP_Win_wait(int wid)
{
  struct MP_Window* win = windows[wid];
  int i;
  
  dbprintf(">MP_Win_wait(%d)\n", wid);

  for(i=0;i<nprocs;i++)
    while(win->post_sent[i] != 0) if(i%2==3) printf("");

  win->count = 0;

  dbprintf("<MP_Win_wait()\n");
}

void MP_Win_test(int wid, int* flag)
{
  struct MP_Window* win = windows[wid];
  int i;
  
  dbprintf(">MP_Win_test(%d)\n", wid);
  *flag = 1; /* Set to true */
  for(i=0;i<nprocs;i++)
    /* if waiting on complete, set to false */
    if(win->post_sent[i] != 0) *flag = 0;

  if(*flag == 1) /* If all completes came in, then act like MP_Win_wait */
    win->count = 0;

  dbprintf("<MP_Win_test()\n");
}

/* Window synchronization may be done using locks.  Locks should not be used
 * concurrently with the other types of synchronization.  Locks may be shared 
 * (read-only) or exclusive (read-write).  No actual enforcement of these 
 * privelages is currently done, it is assumed the user will comply with the 
 * rules (e.g. they will only write to memory shared through a window while 
 * they have an exclusive lock).  A proc may lock local memory that is shared 
 * through a window, reading and writing directly to memory instead of
 * using the one-sided functions.
 */
 
void MP_Win_lock_shared(int rank, int wid)
{ MP_Win_lock(1, rank, wid); }

void MP_Win_lock_exclusive(int rank, int wid)
{ MP_Win_lock(2, rank, wid); }

void MP_Win_lock(int lock_type, int rank, int wid)
{
  struct MP_Window* win = windows[wid];
  struct MP_msg_entry *msg;
  struct MP_lock_hdr lock_hdr;
  int lock_granted=0;
  int mid;

  dbprintf(">MP_Win_lock(%d %d %d)\n", lock_type, rank, wid);

  if( rank == myproc ) {

    Block_Signals();
    do_lock( myproc, lock_type, wid, &lock_granted );

    Unblock_Signals();

  } else {
  
    lock_hdr.win_id = wid;
    lock_hdr.lock_type = lock_type;
    lock_hdr.lock_flag = &lock_granted;

    msg = create_os_msg_entry(NULL, &lock_hdr, sizeof(struct MP_lock_hdr), 
                              myproc, rank, 15, wid);
    mid = msg->id;
    msg->trace_id = trace_start( sizeof(int)*2, rank); /* -DTRACE --> active */
    if( smp_mode ) {
       msg->tag = -17;
       smp_os_send( msg );
    } else {
       tcp_asend( msg );
    }
    add_msgid(wid, mid);
    MP_Wait(&mid);

  }

  /* If lock has not been granted yet, then we have to wait, since exiting
     this function indicates to the user that we have the lock */
  while( !lock_granted ) if(lock_granted % 2 == 3) printf(""); /* busy wait */
    
  dbprintf("<MP_Win_lock()\n");
}

void MP_Win_unlock(int rank, int wid)
{
  /* MP_Win_unlock ensures that all one-sided operations issued after the lock
     was acquired have completed because the request to unlock is sent over
     the one-sided channels.  Thus, once we receive the reply, we know that
     all other traffic on the one-sided channels must have gone through */

  struct MP_msg_entry *msg;
  struct MP_lock_hdr lock_hdr;
  int unlock_granted=0;
  int mid;

  dbprintf(">MP_Win_unlock(%d %d)\n", rank, wid);

  /* Wait on all one-sided communication functions called since we called 
     MP_Win_lock */
  
  mid = show_head_msgid(wid);
  while(mid != -1) {
    MP_Wait(&mid);
    mid = show_head_msgid(wid);
  }

  if( rank == myproc ) {
    
    Block_Signals();
    do_lock( myproc, 0, wid, &unlock_granted );
    Unblock_Signals();

  } else {
  
    lock_hdr.win_id = wid;
    lock_hdr.lock_type = 0;
    lock_hdr.lock_flag = &unlock_granted;

    msg = create_os_msg_entry(NULL, &lock_hdr, sizeof(struct MP_lock_hdr), 
                              myproc, rank, 15, wid);
    mid = msg->id;
    msg->trace_id = trace_start( sizeof(int)*2, rank); /* -DTRACE --> active */
    if( smp_mode ) {
       msg->tag = -17;
       smp_os_send( msg );
    } else {
       tcp_asend( msg );
    }
    add_msgid(wid, mid);
    MP_Wait(&mid);

  }

  while(unlock_granted != 1) if(unlock_granted % 2 == 3) printf("");
    
  dbprintf("<MP_Win_unlock()\n");
}

void MP_APut(void* buf, int nelts, int dest, int dest_off, int wid, int* mid)
{
  struct MP_Window* win = windows[wid];
  struct MP_msg_entry *msg;
  void* dest_ptr;
  int nbytes;

  nbytes = nelts*win->node[dest].disp_unit;

  dbprintf("\n>MP_APut(& %d %d %d %d)\n", nbytes, dest, dest_off, wid);
 
  if(nbytes==0) {
    dbprintf("<MP_APut()\n");
    return;
  }
 
  Block_Signals();

  win_check( dest_off*win->node[dest].disp_unit, nbytes, 
             win->node[dest].size );
  
  range_check( nbytes, dest );
      
  /* Cast base to make sure we're doing byte-based pointer arithmetic */
  dest_ptr = (char*)win->node[dest].base + dest_off*win->node[dest].disp_unit;

  msg = create_os_msg_entry(dest_ptr, buf, nbytes, myproc, dest, 0, wid);
  *mid = msg->id; /* Return message id to user */
  msg->trace_id = trace_start( nbytes, dest);   /* -DTRACE --> active */

  if( dest == myproc ) {   /* Handle sending to self seperately */

    /* Use memmove since memory locations may overlap on same node */
    MP_memmove(dest_ptr, buf, nbytes);
    
    /* Update message */
    msg->nleft = 0;

  } else {

    if( smp_mode ) {
      msg->tag = -2;
      smp_os_send( msg );
    } else {
      tcp_asend( msg );
    }

  }

  /* Add msg id to list so we can wait on it in MP_Win_fence() */
  add_msgid(wid, msg->id);

  Unblock_Signals();
  n_send++; nb_send += nbytes;
  dbprintf("<MP_APut()\n");
}

void MP_Put(void* buf, int nelts, int dest, int dest_off, int wid)
{
  int mid;
  MP_APut(buf, nelts, dest, dest_off, wid, &mid);
  MP_Wait(&mid);
}

void MP_AGet(void* buf, int nelts, int src, int src_off, int wid, int* mid)
{
  struct MP_Window* win = windows[wid];
  struct MP_msg_entry *msg;
  struct MP_os_get_hdr hdr;
  void* src_ptr;
  int nbytes;
  
  nbytes = nelts*win->node[src].disp_unit;

  dbprintf("\n>MP_Get(& %d %d %d)\n", nbytes, src, src_off);
  
  if(nbytes==0) {
    dbprintf("<MP_Get()\n");
    return;
  }
  
  Block_Signals();

  win_check( src_off, nbytes, win->node[src].size );
  
  range_check( nbytes, src );
      
  /* Cast base to make sure we're doing byte-based pointer arithmetic */
  src_ptr = (char*)win->node[src].base + src_off*win->node[src].disp_unit;
  hdr.src_ptr = src_ptr;
  hdr.nbytes = nbytes;
  hdr.win_id = wid;

  /* Create empty message, header specifies a get request */
  msg = create_os_msg_entry(NULL, &hdr, sizeof(struct MP_os_get_hdr), myproc, 
                            src, 1, wid);
  *mid = msg->id;
  msg->trace_id = trace_start( sizeof(struct MP_os_get_hdr), src);   /* -DTRACE --> active */

  if( src == myproc ) {   /* Handle get from self seperately */

    /* Use memmove since memory locations may overlap on same node */
    MP_memmove(buf, src_ptr, nbytes);
    
    /* Update message */
    msg->nleft = 0;

  } else {
    
    /* Add flag to get-completed queue for this request.  Sigio handler
       will set flag to 1 when a get request is complete */
    if(get_complete_head[src]==NULL) {
      get_complete_last[src] = get_complete_head[src] = 
        malloc(sizeof(struct MP_get_complete));
    } else {
      get_complete_last[src]->next = malloc(sizeof(struct MP_get_complete));
      get_complete_last[src] = get_complete_last[src]->next;
    }
    get_complete_last[src]->val = 0;
    get_complete_last[src]->mid = *mid;
    get_complete_last[src]->next = NULL;
    
     /* Add local dest buffer to a queue */
    
    get_buf_addr_q[src][ get_buf_addr_q_tail[src] ] = buf;
    get_buf_addr_q_tail[src] = (get_buf_addr_q_tail[src] + 1) % GET_BUF_Q_SIZE;
    if(get_buf_addr_q_head[src] == get_buf_addr_q_tail[src]) {
      fprintf(stderr, "Error in MP_Get(): Ran out of room for buffer addresses. Increase size of GET_BUF_Q_SIZE in globals.h, recompile and relink\n");
      exit(-1);
    }

    if( smp_mode ) {
      msg->tag = -3;
      smp_os_send( msg );
    } else {
      tcp_asend( msg );
    }

  }

  /* Add msg id to list so we can wait on it in synchronization calls */
  add_msgid(wid, msg->id);

  Unblock_Signals();
  n_send++; nb_send += sizeof(struct MP_os_get_hdr);
  dbprintf("<MP_Get()\n");  

}

void MP_Get(void* buf, int nelts, int src, int src_off, int wid)
{
  int mid;
  MP_AGet(buf, nelts, src, src_off, wid, &mid);
  MP_Wait(&mid);
}

/* Accumulate sum, min, or max at proc dest, where buf is an array whose
   datatype (double, float, or int) is determined by op_type.  nelts is the 
   number of elements of the array to operate on, and dest_off gives the 
   displacement from the beginning of the window data, in units of size 
   win[dest].disp_size */
void MP_AAccumulate(void* buf, int nelts, int dest, int dest_off, int op_type, 
                    int wid, int* mid)
{
  struct MP_Window* win = windows[wid];
  struct MP_msg_entry *msg;
  int tag;
  void* dest_ptr;
  int nbytes;

  nbytes = nelts*win->node[dest].disp_unit;

  dbprintf(">MP_Accumulate()\n");
  if(nbytes==0) {
    dbprintf("<MP_Accumulate()\n");
    return;
  }
    
  Block_Signals();

  win_check( dest_off, nbytes, win->node[dest].size );
  
  range_check( nbytes, dest );
#if defined(USE_SHM)
  tag = -5 - op_type;
#else
  tag = op_type + 3; /* We use tag=0 for puts, 1 for get reqs, and 2 for get 
                        replies, so use >2 for accumulate op_types */
#endif

  /* Cast base to make sure we're doing byte-based pointer arithmetic */
  dest_ptr = (char*)win->node[dest].base + dest_off*win->node[dest].disp_unit;

  msg = create_os_msg_entry(dest_ptr, buf, nbytes, myproc, dest, tag, wid);
  *mid = msg->id;
  msg->trace_id = trace_start( nbytes, dest);   /* -DTRACE --> active */

  if( dest == myproc ) {   /* Handle sending to self seperately */

    do_accum_op( dest_ptr, buf, op_type, nbytes );
    
    /* Update message */
    msg->nleft = 0;

  } else {

    if( smp_mode ) {
      smp_os_send( msg );
    } else {
      tcp_asend( msg );
    }
  }

  add_msgid(wid, msg->id);

  Unblock_Signals();
  n_send++; nb_send += nbytes;
  dbprintf("<MP_Accumulate()\n");
}

void MP_Accumulate(void* buf, int nelts, int dest, int dest_off, int op_type, 
                   int wid)
{
  int mid;
  MP_AAccumulate(buf, nelts, dest, dest_off, op_type, wid, &mid);
  MP_Wait(&mid);
}

/* Asynchronous versions of typed accumulate functions */

void MP_AdSumAccumulate(void* buf, int n, int dest, int off, int win, int* mid)
{ MP_AAccumulate(buf, n, dest, off, 0, win, mid); }
void MP_AiSumAccumulate(void* buf, int n, int dest, int off, int win, int* mid)
{ MP_AAccumulate(buf, n, dest, off, 1, win, mid); }
void MP_AfSumAccumulate(void* buf, int n, int dest, int off, int win, int* mid)
{ MP_AAccumulate(buf, n, dest, off, 2, win, mid); }

void MP_AdMaxAccumulate(void* buf, int n, int dest, int off, int win, int* mid)
{ MP_AAccumulate(buf, n, dest, off, 3, win, mid); }
void MP_AiMaxAccumulate(void* buf, int n, int dest, int off, int win, int* mid)
{ MP_AAccumulate(buf, n, dest, off, 4, win, mid); }
void MP_AfMaxAccumulate(void* buf, int n, int dest, int off, int win, int* mid)
{ MP_AAccumulate(buf, n, dest, off, 5, win, mid); }

void MP_AdMinAccumulate(void* buf, int n, int dest, int off, int win, int* mid)
{ MP_AAccumulate(buf, n, dest, off, 6, win, mid); }
void MP_AiMinAccumulate(void* buf, int n, int dest, int off, int win, int* mid)
{ MP_AAccumulate(buf, n, dest, off, 7, win, mid); }
void MP_AfMinAccumulate(void* buf, int n, int dest, int off, int win, int* mid)
{ MP_AAccumulate(buf, n, dest, off, 8, win, mid); }

void MP_AdReplaceAccumulate(void* buf,int n,int dest,int off,int win,int* mid)
{ MP_AAccumulate(buf, n, dest, off, 9, win, mid); }
void MP_AiReplaceAccumulate(void* buf,int n,int dest,int off,int win,int* mid)
{ MP_AAccumulate(buf, n, dest, off,10, win, mid); }
void MP_AfReplaceAccumulate(void* buf,int n,int dest,int off,int win,int* mid)
{ MP_AAccumulate(buf, n, dest, off,11, win, mid); }

/* Synchronous versions (MP_Wait is called after the one-sided function */

void MP_dSumAccumulate(void* buf, int n, int dest, int off, int win)
{ MP_Accumulate(buf, n, dest, off, 0, win); }
void MP_iSumAccumulate(void* buf, int n, int dest, int off, int win)
{ MP_Accumulate(buf, n, dest, off, 1, win); }
void MP_fSumAccumulate(void* buf, int n, int dest, int off, int win)
{ MP_Accumulate(buf, n, dest, off, 2, win); }

void MP_dMaxAccumulate(void* buf, int n, int dest, int off, int win)
{ MP_Accumulate(buf, n, dest, off, 3, win); }
void MP_iMaxAccumulate(void* buf, int n, int dest, int off, int win)
{ MP_Accumulate(buf, n, dest, off, 4, win); }
void MP_fMaxAccumulate(void* buf, int n, int dest, int off, int win)
{ MP_Accumulate(buf, n, dest, off, 5, win); }

void MP_dMinAccumulate(void* buf, int n, int dest, int off, int win)
{ MP_Accumulate(buf, n, dest, off, 6, win); }
void MP_iMinAccumulate(void* buf, int n, int dest, int off, int win)
{ MP_Accumulate(buf, n, dest, off, 7, win); }
void MP_fMinAccumulate(void* buf, int n, int dest, int off, int win)
{ MP_Accumulate(buf, n, dest, off, 8, win); }

void MP_dReplaceAccumulate(void* buf,int n,int dest,int off,int win)
{ MP_Accumulate(buf, n, dest, off, 9, win); }
void MP_iReplaceAccumulate(void* buf,int n,int dest,int off,int win)
{ MP_Accumulate(buf, n, dest, off,10, win); }
void MP_fReplaceAccumulate(void* buf,int n,int dest,int off,int win)
{ MP_Accumulate(buf, n, dest, off,11, win); }


/* This function is for use within the signal handler.  It is similar to
 * MP_APut, except that it does not require windows, etc.  We need to make
 * sure this function is used in a way that the data will not be trampled
 * before it is sent out, as MP_Wait is never called on the message.
 * Basically this means using a buffer consisting of constant data (e.g. the 
 * current use in do_lock() ).
 *
 * Currently this fuction is necessary for the window locking 
 * funcitonality .
 */
void signal_safe_put(void* dest_ptr, void* buf, int nbytes, int dest)
{
  struct MP_msg_entry *msg;

  dbprintf("\n>signal_safe_put(& %d %d %d)\n", dest_ptr, nbytes, dest);
 
  Block_Signals();

  /* Send over one-sided channel as a put */
  msg = create_os_msg_entry(dest_ptr, buf, nbytes, myproc, dest, 0, -1);
  msg->send_flag = 4;  /* automatically clean up message when it is sent */
  msg->trace_id = trace_start( nbytes, dest);   /* -DTRACE --> active */

  if( dest == myproc ) {   /* Handle sending to self seperately */

    /* Use memmove since memory locations may overlap on same node */
    MP_memmove(dest_ptr, buf, nbytes);
    
    /* Cleanup message */
    msg->buf = NULL;

  } else {

    if( smp_mode ) {
      msg->tag = -2;
      smp_os_send( msg );
    } else {
      tcp_asend( msg );
    }

  }

  Unblock_Signals();

  n_send++; nb_send += nbytes;
  dbprintf("<signal_safe_put()\n");
}

void win_check(int offset, int nbytes, int win_size)
{
  if(offset < 0) {
    fprintf(stderr, "ERROR - Get/Put using offset < 0\n");
    exit(-1);
  }

  if(offset+nbytes > win_size) {
    fprintf(stderr, "ERROR - Get/Put using (offest + number bytes) > window size\n");
    exit(-1);
  }
}

/* 1-sided socket descriptors are stored in sd[node+nprocs][0..nics-1] */

void socket_handshake( int buffer_size, int offset)
{
  int i, j, n, r, nic, node, listen_socket;

  /* Handshaking  
    *   - Each node accepts connections from lesser nodes
    *   - A 'go' signal is sent around in between to keep things in sync
    *   - Each node then initiates connections to greater nodes
    */

  listen_socket = create_listen_socket(portnum[myproc], buffer_size);

  for( node=0; node<myproc; node++) {
    for( nic=0; nic<nics; nic++) {       /* connect for each NIC */
      sd[node+offset][nic] = accept_a_connection_from(node, listen_socket, nic);
    }
  }
  
  if(myproc > 0) {
    n = recv(sd[myproc-1][0], (char *)&j, sizeof(int), 0); /* go signal */
    if( n < 0 ) 
      errorx(errno,"socket_handshake() - recv() go signal error");
  }

  for( node=myproc+1; node<nprocs; node++) {
    for( nic=0; nic<nics; nic++) {     /* connect for each NIC */
      sd[node+offset][nic] = connect_to(node, portnum[node], nic, buffer_size);
    }
  }
  
  if( myproc < nprocs-1 ) {            /* send go signal for next proc */
    n = send(sd[myproc+1][0], (char *)&j, sizeof(int), 0);
    if( n < 0 ) 
      errorx(errno,"socket_handshake() - send() go signal error");
  }

  close(listen_socket);

}

/* Receive data from an incoming one-sided put.  Read header to get 
   destination address, then write data directly to destination. */

void recv_put_data(int src)
{
   struct MP_msg_entry *msg;
   struct MP_msg_header *h=hdr[src];
   int i, n, m = 1, nic, vec;

   dbprintf("  recv_put_data()\n");
   
   if(recv_q[src]==NULL) { /* Have not yet created a msg entry */
     recv_q[src] = create_os_msg_entry(h->dest_ptr, h->dest_ptr, h->nbytes, 
                                       h->src, -1, h->tag, -1);
   }
   msg=recv_q[src];

   while( m > 0 && msg->nleft > 0 ) {
     m = 0;
     read_msg( msg, src, &m);
   }

      /* If not done, keep the msg active and wait for sigio_handler().
       * If done, go to next message and call recv_put_data() to initiate. */ 

   if (msg->nleft > 0 ) { /* Some of message left */

     dbprintf("    Some of message left, service later\n");

   } else { /* All of message received */

     dbprintf("    Got all of message\n");

     n_recv++; nb_recv += msg->nbytes; n_recv_packets += msg->stats;

     /* Reset msg */
     msg->buf = NULL;
     recv_q[src] = NULL;

     /* Check for more data on one-sided sd */
     n = recv( sd[src][0], (char *)h, HDRSIZE, MSG_PEEK);
     if(n == HDRSIZE)
       service_os_socket(src);

   }
}

/* Receive an incoming one-sided get request.  The sender wants this proc
   to return data from a specific address.  Send requested data back over
   one-sided channels. */

void recv_get_request(int src)
{
  /* get_hdr is static in case we only read in part of the message and thus
     leave the scope of this function */
  static struct MP_os_get_hdr get_hdr; 

  struct MP_msg_entry *msg, *reply;
  struct MP_msg_header *h=hdr[src];
  int n, m = 1, nic, vec;
  dbprintf("  recv_get_request()\n");
  
  if(h->nbytes != sizeof(struct MP_os_get_hdr))
    errorx(-1, "recv_get_request() h->nbytes != sizeof MP_os_get_hdr");

  if(recv_q[src]==NULL) { /* Have not yet created a msg entry */
    recv_q[src] = create_os_msg_entry(h->dest_ptr, &get_hdr, h->nbytes, 
                                           h->src, -1, h->tag, -1);
  }
  msg=recv_q[src];

  while( m > 0 && msg->nleft > 0 ) {
    m = 0;
    read_msg( msg, src, &m );
  }
  
  if (msg->nleft > 0 ) {
    
    dbprintf("    Some of message left, service later\n");
    
  } else {
    
    /* Got all of message, respond to get request */
    dbprintf("    Got all of message\n");
    
    /* Update stats for receiving get request */
    n_recv++; nb_recv += msg->nbytes; n_recv_packets += msg->stats;
    
    /* Create new msg containing requested data, send back */
    reply = create_os_msg_entry(NULL, get_hdr.src_ptr, get_hdr.nbytes, 
                                myproc, h->src, 2, -1);
    reply->trace_id = trace_start( get_hdr.nbytes, h->src );
    reply->send_flag = 4; /* automatically clean up message when it is sent */
    tcp_asend( reply );

    /* MP_Wait is dangerous here, because it may call push_to_buffer() if the
       message has not been entirely sent, and push_to_buffer calls malloc. 
       We leave it up to the user to make sure the data requested will
       not be trampled before it is sent to the requesting process.

       NOTE: Even with the wrapper versions of the malloc and free calls,
       it is not a full-proof solution, and we should still avoid calling 
       malloc in the sigio_handler.
    */

    dbprintf("    Started sending of reply\n");

    /* Reset msg */

    msg->buf = NULL;
    recv_q[src] = NULL;
    
    /* Check for more data on one-sided sd */
    n = recv( sd[src][0], (char *)h, HDRSIZE, MSG_PEEK);
    if(n == HDRSIZE)
      service_os_socket(src);
    
  }
  
}

/* Receive the reply to a previously sent get request.  The reply contains
   the data we requested from the target.  The local destination is retrieved
   from a queue */

void recv_get_reply_data(int src)
{
   struct MP_get_complete *gc;
   struct MP_msg_entry *msg;
   struct MP_msg_header *h=hdr[src];
   int n, m = 1, nic, vec, real_src;

   dbprintf("  recv_get_reply_data()\n");
   
   real_src = h->src;

   if(recv_q[src]==NULL) { /* Have not yet created a msg entry */
     recv_q[src] = create_os_msg_entry(h->dest_ptr, 
                get_buf_addr_q[real_src][ get_buf_addr_q_head[real_src] ], 
                                       h->nbytes, h->src, -1, h->tag, -1);
   }

   msg=recv_q[src];

   while( m > 0 && msg->nleft > 0 ) {
      m = 0;
      read_msg( msg, src, &m );
   }

   if (msg->nleft > 0 ) {

     dbprintf("    Some of message left, service later\n");

   } else {

     /* Got all of msg */
     dbprintf("    Got all of message\n");

     /* Let process know that get has completed local writing */
     for(gc=get_complete_head[real_src];gc->val==1;gc=gc->next) ;
     dbprintf("Setting get_completed_head[%d]->val=1 (%d)\n", src, gc);
     gc->val = 1;

     /* remove local get buffer addr from q */     
     get_buf_addr_q_head[real_src] = 
       (get_buf_addr_q_head[real_src] + 1) % GET_BUF_Q_SIZE;

     /* Update stats */
     n_recv++; nb_recv += msg->nbytes; n_recv_packets += msg->stats;

     /* Reset msg */
     msg->buf=NULL;

     recv_q[src] = NULL; /* Indicate that we're finished with this msg */

     /* Check for more data on one-sided sd */
     n = recv( sd[src][0], (char *)h, HDRSIZE, MSG_PEEK);
     if(n == HDRSIZE)
       service_os_socket(src);

   }

}

/* Receive an incoming one-sided accumulate operation.  Operation may be of
   type SUM, MAX, or MIN.  Since we may be receiving multiple elements, we
   determine the datatype of each element from the op stored in header info,
   and then read in only bytesper(datatype) bytes at a time, as we cannot
   dynamically allocate memory in the signal handler. */

void recv_accum_operation(int src)
{
  struct MP_msg_entry *msg;
  struct MP_msg_header *h=hdr[src];
  int i, n, m = 1, nic, vec, op, bytes_per;
  static int idata;
  static float fdata;
  static double ddata;
  void* data_ptr;

  dbprintf("  recv_accum_operation()\n");
  op = h->tag - 3;
  if(op%3==1) { 
    bytes_per = sizeof(int);
    data_ptr = &idata;
  } else if(op%3==2) {
    bytes_per = sizeof(float);
    data_ptr = &fdata;
  } else {
    bytes_per = sizeof(double);
    data_ptr = &ddata;
  }

  if(recv_q[src]==NULL) { /* Have not yet created a msg entry */
    msg = recv_q[src] = create_os_msg_entry(h->dest_ptr, data_ptr, 
                                            h->nbytes, h->src, -1, h->tag, -1);
    msg->nbl[0] = bytes_per; /* We are only reading on sd[src][0] */
    msg->ptr[0] = data_ptr;  /* And we're only reading bytes_per bytes
                                at a time */
  } else {
    msg=recv_q[src];
  }

  while( m > 0 && msg->nleft > 0 ) {
    m = 0;
    
    if( ! GOT_HDR ) {  /* Read the header separately */
      dbprintf("    reading %d bytes of header from node %d <--", 
               msg->nhdr, msg->src);
      
      n = read(sd[src][0], msg->phdr, msg->nhdr);
      
      update_msg_progress( msg, 0, &n);
      
      if( GOT_HDR ) check_header( &msg, src );
      
    }
    
    if( GOT_HDR && msg->nbl[0] > 0 ) {
      dbprintf("    reading %d bytes of data from node %d nic %d <--", 
               msg->nleft, msg->src, 0);
      
      /* Just try to read bytes_per bytes, so we can do the op on
         that element, then read the next bytes_per bytes into the
         same buffer */
      /* NOTE: A more efficient method may be to allocate larger arrays,
         and do blocks of updates instead of one at a time.  Unfortunately
         we cannot use malloc inside sigio handler to allocate one big array */
      n = read(sd[src][0], msg->ptr[0], msg->nbl[0]);

      update_msg_progress( msg, 0, &n);

      if(msg->nbl[0]==0) { /* If we read in an entire elt, do op on it */

        do_accum_op( msg->dest_ptr, msg->buf, op, bytes_per );
        
        if(msg->nleft > 0) { /* reset for next read */
          msg->nbl[0] = bytes_per; 
          msg->ptr[0] = msg->buf;
          msg->dest_ptr += bytes_per; /* move to next element in local array */
        }

      }

    }
    m += n;
  }
  
  if (msg->nleft > 0 ) {

    dbprintf("    Some of message left, service later\n");

  } else {

    /* Got all of msg */
    dbprintf("    Got all of message, operations finished\n");

    /* Update stats */
    n_recv++; nb_recv += msg->nbytes; n_recv_packets += msg->stats;
    
    /* Reset msg */
    msg->buf = NULL;
    recv_q[src] = NULL;

    /* Check for more data on one-sided sd */
    n = recv( sd[src][0], (char *)h, HDRSIZE, MSG_PEEK);
    if(n == HDRSIZE) {
      service_os_socket(src);
    }

  }
}

/* Carry out the accumulate operation in local memory */

void do_accum_op(void* local, void* new, int op, int nbytes)
{
  int    *ilocal, *inew;
  float  *flocal, *fnew;
  double *dlocal, *dnew;
  int rem, n, i;

  dbprintf("do_accum_op()\n");

  if(op%3==1) { 
    ilocal = (int*)local;
    inew   = (int*)new;
    n = nbytes / sizeof(int);
    rem = nbytes % sizeof(int);
  } else if(op%3==2) {
    flocal = (float*)local;
    fnew   = (float*)new;
    n = nbytes / sizeof(float);
    rem = nbytes % sizeof(float);
  } else {
    dlocal = (double*)local;
    dnew   = (double*)new;
    n = nbytes / sizeof(double);
    rem = nbytes % sizeof(double);
  }

  if(rem != 0) {
    fprintf(stderr, "do_accum_op() - ERROR: nbytes is not divisible by datatype size\n");
    exit(-1);
  }

  switch(op) {
  
  case 0: /* double SUM */
    for(i=0;i<n;i++) dlocal[i] += dnew[i];
    break;
  case 1: /* int SUM */
    for(i=0;i<n;i++) ilocal[i] += inew[i];
    break;
  case 2: /* float SUM */
    for(i=0;i<n;i++) flocal[i] += fnew[i];
    break;
    
  case 3: /* double MAX */
    for(i=0;i<n;i++) if( dnew[i] > dlocal[i]) dlocal[i] = dnew[i];
    break;
  case 4: /* int MAX */
    for(i=0;i<n;i++) if( inew[i] > ilocal[i]) ilocal[i] = inew[i];
    break;
  case 5: /* float MAX */
    for(i=0;i<n;i++) if( fnew[i] > flocal[i]) flocal[i] = fnew[i];
    break;
      
  case 6: /* double MIN */
    for(i=0;i<n;i++) if( dnew[i] < dlocal[i]) dlocal[i] = dnew[i];
    break;
  case 7: /* int MIN */
    for(i=0;i<n;i++) if( inew[i] < ilocal[i]) ilocal[i] = inew[i];
    break;
  case 8: /* float MIN */
    for(i=0;i<n;i++) if( fnew[i] < flocal[i]) flocal[i] = fnew[i];
    break;

  case 9:  /* double REPLACE */
    for(i=0;i<n;i++) dlocal[i] = dnew[i];
    break;
  case 10: /* int REPLACE */
    for(i=0;i<n;i++) ilocal[i] = inew[i];
    break;
  case 11: /* float REPLACE */
    for(i=0;i<n;i++) flocal[i] = fnew[i];
    break;

  default:
    fprintf(stderr, "MP_Accumulate() - ERROR: Unsupported op type\n");
    exit(-1);
  }
}

/* Receive an incoming request for a lock on a window. We either grant the
   request immediately and send a response, or add the request to a queue and
   come back to it the next time this window is unlocked.  Requests are simply
   granted in the order received, independent of type of lock requested.
   
   FUTURE CONSIDERATION: Give priority to shared (read-only) locks, since any 
   number of shared locks may be granted at once, while only one exclusive 
   lock can be granted at a time. */

void recv_lock_request(int src)
{
  struct MP_msg_entry *msg;
  struct MP_msg_header *h=hdr[src];
  int i, n, m = 1, nic, vec, op, req_granted;
  static struct MP_lock_hdr lock_hdr;

  dbprintf("  recv_lock_request(%d)\n", src);

  if(recv_q[src]==NULL) { /* Have not yet created a msg entry */
    msg = recv_q[src] = create_os_msg_entry(NULL, &lock_hdr, h->nbytes, 
                                            h->src, -1, h->tag, -1);
    msg->nbl[0] = msg->nbytes; /* We are only reading on sd[src][0] */
  } else {
    msg=recv_q[src];
  }

  while( m > 0 && msg->nleft > 0 ) {
    m = 0;
    
    if( ! GOT_HDR ) {  /* Read the header separately */
      dbprintf("    reading %d bytes of header from node %d <--", 
               msg->nhdr, msg->src);
      
      n = read(sd[src][0], msg->phdr, msg->nhdr);
      
      update_msg_progress( msg, 0, &n);
      
      if( GOT_HDR ) check_header( &msg, src );
      
    }
    
    if( GOT_HDR && msg->nbl[0] > 0 ) {
      dbprintf("    reading %d bytes of data from node %d nic %d <--", 
               msg->nleft, msg->src, 0);
      
      n = read(sd[src][0], msg->ptr[0], msg->nbl[0]);

      update_msg_progress( msg, 0, &n);

    }
    m += n;
  }
  
  if (msg->nleft > 0 ) {

    dbprintf("    Some of message left, service later\n");

  } else {

    /* Got all of msg */
    if(lock_hdr.lock_type==0)
      dbprintf("    Got all of message, attempting unlock\n");
    else if(lock_hdr.lock_type==1)
      dbprintf("    Got all of message, attempting shared lock\n");
    else
      dbprintf("    Got all of message, attempting exclusive lock\n");

    /* Call do_lock and forget about message.  do_lock will either
       immediately grant the lock or add request to a queue and come back
       to it at the next unlock request */
    do_lock( src-nprocs, lock_hdr.lock_type, lock_hdr.win_id, 
             lock_hdr.lock_flag );

    /* Reset msg */
    msg->buf = NULL;
    recv_q[src] = NULL;

    /* Check for more data on one-sided sd */
    n = recv( sd[src][0], (char *)h, HDRSIZE, MSG_PEEK);
    if(n == HDRSIZE)
      service_os_socket(src);

  }
}

/* Attempt a single lock request, send response to requesting proc if
   successful, or add request to queue if unsuccessful.  If request is
   for an unlock, then process queue to grant pending requests. */

void do_lock(int src, int type, int wid, int* lock_flag)
{
  int q_result;
  int req_granted;
  struct MP_Window* win = windows[wid];

  /* Need this static so the memory will remain available even if
     we return from this function before the memory is accessed for
     a send */
  static int LOCK_CONFIRM=1;

  dbprintf("  do_lock(%d %d %d %d)\n", src, type, wid, lock_flag);

  try_lock(type, wid, &req_granted);

  if( type != 0 && req_granted == 1 ) { 
    /* If this is a lock request and the lock attempt was successful, send
       reply to requesting proc */

    signal_safe_put(lock_flag, &LOCK_CONFIRM, sizeof(int), src);

  } else if( type != 0 && req_granted == 0 ) { 
    /* If this is a lock request and lock attempt was not successful, add lock
       request to queue */

    dbprintf("  adding lock request to queue\n");
    /* Add lock request to circular queue */
    
    win->lock_q[win->lock_q_tail].src = src;
    win->lock_q[win->lock_q_tail].type = type;
    win->lock_q[win->lock_q_tail].lock_flag = lock_flag;

    /* set tail index to next empty slot */
    win->lock_q_tail = (win->lock_q_tail + 1) % LOCK_Q_SIZE; 
    
    if(win->lock_q_tail == win->lock_q_head) {
      dbprintf("do_lock() - ERROR: reached LOCK_Q_SIZE limit\n");
      dbprintf("Edit globals.h and increase LOCK_Q_SIZE, then recompile and relink\n");
      fprintf(stderr, "do_lock() - ERROR: reached LOCK_Q_SIZE limit\n");
    }
   
  } else if( type == 0 && req_granted == 1 ) { 
    /* If this is an unlock request, send reply */
    
    signal_safe_put(lock_flag, &LOCK_CONFIRM, sizeof(int), src);

    /* If window is no longer locked, process queue */
    if( win->locked == 0 ) {

      while( win->lock_q_head != win->lock_q_tail ) { 
        /* while q not empty */
        try_lock(win->lock_q[win->lock_q_head].type, wid, 
                 &q_result);
        
        if(q_result == 1) { /* If lock successful, notify proc owning request
                               and remove request from queue */
          
          signal_safe_put(win->lock_q[win->lock_q_head].lock_flag,
                          &LOCK_CONFIRM, sizeof(int), 
                          win->lock_q[win->lock_q_head].src);
          
          win->lock_q_head = (win->lock_q_head + 1) % LOCK_Q_SIZE;
          
        } else {
          
          break; /* If last lock attempt failed, exit loop */
        
        }
        
      }

    }

  }
  
}

/* Attempt the lock, set req_granted to 1 if successful or 0 if unsuccessful */

void try_lock(int type, int wid, int* req_granted)
{
  struct MP_Window* win = windows[wid];

  if( type == 0 ) { /* request for unlock */
    
    win->count--;
    
    if( win->count < 0 ) {
      fprintf(stderr, "try_lock() - ERROR: count for win %d is negative\n",
              wid);
      exit(-1);
    }
    if( win->count == 0 ) /* If no procs are holding locks, mark */
      win->locked = 0;    /* window as unlocked                  */
    
    *req_granted = 1;
    
  } else if( type == 1 ) { /* request for shared lock */
    
    if( win->locked == 0 || win->locked == 1 ) { /* window is not locked or */
      *req_granted = 1;                          /* only has a shared lock */
      win->locked = type;
      win->count++;
    }
    else                     /* window has exclusive lock, can't do shared */
      *req_granted = 0;      /* lock now */
         
  } else { /* request for exclusive lock */
    
    if( win->locked != 0 ) /* window is locked, can't do exlusive lock now */
      *req_granted = 0;
    else {              /* window is not locked, we can grant exclusive lock */
      *req_granted = 1;
      win->locked = type;
      win->count++;
    }

  }

}

void recv_active_target_sync(int src)
{
  struct MP_msg_entry *msg;
  struct MP_msg_header *h=hdr[src];
  int i, n, m = 1, nic, vec, op, req_granted;
  int active_targ_hdr[2];

  dbprintf("  recv_active_target_sync(%d)\n", src);

  if(recv_q[src]==NULL) { /* Have not yet created a msg entry */
    msg = recv_q[src] = create_os_msg_entry(NULL, &active_targ_hdr, h->nbytes, 
                                            h->src, -1, h->tag, -1);
    msg->nbl[0] = msg->nbytes; /* We are only reading on sd[src][0] */
  } else {
    msg=recv_q[src];
  }

  while( m > 0 && msg->nleft > 0 ) {
    m = 0;
    
    if( ! GOT_HDR ) {  /* Read the header separately */
      dbprintf("    reading %d bytes of header from node %d <--", 
               msg->nhdr, msg->src);
      
      n = read(sd[src][0], msg->phdr, msg->nhdr);
      
      update_msg_progress( msg, 0, &n);
      
      if( GOT_HDR ) check_header( &msg, src );
      
    }
    
    if( GOT_HDR && msg->nbl[0] > 0 ) {
      dbprintf("    reading %d bytes of data from node %d nic %d <--", 
               msg->nleft, msg->src, 0);
      
      n = read(sd[src][0], msg->ptr[0], msg->nbl[0]);

      update_msg_progress( msg, 0, &n);

    }
    m += n;
  }
  
  if (msg->nleft > 0 ) {

    dbprintf("    Some of message left, service later\n");

  } else {

    /* active_targ_hdr[0] indicates window ID */
    /* active_targ_hdr[1] indicates type of active target msg */

    /* Got all of msg */
    if(active_targ_hdr[1]==0) {
      dbprintf("    Got all of message, doing post\n");
      windows[active_targ_hdr[0]]->post_received[src-nprocs] = 1;
      
    } else {
      dbprintf("    Got all of message, doing complete\n");
      windows[active_targ_hdr[0]]->post_sent[src-nprocs] = 0;
    }

    /* Reset msg */
    msg->buf = NULL;
    recv_q[src] = NULL;

    /* Check for more data on one-sided sd */
    n = recv( sd[src][0], (char *)h, HDRSIZE, MSG_PEEK);
    if(n == HDRSIZE)
      service_os_socket(src);

  }
}

/* Call appropriate function to handle incoming data on one-sided channel from
   proc <node> */

void service_os_socket(int node)
{
  n_sockets_serviced++;

  if(hdr[node]->tag == 0) { /* Handle incoming put */

    dbprintf("one-sided msg is an incoming put\n");
    recv_put_data(node);

  } else if(hdr[node]->tag == 1) { /* Handle get request */

    dbprintf("one-sided msg is a get request\n");
    recv_get_request(node);

  } else if(hdr[node]->tag == 2) { /* Reply to get request */
    
    dbprintf("one-sided msg is a reply to get request\n");
    recv_get_reply_data(node);

  } else if(hdr[node]->tag > 2 && hdr[node]->tag < 15) {

    dbprintf("one-sided msg is an accumulate operation\n");
    recv_accum_operation(node);

  } else if(hdr[node]->tag == 15) {
    
    dbprintf("one-sided msg is a lock request\n");
    recv_lock_request(node);

  } else if(hdr[node]->tag == 16) {
    
    dbprintf("one-sided msg is an active-target synchronization\n");
    recv_active_target_sync(node);

  } else {

    dbprintf("one-sided msg has invalid tag\n");
    exit(-1);

  }
}

/* SIGIO handler for asynchronous I/O of sends and receives
 */

void start_protect_from_sig_handler()
{
  protect_from_sig_handler = 1;
}

void stop_protect_from_sig_handler()
{  
  protect_from_sig_handler = 0;
  
  if(received_signal == 1) {
    dbprintf("\n*** Received signal during protect from sig handler ***\n");

    /* A SIGIO may arrive before we reset flag to 0, so we have
       a check in sigio_handler() to correct for this */
    
    received_signal = 0;

    need_to_block = 1;
    sigio_handler();
    need_to_block = 0;

  }

}

void sigio_handler()
{
   int node, n, bail, matched;
/*   struct timeval timeout;*/
   struct MP_msg_entry *p, *last_p, *msg;
   struct MP_msg_header *h;
   char *membuf;

   /* If we entered the sigio handler while protect_from_sig_handler flag is
      set, then we should set received_signal flag and exit.  The sigio 
      handler will be called when stop_protect_from_sig_handler() is called */

   if(protect_from_sig_handler == 1) { 
     received_signal = 1;
     return;
   } else if(received_signal == 1) {

     /* Make sure this flag is unset before we proceed, otherwise we'll
        call sigio_handler everytime we call malloc, free, etc */

     received_signal = 0; 
   }

#if ! defined (POSIX_SIGNALS)    /* POSIX does this automatically */
   Block_Signals();
#else
   if(need_to_block) Block_Signals(); /* This is needed so we can call
                                         sigio_handler explicitly in code */
   already_blocking++; /* Protect from other functions unblocking signals */   
#endif

   dbprintf("\n*** Entered sigio_handler() from a SIGIO\n");

  /* Select() doesn't work properly under Linux 2.1.x, 
   * nor under OSF 4.0 or SunOS 5.6 */

/*
#if ! defined(linux) && ! defined(__osf__)
   timeout.tv_sec = 0; timeout.tv_usec=0;
   r = select(nprocs, &rsds, &wsds, NULL, &timeout);
#endif
*/

   /* We need to check the one-sided socket descriptors in case an outgoing
      one-sided operation has been initiated but not finished */
   for( node=0; node<nprocs*2; node++) 
     if( node != myproc && node != myproc+nprocs) {
       if( wactive[node] == active ) {
         n_sockets_serviced++;
         do_send(node);
      }
   }

   for( node=0; node<nprocs; node++) if( node != myproc ) {
      if( ractive[node] == active ) {
         n_sockets_serviced++;
         do_recv(node);
      }
   }

   /* Service all incoming data on one-sided channels */
   for( node=nprocs; node<nprocs*2; node++) if(node != myproc+nprocs) {

     if(recv_q[node] != NULL) { /* Partial msg waiting */
       dbprintf("continuing service on partial one-sided msg\n");
       service_os_socket(node);

     } else { /* Check for new msg */
       h = hdr[node];
       n = recv( sd[node][0], (char *)h, HDRSIZE, MSG_PEEK);
       if( n == HDRSIZE ) {
         dbprintf("one-sided msg found (& %d %d %d %d)\n", h->dest_ptr, 
                  h->src, h->nbytes, h->tag);

         service_os_socket(node);
       }
     }

   }

      /* Check for recv's posted as from any source (-1) */

   if( recv_q[-1] != NULL ) {

         /* Peek at each inactive TCP buffer and activate if 
          * data is found */

      for( node=0; node<nprocs; node++) if( node != myproc ) {

         bail = false;

         while( ractive[node] == inactive && ! bail ) {

            dbprintf("   Peeking at the buffer for node %d - ", node);
            h = hdr[-1];

            n = recv( sd[node][0], (char *)h, HDRSIZE, MSG_PEEK);

            if( n == HDRSIZE ) {     /* create a new message */
               dbprintf("msg found\n");

                   /* Try to match with a header posted to recv_q[-1] */

               matched = false;
               last_p = NULL;
               p = recv_q[-1];

               while( p != NULL ) {

                  if( h->nbytes <= p->nbytes && 
                     ( p->tag < 0 || p->tag == h->tag ) ) {  /* matched */

                     dbprintf("     Matched (%d %d %d) to a msg in recv_q[-1]\n",
                           h->nbytes, node, h->tag);

                     if( h->nbytes < p->nbytes ) {  /* shorter msg OK */

                        dbprintf("WARNING: Message of %d Bytes is shorter "
                              "than the posted recv of %d Bytes\n",
                              h->nbytes, p->nbytes);
                     }

                     set_message( p, h->nbytes, node, h->tag);

                     matched = true;
                     
                        /* Take the msg out of the recv_q[-1] list */

                     if( p == recv_q[-1] ) recv_q[-1] = p->next;
                     else last_p->next = p->next;

                        /* Add it to the start of the recv_q[node] queue */

                     p->next = recv_q[node];
                     recv_q[node] = p;

                     p->phdr = (void *) hdr[node];

                     ractive[node] = active;
                     do_recv( node );

                     if( recv_q[-1] == NULL ) bail = true;
                     n_sockets_serviced++;

                     break;
                  }
                  last_p = p;
                  p = p->next;
               }

               if( ! matched ) {
                  dbprintf("     Pushing (%d %d %d) to recv_q[%d] and buffering\n",
                        h->nbytes, node, h->tag, node);

                  membuf = malloc(h->nbytes); /* malloc in sigio handler bad */
                  if( ! membuf ) errorx(0,"tcp.c sigio_handler() - malloc() failed");

                  msg = create_msg_entry( membuf, h->nbytes, node, -1, h->tag);

                  msg->recv_flag = 2;
                  post( msg, msg_q );
                  post( msg, recv_q );

                  do_recv( node );
                  n_sockets_serviced++;
               }
            } else {
               dbprintf("nothing\n");
               bail = true;
            }
         }
         if( recv_q[-1] == NULL ) break;
      }
   }

   n_interrupts++;

#if ! defined (POSIX_SIGNALS)    /* POSIX does this automatically */
   Unblock_Signals();
#else
   already_blocking--; /* ok to unblock now */
   if(need_to_block) Unblock_Signals();
#endif
   dbprintf("*** signals re-enabled in sigio_handler()\n\n");
}


void tcp_asend( struct MP_msg_entry *msg )     
{
  int dest = msg->dest;

   /* add nprocs to dest if this is one-sided msg */	

   dest += ( msg->one_sided ? nprocs : 0 );

   post( msg, send_q );

      /* Initiate the write by calling do_send() if sd is not active now */

   if( wactive[dest] == inactive ) {

      do_send(dest);

   }
}

void tcp_arecv( struct MP_msg_entry *msg )     
{
   recv_from_q( msg );   /* Check the msg_q[*] first */

   if( ! GOT_HDR ) {             /* Did not find a message */

      post( msg, recv_q );       /* Post the msg to the recv_q */

         /* initiate the read by calling do_recv() if not already active */

      if( msg->src < 0 ) {     /* Initiate a recv from an unknown source */

         dbprintf("  Calling sigio_handler() to initiate the recv(src = -1)\n");

         sigio_handler();

      } else if( ractive[msg->src] == inactive ) {  /* Initiate the recv */

         do_recv( msg->src );

      }
   }
}


void tcp_block(struct MP_msg_entry *msg)
{
   int counter = 0;
   double t0 = 0.0;

   while(msg->nleft != 0) {
/*      usleep(1);*/
      if( ++counter > 200000 ) {
         stall_check( &t0, msg->nbytes, msg->src, msg->tag);
      }
   }
}



/* Barrier sync using separate blocking sockets
 *   - this avoids conflicts with the existing ARecvs so you can for example
 *     post an ARecv and do an MP_Sync() before sending the data to insure
 *     that the ARecv is preposted and the data will bypass the send buffer.
 *   - This is not the fastest sync(), but it is a simple and sure method.
 *   - Must protect against interrupts so put send/recv in while loops.
 */

void tcp_sync()
{
   int r1, r2, r3, r4, token = 777, szoi = sizeof(int), tid;

   if( nprocs == 1) return;

/* If the signals are blocked here, do_send() may not completely
 * send messages pushed to a send buffer.*/

/*   Block_Signals();*/
   dbprintf("\n>MP_Sync() started - ");
   r1 = r2 = r3 = r4 = -1;

   tid = trace_start( 0, 0);   /* Only active if compiled with -DTRACE */

   if( myproc == 0 )
      while(r1 <= 0 ) r1 = send(sd_next, (char *)&token, szoi, 0);
   while( r2 <= 0 ) r2 = recv(sd_prev, (char *)&token, szoi, 0);
   while( r3 <= 0 ) r3 = send(sd_next, (char *)&token, szoi, 0);
   while( r4 <= 0 ) r4 = recv(sd_prev, (char *)&token, szoi, 0);
   if( myproc != 0 )
      while(r1 <= 0 ) r1 = send(sd_next, (char *)&token, szoi, 0);

   trace_end( tid, 0, 0);
   dbprintf("done\n");
/*   Unblock_Signals();*/
}

/* If USE_SHM is defined and all procs are on an SMP node, use smp_sync() */

void MP_Sync()
{
  if( smp_mode ) smp_sync();
  else           tcp_sync();
  //tcp_sync();
}



/* Utility routines for above */

void dump_and_quit()
{
   lprintf("\n\nReceived a SIGINT (cntl-c) --> dump and exit\n");

   dump_msg_state();

   if( smp_mode ) smp_cleanup();

   fclose(mylog);
   sleep(1);
   set_status(-1, "INTERRUPT", "Received a Control-C or SEG fault, setting abort signal");
}

/* Dump the current state of all sends, recvs, and recvs buffered in msg_q[].
 */

void dump_msg_state()
{
   int node;
   struct MP_msg_entry *msg, *p;

   lprintf("Dumping the current state of the message queues:\n");

   for( node=0; node<nprocs; node++) if( node != myproc ) {
      msg = send_q[node];
      while( msg != NULL ) {
         lprintf("  --> Sent %d of %d bytes to node %d with tag %d %s\n", 
             msg->nbytes - msg->nleft + HDRSIZE, msg->nbytes, node, msg->tag,
             msg->send_flag == 3 ? "- the rest is buffered" : "");
         msg = msg->next;
   }  }

   for( node=0; node<nprocs; node++) if( node != myproc ) {
      msg = recv_q[node];
      while( msg != NULL ) {
         lprintf("  <-- Received %d of %d bytes from node %d with tag %d\n", 
             msg->nbytes - msg->nleft + HDRSIZE, msg->nbytes, node, msg->tag);
         msg = msg->next;
   }  }

   for( node=0; node<nprocs; node++) {
      p = msg_q[node];
      while( p != NULL ) {
         lprintf("  <-> Buffered a %d byte message from node %d with tag %d\n", 
              p->nbytes, p->src, p->tag);
         p = p->mnext;
   }  }
}


/* Update msg for NIC i and n bytes */

void update_msg_progress( struct MP_msg_entry *msg, int nic, int *n )
{
      /* Check for a TCP read/write error */

   if( *n < 0 && errno == EWOULDBLOCK ) *n = 0;
   if( *n < 0 ) {
      check_status();
      if( msg->send_flag > 0 ) {     /* This is a send */
         lprintf("While writing %d bytes to %d on nic #%d <--", 
                  msg->nbytes, msg->dest, nic);
         errorx(errno,"send() - write() failed");
      } else {
         lprintf("While reading %d bytes from %d on nic #%d <--", 
                  msg->nbytes, msg->src, nic);
         errorx(errno,"recv() - read() failed");
      }
   }

   dbprintf(" %d bytes transferred\n",*n);

      /* Update the byte counters for the message header or data segment */

   msg->nleft  -= *n;

   if( msg->nhdr > 0 ) {      /* We are updating the header */

      msg->nhdr -= *n;
      msg->phdr += *n;

   } else {                   /* Else we are updating the data count */

      msg->nbl[nic] -= *n;
      msg->ptr[nic] += *n;

   }

   msg->stats++;
}

     /* Send to msg_q[], including mallocing a send buffer */

void send_to_q( struct MP_msg_entry *msg )
{
   char *membuf;

   dbprintf("  Sending the message (& %d %d %d) to my own msg_q[myproc]\n",
           msg->nbytes, msg->src, msg->tag); 

   membuf = malloc(msg->nbytes);
   if( ! membuf ) errorx(0,"tcp.c send_to_self() - malloc() failed");

   MP_memcpy(membuf, msg->buf, msg->nbytes);

   msg->buf = membuf;
   msg->nleft = 0;
   msg->nhdr = 0;
   msg->send_flag = 3;

   post( msg, msg_q );
}


   /* Post a msg to the end of the msg_q[], recv_q[], or send_q[] queues
    */

void post( struct MP_msg_entry *msg, void **q)
{
   struct MP_msg_entry *p;
   int dest;
   
   if( q == msg_q ) {

      dbprintf("  posting msg (& %d %d %d) to msg_q[%d]\n",
              msg->nbytes, msg->src, msg->tag, msg->src); 

      if( msg_q[msg->src] == NULL ) {
         msg_q[msg->src] = msg;
      } else {
         p = msg_q[msg->src];
         while (p->mnext != NULL ) p = p->mnext;
         p->mnext = msg;
      }

   } else if( q == recv_q ) {

      dbprintf("  posting msg (& %d %d %d) to recv_q[%d]\n",
              msg->nbytes, msg->src, msg->tag, msg->src); 

      if( recv_q[msg->src] == NULL ) {
         recv_q[msg->src] = msg;
      } else {
         p = recv_q[msg->src];
         while (p->next != NULL ) p = p->next;
         p->next = msg;
      }

   } else {

      dest = msg->dest;

      /* add nprocs to dest if this is a one-sided msg */
      dest += ( msg->one_sided ? nprocs : 0 );
      
      dbprintf("  posting msg (& %d %d %d) to send_q[%d]\n",
              msg->nbytes, msg->dest, msg->tag, dest); 
      
      if( send_q[dest] == NULL ) {
         send_q[dest] = msg;
      } else {
         p = send_q[dest];
         while (p->next != NULL ) p = p->next;
         p->next = msg;
      }
   }
}


/* Retrieve a msg from msg_q[] if src and tag match.
 *   If the message is smaller than requested, all
 *   is copied and msg->nbytes is set to the lower value,
 *   which the user can access through MP_Get_Last_Size().
 *
 * In the SIGIO version, if a partial message is found it is
 * copied over immediately to save any additional buffering costs.
 */

void recv_from_q( struct MP_msg_entry *msg )
{
   struct MP_msg_entry *p, *last_p;
   int i, num_qs = 1, tsrc = 0, n, nic;
   char *ptr;

   if( msg->src >= 0 && msg_q[msg->src] == NULL ) return;

   dbprintf("    recv_from_q() trying to match (& %d %d %d) --> ", 
            msg->nbytes, msg->src, msg->tag);

   if( msg->src < 0 ) num_qs = nprocs;  
   else tsrc = msg->src;

     /* Check msg_q[msg->src] if the src is known,
        otherwise cycle thru all queues if src = -1 */

   for( i=0; i<num_qs; i++) {

      last_p = NULL;
      p = msg_q[tsrc];
      while( p != NULL ) {

            /* Check to see that the header has been read, and
             * nbytes and tag match.
             */

         if( p->nhdr == 0 && p->nbytes <= msg->nbytes &&
            ( msg->tag < 0 || p->tag == msg->tag ) ) { /* Found a match */

            if( p->nbytes < msg->nbytes ) {  /* shorter msg OK */

               dbprintf("\nWARNING: Message of %d Bytes is shorter "
                     "than the posted recv of %d Bytes\n",
                     p->nbytes, msg->nbytes);
            }

            set_message( msg, p->nbytes, tsrc, p->tag);

            if( p->nleft == 0 ) {        /* and it's a completed message */
               dbprintf("found a completed match in msg_q[%d]\n", tsrc);

               MP_memcpy( msg->buf, p->buf, msg->nbytes);

               msg->nleft = 0;
               msg->nhdr  = 0;

            } else {

               dbprintf("found an incomplete msg match in msg_q[%d]\n", tsrc);

               Block_Signals();

                  /* Copy the msg header and partial data from msg_q[] */

               msg->nhdr     = 0;
               msg->nleft    = p->nleft;
               msg->mtu      = p->mtu;
               msg->trace_id = p->trace_id;
               msg->stats    = p->stats;

               ptr = p->buf;

               for( nic=0; nic<nics; nic++ ) {

                  n = msg->nbl[nic] - p->nbl[nic];

                  dbprintf("  copying %d bytes from nic %d buffer\n", n, nic);

                  MP_memcpy( msg->ptr[nic], ptr, n);

                  ptr +=  msg->nbl[nic];

                  msg->nbl[nic] -= n;
                  msg->ptr[nic] += n;
               }

                  /* Exchange msg with p at the head of the recv_q[src] */

               msg->next = p->next;
               recv_q[tsrc] = msg;

               Unblock_Signals();
            }

            n_recv_buffered++;  nb_recv_buffered += msg->nbytes - msg->nleft;

                  /* Take out of the msg_q list */

            if( last_p == NULL ) msg_q[tsrc] = p->mnext;
            else last_p->mnext = p->mnext;

            free(p->buf);
            p->buf = NULL;                 /* Mark free for reuse */
            return;                        /* Then bail */
         }
         last_p = p;
         p = p->mnext;
      }
      tsrc++;
   }

   if( msg->src == myproc )
      errorx(msg->nbytes, "tcp.c recv_from_q() - no message-to-self available");

   dbprintf("no match\n");
}

struct MP_msg_entry *create_os_msg_entry(void* dest_ptr, void *buf, int nbytes,
                                         int src, int dest, int tag, int wid)
{
  struct MP_msg_entry *msg = create_msg_entry(buf, nbytes, src, dest, tag);

  msg->dest_ptr = dest_ptr;
  msg->one_sided = 1; /* Send over one-sided sockets */
  msg->window_id = wid; /* Set window id */
  
  if(dest<0)
    msg->phdr = (void *) hdr[src+nprocs];

  return msg;
}

struct MP_msg_entry *create_msg_entry( void *buf, int nbytes,
                                       int src, int dest, int tag)
{
   struct MP_msg_entry *msg;
   int mid_start;
   static int mid = 0;

   mid_start = mid;
   while( msg_list[++mid%MAX_MSGS]->buf != NULL ) { 
      if( mid%MAX_MSGS == mid_start ) {
         lprintf("You have more than MAX_MSGS outstanding messages\n");
         lprintf("Edit globals.h to increase MAX_MSGS then recompile and relink\n");
         errorx(MAX_MSGS,"You have more than MAX_MSGS outstanding message headers");
      }
   }
   mid = mid%MAX_MSGS;
   msg = msg_list[mid];

   *msg = msg_null;  /* sets the default values from globals.h */

   msg->id = mid;
   msg->buf = msg->cur_ptr = msg->ptr[0] = buf;

   set_message( msg, nbytes, src, tag);

   if( dest < 0 ) {        /* This is a recv() */
      msg->recv_flag = 1;
      msg->dest = myproc;
      msg->phdr = (void *) hdr[src];
   } else {                /* This is a send() */
      msg->dest = dest;
      msg->send_flag = 1;
   }

   if( nics > 1 ) msg->mtu = MTU_MIN;
   else msg->mtu = 1000000;

   return msg;
}

/* Check the header to see if it matches the current message that we are
 * looking for.  If the msg tag is less than 0, match any tag.
 *
 * If the header doesn't match, check recv_q[] for a match or post
 * to msg_q[].  Either way, process the intervening message to get
 * to the one we want.
 */

void check_header( struct MP_msg_entry **msg, int src)
{
   struct MP_msg_entry *p, *tp, *last_p;
   char *buffer;
   struct MP_msg_header *h;

   h = hdr[src];

   if(src >= nprocs) {

     check_os_header(msg, src);

   } else 

     if( h->nbytes <= (*msg)->nbytes && 
         ( (*msg)->tag < 0 || h->tag == (*msg)->tag ) ) {

      dbprintf("    * Header matched - (%d %d %d)\n", h->nbytes, h->src, h->tag);

      if( h->nbytes < (*msg)->nbytes ) {  /* shorter msg OK */

         dbprintf("WARNING: Message of %d Bytes is shorter "
               "than the posted recv of %d Bytes\n",
               h->nbytes, (*msg)->nbytes);
      }

      set_message( (*msg), h->nbytes, h->src, h->tag);
      (*msg)->nhdr = 0;
      (*msg)->nleft -= HDRSIZE;

   } else {

      dbprintf("     Header mismatch (%d %d %d), ", 
            h->nbytes, h->src, h->tag);

         /* Reset the current message byte counts and header pointer */

      (*msg)->nhdr = HDRSIZE;
      (*msg)->nleft = (*msg)->nbytes + HDRSIZE;
      (*msg)->phdr = (void *) hdr[src];

         /* Wrong msg, check entire recv_q[] for a match.
          * If found, move the match to the start of the
          * queue and process. 
          * If not found, create a msg and post to msg_q[]
          * and start of recv_q[].
          * do_recv or MP_Recv will process it first,
          * then continue until the desired message is found.
          */

      last_p = NULL;
      p = recv_q[src];

      while( p != NULL ) {    /* Check all msgs in recv_q[src] for a match */

         if( h->nbytes <= p->nbytes && 
             (p->tag < 0 || h->tag == p->tag) ) {

            dbprintf("but found a match in recv_q[]\n");

            if( p != recv_q[src] ) {   /* Move the match to the q start */

               last_p->next = p->next;

               tp = recv_q[src];
               recv_q[src] = p;
               p->next = tp;
            }

            *msg = p;
            break;
         }
         last_p = p;
         p = p->next;
      }

      if( p == NULL ) {  /* No match found, push to the msg_q[] */

         dbprintf("pushing to the msg_q[] for processing\n");

         buffer = malloc( h->nbytes );
         *msg = create_msg_entry( buffer, h->nbytes, h->src, -1, h->tag );

         (*msg)->next = recv_q[src];
         recv_q[src] = (*msg);

         post( (*msg), msg_q );     /* Add to the end of the msg_q[] */
      }

         /* We already have the header, so adjust the byte counts down */

      (*msg)->nhdr = 0;
      (*msg)->nleft -= HDRSIZE;

   }
}

void check_os_header( struct MP_msg_entry **msg, int src)
{
  struct MP_msg_header *h=hdr[src];

  if( (*msg)->dest_ptr == h->dest_ptr &&
      (*msg)->nbytes == h->nbytes     &&
      (*msg)->tag == h->tag) {
    
    dbprintf("  matched one-sided header (%d %d %d)\n", h->dest_ptr, 
             h->nbytes, h->tag);

    /* Not necessary since we already called create_os_msg_entry I think... */
    /*
      set_os_message( msg, h->dest_ptr, h->nbytes, h->src, h->tag );
      msg->nbytes -= HDR_SIZE;
      msg->nhdr = 0;
    */

  } else {
    fprintf(stderr, "check_os_header() - ERROR: Header did not match msg\n");
    exit(-1);
  }
}

/* MP_Bedcheck() puts all processes to sleep on a SIGUSR1 signal,
 * then wakes them up on the next SIGUSR1 signal.
 * NOTE: The pThreads package uses SIGUSR1 & 2, so this function will
 * not work if pThreads is being used to manage the message queue.
 *
 *  mplite/bin/mpstop - reads .mplite.config and puts the current proc to bed
 *  mplite/bin/mpcont - reads .mplite.config and wakes the current proc up
 */

static int bedtime = 0;

void MP_Bedcheck()
{
   Block_SIGUSR1();
   MP_iMax( &bedtime, 1);

   if( bedtime ) {
      set_status(0, "snoozing", "Put to sleep using SIGUSR1, issue again and wait to wake up");
      while( bedtime ) {
         Unblock_SIGUSR1();
         sleep(60);
         Block_SIGUSR1();
         MP_iMin( &bedtime, 1);
      }
      set_status(0, "running", "Woke up from a deep SIGUSR1 sleep");
   }
   Unblock_SIGUSR1();
}

void nanny()
{
   bedtime = 1-bedtime;
}

