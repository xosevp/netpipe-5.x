/* SYNCHRONOUS VERSION
 *
 *    An MP_Send() simply does writev()'s until the header and entire message
 * has been sent.  As mentioned above, it can lock up if the message is larger
 * than the TCP buffer and the destination node is not reading data out of 
 * its receive TCP buffer.  If the send is to itself, it uses send_to_q() to
 * shove the data to a message buffer.  An MP_ASend() simply does an MP_Send()
 * since it is assumed that all data will fit into the TCP buffers anyway.
 *    An MP_Recv() checks the message queue for a match to a previously 
 * buffered message. Otherwise, it does a readv() and tests the header for a 
 * match. If there is a message but the headers don't match, the rest of the 
 * message is read and buffered in the message q and another try is made for
 * the original message.  In this way, out-of-order messages are handled, 
 * although with less efficiency since they involve some buffering.  When the 
 * source is not known, MP_Recv() must check all incoming buffers and buffer
 * all the messages in the message q until the desired one is found. Obviously
 * it is much more efficient if the source is known.  MP_ARecv() simply creates
 * a message header but lets MP_Wait() handle the actual receive with 
 * MP_Recv().
 */


/* Definitions for splitting messages across multiple NICs if available */

#define MIN_SPLIT 1500
#define MTU_MIN 8192

#define GOT_HDR  (msg->nhdr == 0)
#define HDR_SENT (msg->nhdr == 0)
#define HDRSIZE (int)(sizeof(struct MP_msg_header))

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

#define  TCP_PORT 5800
#define  TCP_PORT_MAX 5999

#define MAX_MSGS 10000

/* OSF 4.0 version only works with Posix signals
 * most other systems work with either Posix or BSD
 */
#define POSIX_SIGNALS
/*#define _BSD_SIGNALS*/

#if defined (POSIX_SIGNALS)
  struct sigaction sigact;
  sigset_t sig_mask;
#endif

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

   /* Set up socket buffers for one-sided communications */

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

   sigaddset( &sig_mask, SIGUSR1);
   sigact.sa_handler = nanny;
   sigaction(SIGUSR1, &sigact, NULL);

   sigact.sa_handler = dump_and_quit;
   sigaction(SIGINT, &sigact, NULL);
   sigaction(SIGHUP, &sigact, NULL);

#elif defined (_BSD_SIGNALS)

   signal(SIGINT, dump_and_quit);
   signal(SIGHUP, dump_and_quit);

   signal(SIGUSR1, nanny);

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

   socket_flags = socket_flags + FASYNC;


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


   lprintf("************************************************\n");
   fclose(mylog);
   trace_dump();         /* Only active if compiled with -DTRACE */
}


/* MP_Send()  - Uses TCP level buffering, blocks until copied to TCP packets
 * MP_ASend() - Same since everything must go through the TCP buffer anyway
 * MP_Recv()  - Uses TCP level buffering, blocks until message copied in
 * MP_ARecv() - Sets up msg recv entry, delays anything until MP_Wait()
 * MP_Wait()  - MP_Recv() using *MP_ARecv() entry, or nothing for *MP_ASend()
 * MP_Block() - Does nothing
 */

void MP_Send(void *buf, int nbytes, int dest, int tag)
{
   struct MP_msg_entry *msg;
   int nic, n, tid, nb;
   double t0 = 0.0;

   dbprintf(">MP_Send(& %d %d %d)\n", nbytes, dest, tag);
   range_check(nbytes, dest);
   tid = trace_start( nbytes, dest);  /* Only active if compiled with -DTRACE */

   msg = create_msg_entry( buf, nbytes, myproc, dest, tag);

   if( dest == myproc ) {           /* Handle sending to self separately */

      send_to_q( msg );

   } else {

      while (msg->nleft > 0) {       /* Send the message until complete */

         for( nic=0; nic<nics; nic++) {

            if( nic == 0 && ! HDR_SENT ) {
               dbprintf("  sending %d bytes of header to node %d -->",
                        msg->nhdr, msg->dest);

               n =  write(sd[dest][0], msg->phdr, msg->nhdr);

               update_msg_progress( msg, 0, &n);
            }

            if( HDR_SENT && msg->nbl[nic] > 0 ) {
               dbprintf("  sending %d bytes of data to node %d nic %d -->",
                        msg->nbl[nic], msg->dest, nic);

               nb = MIN(msg->nbl[nic], msg->mtu);

               n =  write(sd[dest][nic], msg->ptr[nic], nb);

               update_msg_progress( msg, nic, &n);
            }

         }
              /* increase the packet size on a sliding scale to decrease
                 the effects of latency while keeping both NICs active */

         if( nics > 1 && msg->mtu < 10000000 ) msg->mtu *= 2;
      }

         /* Warn or bail if we have to wait too long */

      if( msg->stats >= 10000 && (msg->stats % 10000) == 0 ) {
         stall_check( &t0, msg->nbytes, msg->src, msg->tag);
      }
   }

   msg->buf = NULL;       /* Mark the msg header as free for reuse */

   trace_end( tid, msg->stats, 0);
   n_send++; nb_send += nbytes; n_send_packets += msg->stats;
   dbprintf("<MP_Send()\n\n");
}

void MP_ASend(void *buf, int nbytes, int dest, int tag, int *msg_id)
{
   MP_Send(buf, nbytes, dest, tag);
   *msg_id = -1;
}

/* Check the msg_q[] for a match first.
 * Grab and process messages from TCP buffers until the desired one is found.
 * Intervening messages get pushed to the msg_q[].
 */

void MP_Recv(void *buf, int nbytes, int src, int tag)
{
   struct MP_msg_entry *msg, *msg_orig;
   int nic, n, tid, tsrc;
   double t0 = 0.0;


   dbprintf(">MP_Recv(& %d %d %d)\n", nbytes, src, tag);
   if( src < 0 ) src = -1;
   range_check(nbytes, src);
   tid = trace_start( -nbytes, src);   /* -DTRACE --> active */
   n_recv++; nb_recv += nbytes;

   msg_orig = msg = create_msg_entry( buf, nbytes, src, -1, tag);

   if( src < 0 || msg_q[src] != NULL ) {
      recv_from_q( msg );                         /* Check msg_q[] first */
   }

        /* If the msg was not in msg_q[], get it through the TCP buffers */

   tsrc = src;
   while( msg_orig->nleft > 0 ) { 

         /* If src < 0 (ANYSOURCE), we need to cycle through sources until
          * a msg is found that matches the message tag.  This cuts
          * the performance greatly since we must peek into other buffers,
          * but it is a convenience at times.  This is implemented in a
          * way that it will not hurt the performance of normal communications
          * where the source node is specified.
          */

      tsrc = find_a_msg( msg, tsrc);
      msg->phdr = (void *) hdr[tsrc];

      while (msg->nleft > 0 ) {

         for( nic=0; nic<nics; nic++) {

            if( nic == 0 && ! GOT_HDR ) {  /* Read the header separately */
               dbprintf("  reading %d bytes of header from node %d <--",
                        msg->nhdr, tsrc);

               n = read(sd[tsrc][0], msg->phdr, msg->nhdr);

               update_msg_progress( msg, 0, &n);

               if( GOT_HDR ) check_header( &msg, tsrc);
            }

            if( GOT_HDR && msg->nbl[nic] > 0 ) {
               dbprintf("  reading %d bytes of data from node %d nic %d <--",
                        msg->nbl[nic], tsrc, nic);

               n = read(sd[tsrc][nic], msg->ptr[nic], msg->nbl[nic]);

               update_msg_progress( msg, nic, &n);
            }
         }

         if( msg->stats >= 10000 && (msg->stats % 10000) == 0 ) {
            stall_check( &t0, msg->nbytes, msg->src, msg->tag);
         }
      }

      if( msg_orig->nleft > 0 ) {
         msg = msg_orig;
         dbprintf("  Buffered an intervening message, now trying again for the original\n");
      }
   }

      /* These can be retrieved using the MP_Get_Last_Source()/Tag()/Size() 
       * functions. */

   last_src = msg->src;
   last_tag = msg->tag;
   last_size = msg->nbytes;

   msg_orig->buf = NULL;       /* Mark the msg as free for reuse */

   trace_end( tid, msg->stats, 0);
   n_recv++; n_recv_packets += msg->stats;
   dbprintf("<MP_Recv()\n\n");
}

void MP_ARecv(void *buf, int nbytes, int src, int tag, int *msg_id)
{
   struct MP_msg_entry *msg;

   range_check(nbytes, src);

   msg = create_msg_entry( buf, nbytes, src, -1, tag);
   *msg_id = msg->id;

   dbprintf("-MP_ARecv(& %d %d %d) posted\n", msg->nbytes, msg->src, msg->tag);
}

void MP_Wait(int *msg_id)
{
   struct MP_msg_entry *msg;

   /* *msg_id = -1 means it was an MP_ASend(), do nothing */

   dbprintf(">MP_Wait(%d)\n", *msg_id);

   if( *msg_id >= 0 ) {

      msg = msg_list[ *msg_id ];

      MP_Recv( msg->buf, msg->nbytes, msg->src, msg->tag);

      msg->buf = NULL;       /* Mark the msg header as free for reuse */
   }

   dbprintf("<MP_Wait(%d)\n", *msg_id);
}


void MP_Block(int *msg_id) { }

void MP_Sync()
{
   int r1, r2, r3, r4, token = 777, szoi = sizeof(int), tid;

   if( nprocs == 1) return;

   dbprintf(">MP_Sync() started -");
   tid = trace_start( 0, 0);   /* -DTRACE --> active */

   r1 = r2 = r3 = r4 = -1;

   if( myproc == 0 )
      r1 = send(sync_sd_next, (char *)&token, szoi, 0);

   r2 = recv(sync_sd_prev, (char *)&token, szoi, 0);
   r3 = send(sync_sd_next, (char *)&token, szoi, 0);
   r4 = recv(sync_sd_prev, (char *)&token, szoi, 0);

   if( myproc != 0 )
      r1 = send(sync_sd_next, (char *)&token, szoi, 0);

   if( r1 <= 0 || r2 <= 0 || r3 <= 0 || r4 <= 0 )
      errorx(0,"problem in MP_Sync()");

   trace_end( tid, 0, 0);
   dbprintf(" done\n\n");
}

/* Utility routines for above */

void dump_and_quit()
{
   lprintf("\n\nReceived a SIGINT (cntl-c) --> dump and exit\n");
   dump_msg_state();
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



         /* No recv_q[] in sync version, so just push the message
          * to msg_q[] and let MP_Recv() process it and grab the
          * next message until the desired one is found.
          */

      dbprintf("  pushing to the msg_q[] for processing\n");

      buffer = malloc( h->nbytes );
      *msg = create_msg_entry( buffer, h->nbytes, h->src, -1, h->tag );

      post( (*msg), msg_q );     /* Add to the end of the msg_q[] */

         /* We already have the header, so adjust the byte counts down */

      (*msg)->nhdr = 0;
      (*msg)->nleft -= HDRSIZE;

   }
}

/* If the source node has not been specified, we will have to cycle through
 * the TCP buffers looking for one with a message for MP_Recv() to
 * match against the message tag and nbytes.
 */

int find_a_msg( struct MP_msg_entry *msg, int tsrc)
{
   int n = 0;
   double t0 = 0.0;

   if( msg->src >= 0 ) return msg->src;  /* Good programmer has specified the source */

   while ( n != HDRSIZE ) {   /* Cycle through buffers for one with data */

      tsrc = (tsrc+1) % nprocs;

      if( tsrc == myproc ) {
         stall_check( &t0, msg->nbytes, msg->src, msg->tag);
         tsrc = (tsrc+1) % nprocs;
      }

      dbprintf("   Peeking at the TCP buffer for node %d - ", tsrc);


      fcntl(sd[tsrc][0], F_SETFL, socket_nonblocking);

      n = recv( sd[tsrc][0], (char *)hdr[-1], HDRSIZE, MSG_PEEK);

      fcntl(sd[tsrc][0], F_SETFL, socket_blocking);

      if( n == HDRSIZE ) {
         dbprintf("msg found\n");
      } else { 
         dbprintf("nothing\n");
      }
   }
   return tsrc;
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
