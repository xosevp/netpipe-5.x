
/*            Parallel Matrix-Matrix Multiplication Algorithm                */
/*           -------------------------------------------------               */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/*#include "protos.h"*/

/* ------------------- Helper Functions Prototypes ------------------------ */

void printmatrix (double **P, int N);
void matmul	 (double **A, double **B, double **C, int N);

/* -------------------------- Main Program -------------------------------- */

int main(int argc, char *argv[])
{
        int myproc, nprocs, tag;
	int N, N_loc;
	int i, j, k, li, lj, lk;
	int stage;
	int i_min, i_max, j_min, j_max;

	double **A_loc, **B_loc, **C_loc, **A_recv, **B_recv;

	int ndims=2, dims[2], coords[2], tmpcoords[2];
	int sqrt_P;

	int a_recv, b_recv;
	int tmprank;
	int dest, src;
	int root, *row_mask;

	double error, maxerror = 0.0, test, flops, time, MFlops;
        double MP_Get_Flops(), MP_Get_Time();

	
        MP_Init(&argc, &argv);
        MP_Myproc( &myproc );
        MP_Nprocs( &nprocs );

	MP_Enter("All");

        if (argc == 0)
                N = 1000;

        if (argc > 2)
        {
		if (myproc == 0)
                	printf("\nError... Enter only one argument (Size of the Matrix).\n");
                exit(1);
        }

        N = atoi(argv[1]);

	if (N <= 0)
	{
                if (myproc == 0)
			printf("\nMatrix Dimension is not a valid value. Enter again... \n");
		exit(1);
	}

	if ( (double) nprocs != floor(sqrt(nprocs)) * floor(sqrt(nprocs)) )
	{
                if (myproc == 0)
			printf("\nThe Number of Processors is not a Perfect Square ... \n");
		exit(1);
	}
		
	if ( (N % (int)(sqrt(nprocs))) != 0)
	{
                if (myproc == 0)
			printf("\nThe Matrix cannot be subdivided among the processors...\n");
		exit(1);
	}

	MP_Sync();
   	if ( myproc == 0 ) 
		printf("Doing a %d X %d matrix multiplication on %d nodes\n",
		        N, N, nprocs);
     
	sqrt_P = (int)sqrt((double)nprocs);

	N_loc = N / (int)(sqrt(nprocs));

        /* Allocate memory for mask */

        row_mask = (int *) malloc(nprocs * sizeof(int));

	A_loc = (double **) malloc(N_loc * sizeof(double *));
	B_loc = (double **) malloc(N_loc * sizeof(double *));
	C_loc = (double **) malloc(N_loc * sizeof(double *));
	A_recv = (double **) malloc(N_loc * sizeof(double *));
	B_recv = (double **) malloc(N_loc * sizeof(double *));

        A_loc[0] = (double *) malloc(N_loc * N_loc * sizeof(double));
        B_loc[0] = (double *) malloc(N_loc * N_loc * sizeof(double));
        C_loc[0] = (double *) malloc(N_loc * N_loc * sizeof(double));
        A_recv[0] = (double *) malloc(N_loc * N_loc * sizeof(double));
        B_recv[0] = (double *) malloc(N_loc * N_loc * sizeof(double));

	if (B_recv[0] == NULL)
	{
		printf("\nError. Malloc Failed...\n");
		exit(1);
	}

	for(i=1; i<N_loc; i++)
	{
		A_loc[i] = A_loc[i-1] + N_loc;
                B_loc[i] = B_loc[i-1] + N_loc;
                C_loc[i] = C_loc[i-1] + N_loc;
                A_recv[i] = A_recv[i-1] + N_loc;
                B_recv[i] = B_recv[i-1] + N_loc;
	}

	for (i=0; i<2; i++)
		dims[i] = sqrt_P;

	MP_Cart_Create(ndims, dims);
        MP_Cart_Coords(myproc, coords);

	i_min = coords[1] * N_loc;
	i_max = (coords[1] * N_loc) + N_loc - 1;
	j_min = coords[0] * N_loc;
	j_max = (coords[0] * N_loc) + N_loc - 1;

	/* Load Local Subblocks */

	li = 0;
        for(i = i_min; i <= i_max; i++)
	{
		lj = 0;
       		for(j = j_min; j <= j_max; j++)
                {
                 k = i * N + j;
        	        A_loc[li][lj] = (double) k;
                 B_loc[li][lj] = (double)(N*N - k) * 0.1;
                 C_loc[li][lj] = 0.0;
			lj++;
                }
		li++;
	}

	/* Row Mask is Calculated */

	for(i=0; i<nprocs; i++)
	{
		MP_Cart_Coords(i, tmpcoords);

		if (tmpcoords[1] == coords[1])
			row_mask[i] = 1;
		else
			row_mask[i] = 0;
	}

	MP_Sync();
	MP_Enter("pmatmult");

	/* Loop over sqrt_P Stages */

	for(stage=0; stage<sqrt_P; stage++)
	{
		/* Checks if it is my A data. If yes broadcast */

		if (coords[1] == (sqrt_P + coords[0] - stage) % sqrt_P)
		{
			root = myproc;
			MP_dBcast_masked(*A_loc, N_loc * N_loc, root, row_mask);
		}
		else
		{
			tmpcoords[0] =  coords[0] - (myproc % sqrt_P) + ((stage + coords[1]) % sqrt_P) ; 
			tmpcoords[1] = coords[1];
			
			MP_Cart_Rank(tmpcoords, &root);
			MP_dBcast_masked(*A_recv, N_loc * N_loc, root, row_mask);
		}

		/* Shift B up */

                MP_Cart_Shift(1, 1, &dest, &src);
                tag = N + stage;

		if (stage != 0)
		{
                	MP_ARecv(*B_recv, (N_loc * N_loc) * sizeof(double), src, tag, &b_recv);
                	MP_Send(*B_loc, (N_loc * N_loc) * sizeof(double), dest, tag);
			MP_Wait(&b_recv);

			for(li=0; li<N_loc; li++)
				for(lj=0; lj<N_loc; lj++)
					B_loc[li][lj] = B_recv[li][lj];
		}

		if (coords[1] == (sqrt_P + coords[0] - stage) % sqrt_P)
			matmul(A_loc, B_loc, C_loc, N_loc);
		else
			matmul(A_recv, B_loc, C_loc, N_loc);	
	}

	flops = (double) 2 * N_loc*N_loc*N_loc * sqrt_P;
	MP_Leave(flops);     /* pmatmult */

 	/* Testing Accuracy */

       	for(li=0; li<N_loc; li++)
       	{
      		for(lj=0; lj<N_loc; lj++)
 		{
			i = (coords[1] * N_loc) + li;
			j = (coords[0] * N_loc) + lj;

			test = 0.0;
			for(k=0; k<N; k++)
            test += (double) (i*N + k) * (double) (N*N - k*N - j) * 0.1;

         if( C_loc[li][lj] != 0.0 )
			   error = fabs( (C_loc[li][lj] - test) / C_loc[li][lj]);

			if (error > maxerror)
				maxerror = error;
		}
 	}

	error = maxerror;
	MP_dMax(&maxerror, 1);
	if (error == maxerror) 
		printf("\nMaximum fractional error = %1.16f\n", maxerror);

	MP_Sync();
	MP_Leave(0.0);             /* All */

	MP_Time_Report("fox.out");

	flops = MP_Get_Flops( "pmatmult" );
	time = MP_Get_Time( "pmatmult" );

	MP_dSum( &flops, 1);

	MP_dMax( &time, 1);

        MFlops = flops/1e6 / time;

	if( myproc == 0 ) {
		printf("The matrix multiplication took %.3f seconds\n", time);
		printf("The calculation ran at %.1f MFlops (%.1f MFlops/node)\n",
		        MFlops, MFlops / nprocs);
	}
   
        MP_Finalize();

	free(row_mask);

	free(A_loc[0]);
	free(B_loc[0]);
	free(C_loc[0]);
	free(A_recv[0]);
	free(B_recv[0]);

        free(A_loc);
        free(B_loc);
        free(C_loc);
        free(A_recv);
        free(B_recv);

	return 0;
}

/* ------------------------ Print Matrix --------------------------------- */

void printmatrix(double **P, int N)
{
	int i, j;

	for(i=0; i<N; i++)
	{
		for(j=0; j<N; j++)
			printf("%f	", P[i][j]);
		printf("\n");
	}
}

/* ------------------ Sub-Matrix Multiplication ------------------------- */

void matmul(double **X, double **Y, double **Z, int N)
{
	int i, j, k;
	
	for(i=0; i<N; i++)
	{
		for(j=0; j<N; j++)
		{
			for(k=0; k<N; k++)
			{
				Z[i][j] += X[i][k] * Y[k][j];
			}
		}
	}
}

