
          /* MPI C routine to calculate PI - Dave Turner */

/* This code calculates pi by doing a discrete integration of a circle of
 * radius 1 from 0 to 1, then multiplying the result by 4.
 */

#include <stdio.h>
#include <math.h>
#include "mpi.h"


int main(int argc, char *argv[])
{
   int nprocs, myproc;
   int i, n;

   double pi_25 = 3.141592653589793238462643, error;
   double pi, h, w, x, y;
   double t0, t1;

        /* MPI initialization */

   MPI_Init( &argc, &argv );
   MPI_Comm_size( MPI_COMM_WORLD, &nprocs );
   MPI_Comm_rank( MPI_COMM_WORLD, &myproc );

        /* Determine the number of divisions to use */

   if( argc == 2 ) {      /* Take n from the command line argument */
      sscanf( argv[1], "%d", &n );
   } else {
      n = 1000;           /* Otherwise use 1000 divisions */
   }
   if( myproc == 0 ) printf("Calculating pi using %d divisions\n", n);

        /* Broadcast the number of divisions to all nodes */

   MPI_Bcast( &n, 1, MPI_INT, 0, MPI_COMM_WORLD);  /* ??? Is this needed ??? */

        /* Start the timer after a barrier command */

   MPI_Barrier( MPI_COMM_WORLD );
   t0 = MPI_Wtime();

   pi = 0.0;
   w = 1.0 / n;

        /* Each processor starts at a different value and computes its
         * own contributions to pi.
         */

   for( i=myproc; i<n; i+=nprocs ) {

      x = (double) i / n;

      y = sqrt( 1.0 - x*x );

      pi += y * w;

   }

   MPI_Allreduce( &pi, &pi, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD );
   pi *= 4;
   error = fabs( pi - pi_25 );

   t1 = MPI_Wtime();

   if( myproc == 0 ) {
      printf("The calculated pi = %f (error = %f)\n", pi, error);
      printf("The calculation took %f seconds on %d nodes\n", t1-t0, nprocs);
   }

   MPI_Finalize();
}

