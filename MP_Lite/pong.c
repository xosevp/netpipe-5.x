/* pong.c ping-pong test code for MP_Lite
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


/*                  pong.c Generic Benchmark code
 *
 *  Most Unix timers cannot be trusted for very short times, so take this
 *  into account when looking at the results.  This code also only times
 *  a single message passing event for each size, so the results may vary
 *  between runs.  For more accurate measurements, please grab NetPIPE from
 *  http://www.scl.ameslab.gov/ .
 */

#include <stdio.h>
#include <time.h>
#include "mplite.h"

#define MAX_SIZE 1048576
/*#define MAX_SIZE 65536*/

int main(int argc, char *argv[])
{
   int myproc, size, other_node, nprocs, i, last, tag = 0;
   double t1,t2,time, MP_Time();
   double *a, *b;
   double max_rate = 0.0, min_latency = 10e6;
   int irid, irid_a, irid_b;
   struct timespec ns, ns_rem;
   int usleep();

/*printf("starting pong.c\n");*/

#if defined (_CRAYT3E)
   a = (double *) shmalloc(132000*sizeof(double));
   b = (double *) shmalloc(132000*sizeof(double));
#else
   a = (double *) malloc(132000*sizeof(double));
   b = (double *) malloc(132000*sizeof(double));
#endif

   for(i=0; i<132000; i++) {
      a[i] = (double) i;
      b[i] = 0.0;
   }

   MP_Init(&argc, &argv);
   MP_Myproc( &myproc );
   MP_Nprocs( &nprocs );

   if( nprocs != 2) exit(1);
   other_node = (myproc+1)%2;

/*   printf("Hello from %d of %d\n",myproc, nprocs);*/
   MP_Sync();

/* Timer and sleep accuracy tests */

   t1 = MP_Time();
   t2 = MP_Time();
   while( t2 == t1 ) t2 = MP_Time();
   if( myproc == 0 ) printf("Timer accuracy might be ~%f usecs\n\n", 
      (t2-t1)*1000000);

   t1 = MP_Time();
   usleep(1);
   t2 = MP_Time();
   if( myproc == 0 ) printf("usleep(1) accuracy might be ~%f usecs\n\n", 
      (t2-t1)*1000000);

   ns.tv_sec = 0;
   ns.tv_nsec = 1;
   t1 = MP_Time();
   nanosleep(&ns, &ns_rem);
   t2 = MP_Time();
   if( myproc == 0 ) printf("nanosleep(1) accuracy might be ~%f usecs\n\n", 
      (t2-t1)*1000000);

   ns.tv_sec = 0;
   ns.tv_nsec = 1;
   t1 = MP_Time();
   for(i=0; i<5000; i++ ) if( i%10 == 100 ) printf(".");
   t2 = MP_Time();
   if( myproc == 0 ) printf("twiddle accuracy might be ~%f usecs\n\n", 
      (t2-t1)*1000000);

/* Communication tests between nodes 
 *   - Blocking sends and recvs
 *   - No guarantee of a preposted recv, so it might pass through comm buffer
 */

/*   for(size=8;size<=MAX_SIZE;size*=2) {*/
   for(size=0;size<=MAX_SIZE;size*=2) {
/*   for(size=8;size<=64;size*=2) {*/

      for( i=0; i<size/8; i++) { a[i] = (double) i; b[i] = 0.0;}
      last = size/8 - 1;
      a[0] -= 1.0; if (last > 0) a[last] -= 1.0;

      MP_Sync();
      t1=MP_Time();

      if (myproc == 0) {                /* Send first, then receive */
         MP_Send(a, size, other_node, tag);
         tag++;
         MP_Recv(b, size, other_node, tag);
      } else {                          /* Receive first, then send */
         MP_Recv(b, size, other_node, tag);
         b[0] += 1.0; if (last > 0) b[last] += 1.0;
         tag++;
         MP_Send(b, size, other_node, tag);
      }

      t2=MP_Time();
      time=1.e6*(t2-t1);
      MP_Sync();

         /* Check the message for integrity */
/*
      for(i=0; i<last; i++) {
         if( b[i] != (double) i ) {
            printf("Node %d ERROR - b[%d] = %f\n", myproc, i, b[i]);
         }
      }
*/
         /* Print out the time.  Not real accurate for small messages */

      if (myproc == 0 && time > 0.000001) {
         printf(" %7d bytes took %9.0f usec (%8.3f MB/sec)\n",
                                 size,time,2.0*size/time);
         if( 2*size/time > max_rate ) max_rate = 2*size/time;
         if( time/2 < min_latency ) min_latency = time/2;
      } else if (myproc==0) {
         printf(" %7d bytes took less than the timer accuracy\n",size);
      }
      if( size == 0 ) size = 4;
   }

/* Asynchronous communications
 *   - Prepost receives to guarantee bypassing the comm buffer
 */

   MP_Sync();
   if( myproc==0) printf("\n  Asynchronous ping-pong\n\n");

/*   for(size=8;size<=MAX_SIZE;size*=2) {*/
   for(size=0;size<=MAX_SIZE;size*=2) {

      for( i=0; i<size/8; i++) { a[i] = (double) i; b[i] = 0.0;}
      last = size/8 - 1;
      a[0] -= 1.0; if (last > 0) a[last] -= 1.0;
      tag++;

      MP_ARecv(b, size, other_node, tag, &irid);
      MP_Sync();
      t1=MP_Time();

      if (myproc == 0) {   /* Send first, then wait for the recv to complete */
         MP_Send(a, size, other_node, tag);
         MP_Wait(&irid);
      } else {             /* Wait for the recv, then send the data back */
         MP_Wait(&irid);
         b[0] += 1.0; if (last > 0) b[last] += 1.0;
         MP_Send(b, size, other_node, tag);
      }

      t2=MP_Time();
      time=1.e6*(t2-t1);
      MP_Sync();

         /* Check the message for integrity */

      for(i=0; i<last; i++) {
         if( b[i] != (double) i ) {
            printf("Node %d ERROR - b[%d] = %f\n", myproc, i, b[i]);
            exit(1);
         }
      }

      if (myproc == 0 && time > 0.000001) {
         printf(" %7d bytes took %9.0f usec (%8.3f MB/sec)\n",
                                 size,time,2.0*size/time);
         if( 2*size/time > max_rate ) max_rate = 2*size/time;
         if( time/2 < min_latency ) min_latency = time/2;
      } else if (myproc==0) {
         printf(" %7d bytes took less than the timer accuracy\n",size);
      }
      if( size == 0 ) size = 4;
   }

/* Bidirectional communications
 *   - Communicate in both directions at the same time
 *     using asynchronous communications
 */

   if( myproc==0) printf("\n  Bi-directional asynchronous ping-pong\n\n");

/*   for(size=8;size<=MAX_SIZE;size*=2) {*/
   for(size=0;size<=MAX_SIZE;size*=2) {

      for( i=0; i<size/8; i++) { a[i] = (double) i; b[i] = 0.0;}
      last = size/8 - 1;
      a[0] -= 1.0; if (last > 0) a[last] -= 1.0;
      tag++;

      MP_ARecv(b, size, other_node, tag, &irid_b);
      MP_ARecv(a, size, other_node, tag+1, &irid_a);
      MP_Sync();
      t1=MP_Time();
 
         MP_Send(a, size, other_node, tag);
         MP_Wait(&irid_b);
 
         b[0] += 1.0; if (last > 0) b[last] += 1.0;
         tag++;
 
         MP_Send(b, size, other_node, tag);
         MP_Wait(&irid_a);
 
      t2=MP_Time();
      time=1.e6*(t2-t1);
      MP_Sync();

         /* Check the message for integrity */

      for(i=0; i<last; i++) {
         if( b[i] != (double) i ) {
            printf("Node %d ERROR - b[%d] = %f\n", myproc, i, b[i]);
            exit(1);
         }
      }

      if (myproc == 0 && time > 0.000001) {
         printf(" %7d bytes took %9.0f usec (%8.3f MB/sec)\n",
                                 size,time,2.0*size/time);
         if( 2*size/time > max_rate ) max_rate = 2*size/time;
         if( time/2 < min_latency ) min_latency = time/2;
      } else if (myproc==0) {
         printf(" %7d bytes took less than the timer accuracy\n",size);
      }
      if( size == 0 ) size = 4;
   }
 
   if( myproc==0) printf("\n Max rate = %f MB/sec  Min latency = %f usec\n",
                         max_rate, min_latency);

   MP_Finalize(); 
}


