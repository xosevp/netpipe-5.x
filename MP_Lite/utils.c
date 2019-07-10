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
 * Contact: Dave Turner - turner@ameslab.gov
 */

#include "globals.h"
#include "mplite.h"

#include <netdb.h>

void push_to_buffer(struct MP_msg_entry *msg)
{
   int i;

   Block_Signals();

      /* Create send buffers for each NIC */

   for( i=0; i<nics; i++) if( msg->nbl[i] > 0 ) {

      msg->buffer[i] = malloc( msg->nbl[i] );

      if( ! msg->buffer[i] )
          errorx(msg->nbl[i], "utils.c push_to_buffer() - malloc(send buffer)");

      MP_memcpy(msg->buffer[i], msg->ptr[i], msg->nbl[i]);

      msg->ptr[i] = msg->buffer[i];
   }

   msg->send_flag = 3;

   n_send_buffered++;  nb_send_buffered += msg->nleft - msg->nhdr;
   dbprintf("    pushed %d bytes into send buffers\n", msg->nleft - msg->nhdr);
   Unblock_Signals();
}

int create_socket(int bsize)
{
   int sockdr, one = 1, d = sizeof(int), r;

   sockdr = socket(AF_INET, SOCK_STREAM, 0);
   if( sockdr < 0 ) errorx(errno,"MP_Init()-create_socket()");

      /* Set TCP_NODELAY */

   r = setsockopt(sockdr, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));
   if( r < 0) errorx(errno,"MP_Init()-setsockopt(TCP_NODELAY)");

      /* Set SO_REUSEADDR to prevent quick restart problems between runs */

   r = setsockopt(sockdr, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));
   if( r < 0) errorx(errno,"MP_Init()-setsockopt(SO_REUSEADDR)");

   /* Get the default buffer size */

   r = getsockopt(sockdr, SOL_SOCKET, SO_RCVBUF,
                  (char *) &buffer_orig,(void *) &d);
   if( r < 0) errorw(errno,"MP_Init()-getsockopt(SO_RCVBUF)");

   /* Attempt to reset the send and receive buffer sizes */

   if( bsize < buffer_orig ) {  /* set the buffers to bsize, 
                                   try increasing if too small */
     r = -1;
     while( r < 0 && bsize < buffer_orig ) {
       r = setsockopt(sockdr, SOL_SOCKET, SO_SNDBUF,
                      (char *) &bsize, sizeof(bsize));
       r = setsockopt(sockdr, SOL_SOCKET, SO_RCVBUF,
                      (char *) &bsize, sizeof(bsize));
       if( r < 0 ) buffer_size *= 2;
     }
   } else if( bsize > buffer_orig ) { /* set the buffers to bsize,
                                         try decreasing if too large */
     r = -1;
     while ( r < 0 && bsize > buffer_orig ) {
       r = setsockopt(sockdr, SOL_SOCKET, SO_SNDBUF,
                      (char *) &bsize, sizeof(bsize));
       r = setsockopt(sockdr, SOL_SOCKET, SO_RCVBUF,
                      (char *) &bsize, sizeof(bsize));
       if( r < 0 ) bsize /= 2;
     }
   }

   return sockdr;
}

int create_listen_socket(int port, int bsize)
{
   int sockdr, bound = -2;

   sockdr = create_socket(bsize);
   serv_addr.sin_family      = AF_INET;
   serv_addr.sin_port        = htons(port);
   serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);


   lprintf("Trying to bind to listen_socket using port %d - ", port);

   bound = bind( sockdr, (struct sockaddr *) &serv_addr, salen);

   if( bound < 0 ) {
      lprintf("\nFailed to bind to listen port %d - errno = %d\n",
                port, errno);
      errorx(errno, "MP_Init()-bind() failed");
   }

   lprintf("bind done - ");

   listen(sockdr, 5);
   lprintf("listen completed\n");

   return sockdr;
}


int accept_a_connection_from(int node, int listen_socket, int nic)
{
   int sockdr, iproc;

   lprintf("Node %d accepting a connection from node %d nic %d - ",
              myproc, node, nic);
   sprintf(cbuf, "Accepting a connection from node %d nic %d", node, nic);
   set_status(0,"accepting", cbuf);

   bzero( (char *) &serv_addr, sizeof(serv_addr) );
   sockdr = accept(listen_socket, (struct sockaddr *) &serv_addr,
                  (void *) &salen );
   if( sockdr < 0 ) errorx(errno,"MP_Init()-accept()");

   recv(sockdr, (char *)&iproc,  sizeof(int), 0);
   send(sockdr, (char *)&myproc, sizeof(int), 0);

   if( iproc != node ) errorx(iproc, "MP_Init() ack came back wrong");

   sprintf(cbuf, "Connected to node %d nic %d", node, nic);
   set_status(0,"accepted", cbuf);

   lprintf("accepted\n");

   return sockdr;
}


int connect_to(int node, int port, int nic, int bsize)
{
   struct hostent *addr;
   int sockdr=0, connected = -1, tries = 0, iproc, print_once = false;
   static int max_tries = 20;

   lprintf("Node %d connecting to node %d nic %d on port %d -", 
             myproc, node, nic, port);
   sprintf(cbuf, "Connecting to node %d nic %d on port %d", node, nic, port);
   set_status(0,"connecting", cbuf);

   addr = gethostbyname(interface[node][nic]);
   if( addr == NULL ) errorxs(0,"Invalid interface %s\n",interface[node][nic]);

   /* Try to connect for up to 15 seconds before giving up */

   while( connected < 0 && tries++ < max_tries )  {
      serv_addr.sin_family = addr->h_addrtype;
      bcopy(addr->h_addr, (void *) &(serv_addr.sin_addr.s_addr), 
            addr->h_length);

      serv_addr.sin_port = htons(port);

      /* Linux needs the socket reset after each try for some reason */

      sockdr = create_socket(bsize);
      connected = connect(sockdr, (struct sockaddr *) &serv_addr, salen);

      if (connected < 0) {
        close(sockdr);
        if (errno != ECONNREFUSED) errorx(errno,"utils.c MP_Init() connect_to()");
        errno = 0;
        if( print_once ) {
           fprintf(stderr, ".");
        } else {
           fprintf(stderr,"Node %d retrying port %d for node %d .", 
                   myproc, port, node);
           lprintf("\nNode %d retrying port %d for node %d .", 
                   myproc, port, node);
           print_once = true;
        }
        sleep(1);
      }
   }
   max_tries = ( max_tries-tries ) > 5 ? max_tries-tries : 5;

   if( connected < 0 ) {

      return -1;    /* Bail and try contacting via a .changeport file */

   } else {

      /* Check the connection by bouncing the processor numbers between nodes */

      send(sockdr, (char *)&myproc, sizeof(int), 0);
      recv(sockdr, (char *)&iproc,  sizeof(int), 0);

      sprintf(cbuf, "Connected to node %d nic %d on port %d", node, nic, port);
      set_status(0,"  connected", cbuf);
      if( print_once ) {
         fprintf(stderr, "connected \n");
      }
      lprintf("connected \n");

   }

   return sockdr;
}

/* Set (or reset for shorter messages) the parameters for the message. */

void set_message( struct MP_msg_entry *msg, int nbytes, int src, int tag)
{
   int i, nb = 0;

   msg->src = src;
   msg->tag = tag;
   msg->nbytes = msg->nbl[0] = nbytes;
   msg->nleft = nbytes + HDRSIZE;
   msg->ptr[0] = (void *) msg->buf;

      /* NIC #0 gets the msg header which is the first 3 integers of msg
         (nbytes, src, tag), and all NICs get part of the body */

   msg->phdr = (void *) msg; 
   msg->nhdr = HDRSIZE; 

   if( nbytes > MIN_SPLIT && nics > 1) {
      for( i=0; i<nics-1; i++) {
         msg->nbl[i] = ((nbytes/8)/nics)*8;
         nb += msg->nbl[i];
      }
      msg->nbl[nics-1] = nbytes - nb;
      for( i=1; i<nics; i++) {
         msg->ptr[i] = msg->ptr[i-1] + msg->nbl[i-1];
      }
   }
}

void set_os_message( struct MP_msg_entry *msg, void* dest_ptr, 
                     int nbytes, int src, int tag)
{
  set_message( msg, nbytes, src, tag );
  msg->dest_ptr = dest_ptr;
}

/* One-Sided msg header creation */
struct MP_msg_header *create_os_msg_header( void* dest_ptr, int nbytes, 
                                            int src, int tag)
{
   struct MP_msg_header *header;

   header = malloc( HDRSIZE );
   if (! header) errorx(0,"utils.c malloc of MP_msg_header (one-sided) failed");

   header->nbytes = nbytes;
   header->src = src;
   header->tag = tag;
   header->dest_ptr = dest_ptr;

   return header;
}

struct MP_msg_header *create_msg_header( int nbytes, int src, int tag)
{
   struct MP_msg_header *header;

   header = malloc( HDRSIZE );
   if (! header) errorx(0,"utils.c malloc of MP_msg_header failed");

   header->nbytes = nbytes;
   header->src = src;
   header->tag = tag;
   header->dest_ptr = NULL;

   return header;
}


/* The twiddle() function simply idles in a busy wait loop for 
 * approximately the amount of time specified by t.  Accuracy
 * should be at least within a factor of 2.
 */

void twiddle( double t )  /* t is the fractional number of seconds */
{
   static int isec;          /* increments for 1 second */
   int i, n;
   double t0, t1, MP_Time();

   if( t == 0.0 ) {      /* initiate twiddle by setting the iterations */

      t0 = MP_Time();

      n = 50000000;

      for( i=0; i<n; i++) if( i%2 == 3 ) printf(".");

      t1 = MP_Time();

      isec = (int) (n / (t1-t0));

      dbprintf("\nTwiddle() initialized to isec = %d\n", isec);

   } else {

      n = isec * t;

      dbprintf("%f",t0=MP_Time());

      for( i=0; i<n; i++) if( i%2 == 3 ) printf(".");

      dbprintf("%f",t1=MP_Time());
      dbprintf("  twiddle(%f) napped for around %f seconds\n", t, t1-t0);

   }
}
