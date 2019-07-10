/*                  pong.c Generic Benchmark code
 *               Dave Turner - Ames Lab - July of 1994+++
 *
 *  Most Unix timers can't be trusted for very short times, so take this
 *  into account when looking at the results.  This code also only times
 *  a single message passing event for each size, so the results may vary
 *  between runs.  For more accurate measurements, grab NetPIPE from
 *  http://www.scl.ameslab.gov/ .
 */

#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

int main(int argc, char *argv[])
{
   int myproc, end_node, size, other_node, nprocs, i, last;
   double t1,t2,time;
   double *a, *b, *c;
/*   double a[132000],b[132000],t1,t2,time;*/
   double max_rate = 0.0, min_latency = 10e6;
   MPI_Request isid, irid, irid_a, irid_b;
   MPI_Status stat;

#if defined (_CRAYT3E)
   a = (double *) shmalloc(132000*sizeof(double));
   b = (double *) shmalloc(132000*sizeof(double));
#else
   a = malloc(132000*sizeof(double));
   b = malloc(132000*sizeof(double));
#endif

   printf("Hello from pong_mpi.c\n");
exit(0);

/*   printf("size is %i\n",sizeof(double)); */
   for(i=0; i<132000; i++) {
      a[i] = (double) i;
      b[i] = 0.0;
   }
   MPI_Init(&argc, &argv);
   fprintf(stdout,"After init\n");
   MPI_Comm_rank(MPI_COMM_WORLD, &myproc);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
   if( nprocs != 2) {
      printf("Nprocs=%d must equal 2\n", nprocs);
      exit(1);
   }
   end_node=nprocs-1;
   other_node = (myproc+1)%2;

   printf("Hello from %d of %d\n",myproc, nprocs); 
   MPI_Barrier(MPI_COMM_WORLD);

/* Timer accuracy test */

   t1 = MPI_Wtime();
   t2 = MPI_Wtime();
   while( t2 == t1 ) t2 = MPI_Wtime();
   if( myproc==0 ) printf("Timer accuracy of ~%f usecs\n\n", (t2-t1)*1000000);


/* Communications between nodes 
 *   - Blocking sends and recvs
 *   - No guarantee of prepost, so might pass through comm buffer
 */

   for(size=8;size<=1048576;size*=2) {
     for( i=0; i<size/8; i++) { a[i] = (double) i; b[i] = 0.0;}
     last = size/8 - 1;
     MPI_Barrier(MPI_COMM_WORLD);
     t1=MPI_Wtime();
     if (myproc == 0) {
        MPI_Send(a,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD);
        MPI_Recv(b,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD,&stat);
     } else {
        MPI_Recv(b,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD,&stat);
        b[0] += 1.0; if (last != 0) b[last] += 1.0;
        MPI_Send(b,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD);
     }
     t2=MPI_Wtime();
     time=1.e6*(t2-t1);
     MPI_Barrier(MPI_COMM_WORLD);
     if ((b[0] != 1.0 || b[last] != last + 1)) {
        printf("ERROR - b[0] = %f b[%d] = %f\n",b[0],last,b[last]);
        exit(1);
     }
     for(i=1; i<last-1; i++)
        if( b[i] != (double) i ) printf("ERROR - b[%d] = %f\n",i, b[i]);
     if (myproc == 0 && time > 0.000001) {
        printf(" %7d bytes took %9.0f usec (%8.3f MB/sec)\n",
                                size,time,2.0*size/time);
        if( 2*size/time > max_rate ) max_rate = 2*size/time;
        if( time/2 < min_latency ) min_latency = time/2;
     } else if (myproc==0) {
        printf(" %7d bytes took less than the timer accuracy\n",size);
     }
  }

MPI_Finalize(); 
exit(0);

/* Async communications
 *   - Prepost receives to guarantee bypassing the comm buffer
 */

   MPI_Barrier(MPI_COMM_WORLD);
   if( myproc==0) printf("\n  Asynchronous ping-pong\n\n");
   for(size=8;size<=1048576;size*=2) {
     for( i=0; i<size/8; i++) { a[i] = (double) i; b[i] = 0.0;}
     last = size/8 - 1;
     MPI_Irecv(b,1,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD,&irid);
     MPI_Barrier(MPI_COMM_WORLD);
     t1=MPI_Wtime();
     if (myproc == 0) {
        MPI_Send(a,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD);
        MPI_Wait(&irid,&stat);
     } else {
        MPI_Wait(&irid,&stat);
        b[0] += 1.0; if (last != 0) b[last] += 1.0;
        MPI_Send(b,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD);
     }
     t2=MPI_Wtime();
     time=1.e6*(t2-t1);
     MPI_Barrier(MPI_COMM_WORLD);
     if ((b[0] != 1.0 || b[last] != last + 1)) {
        printf("ERROR - b[0] = %f b[%d] = %f\n",b[0],last,b[last]);
     }
     for(i=1; i<last-1; i++)
        if( b[i] != (double) i ) printf("ERROR - b[%d] = %f\n",i, b[i]);
     if (myproc == 0 && time > 0.000001) {
        printf(" %7d bytes took %9.0f usec (%8.3f MB/sec)\n",
                                size,time,2.0*size/time);
        if( 2*size/time > max_rate ) max_rate = 2*size/time;
        if( time/2 < min_latency ) min_latency = time/2;
     } else if (myproc==0) {
        printf(" %7d bytes took less than the timer accuracy\n",size);
     }
  }

   if( myproc==0) printf("\n  Bi-directional asynchronous ping-pong\n\n");
   for(size=8;size<=1048576;size*=2) {
     for( i=0; i<size/8; i++) { a[i] = (double) i; b[i] = 0.0;}
     last = size/8 - 1;
     MPI_Irecv(b,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD,&irid_b);
     MPI_Irecv(a,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD,&irid_a);
     MPI_Barrier(MPI_COMM_WORLD);
     t1=MPI_Wtime();

        MPI_Send(a,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD);
        MPI_Wait(&irid_b,&stat);

        b[0] += 1.0; if (last != 0) b[last] += 1.0;

        MPI_Send(b,size/8,MPI_DOUBLE,other_node,1,MPI_COMM_WORLD);
        MPI_Wait(&irid_a,&stat);

     t2=MPI_Wtime();
     time=1.e6*(t2-t1);
     MPI_Barrier(MPI_COMM_WORLD);
     if ((a[0] != 1.0 || a[last] != last + 1)) {
        printf("ERROR - a[0] = %f a[%d] = %f\n",a[0],last,a[last]);
     }
     for(i=1; i<last-1; i++)
        if( a[i] != (double) i ) printf("ERROR - a[%d] = %f\n",i, a[i]);
     if (myproc == 0 && time > 0.000001) {
        printf(" %7d bytes took %9.0f usec (%8.3f MB/sec)\n",
                                size,time,2.0*size/time);
        if( 2*size/time > max_rate ) max_rate = 2*size/time;
        if( time/2 < min_latency ) min_latency = time/2;
     } else if (myproc==0) {
        printf(" %7d bytes took less than the timer accuracy\n",size);
     }
  }

  if( myproc==0) printf("\n Max rate = %f MB/sec  Min latency = %f usec\n",
                         max_rate, min_latency);

   MPI_Finalize(); 
}


