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

/* Run mplite on top of the resident MPI implementation instead of TCP or SHMEM
 *   - Won't work for p4pg runs, since they use argc and argv arguments
 *     to MPI_Init() to start the processes on other nodes.  But this is why
 *     p4pg files don't work with Fortran codes at all anyway.
 */

#ifndef MP_MPI_C_C
#define MP_MPI_C_C

#include "mplite.h" /* For memory wrappers */
#include "globals.h"

#include <mpi.h>

#define MAX_ASYNC 2000
static int mid = 0;
static MPI_Request *msg_list[MAX_ASYNC];


void MP_Init(int *argc, char ***argv)
{
  int i;

/*  printf("In MP_Init() before MPI_Init()\n");*/
  if( argc == NULL ) {         
      /* Assume this came from a Fortran program.
       *    MPI_Init(ierror) has already been done on the Fortran side.
       */
   } else {
/*      printf("Using C version of MPI_Init()\n");*/
      MPI_Init(argc, argv);
   }
/*  printf("In MP_Init() after MPI_Init()\n");*/

   MPI_Comm_rank(MPI_COMM_WORLD, &myproc);

   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

   sprintf(filename, ".node%d", myproc);
   if ( !(mylog=fopen(filename,"w")) ) {
     fprintf(stderr,"ERROR - Could not open log file %s\n",filename);
     exit(1);
   }

   myhostname[0]='\0';
   gethostname( hostname[0], 100);
   lprintf("Node %d of %d in MP_Init() - [%s]\n", myproc, nprocs, hostname[0]);

   /* Set up a default mask for the global operations that include all procs */

   MP_all_mask = malloc(nprocs*sizeof(int));
   if( ! MP_all_mask ) errorx(0,"ERROR in tcp.c - malloc(MP_all_mask) failed");
   for( i=0; i<nprocs; i++) MP_all_mask[i] = 1;
}


void MP_Finalize()
{
   dbprintf("MPI_Finalize()\n");
   MPI_Finalize();
}

void MP_Send(void *buf, int nbytes, int dest, int tag)
{
   dbprintf("  MPI_Send(& %d %d %d)\n", nbytes, dest, tag);
   MPI_Send(buf, nbytes, MPI_BYTE, dest, tag, MPI_COMM_WORLD); }

void MP_ASend(void *buf, int nbytes, int dest, int tag, int *msg_id)
{
   MPI_Request *mpi_id;
   int mid_end;

   mpi_id = malloc( sizeof(MPI_Request) );

   mid_end = mid;  mid = (mid+1) % MAX_ASYNC;
   while( msg_list[mid] != NULL && mid != mid_end ){ mid = (mid+1) % MAX_ASYNC;}
   if( mid == mid_end ) {
      errorx(MAX_ASYNC,"mpi_c.c - Too many active asynchronous communicats, increase MAX_ASYNC in mpi_c.c");
   }
   *msg_id = mid;

   dbprintf("  MPI_Isend(& %d %d %d %d)\n", nbytes, dest, tag, *msg_id);
   MPI_Isend(buf, nbytes, MPI_BYTE, dest, tag, MPI_COMM_WORLD, mpi_id);
   ddbprintf("  MPI_Isend()\n");
   msg_list[*msg_id] = mpi_id;
}

void MP_Recv(void *buf, int nbytes, int src, int tag)
{
   MPI_Status status;
   dbprintf("  MPI_Recv(& %d %d %d)\n", nbytes, src, tag);

   if( src < 0 ) src = MPI_ANY_SOURCE;
   if( tag < 0 ) tag = MPI_ANY_TAG;

   MPI_Recv(buf, nbytes, MPI_BYTE, src, tag, MPI_COMM_WORLD, &status);

   last_src = status.MPI_SOURCE;
   last_tag = status.MPI_TAG;
   MPI_Get_count( &status, MPI_BYTE, &last_size );
}

void MP_ARecv(void *buf, int nbytes, int src, int tag, int *msg_id)
{
   MPI_Request *mpi_id;
   int mid_end;

   mpi_id = malloc( sizeof(MPI_Request) );

   mid_end = mid;  mid = (mid+1) % MAX_ASYNC;
   while( msg_list[mid] != NULL && mid != mid_end ){ mid = (mid+1) % MAX_ASYNC;}
   if( mid == mid_end ) {
      errorx(MAX_ASYNC,"mpi_c.c - Too many active asynchronous communicats, increase MAX_ASYNC in mpi_c.c");
   }
   *msg_id = mid;

   if( src < 0 ) src = MPI_ANY_SOURCE;
   if( tag < 0 ) tag = MPI_ANY_TAG;

   dbprintf("  MPI_Irecv(& %d %d %d %d)\n", nbytes, src, tag, *msg_id);
   MPI_Irecv(buf, nbytes, MPI_BYTE, src, tag, MPI_COMM_WORLD, mpi_id);
   ddbprintf("  MPI_Irecv()\n");

   msg_list[*msg_id] = mpi_id;
}

void MP_Wait(int *msg_id)
{
   MPI_Status status;

   dbprintf("  MPI_Wait(%d)\n", *msg_id);
   MPI_Wait((MPI_Request *) msg_list[*msg_id], &status);
   ddbprintf("  MPI_Wait()\n");

   last_src = status.MPI_SOURCE;
   last_tag = status.MPI_TAG;
   MPI_Get_count( &status, MPI_BYTE, &last_size );

   free( (MPI_Request *) msg_list[*msg_id] );
   msg_list[*msg_id] = NULL;
}

void MP_Sync()
{
   dbprintf("  MPI_Barrier()\n");
   MPI_Barrier(MPI_COMM_WORLD);
}

/*double MP_Time()*/
/*{ extern double MPI_Wtime(); return MPI_Wtime(); }*/

/* The stuff below is not supported under MPI */

void MP_Bedcheck() {}

void MP_IPSend( void *data_ptr, int len, int dest, int tag)
{ printf("WARNING - MP_IPSend() is not supported under MPI\n"); }

void MP_IPRecv( void **data_ptr, int len, int src, int tag)
{ printf("WARNING - MP_IPRecv() is not supported under MPI\n"); }

void *MP_shmMalloc( int nbytes)
{ return malloc( nbytes ); }

void MP_shmFree( void *shmptr )
{ free( shmptr ); }

void dump_msg_state() {}

#endif /* MP_MPI_C_C */
