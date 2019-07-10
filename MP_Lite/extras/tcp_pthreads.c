/* pTHREADS VERSION
 *
 *    This is experimental (& undocumented too).
 *
 */


/* Definitions for splitting messages across multiple NICs if available */

#define MIN_SPLIT 1500
#define MTU_MIN 8192

#define GOT_HDR  (msg->nhdr == 0)
#define HDR_SENT (msg->nhdr == 0)
#define HDRSIZE (int)(sizeof(struct MP_msg_header))

#include "globals.h"   /* Globals myproc, nprocs, mylog, interface, hostname */
#include "mplite.h"

#include <pthread.h>

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


/* OSF 4.0 version only works with Posix signals
 * most other systems work with either Posix or BSD
 */
#define POSIX_SIGNALS
/*#define _BSD_SIGNALS*/

#if defined (POSIX_SIGNALS)
  struct sigaction sigact;
  sigset_t sig_mask;
#endif

#define  TCP_PORT 5800
#define  TCP_PORT_MAX 5999

#define MAX_MSGS 10000


int n_send = 0, n_recv = 0, n_send_packets = 0, n_recv_packets = 0;
int nb_send = 0, nb_recv = 0, nb_send_buffered = 0, nb_recv_buffered = 0;
int n_recv_buffered = 0, n_send_buffered = 0;
int n_interrupts = 0, n_sockets_serviced = 0;
int sd[MAX_PROCS*2][MAX_NICS], sync_sd_prev, sync_sd_next;   /* socket descriptors */
int portnum[MAX_PROCS+1], mypid, lowestpid, buffer_orig = 0;
int nsmp = 0, nics;
int exit_thread = 0, socket_blocking, socket_nonblocking;
int salen = sizeof(struct sockaddr_in);
struct sockaddr_in serv_addr;
char cbuf[100];

struct MP_msg_entry  *msg_list[MAX_MSGS*2];
void **recv_q, **send_q, **msg_q;
struct MP_msg_header **hdr;
int *ractive, *wactive, active=1, inactive=0;
struct MP_get_complete* get_complete_head[MAX_PROCS];/* linked-list head ptr */
struct MP_get_complete* get_complete_last[MAX_PROCS];/* linked-list last ptr */
int already_blocking = 0;
int protect_from_sig_handler = 0;
int need_to_block = 0;
int received_signal = 0;

pthread_t ptid;

void Block_Signals() {}
void Unblock_Signals() {}



void MP_Init(int *argc, char ***argv)
{
   FILE *fd;
   int iproc, i, j, p, n, r, node, next, prev, tmp;
   int nic, one = 1, socket_flags, is_ip, ndots = 0;
   int port = TCP_PORT, sync_port, sync_port_next, listen_socket;
   char *ptr, garb[100], command[100];
   struct hostent *host_entry;
   struct in_addr ipadr;

   errno = -1;

   gethostname(myhostname,100);
   ptr = strstr(myhostname,".");
   if (ptr != NULL && ptr != myhostname) strcpy(ptr,"\0");
   mypid = getpid();
   myuid = getuid();


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

   fgets(garb, 90, fd);
   sscanf(garb,"%d",&nprocs);
   fgets(garb, 90, fd);
   sscanf(garb,"%d",&nics);
   fgets(garb, 90, fd);
   sscanf(garb, "%s", prog_name);

   for(i=0; i<nprocs; i++) {
      for(j=0; j<nics; j++) interface[i][j] = malloc(100*sizeof(char));
      if( nics == 1 ) {
         fscanf(fd,"%d %s\n",&iproc,          interface[i][0]);
      } else if( nics == 2 ) {
         fscanf(fd,"%d %s %s\n",&iproc,       interface[i][0], interface[i][1]);
      } else if( nics == 3 ) {
         fscanf(fd,"%d %s %s %s\n",&iproc,    interface[i][0], interface[i][1], 
                                              interface[i][2]);
      } else if( nics == 4 ) {
         fscanf(fd,"%d %s %s %s %s\n",&iproc, interface[i][0], interface[i][1], 
                                              interface[i][2], interface[i][3]);
      } else {
         errorx(nics, "4 NICs maximum supported at this time");
      }

      /* If these are IP numbers, convert into dot.style.interface.addresses.
       * If it is an alias, asign the real hostname instead.
       */

      for( nic=0; nic<nics; nic++) {

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

            host_entry = gethostbyname( interface[i][0] );
            strcpy( hostname[i], host_entry->h_name );

         }

      }

         /* Cut the hostname down */

      strcpy(hostname[i], interface[i][0]);
      ptr = strstr(hostname[i],".");
      if (ptr != NULL && ptr != hostname[i]) strcpy(ptr,"\0");

         /* Is this host an SMP node? */

      n = 0;
      for( j=0; j<=i; j++) {
         if( !strcmp(hostname[j],hostname[i]) ) n++;
      }

      if( !strcmp(hostname[i],myhostname) ) { 
         smpnum[i] = n-1; nsmp = n;
         is_smp[i] = 1;
         if( myproc < 0 ) myproc = i;
      } else {
         smpnum[i] = -n;
         is_smp[i] = 0;
      }
   }
   fclose(fd);

   if( myproc < 0 ) {
      errorxs(myproc,"%s matched no entry in .mplite.config",myhostname);
   }

   /* Get a listen port number starting at TCP_PORT and working up. */

   listen_socket = create_listen_socket( &port, buffer_size );


   /* For SMP nodes, myproc is set by the order the procs grabbed ports. */

   if( nsmp > 1 && port-TCP_PORT < nsmp ) {

      for( node=0; node<nprocs; node++) {
         if( smpnum[node] == port-TCP_PORT ) myproc = node;
      }

   }

   /* If port != TCP_PORT, the main port was busy and we need to signal
    * node 0 using a .changeport file.  For SMP nodes, we only do this
    * for port >= TCP_PORT+nsmp.  Otherwise, the node numbers for SMP nodes are
    * set in order by the ports they grabbed.  If node 0 is SMP, then we
    * also create this file to signal the other SMP procs that the master
    * has port TCP_PORT.  Otherwise, the lowest .changeport port number
    * becomes master (node 0).
    */

   if( port >= TCP_PORT+nsmp || (myproc == 0 && nsmp > 1) ) {

      sprintf(filename, ".changeport.%s.%d\0", myhostname, port);
      lprintf("Creating the lock file %s\n", filename);

      sprintf(command, "touch .changeport.%s.%d\0", myhostname, port);
      system( command );

      /* Other nodes must arbitrate for master if needed */

      if( myproc == 0 && nsmp > 1 && port >= TCP_PORT+nsmp) {

         sleep(5);
         p = TCP_PORT;
         while( p < port ) {
            sprintf(filename, ".changeport.%s.%d\0", myhostname, p);
            fd = fopen( filename, "r" );
            if( fd ) break;
            p++;
         }

         if( p == port ) {     /* Keep myproc = 0, I am the new master. */
            lprintf("*** Hail the new master!!!\n");
         } else {
            lprintf("I'm no master :(\n");
            fclose( fd );
            myproc = nprocs-1;   /* node 0 will assign my myproc later */
         }

      }
   }

   /* At this point, all nodes know their myproc unless an SMP proc had to
    * use a .changeport file to contact node 0.  In this case, node 0 will
    * assign myproc upon contact. */


   /* Handle first contact 
    *   --> Node 0 contacts port TCP_PORT on each node, or TCP_PORT++
    *       for SMP procs.
    *   ?-> If a time-out occurs, node 0 checks for an appropriate 
    *       .changeport... file to get the initial port number.
    *   --> Once contacted, each node sends its sync_port number to
    *       node 0.
    *   --> Then node 0 connects to each other node and sends the list
    *       main port numbers.
    */

   initialize_status_file();

   if( myproc == 0 ) {
      portnum[0] = port;
      for( node=1; node<nprocs; node++) {

         if( smpnum[node] >= 0 ) {
            portnum[node] = TCP_PORT + smpnum[node];
         } else {
            portnum[node] = TCP_PORT - smpnum[node] - 1;
         }

         sprintf(cbuf, "node %d port %d", node, portnum[node]);
         set_status(0,"contacting", cbuf);

         sd[node][0] = connect_to(node, portnum[node], 0, buffer_size);

         if( sd[node][0] <= 0 ) {   /* Bad port, contact via .changeport file */
            lprintf("\n--> Failed to connect, use .changeport - ");

              /* Check for a .changeport.hostname[node].port# file. */

            while( ++portnum[node] < TCP_PORT_MAX ) {

               sprintf(cbuf, "node %d port %d", node, portnum[node]);
               set_status(0,"changeport", cbuf);

               sprintf(filename, ".changeport.%s.%d\0", hostname[node], 
                                 portnum[node]);
               fd = fopen( filename, "r" );
               if( fd ) {
                  fclose( fd );

                  lprintf(" connected via a file to %d\n", portnum[node]);
                  fprintf(stderr," connected via a file to %d\n",portnum[node]);

                  sprintf( command, "rm -f %s\0", filename);
                  system( command );

                  break;
               }

            }

            if( portnum[node] == TCP_PORT_MAX ) {  /* No .changeport file */

               errorx(node, "Bad port and no .changeport file");

            } else {

               set_status(0,"contacting", cbuf);

               sd[node][0] = connect_to(node, portnum[node], 0, buffer_size);
               if( sd[node][0] <= 0 ) {
                  errorx(node, "Found .changeport file, but couldn't connect");
               }

            }
         }

         /* Now connect other NICS if needed */

          for( nic=1; nic<nics; nic++) {
             sd[node][nic] = connect_to(node, portnum[node], nic, buffer_size);
          }
      }

      lprintf("\nNode 0 sending port numbers to all the nodes\n");
      for( node=1; node<nprocs; node++) {
         portnum[nprocs] = node;
         n = send(sd[node][0], (char *)portnum, (nprocs+1)*sizeof(int), 0);
      }

   } else {    /* not master node */

      for( nic=0; nic<nics; nic++) {       /* connect for each NIC */
         sd[0][nic] = accept_a_connection_from(0, listen_socket, nic);

              /*
                TCP_NODELAY may or may not (AIX) be propagated to accepted
                sockets, so attempt to set it again.
               */

         r = setsockopt(sd[0][nic], IPPROTO_TCP, TCP_NODELAY, 
                        (char *)&one, sizeof(one));
         if( r != 0) errorx(errno,"MP_Init()-setsockopt(NODELAY)");

      }

      lprintf("Getting port numbers from node 0 - ");

      n = recv(sd[0][0], (char *)portnum, (nprocs+1)*sizeof(int), 0);

      if( port-TCP_PORT > nsmp-1 ) myproc = portnum[nprocs];

      if( portnum[nprocs] != myproc ) {
         lprintf("Node 0 thinks I'm node %d\n", portnum[nprocs]);
         errorx(portnum[nprocs], "Node 0 thinks I'm the wrong node number");
      }
      sprintf(cbuf, "node %d port %d connected to node 0", 
              myproc, portnum[myproc]);
      set_status(0,"connectd", cbuf);
      lprintf("done");
   }

   /* Now we all know myproc, so move .node.myhostname.mypid to .node#
    * where # is myproc.
    */

   sprintf(command, "mv .node.%s.%d .node%d", myhostname, mypid, myproc);
   system( command );

   lprintf("\n%s PID %d is node %d of %d connected to port %d\n\n", 
            myhostname, mypid, myproc, nprocs, portnum[myproc]);
   for(i=0; i<nprocs; i++) {
      lprintf("   %d ==> ", i);
      for( nic=0; nic<nics; nic++) { lprintf(" %s ", interface[i][nic]); }
      lprintf(" port %d\n", portnum[i]);
   }

   /* Do the rest of the handshaking  
    *   - Each node accepts connections from lesser nodes
    *   - Each node then initiates connections to greater nodes
    *   - A 'go' signal is sent around in between to keep things in sync
    */

   if( myproc > 0 ) {
      for( node=1; node<myproc; node++) {
         for( nic=0; nic<nics; nic++) {       /* connect for each NIC */
            sd[node][nic] = accept_a_connection_from(node, listen_socket, nic);
         }
      }

      n = recv(sd[myproc-1][0], (char *)&j, sizeof(int), 0); /* go signal */
        if( n < 0 ) errorx(errno,"MP_Init() - recv() go signal error");

      for( node=myproc+1; node<nprocs; node++) {
         for( nic=0; nic<nics; nic++) {     /* connect for each NIC */
            sd[node][nic] = connect_to(node, portnum[node], nic, buffer_size);
         }
      }
   }

   if( myproc < nprocs-1 ) {            /* send go signal for next proc */
      n = send(sd[myproc+1][0], (char *)&j, sizeof(int), 0);
        if( n < 0 ) errorx(errno,"MP_Init() - send() go signal error");
   }
   close(listen_socket);

   if( myproc == 0 && nsmp > 1 ) {
      sprintf(command, "rm -f .changeport.%s.%d\0", myhostname, port);
      system( command );
   }

   if( nprocs > 1 ) {
      i = sizeof(int);
      j = (myproc+1) % nprocs;
      getsockopt(sd[j][0], SOL_SOCKET, SO_RCVBUF,
                 (char *) &tmp, (void *) &i);
      lprintf("\nSend/Receive Buffers: %d ==> %d bytes\n\n",
               buffer_orig,tmp);
   }

   /* Set up blocking sockets to the next and previous nodes for MP_Sync() */

   sync_port = portnum[myproc]+1;
   for(i=0; i<nprocs; i++) {
      if( !strcmp(hostname[i], myhostname) && portnum[i] >= sync_port )
         sync_port = portnum[i]+1;
   }

   if( nprocs > 1 ) {
        lprintf("\n   Setting up special sockets for MP_Sync()\n");
      next = (myproc+1)%nprocs;
      prev = (myproc+nprocs-1)%nprocs;

      listen_socket = create_listen_socket( &sync_port, small_buffer_size );

      r = send(sd[prev][0], (char *)&sync_port,      sizeof(int), 0);
        if( r != sizeof(int) ) errorx(r,"MP_Init() sending sync port to prev");
      r = recv(sd[next][0], (char *)&sync_port_next, sizeof(int), 0);
        if( r != sizeof(int) ) errorx(r,"MP_Init() recv sync port from next");

      if( myproc == 0 ) {
         sync_sd_next = connect_to(next, sync_port_next, 0, small_buffer_size);
         if( nprocs == 2 ) {
            sync_sd_prev = sync_sd_next;
         } else {
            sync_sd_prev = accept_a_connection_from(prev, listen_socket, 0);
         }
      } else {
         sync_sd_prev = accept_a_connection_from(prev, listen_socket, 0);
         if( nprocs == 2 ) {
            sync_sd_next = sync_sd_prev;
         } else {
            sync_sd_next = connect_to(next, sync_port_next, 0, 
                                      small_buffer_size);
         }
      }
      lprintf("%d sockets created for MP_Sync()\n\n", nprocs == 2 ? 1 : 2);
      close(listen_socket);
   }

   if( nprocs > 1 ) {
      i = sizeof(int);
      getsockopt(sync_sd_next, SOL_SOCKET, SO_RCVBUF,
                 (char *) &tmp, (void *) &i);
      lprintf("\nSync Buffers: %d ==> %d bytes\n\n", buffer_orig, tmp);
   }

   trace_set0();

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
      one-sided msgs */
   msg_q  = (void **) malloc( (nprocs+1)*sizeof(void *) ) + 1;
   ractive = (int *) malloc( (nprocs+1)*sizeof(int) ) + 1;  /* [-1:nprocs-1] */
   wactive = (int *) malloc( (nprocs+1)*sizeof(int) ) + 1; 
   hdr = (struct MP_msg_header **) malloc( (nprocs+1)*sizeof(void *) ) + 1;
   recv_q = (void **) malloc( (nprocs+1)*sizeof(void *) ) + 1;
   send_q = (void **) malloc( (nprocs+1)*sizeof(void *) ) + 1;

   for( i = -1; i<nprocs; i++) {
      ractive[i] = wactive[i] = inactive;
      recv_q[i] = send_q[i] = msg_q[i] = NULL;
      hdr[i] = create_msg_header( -2, -2, -2);
   }

   /* Cntl-c or kill -HUP will dump the message queue state then exit.
    * sigio_handler() will handle SIGIO interrupts for fcntl() and 
    *    SIGPOLL interrupts when ioctl() is used
    */

#if defined (POSIX_SIGNALS)

   sigact.sa_handler = dump_and_quit;
   sigaction(SIGINT, &sigact, NULL);
   sigaction(SIGHUP, &sigact, NULL);

#elif defined (_BSD_SIGNALS)

   signal(SIGINT, dump_and_quit);
   signal(SIGHUP, dump_and_quit);

#endif

   socket_flags = socket_blocking = fcntl(sd[0][0], F_GETFL, 0);
#if defined (FNONBLK)
   socket_flags = socket_flags + FNONBLK;
#elif defined (FNONBLOCK)
   socket_flags = socket_flags + FNONBLOCK;
#else
   socket_flags = socket_flags + O_NONBLOCK;
#endif
   socket_nonblocking = socket_flags;


   /* Enable asynchronous I/O now on all sockets */

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


/*     pthread_cond_t cptr = PTHREAD_COND_INITIALIZER;*/
/*     pthread_mutex_t mptr = PTHREAD_MUTEX_INITIALIZER;*/
/*     struct timespec abstime;*/
/*     abstime.tv_sec = (long int) t0;*/
/*     abstime.tv_nsec = (t0 - abstime.tv_sec) * 1000000000 + 1000 * 1000;*/

   { double t0, t1, t2, t3, t4;
     t0 = MP_Time();
       usleep(   100 );
     t1 = MP_Time();
       usleep(  1000 );
     t2 = MP_Time();
       usleep( 10000 );
     t3 = MP_Time();
     dbprintf(" usleep(  100) = %lf us\n", (t1-t0)*1000000);
     dbprintf(" usleep( 1000) = %lf us\n", (t2-t1)*1000000);
     dbprintf(" usleep(10000) = %lf us\n", (t3-t2)*1000000);
   }

      dbprintf("Splitting off the msg_q_thread() using pthread_create()\n");

   errno = pthread_create( &ptid, NULL, msg_q_thread, NULL);

      dbprintf("Done splitting off the msg_q_thread()\n");

   if( errno != 0 ) errorx(errno, "Could not create the message queue thread\n");

   /* Handshake with all, just to be polite */

   for(i=0; i<nprocs; i++) if( i != myproc ) {
      MP_Send(&myproc,  sizeof(int), i, 0);
   }
   for(i=0; i<nprocs; i++) if( i != myproc ) {
      MP_Recv(&node, sizeof(int), i, 0);
      if( node != i ) 
        lprintf("WARNING - socket(%d) connected to node %d in MP_Init()\n",i, node);
   }


   /* Set up a default mask for the global operations that include all procs */

   MP_all_mask = malloc(nprocs*sizeof(int));
   if( ! MP_all_mask ) errorx(0,"tcp.c - malloc(MP_all_mask) failed");
   for( i=0; i<nprocs; i++) MP_all_mask[i] = 1;

   twiddle( 0.0 );   /* Initialize the twiddle increment */

   MP_Sync();   /* Just to test it out */

   n_send = n_send_buffered = nb_send_buffered = n_send_packets = 0;
   n_recv = n_recv_buffered = nb_recv_buffered = n_recv_packets = 0;
   n_interrupts = n_sockets_serviced = 0;

   if( myproc == 0 ) 
      fprintf(stderr, "----------  Done handshaking  ----------\n");
   lprintf("Done with all the handshaking, exiting MP_Init()\n");
   lprintf("------------------------------------------------\n");
   set_status(0, "running", "");
}


void MP_Finalize()
{
   int node, nic;
   int i;
      /* Close all sockets.  The side with the dynamic port must be closed
       * first, leaving it in a TIMEWAIT state for an ack for ~30 seconds.
       * Otherwise, the known ports would be tied up, forcing negotiation
       * for the next mprun if it was done within ~30 seconds.
       */

   MP_Sync();

   for(i=node=0;i<MAX_MSGS;i++) if(msg_list[i]->buf != NULL) node++;
   lprintf("Messages in use: %d\n", node);

   for( node=myproc+1; node<nprocs; node++) {
      for( nic=0; nic<nics; nic++) {
         close(sd[node][nic]);
      }
   }

   MP_Sync();

   if( nprocs > 1 ) close( sync_sd_next );

   for( node=0; node<myproc; node++) {
      for( nic=0; nic<nics; nic++) {
         close(sd[node][nic]);
      }
   }

   sleep(1);

   if( nprocs > 2 ) close( sync_sd_prev );

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

   exit_thread = 1;          /* signal msg_q_thread() to terminate */
   pthread_join( ptid, NULL);

   lprintf("************************************************\n");
   fclose(mylog);
   trace_dump();         /* Only active if compiled with -DTRACE */
}

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

   } else {                                         /* Recv from other */

      dbprintf("  going into a wait loop for %d bytes\n", msg->nleft);
      tcp_block( msg );
      dbprintf("  wait loop done\n");
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

      if( nics > 1 && msg->mtu < 10000000 ) msg->mtu *= 2;

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

void *msg_q_thread( void *arglist )
{
   int node, n_us = 1000, r=0;
   double t0, t1, t2, t3, t4;
   pthread_cond_t cptr = PTHREAD_COND_INITIALIZER;
   pthread_mutex_t mptr = PTHREAD_MUTEX_INITIALIZER;
   struct timespec abstime;

   dbprintf("Entering msg_q_thread()\n");
/*   pthread_detach( pthread_self() );*/

   while( ! exit_thread ) {
      for( node=0; node<nprocs; node++) if( node != myproc ) {
         if( wactive[node] == active ) {
            n_sockets_serviced++;
            dbprintf("  msg_q_thread servicing a send - %d\n", n_interrupts);
            do_send(node);
         }
      }

      for( node=0; node<nprocs; node++) if( node != myproc ) {
         if( ractive[node] == active ) {
            n_sockets_serviced++;
            dbprintf("  msg_q_thread servicing a recv - %d\n", n_interrupts);
            do_recv(node);
         }
      }

      n_interrupts++;

         /* sleep for n microseconds */

      usleep( n_us );

/*
      abstime.tv_sec = (long int) MP_Time();
      abstime.tv_nsec = (t0 - abstime.tv_sec) * 1000000000 + n_us * 1000;
      r = pthread_cond_timedwait( &cptr, &mptr, &abstime );
*/
   }

   dbprintf("Exiting msg_q_thread() - %d\n", n_interrupts);
}

void tcp_asend( struct MP_msg_entry *msg )     
{
   int dest = msg->dest;

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
 *   - this avoid conflicts with the existing ARecvs so you can for example
 *     post an ARecv and do an MP_Sync() before sending the data to insure
 *     that the ARecv is preposted and the data will bypass the send buffer.
 *   - This is not the fastest sync(), but it is a simple and sure method.
 *   - Must protect agains interrupts so put send/recv in while loops.
 */

void MP_Sync()
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
      while(r1 <= 0 ) r1 = send(sync_sd_next, (char *)&token, szoi, 0);
   while( r2 <= 0 ) r2 = recv(sync_sd_prev, (char *)&token, szoi, 0);
   while( r3 <= 0 ) r3 = send(sync_sd_next, (char *)&token, szoi, 0);
   while( r4 <= 0 ) r4 = recv(sync_sd_prev, (char *)&token, szoi, 0);
   if( myproc != 0 )
      while(r1 <= 0 ) r1 = send(sync_sd_next, (char *)&token, szoi, 0);

   trace_end( tid, 0, 0);
   dbprintf("done\n");
/*   Unblock_Signals();*/
}

/* Utility routines for above */

void dump_and_quit()
{
   lprintf("\n\nReceived a SIGINT (cntl-c) --> dump and exit\n");
   dump_msg_state();
   fclose(mylog);
   exit_thread = 1;   /* exit msg_q_handler() if using thread-based manager */
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


/* Check the header to see if it matches the current message that we are
 * looking for.  If the msg tag is less than 0, match any tag.
 *
 * If the header doesn't match, check recv_q[] for a match or post
 * to msg_q[].  Either way, process the intervening message to get
 * to the one we want.
 */

void check_header( struct MP_msg_entry **msg, int src)
{
   char *buffer;
   struct MP_msg_header *h;

   h = hdr[src];

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
     
     /* We already have the header, so adjust the byte counts down */
     
     (*msg)->nhdr = 0;
     (*msg)->nleft -= HDRSIZE;

   }
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
