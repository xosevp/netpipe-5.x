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

/* Stubs to convert MPI calls to mplite (f77 stubs are include).
 * (Use mpi include path, but don't link to mpi library)
 *    Message types are not used
 *    Only MPI_COMM_WORLD is supported
 *
 * MPI_Abort() and MPI_Waitall() functions added by 
 *    Scott Brozell, March 27, 2002.
 *
 * MPI_Test() and incomplete MPI_Reduce() functions added by 
 *    Scott Brozell, October 3 and 4, 2002.
 */

#include "globals.h"
#include "mplite.h"
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mplite.h"
int MPI_Status_c2f( MPI_Status *status, int *fstatus);

int nbytes;
MPI_Group comm_world_group;

int bytesper(MPI_Datatype datatype)
{
   if(      datatype == MPI_CHAR             ) return sizeof(char);
   else if( datatype == MPI_UNSIGNED_CHAR    ) return sizeof(unsigned char);
   else if( datatype == MPI_BYTE             ) return 1;
   else if( datatype == MPI_SHORT            ) return sizeof(short);
   else if( datatype == MPI_UNSIGNED_SHORT   ) return sizeof(unsigned short);
   else if( datatype == MPI_INT              ) return sizeof(int);
   else if( datatype == MPI_UNSIGNED         ) return sizeof(unsigned int);
   else if( datatype == MPI_LONG             ) return sizeof(long);
   else if( datatype == MPI_UNSIGNED_LONG    ) return sizeof(unsigned long);
   else if( datatype == MPI_FLOAT            ) return sizeof(float);
   else if( datatype == MPI_DOUBLE           ) return sizeof(double);
/*   else if( datatype == MPI_LONG_DOUBLE      ) return sizeof(long double);*/
/*   else if( datatype == MPI_LONG_LONG_INT    ) return sizeof(long long int);*/

      /* f77 datatypes */

   else if( datatype == MPI_CHARACTER        ) return sizeof(char);
   else if( datatype == MPI_COMPLEX          ) return 2*sizeof(float);
   else if( datatype == MPI_DOUBLE_COMPLEX   ) return 2*sizeof(double);
   else if( datatype == MPI_LOGICAL          ) return 1;
   else if( datatype == MPI_REAL             ) return sizeof(float);
   else if( datatype == MPI_DOUBLE_PRECISION ) return sizeof(double);
   else if( datatype == MPI_INTEGER          ) return sizeof(long);

   else if( datatype == MPI_2INTEGER         ) return 2*sizeof(long);
   else if( datatype == MPI_2COMPLEX         ) return 4*sizeof(float);
   else if( datatype == MPI_2DOUBLE_COMPLEX  ) return 4*sizeof(double);
   else if( datatype == MPI_2REAL            ) return 2*sizeof(float);
   else if( datatype == MPI_2DOUBLE_PRECISION) return 2*sizeof(double);

   else if( datatype == MPI_REAL4            ) return 4;
   else if( datatype == MPI_REAL8            ) return 8;
   else if( datatype == MPI_INTEGER1         ) return 1;
   else if( datatype == MPI_INTEGER2         ) return 2;
   else if( datatype == MPI_INTEGER4         ) return 4;
   else {
      fprintf(stderr,"ERROR - Unsupported MPI_Datatype %d in mpi_wrappers.c\n",
              datatype);
      exit(-1);
   }
   return 0;  /* To keep some compilers from issuing warnings */
}

void Check_Communicator(MPI_Comm comm)
{
   if( comm != MPI_COMM_WORLD ) {
      fprintf(stderr,"ERROR - Only MPI_COMM_WORLD is supported\n");
      exit(-1);
   }
}

int MPI_Init(int *argc, char ***argv)
{ 
  int i;
  MP_Init(NULL,NULL);

  /* Set up MPI_Group associated with MPI_COMM_WORLD */
  comm_world_group.size = nprocs;
  comm_world_group.ranks = malloc(sizeof(int)*nprocs);
  for(i=0;i<nprocs;i++) comm_world_group.ranks[i] = i;

  return MPI_SUCCESS; 
}

  void MPI_INIT  (int *ierr) { MP_Init(NULL,NULL); *ierr=0; }
  void mpi_init  (int *ierr) { MP_Init(NULL,NULL); *ierr=0; }
  void mpi_init_ (int *ierr) { MP_Init(NULL,NULL); *ierr=0; }
  void mpi_init__(int *ierr) { MP_Init(NULL,NULL); *ierr=0; }


int MPI_Finalize()
{ MP_Finalize(); return MPI_SUCCESS; }

  void MPI_FINALIZE  (int *ierr) { MP_Finalize(); *ierr=0; }
  void mpi_finalize  (int *ierr) { MP_Finalize(); *ierr=0; }
  void mpi_finalize_ (int *ierr) { MP_Finalize(); *ierr=0; }
  void mpi_finalize__(int *ierr) { MP_Finalize(); *ierr=0; }


int MPI_Comm_rank(MPI_Comm comm, int *rank)
{
   Check_Communicator(comm);
   MP_Myproc(rank);
   return MPI_SUCCESS;
}

  void MPI_COMM_RANK  (MPI_Comm *comm, int *rank, int *ierr)
  { int MPI_Comm_rank(); *ierr=MPI_Comm_rank( *comm, rank); }
  void mpi_comm_rank  (MPI_Comm *comm, int *rank, int *ierr)
  { int MPI_Comm_rank(); *ierr=MPI_Comm_rank( *comm, rank); }
  void mpi_comm_rank_ (MPI_Comm *comm, int *rank, int *ierr)
  { int MPI_Comm_rank(); *ierr=MPI_Comm_rank( *comm, rank); }
  void mpi_comm_rank__(MPI_Comm *comm, int *rank, int *ierr)
  { int MPI_Comm_rank(); *ierr=MPI_Comm_rank( *comm, rank); }


int MPI_Comm_size(MPI_Comm comm, int *size)
{
   Check_Communicator(comm);
   MP_Nprocs(size);
   return MPI_SUCCESS;
}

  void MPI_COMM_SIZE  (MPI_Comm *comm, int *size, int *ierr)
  { int MPI_Comm_size(); *ierr=MPI_Comm_size( *comm, size); }
  void mpi_comm_size  (MPI_Comm *comm, int *size, int *ierr)
  { int MPI_Comm_size(); *ierr=MPI_Comm_size( *comm, size); }
  void mpi_comm_size_ (MPI_Comm *comm, int *size, int *ierr)
  { int MPI_Comm_size(); *ierr=MPI_Comm_size( *comm, size); }
  void mpi_comm_size__(MPI_Comm *comm, int *size, int *ierr)
  { int MPI_Comm_size(); *ierr=MPI_Comm_size( *comm, size); }



int MPI_Send(void *buf, int count, MPI_Datatype datatype, 
             int destination, int mtype, MPI_Comm comm)
{
   Check_Communicator(comm);
   nbytes = count * bytesper(datatype);
   MP_Send(buf, nbytes, destination, mtype);
   return MPI_SUCCESS;
}

  void MPI_SEND  (void *buf, int *count, MPI_Datatype *datatype, 
               int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Send(); *ierr=MPI_Send(buf, *count, *datatype,
               *destination, *mtype, *comm); }
  void mpi_send  (void *buf, int *count, MPI_Datatype *datatype, 
               int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Send(); *ierr=MPI_Send(buf, *count, *datatype,
               *destination, *mtype, *comm); }
  void mpi_send_ (void *buf, int *count, MPI_Datatype *datatype, 
               int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Send(); *ierr=MPI_Send(buf, *count, *datatype,
               *destination, *mtype, *comm); }
  void mpi_send__(void *buf, int *count, MPI_Datatype *datatype, 
               int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Send(); *ierr=MPI_Send(buf, *count, *datatype,
               *destination, *mtype, *comm); }


int MPI_Isend(void* buf, int count, MPI_Datatype datatype, 
             int destination, int mtype, MPI_Comm comm, MPI_Request* msg_id)
{
   Check_Communicator(comm);
   nbytes = count * bytesper(datatype);
   MP_ASend(buf, nbytes, destination, mtype, msg_id);
   return MPI_SUCCESS;
}

  void MPI_ISEND  (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Isend(); *ierr=MPI_Isend(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }
  void mpi_isend  (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Isend(); *ierr=MPI_Isend(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }
  void mpi_isend_ (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Isend(); *ierr=MPI_Isend(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }
  void mpi_isend__(void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Isend(); *ierr=MPI_Isend(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }


/* The buffered send is just mapped to standard MP_Send() function
 * that automatically manage the buffers.  The buffer attach and 
 * detach functions are then null functions.
 */

/*static void *MPI_Buffer;*/
/*static int MPI_Buffer_size;*/

int MPI_Buffer_attach(void* MPI_Buffer, int MPI_Buffer_size)
{
   return MPI_SUCCESS;
}

  void MPI_BUFFER_ATTACH  (void* MPI_Buffer, int* MPI_Buffer_size) { }
  void mpi_buffer_attach  (void* MPI_Buffer, int* MPI_Buffer_size) { }
  void mpi_buffer_attach_ (void* MPI_Buffer, int* MPI_Buffer_size) { }
  void mpi_buffer_attach__(void* MPI_Buffer, int* MPI_Buffer_size) { }

int MPI_Buffer_detach(void* MPI_Buffer, int *MPI_Buffer_size)
{
   return MPI_SUCCESS;
}

  void MPI_BUFFER_DETACH  (void* MPI_Buffer, int* MPI_Buffer_size) { }
  void mpi_buffer_detach  (void* MPI_Buffer, int* MPI_Buffer_size) { }
  void mpi_buffer_detach_ (void* MPI_Buffer, int* MPI_Buffer_size) { }
  void mpi_buffer_detach__(void* MPI_Buffer, int* MPI_Buffer_size) { }



int MPI_Bsend(void* buf, int count, MPI_Datatype datatype, 
             int destination, int mtype, MPI_Comm comm)
{
   Check_Communicator(comm);
   nbytes = count * bytesper(datatype);
   MP_Send(buf, nbytes, destination, mtype);
   return MPI_SUCCESS;
}

  void MPI_BSEND  (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Bsend(); *ierr=MPI_Bsend(buf, *count, *datatype,
               *destination, *mtype, *comm); }
  void mpi_bsend  (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Bsend(); *ierr=MPI_Bsend(buf, *count, *datatype,
               *destination, *mtype, *comm); }
  void mpi_bsend_ (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Bsend(); *ierr=MPI_Bsend(buf, *count, *datatype,
               *destination, *mtype, *comm); }
  void mpi_bsend__(void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Bsend(); *ierr=MPI_Bsend(buf, *count, *datatype,
               *destination, *mtype, *comm); }

int MPI_Ibsend(void* buf, int count, MPI_Datatype datatype,
             int destination, int mtype, MPI_Comm comm, MPI_Request* msg_id)
{
   Check_Communicator(comm);
   nbytes = count * bytesper(datatype);
   MP_ASend(buf, nbytes, destination, mtype, msg_id);
   return MPI_SUCCESS;
}

  void MPI_IBSEND  (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Ibsend(); *ierr=MPI_Ibsend(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }
  void mpi_ibsend  (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Ibsend(); *ierr=MPI_Ibsend(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }
  void mpi_ibsend_ (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Ibsend(); *ierr=MPI_Ibsend(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }
  void mpi_ibsend__(void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Ibsend(); *ierr=MPI_Ibsend(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }


   /* The synchronous send does not complete until the receive starts.
    * For MP_Lite, it does not complete until the receive completes,
    * which is functionally the same from a programer's view. */

int MPI_Ssend(void *buf, int count, MPI_Datatype datatype, 
             int destination, int mtype, MPI_Comm comm)
{
   Check_Communicator(comm);
   nbytes = count * bytesper(datatype);
   MP_SSend(buf, nbytes, destination, mtype);
   return MPI_SUCCESS;
}

  void MPI_SSEND  (void *buf, int *count, MPI_Datatype *datatype, 
               int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Ssend(); *ierr=MPI_Ssend(buf, *count, *datatype,
               *destination, *mtype, *comm); }
  void mpi_ssend  (void *buf, int *count, MPI_Datatype *datatype, 
               int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Ssend(); *ierr=MPI_Ssend(buf, *count, *datatype,
               *destination, *mtype, *comm); }
  void mpi_ssend_ (void *buf, int *count, MPI_Datatype *datatype, 
               int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Ssend(); *ierr=MPI_Ssend(buf, *count, *datatype,
               *destination, *mtype, *comm); }
  void mpi_ssend__(void *buf, int *count, MPI_Datatype *datatype, 
               int *destination, int *mtype, MPI_Comm *comm, int *ierr)
  {  int MPI_Ssend(); *ierr=MPI_Ssend(buf, *count, *datatype,
               *destination, *mtype, *comm); }



int MPI_Recv(void *buf, int count, MPI_Datatype datatype, 
             int source, int mtype, MPI_Comm comm, MPI_Status* status)
{ 
   Check_Communicator(comm);
   nbytes = count * bytesper(datatype);
   MP_Recv(buf, nbytes, source, mtype);

   if( status != NULL ) {
      MP_Get_Last_Size( &status->nbytes );
      MP_Get_Last_Source( &status->MPI_SOURCE );
      MP_Get_Last_Tag( &status->MPI_TAG );
   }

   return MPI_SUCCESS;
}

void MPI_RECV  (void *buf, int *count, MPI_Datatype *datatype,
                int *destination, int *mtype, MPI_Comm *comm, 
                int *fstatus, int *ierr)
   {  int MPI_Recv();
      MPI_Status *status = malloc( sizeof(MPI_Status) );
      *ierr=MPI_Recv(buf, *count, *datatype,
                *destination, *mtype, *comm, status);
      MPI_Status_c2f( status, fstatus);
      free( status );
   }
void mpi_recv  (void *buf, int *count, MPI_Datatype *datatype,
                int *destination, int *mtype, MPI_Comm *comm, 
                int *fstatus, int *ierr)
   {  int MPI_Recv();
      MPI_Status *status = malloc( sizeof(MPI_Status) );
      *ierr=MPI_Recv(buf, *count, *datatype,
               *destination, *mtype, *comm, status);
      MPI_Status_c2f( status, fstatus);
      free( status );
  }
void mpi_recv_ (void *buf, int *count, MPI_Datatype *datatype,
                int *destination, int *mtype, MPI_Comm *comm, 
                int *fstatus, int *ierr)
   {  int MPI_Recv();
      MPI_Status *status = malloc( sizeof(MPI_Status) );
      *ierr=MPI_Recv(buf, *count, *datatype,
                *destination, *mtype, *comm, status);
      MPI_Status_c2f( status, fstatus);
      free( status );
  }
void mpi_recv__(void *buf, int *count, MPI_Datatype *datatype,
                int *destination, int *mtype, MPI_Comm *comm, 
                int *fstatus, int *ierr)
   {  int MPI_Recv();
      MPI_Status *status = malloc( sizeof(MPI_Status) );
      *ierr=MPI_Recv(buf, *count, *datatype,
                *destination, *mtype, *comm, status);
      MPI_Status_c2f( status, fstatus);
      free( status );
  }


int MPI_Irecv(void *buf, int count, MPI_Datatype datatype, 
             int source, int mtype, MPI_Comm comm, MPI_Request* msg_id)
{ 
   Check_Communicator(comm);
   nbytes = count * bytesper(datatype);
   MP_ARecv(buf, nbytes, source, mtype, msg_id);
   return MPI_SUCCESS;
}

  void MPI_IRECV  (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Irecv(); *ierr=MPI_Irecv(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }
  void mpi_irecv  (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Irecv(); *ierr=MPI_Irecv(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }
  void mpi_irecv_ (void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Irecv(); *ierr=MPI_Irecv(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }
  void mpi_irecv__(void *buf, int *count, MPI_Datatype *datatype,
   int *destination, int *mtype, MPI_Comm *comm, MPI_Request* msg_id, int *ierr)
  {  int MPI_Irecv(); *ierr=MPI_Irecv(buf, *count, *datatype,
               *destination, *mtype, *comm, msg_id); }


int MPI_Wait(MPI_Request* msg_id, MPI_Status* status)
{
   MP_Wait(msg_id);
   if( status != NULL ) {
      MP_Get_Last_Size( &status->nbytes );
      MP_Get_Last_Source( &status->MPI_SOURCE );
      MP_Get_Last_Tag( &status->MPI_TAG );
   }
   return MPI_SUCCESS;
}

  void MPI_WAIT  (MPI_Request* msg_id, int* fstatus, int *ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
      *ierr=MPI_Wait( msg_id, status); 
      MPI_Status_c2f( status, fstatus );
      free( status ); }
  void mpi_wait  (MPI_Request* msg_id, int* fstatus, int *ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
      *ierr=MPI_Wait( msg_id, status); 
      MPI_Status_c2f( status, fstatus );
      free( status ); }
  void mpi_wait_ (MPI_Request* msg_id, int* fstatus, int *ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
      *ierr=MPI_Wait( msg_id, status); 
      MPI_Status_c2f( status, fstatus );
      free( status ); }
  void mpi_wait__(MPI_Request* msg_id, int* fstatus, int *ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
      *ierr=MPI_Wait( msg_id, status); 
      MPI_Status_c2f( status, fstatus );
      free( status ); }

#ifndef INFINIBAND
      
int MPI_Test(MPI_Request* msg_id, int* flag, MPI_Status* status)
{
   MP_Test(msg_id, flag);
   if( status != NULL ) {
      MP_Get_Last_Size( &status->nbytes );
      MP_Get_Last_Source( &status->MPI_SOURCE );
      MP_Get_Last_Tag( &status->MPI_TAG );
   }
   return MPI_SUCCESS;
}

  void MPI_TEST(MPI_Request* msg_id, int* flag, int* fstatus, int* ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
    *ierr=MPI_Test( msg_id, flag, status );
    MPI_Status_c2f( status, fstatus );
    free( status ); }
  void mpi_test(MPI_Request* msg_id, int* flag, int* fstatus, int* ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
    *ierr=MPI_Test( msg_id, flag, status );
    MPI_Status_c2f( status, fstatus );
    free( status ); }
  void mpi_test_(MPI_Request* msg_id, int* flag, int* fstatus, int* ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
    *ierr=MPI_Test( msg_id, flag, status );
    MPI_Status_c2f( status, fstatus );
    free( status ); }
  void mpi_test__(MPI_Request* msg_id, int* flag, int* fstatus, int* ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
    *ierr=MPI_Test( msg_id, flag, status );
    MPI_Status_c2f( status, fstatus );
    free( status ); }



int MPI_Testall(int count, MPI_Request* msg_ids, int* flag, MPI_Status* stats)
{
  MP_Testall(count, msg_ids, flag);
  return MPI_SUCCESS;
}

  void MPI_TESTALL(int count, MPI_Request* msg_ids, int* flag, int* fstatus, int* ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
    *ierr=MPI_Testall( count, msg_ids, flag, status );
    MPI_Status_c2f( status, fstatus );
    free( status ); }
  void mpi_testall(int count, MPI_Request* msg_ids, int* flag, int* fstatus, int* ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
    *ierr=MPI_Testall( count, msg_ids, flag, status );
    MPI_Status_c2f( status, fstatus );
    free( status ); }
  void mpi_testall_(int count, MPI_Request* msg_ids, int* flag, int* fstatus, int* ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
    *ierr=MPI_Testall( count, msg_ids, flag, status );
    MPI_Status_c2f( status, fstatus );
    free( status ); }
  void mpi_testall__(int count, MPI_Request* msg_ids, int* flag, int* fstatus, int* ierr)
    { MPI_Status *status = malloc( sizeof(MPI_Status) );
    *ierr=MPI_Testall( count, msg_ids, flag, status );
    MPI_Status_c2f( status, fstatus );
    free( status ); }

#endif /* INFINIBAND */
    
int MPI_Barrier(MPI_Comm comm)
{
   Check_Communicator(comm);
   MP_Sync();
   return MPI_SUCCESS;
}

  void MPI_BARRIER  (MPI_Comm *comm, int *ierr)
  { Check_Communicator(*comm); MP_Sync(); *ierr=MPI_SUCCESS; }
  void mpi_barrier  (MPI_Comm *comm, int *ierr)
  { Check_Communicator(*comm); MP_Sync(); *ierr=MPI_SUCCESS; }
  void mpi_barrier_ (MPI_Comm *comm, int *ierr)
  { Check_Communicator(*comm); MP_Sync(); *ierr=MPI_SUCCESS; }
  void mpi_barrier__(MPI_Comm *comm, int *ierr)
  { Check_Communicator(*comm); MP_Sync(); *ierr=MPI_SUCCESS; }


double MPI_Wtime()
{
  extern double MP_Time();
/*printf("In MPI_Wtime() time = %f\n",MP_Time());*/
   return MP_Time();
}

  double MPI_WTIME  () { extern double MP_Time(); return MP_Time(); }
  double mpi_wtime  () { extern double MP_Time(); return MP_Time(); }
  double mpi_wtime_ () { extern double MP_Time(); return MP_Time(); }
  double mpi_wtime__() { extern double MP_Time(); return MP_Time(); }


/* Global functions */

int MPI_Allreduce(void *sbuf, void *rbuf, int count, MPI_Datatype datatype,
               MPI_Op op, MPI_Comm comm)
{
   int opcode;
/*   extern int *MP_all_mask;*/

   Check_Communicator(comm);
   if( op == MPI_SUM ) opcode = 0;
   else if( op == MPI_MAX ) opcode = 3;
   else if( op == MPI_MIN ) opcode = 6;
   else {
      fprintf(stderr,"ERROR in MPI_Allreduce(), unsupported MPI_Op\n");
      exit(-1);
   }

   if( ( (datatype == MPI_INT || datatype == MPI_INTEGER) && sizeof(int) == 4) ||
         (datatype == MPI_LONG && sizeof(long) == 4) ) {
      opcode += 1;
   } else if( datatype == MPI_FLOAT ||
              datatype == MPI_REAL   ||
              datatype == MPI_REAL4 ) {
      opcode += 2;
   } else if( datatype == MPI_DOUBLE ||
              datatype == MPI_REAL8 ||
              datatype == MPI_DOUBLE_PRECISION ) {
      opcode += 0;
   } else {
      fprintf(stderr,"ERROR in MPI_Allreduce(), unsupported MPI_Datatype\n");
      exit(-1);
   }

   memcpy(rbuf,sbuf,count*bytesper(datatype));    /* sbuf --> rbuf */
   MP_Global(rbuf, count, opcode, MP_all_mask);

   return MPI_SUCCESS;
}

int MPI_Allgather( void *sbuf, int sendcount, MPI_Datatype sendtype,
                   void *rbuf, int recvcount, MPI_Datatype recvtype,
                   MPI_Comm comm)
{
   int opcode, offset;

   Check_Communicator(comm);

   if( sendtype != recvtype ) {
     fprintf(stderr,"ERROR in MPI_Allgather(), send and recv MPI_Datatype must match\n");
     exit(-1);
   }

   if( ( (sendtype == MPI_INT || sendtype == MPI_INTEGER) && sizeof(int) == 4) ||
         (sendtype == MPI_LONG && sizeof(long) == 4) ) {
      offset = myproc*4;
      opcode = 1;
   } else if( sendtype == MPI_FLOAT ||
              sendtype == MPI_REAL   ||
              sendtype == MPI_REAL4 ) {
      offset = myproc*sizeof(float);
      opcode = 2;
   } else if( sendtype == MPI_DOUBLE ||
              sendtype == MPI_REAL8 ||
              sendtype == MPI_DOUBLE_PRECISION ) {
      offset = myproc*sizeof(double);
      opcode = 0;
   } else if( sendtype == MPI_CHAR ||
              sendtype == MPI_UNSIGNED_CHAR ||
              sendtype == MPI_BYTE) {
      offset = myproc;
      opcode = 3;
   } else {
     fprintf(stderr, "ERROR in MPI_Allgather(), unsupported MPI_Datatype\n");
     exit(-1);
   }
   
   offset *= sendcount;
   memcpy((char *)rbuf+offset,sbuf,sendcount*bytesper(sendtype)); /* sbuf --> rbuf */
   MP_Global_gather(rbuf, sendcount, opcode, MP_all_mask);

   return MPI_SUCCESS;
}

   /* MPI_Gather() grabs the same info from each proc to proc 'root' only
    * and assembles it in proc order */

int MPI_Gather( void *sbuf, int sendcount, MPI_Datatype sendtype,
                void *rbuf, int recvcount, MPI_Datatype recvtype,
                int root, MPI_Comm comm)
{
   char *r = rbuf, *r_myproc;
   int n, size, msg_id[MAX_PROCS];

   Check_Communicator(comm);

   if( sendtype != recvtype ) {
     fprintf(stderr,"ERROR in MPI_Gather(), send and recv MPI_Datatype must match\n");
     exit(-1);
   }

   size = sendcount * bytesper( sendtype );

   if( myproc == root ) { /* I will receive the packets from the other procs */
      for( n=0; n<nprocs; n++ ) {
         if( n != myproc ) {
            MP_ARecv( r, size, n, n, &msg_id[n] );
         } else r_myproc = r;
         r += size;
      }

      memcpy(r_myproc, sbuf, size);  /* Copy my data to r_buf */

         /* Block until all data is in */

      for( n=0; n<nprocs; n++ ) {
         if( n != myproc ) {
            MP_Wait( &msg_id[n] );
         }
      }

   } else {    /* All other nodes just send their data to the root proc */

      MP_Send( sbuf, size, root, myproc );

   }

   return MPI_SUCCESS;
}



int MPI_Sendrecv( void *sendbuf, int sendcount, MPI_Datatype sendtype, 
                  int dest, int sendtag, void *recvbuf, int recvcount,
                  MPI_Datatype recvtype, int source, int recvtag,
                  MPI_Comm comm, MPI_Status *status)
{
   int nsendbytes, nrecvbytes, msg_id = -1;

   Check_Communicator(comm);
   nsendbytes = sendcount * bytesper(sendtype);
   nrecvbytes = recvcount * bytesper(recvtype);

   MP_ARecv( recvbuf, nrecvbytes, source, recvtag, &msg_id);
   MP_Send(  sendbuf, nsendbytes, dest, sendtag);
   MP_Wait( &msg_id );

   MP_Get_Last_Size( &status->nbytes );
   MP_Get_Last_Source( &status->MPI_SOURCE );
   MP_Get_Last_Tag( &status->MPI_TAG );

   return MPI_SUCCESS;
}

int MPI_Sendrecv_replace( void *buf, int count, MPI_Datatype datatype, 
                  int dest, int sendtag, int source, int recvtag,
                  MPI_Comm comm, MPI_Status *status)
{
   int nbytes;

   Check_Communicator(comm);
   nbytes = count * bytesper(datatype);

   MP_Send( buf, nbytes, dest, sendtag);
   MP_Recv( buf, nbytes, source, recvtag);

   MP_Get_Last_Size( &status->nbytes );
   MP_Get_Last_Source( &status->MPI_SOURCE );
   MP_Get_Last_Tag( &status->MPI_TAG );

   return MPI_SUCCESS;
}

int MPI_Bcast(void *buf, int count, MPI_Datatype datatype, int root, MPI_Comm comm)
{
    Check_Communicator(comm);
    nbytes = count * bytesper(datatype);
    MP_Bcast( buf, nbytes, root);
    return MPI_SUCCESS;
}


void MPI_ALLREDUCE  (void *buf1, void *buf2, int *count, 
          MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, int *ierr)
   { int MPI_Allreduce();
     *ierr=MPI_Allreduce(buf1, buf2, *count, *datatype, *op, *comm); }
void mpi_allreduce  (void *buf1, void *buf2, int *count,
          MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, int *ierr)
   { int MPI_Allreduce();
     *ierr=MPI_Allreduce(buf1, buf2, *count, *datatype, *op, *comm); }
void mpi_allreduce_ (void *buf1, void *buf2, int *count,
          MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, int *ierr)
   { int MPI_Allreduce();
     *ierr=MPI_Allreduce(buf1, buf2, *count, *datatype, *op, *comm); }
void mpi_allreduce__(void *buf1, void *buf2, int *count,
          MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, int *ierr)
   { int MPI_Allreduce();
     *ierr=MPI_Allreduce(buf1, buf2, *count, *datatype, *op, *comm); }


void MPI_GATHER  (void *sbuf, int *scnt, MPI_Datatype *stype, void *rbuf,
           int *rcnt, MPI_Datatype *rtype, int *root, MPI_Comm *comm, int *ierr)
   { int MPI_Gather();
     *ierr=MPI_Gather(sbuf, *scnt, *stype, rbuf, *rcnt, *rtype, *root, *comm); }
void mpi_gather  (void *sbuf, int *scnt, MPI_Datatype *stype, void *rbuf,
           int *rcnt, MPI_Datatype *rtype, int *root, MPI_Comm *comm, int *ierr)
   { int MPI_Gather();
     *ierr=MPI_Gather(sbuf, *scnt, *stype, rbuf, *rcnt, *rtype, *root, *comm); }
void mpi_gather_ (void *sbuf, int *scnt, MPI_Datatype *stype, void *rbuf,
           int *rcnt, MPI_Datatype *rtype, int *root, MPI_Comm *comm, int *ierr)
{ int MPI_Gather();
     *ierr=MPI_Gather(sbuf, *scnt, *stype, rbuf, *rcnt, *rtype, *root, *comm); }
void mpi_gather__(void *sbuf, int *scnt, MPI_Datatype *stype, void *rbuf,
           int *rcnt, MPI_Datatype *rtype, int *root, MPI_Comm *comm, int *ierr)
   { int MPI_Gather();
     *ierr=MPI_Gather(sbuf, *scnt, *stype, rbuf, *rcnt, *rtype, *root, *comm); }


void MPI_ALLGATHER  (void *sbuf, int *scnt, MPI_Datatype *stype, void *rbuf,
                     int *rcnt, MPI_Datatype *rtype, MPI_Comm *comm, int *ierr)
   { int MPI_Allgather();
     *ierr=MPI_Allgather(sbuf, *scnt, *stype, rbuf, *rcnt, *rtype, *comm); }
void mpi_allgather  (void *sbuf, int *scnt, MPI_Datatype *stype, void *rbuf,
                     int *rcnt, MPI_Datatype *rtype, MPI_Comm *comm, int *ierr)
   { int MPI_Allgather();
     *ierr=MPI_Allgather(sbuf, *scnt, *stype, rbuf, *rcnt, *rtype, *comm); }
void mpi_allgather_ (void *sbuf, int *scnt, MPI_Datatype *stype, void *rbuf,
                     int *rcnt, MPI_Datatype *rtype, MPI_Comm *comm, int *ierr)
{ int MPI_Allgather();
     *ierr=MPI_Allgather(sbuf, *scnt, *stype, rbuf, *rcnt, *rtype, *comm); }
void mpi_allgather__(void *sbuf, int *scnt, MPI_Datatype *stype, void *rbuf,
                     int *rcnt, MPI_Datatype *rtype, MPI_Comm *comm, int *ierr)
   { int MPI_Allgather();
     *ierr=MPI_Allgather(sbuf, *scnt, *stype, rbuf, *rcnt, *rtype, *comm); }


void MPI_SENDRECV  ( void *sendbuf, int *sendcount, MPI_Datatype *sendtype,
                    int *dest, int *sendtag, void *recvbuf, int *recvcount,
                    MPI_Datatype *recvtype, int *source, int *recvtag,
                    MPI_Comm *comm, int *fstatus, int *ierr)
   { int MPI_Sendrecv(); MPI_Status *status = malloc(sizeof(MPI_Status));
     *ierr=MPI_Sendrecv( sendbuf, *sendcount, *sendtype, *dest, *sendtag,
         recvbuf, *recvcount, *recvtype, *source, *recvtag, *comm, status);
     MPI_Status_c2f( status, fstatus );
     free( status ); }
void mpi_sendrecv  ( void *sendbuf, int *sendcount, MPI_Datatype *sendtype,
                    int *dest, int *sendtag, void *recvbuf, int *recvcount,
                    MPI_Datatype *recvtype, int *source, int *recvtag,
                    MPI_Comm *comm, int *fstatus, int *ierr)
   { int MPI_Sendrecv(); MPI_Status *status = malloc(sizeof(MPI_Status));
     *ierr=MPI_Sendrecv( sendbuf, *sendcount, *sendtype, *dest, *sendtag,
         recvbuf, *recvcount, *recvtype, *source, *recvtag, *comm, status);
     MPI_Status_c2f( status, fstatus );
     free( status ); }
void mpi_sendrecv_ ( void *sendbuf, int *sendcount, MPI_Datatype *sendtype,
                    int *dest, int *sendtag, void *recvbuf, int *recvcount,
                    MPI_Datatype *recvtype, int *source, int *recvtag,
                    MPI_Comm *comm, int *fstatus, int *ierr)
   { int MPI_Sendrecv(); MPI_Status *status = malloc(sizeof(MPI_Status));
     *ierr=MPI_Sendrecv( sendbuf, *sendcount, *sendtype, *dest, *sendtag,
         recvbuf, *recvcount, *recvtype, *source, *recvtag, *comm, status);
     MPI_Status_c2f( status, fstatus );
     free( status ); }
void mpi_sendrecv__( void *sendbuf, int *sendcount, MPI_Datatype *sendtype,
                    int *dest, int *sendtag, void *recvbuf, int *recvcount,
                    MPI_Datatype *recvtype, int *source, int *recvtag,
                    MPI_Comm *comm, int *fstatus, int *ierr)
   { int MPI_Sendrecv(); MPI_Status *status = malloc(sizeof(MPI_Status));
     *ierr=MPI_Sendrecv( sendbuf, *sendcount, *sendtype, *dest, *sendtag,
         recvbuf, *recvcount, *recvtype, *source, *recvtag, *comm, status);
     MPI_Status_c2f( status, fstatus );
     free( status ); }


void MPI_SENDRECV_REPLACE  ( void *buf, int *count, MPI_Datatype *datatype,
                    int *dest, int *sendtag, int *source, int *recvtag,
                    MPI_Comm *comm, int *fstatus, int *ierr)
   { int MPI_Sendrecv_replace(); MPI_Status *status = malloc(sizeof(MPI_Status));
     *ierr=MPI_Sendrecv_replace( buf, *count, *datatype, *dest, *sendtag,
                                 *source, *recvtag, *comm, status);
     MPI_Status_c2f( status, fstatus );
     free( status ); }
void mpi_sendrecv_replace  ( void *buf, int *count, MPI_Datatype *datatype,
                    int *dest, int *sendtag, int *source, int *recvtag,
                    MPI_Comm *comm, int *fstatus, int *ierr)
   { int MPI_Sendrecv_replace(); MPI_Status *status = malloc(sizeof(MPI_Status));
     *ierr=MPI_Sendrecv_replace( buf, *count, *datatype, *dest, *sendtag,
                                 *source, *recvtag, *comm, status);
     MPI_Status_c2f( status, fstatus );
     free( status ); }
void mpi_sendrecv_replace_ ( void *buf, int *count, MPI_Datatype *datatype,
                    int *dest, int *sendtag, int *source, int *recvtag,
                    MPI_Comm *comm, int *fstatus, int *ierr)
   { int MPI_Sendrecv_replace(); MPI_Status *status = malloc(sizeof(MPI_Status));
     *ierr=MPI_Sendrecv_replace( buf, *count, *datatype, *dest, *sendtag,
                                 *source, *recvtag, *comm, status);
     MPI_Status_c2f( status, fstatus );
     free( status ); }
void mpi_sendrecv_replace__( void *buf, int *count, MPI_Datatype *datatype,
                    int *dest, int *sendtag, int *source, int *recvtag,
                    MPI_Comm *comm, int *fstatus, int *ierr)
   { int MPI_Sendrecv_replace(); MPI_Status *status = malloc(sizeof(MPI_Status));
     *ierr=MPI_Sendrecv_replace( buf, *count, *datatype, *dest, *sendtag,
                                 *source, *recvtag, *comm, status);
     MPI_Status_c2f( status, fstatus );
     free( status ); }


void MPI_BCAST  (void *buf, int *count, MPI_Datatype *datatype, int *root,
                MPI_Comm *comm, int *ierr)
   { int MPI_Bcast();
     *ierr=MPI_Bcast(buf, *count, *datatype, *root, *comm); }
void mpi_bcast  (void *buf, int *count, MPI_Datatype *datatype, int *root,
                MPI_Comm *comm, int *ierr)
   { int MPI_Bcast();
     *ierr=MPI_Bcast(buf, *count, *datatype, *root, *comm); }
void mpi_bcast_ (void *buf, int *count, MPI_Datatype *datatype, int *root,
                MPI_Comm *comm, int *ierr)
   { int MPI_Bcast();
     *ierr=MPI_Bcast(buf, *count, *datatype, *root, *comm); }
void mpi_bcast__(void *buf, int *count, MPI_Datatype *datatype, int *root,
                MPI_Comm *comm, int *ierr)
   { int MPI_Bcast();
     *ierr=MPI_Bcast(buf, *count, *datatype, *root, *comm); }


/* MPI wrappers for cartesian coordinate functions.
 *   The periodicity is ignored, as is reorder, since other MPI 
 *   implementations ignore them as well.
 */

int MPI_Cart_Create( MPI_Comm comm, int ndims, int *dims, int *periods,
                      int reorder, MPI_Comm *comm_cart)
{
   Check_Communicator(comm);
   MP_Cart_Create( ndims, dims);
   *comm_cart = comm;
   return MPI_SUCCESS;
}

int MPI_Cart_Coords( MPI_Comm comm, int rank, int maxdims, int *coords)
{
   Check_Communicator(comm);
   MP_Cart_Coords( rank, coords);
   return MPI_SUCCESS;
}

int MPI_Cart_Get( MPI_Comm comm, int maxdims, int *dims, int *periods, 
                   int *coords)
{
   Check_Communicator(comm);
   MP_Cart_Get( dims, coords);
   return MPI_SUCCESS;
}

int MPI_Cart_Shift( MPI_Comm comm, int dir, int dist, int *source, int *dest)
{
   Check_Communicator(comm);
   MP_Cart_Shift( dir, dist, source, dest);
   return MPI_SUCCESS;
}
   
int MPI_Cart_Rank( MPI_Comm comm, int *coords, int *rank)
{
   Check_Communicator(comm);
   MP_Cart_Rank( coords, rank );
   return MPI_SUCCESS;
}


void MPI_CART_CREATE  ( MPI_Comm *comm, int *ndims, int *dims, int *periods,
                      int *reorder, MPI_Comm *comm_cart, int *ierr)
 { int MPI_Cart_Create();
   *ierr=MPI_Cart_Create( *comm, *ndims, dims, periods, *reorder, comm_cart);} 
void mpi_cart_create  ( MPI_Comm *comm, int *ndims, int *dims, int *periods,
                      int *reorder, MPI_Comm *comm_cart, int *ierr)
 { int MPI_Cart_Create();
   *ierr=MPI_Cart_Create( *comm, *ndims, dims, periods, *reorder, comm_cart);} 
void mpi_cart_create_ ( MPI_Comm *comm, int *ndims, int *dims, int *periods,
                      int *reorder, MPI_Comm *comm_cart, int *ierr)
 { int MPI_Cart_Create();
   *ierr=MPI_Cart_Create( *comm, *ndims, dims, periods, *reorder, comm_cart);} 
void mpi_cart_create__( MPI_Comm *comm, int *ndims, int *dims, int *periods,
                      int *reorder, MPI_Comm *comm_cart, int *ierr)
 { int MPI_Cart_Create();
   *ierr=MPI_Cart_Create( *comm, *ndims, dims, periods, *reorder, comm_cart);} 



void MPI_CART_COORDS  ( MPI_Comm *comm, int *rank, int *maxdims, int *coords,
                        int *ierr)
  { int MPI_Cart_Coords();
    *ierr=MPI_Cart_Coords( *comm, *rank, *maxdims, coords);}
void mpi_cart_coords  ( MPI_Comm *comm, int *rank, int *maxdims, int *coords,
                        int *ierr)
  { int MPI_Cart_Coords();
    *ierr=MPI_Cart_Coords( *comm, *rank, *maxdims, coords);}
void mpi_cart_coords_ ( MPI_Comm *comm, int *rank, int *maxdims, int *coords,
                        int *ierr)
  { int MPI_Cart_Coords();
    *ierr=MPI_Cart_Coords( *comm, *rank, *maxdims, coords);}
void mpi_cart_coords__( MPI_Comm *comm, int *rank, int *maxdims, int *coords,
                        int *ierr)
  { int MPI_Cart_Coords();
    *ierr=MPI_Cart_Coords( *comm, *rank, *maxdims, coords);}


void MPI_CART_GET  ( MPI_Comm *comm, int *maxdims, int *dims, int *periods, 
                     int *coords, int *ierr)
  { int MPI_Cart_Get();
    *ierr=MPI_Cart_Get( *comm, *maxdims, dims, periods, coords);}  
void mpi_cart_get  ( MPI_Comm *comm, int *maxdims, int *dims, int *periods,
                     int *coords, int *ierr)
  { int MPI_Cart_Get();
    *ierr=MPI_Cart_Get( *comm, *maxdims, dims, periods, coords);}  
void mpi_cart_get_ ( MPI_Comm *comm, int *maxdims, int *dims, int *periods,
                     int *coords, int *ierr)
  { int MPI_Cart_Get();
    *ierr=MPI_Cart_Get( *comm, *maxdims, dims, periods, coords);}  
void mpi_cart_get__( MPI_Comm *comm, int *maxdims, int *dims, int *periods,
                     int *coords, int *ierr)
  { int MPI_Cart_Get();
    *ierr=MPI_Cart_Get( *comm, *maxdims, dims, periods, coords);}  


void MPI_CART_SHIFT  ( MPI_Comm *comm, int *dir, int *dist, int *source,
                       int *dest, int *ierr)
  { int MPI_Cart_Shift();
    *ierr=MPI_Cart_Shift( *comm, *dir, *dist, source, dest);}
void mpi_cart_shift  ( MPI_Comm *comm, int *dir, int *dist, int *source,
                       int *dest, int *ierr)
  { int MPI_Cart_Shift();
    *ierr=MPI_Cart_Shift( *comm, *dir, *dist, source, dest);}
void mpi_cart_shift_ ( MPI_Comm *comm, int *dir, int *dist, int *source,
                       int *dest, int *ierr)
  { int MPI_Cart_Shift();
    *ierr=MPI_Cart_Shift( *comm, *dir, *dist, source, dest);}
void mpi_cart_shift__( MPI_Comm *comm, int *dir, int *dist, int *source,
                       int *dest, int *ierr)
  { int MPI_Cart_Shift();
    *ierr=MPI_Cart_Shift( *comm, *dir, *dist, source, dest);}


void MPI_CART_RANK  ( MPI_Comm *comm, int *coords, int *rank, int *ierr)
  { int MPI_Cart_Rank();
    *ierr=MPI_Cart_Rank( *comm, coords, rank);}
void mpi_cart_rank  ( MPI_Comm *comm, int *coords, int *rank, int *ierr)
  { int MPI_Cart_Rank();
    *ierr=MPI_Cart_Rank( *comm, coords, rank);}
void mpi_cart_rank_ ( MPI_Comm *comm, int *coords, int *rank, int *ierr)
  { int MPI_Cart_Rank();
    *ierr=MPI_Cart_Rank( *comm, coords, rank);}
void mpi_cart_rank__( MPI_Comm *comm, int *coords, int *rank, int *ierr)
  { int MPI_Cart_Rank();
    *ierr=MPI_Cart_Rank( *comm, coords, rank);}


int MPI_Comm_free( MPI_Comm *comm ) {
   Check_Communicator(*comm);
   return MPI_SUCCESS; }

void MPI_COMM_FREE  ( MPI_Comm *comm, int *ierr ){*ierr=MPI_Comm_free(comm);}
void mpi_comm_free  ( MPI_Comm *comm, int *ierr ){*ierr=MPI_Comm_free(comm);}
void mpi_comm_free_ ( MPI_Comm *comm, int *ierr ){*ierr=MPI_Comm_free(comm);}
void mpi_comm_free__( MPI_Comm *comm, int *ierr ){*ierr=MPI_Comm_free(comm);}


int MPI_Get_count( MPI_Status *status, MPI_Datatype datatype, int *count)
{
   if( datatype == 0 ) {  /* Just following the crowd on this one */

      *count = 0;

   } else if( status->nbytes % bytesper(datatype) == 0 ) {

      *count = status->nbytes / bytesper(datatype);

   } else return MPI_UNDEFINED;
      
   return MPI_SUCCESS;
}


void MPI_GET_COUNT  ( int *fstatus, MPI_Datatype *datatype, 
                      int *count, int *ierr)
   { int MPI_Get_count();
     MPI_Status *status = malloc(sizeof(MPI_Status));
     status->nbytes = fstatus[0];
     *ierr=MPI_Get_count( status, *datatype, count);
     free( status ); }
void mpi_get_count  ( int *fstatus, MPI_Datatype *datatype, 
                      int *count, int *ierr)
   { int MPI_Get_count();
     MPI_Status *status = malloc(sizeof(MPI_Status));
     status->nbytes = fstatus[0];
     *ierr=MPI_Get_count( status, *datatype, count);
     free( status ); }
void mpi_get_count_ ( int *fstatus, MPI_Datatype *datatype, 
                      int *count, int *ierr)
   { int MPI_Get_count();
     MPI_Status *status = malloc(sizeof(MPI_Status));
     status->nbytes = fstatus[0];
     *ierr=MPI_Get_count( status, *datatype, count);
     free( status ); }
void mpi_get_count__( int *fstatus, MPI_Datatype *datatype, 
                      int *count, int *ierr)
   { int MPI_Get_count();
     MPI_Status *status = malloc(sizeof(MPI_Status));
     status->nbytes = fstatus[0];
     *ierr=MPI_Get_count( status, *datatype, count);
     free( status ); }


int MPI_Status_c2f( MPI_Status *status, int *fstatus)
{
   fstatus[0] = status->nbytes;
   fstatus[1] = status->MPI_SOURCE;
   fstatus[2] = status->MPI_TAG;
   return MPI_SUCCESS;
}


/* Scott Brozell - March 27, 2002 (slight modifications by DDT)
 */

int MPI_Abort( MPI_Comm comm, int errorcode )
{
   char text[18+11+1];
   Check_Communicator(comm);
   fprintf(stderr, "MPI_Abort called with error code %d\nWarning - MPI_Abort may not cleanup promptly; check for undead processes.\n", errorcode);
   sprintf(text, "The error code is %11.1d", errorcode);
/* first argument less than 0 to force abortion; sign of errorcode ?? */
   set_status(-1, "MPI_Abort", text);   /* This will exit */
   return MPI_SUCCESS;
}

  void MPI_ABORT  ( MPI_Comm *comm, int *errorcode, int *ierr )
  { int MPI_Abort(); *ierr = MPI_Abort( *comm, *errorcode ); }
  void mpi_abort  ( MPI_Comm *comm, int *errorcode, int *ierr )
  { int MPI_Abort(); *ierr = MPI_Abort( *comm, *errorcode ); }
  void mpi_abort_ ( MPI_Comm *comm, int *errorcode, int *ierr )
  { int MPI_Abort(); *ierr = MPI_Abort( *comm, *errorcode ); }
  void mpi_abort__( MPI_Comm *comm, int *errorcode, int *ierr )
  { int MPI_Abort(); *ierr = MPI_Abort( *comm, *errorcode ); }


int MPI_Waitall( int count, MPI_Request array_of_requests[], 
                 MPI_Status array_of_statuses[] )
{
   int i;
/*   fprintf(stderr,"Warning - MPI_Waitall may be incorrect.\n");*/
   for (i = 0; i < count; ++i) {
      (void) MPI_Wait( & array_of_requests[i], & array_of_statuses[i] );
   }
   return MPI_SUCCESS;
}

  void MPI_WAITALL  ( int *count, MPI_Request array_of_requests[],
                      int array_of_fstatuses[], int *ierr )
  { int i;
    MPI_Status *array_of_statuses = malloc( *count * sizeof(MPI_Status) );
    *ierr = MPI_Waitall( *count, array_of_requests, array_of_statuses );
    for (i = 0; i < *count; ++i) {
      MPI_Status_c2f( & array_of_statuses[i], & array_of_fstatuses[i] );
    }
    free( array_of_statuses );
  }
  void mpi_waitall  ( int *count, MPI_Request array_of_requests[],
                      int array_of_fstatuses[], int *ierr )
  { int i;
    MPI_Status *array_of_statuses = malloc( *count * sizeof(MPI_Status) );
    *ierr = MPI_Waitall( *count, array_of_requests, array_of_statuses );
    for (i = 0; i < *count; ++i) {
      MPI_Status_c2f( & array_of_statuses[i], & array_of_fstatuses[i] );
    }
    free( array_of_statuses );
  }
  void mpi_waitall_ ( int *count, MPI_Request array_of_requests[],
                      int array_of_fstatuses[], int *ierr )
  { int i;
    MPI_Status *array_of_statuses = malloc( *count * sizeof(MPI_Status) );
    *ierr = MPI_Waitall( *count, array_of_requests, array_of_statuses );
    for (i = 0; i < *count; ++i) {
      MPI_Status_c2f( & array_of_statuses[i], & array_of_fstatuses[i] );
    }
    free( array_of_statuses );
  }
  void mpi_waitall__( int *count, MPI_Request array_of_requests[],
                      int array_of_fstatuses[], int *ierr )
  { int i;
    MPI_Status *array_of_statuses = malloc( *count * sizeof(MPI_Status) );
    *ierr = MPI_Waitall( *count, array_of_requests, array_of_statuses );
    for (i = 0; i < *count; ++i) {
      MPI_Status_c2f( & array_of_statuses[i], & array_of_fstatuses[i] );
    }
    free( array_of_statuses );
  }


/* This MPI_Reduce is actually an MPI_Allreduce.  Problems could arise
 * if a non-root mpi process does not allocate the receive buffer.
 * Scott Brozell - October 4, 2002 
 * 
 * DDT - The MPI standard is not clear on whether all processes need
 * to allocate space in the receive buffer as workspace.  In this
 * implementation, we will just assume that the user is providing
 * a usable receive buffer.  I've at least added a check for a NULL
 * pointer.
 */

int MPI_Reduce(void *sbuf, void *rbuf, int count, MPI_Datatype datatype,
               MPI_Op op, int root, MPI_Comm comm)
{
   int opcode;
/*   extern int *MP_all_mask;*/

   Check_Communicator(comm);
   if( rbuf == NULL ) 
      errorx(0, "You must supply a non-NULL receive buffer for MPI_Reduce()\n");

   if( op == MPI_SUM ) opcode = 0;
   else if( op == MPI_MAX ) opcode = 3;
   else if( op == MPI_MIN ) opcode = 6;
   else {
      fprintf(stderr,"ERROR in MPI_Reduce(), unsupported MPI_Op\n");
      exit(-1);
   }

   if( ( (datatype == MPI_INT || datatype == MPI_INTEGER) && sizeof(int) == 4) ||
         (datatype == MPI_LONG && sizeof(long) == 4) ) {
      opcode += 1;
   } else if( datatype == MPI_FLOAT ||
              datatype == MPI_REAL   ||
              datatype == MPI_REAL4 ) {
      opcode += 2;
   } else if( datatype == MPI_DOUBLE ||
              datatype == MPI_REAL8 ||
              datatype == MPI_DOUBLE_PRECISION ) {
      opcode += 0;
   } else {
      fprintf(stderr,"ERROR in MPI_Reduce(), unsupported MPI_Datatype\n");
      exit(-1);
   }

   memcpy(rbuf,sbuf,count*bytesper(datatype));    /* sbuf --> rbuf */
   MP_Global(rbuf, count, opcode, MP_all_mask);

   return MPI_SUCCESS;
}

void MPI_REDUCE  (void *buf1, void *buf2, int *count, MPI_Datatype *datatype,
          MPI_Op *op, int *root, MPI_Comm *comm, int *ierr)
   { int MPI_Reduce();
     *ierr=MPI_Reduce(buf1, buf2, *count, *datatype, *op, *root, *comm); }
void mpi_reduce  (void *buf1, void *buf2, int *count, MPI_Datatype *datatype,
          MPI_Op *op, int *root, MPI_Comm *comm, int *ierr)
   { int MPI_Reduce();
     *ierr=MPI_Reduce(buf1, buf2, *count, *datatype, *op, *root, *comm); }
void mpi_reduce_ (void *buf1, void *buf2, int *count, MPI_Datatype *datatype,
          MPI_Op *op, int *root, MPI_Comm *comm, int *ierr)
   { int MPI_Reduce();
     *ierr=MPI_Reduce(buf1, buf2, *count, *datatype, *op, *root, *comm); }
void mpi_reduce__(void *buf1, void *buf2, int *count, MPI_Datatype *datatype,
          MPI_Op *op, int *root, MPI_Comm *comm, int *ierr)
   { int MPI_Reduce();
     *ierr=MPI_Reduce(buf1, buf2, *count, *datatype, *op, *root, *comm); }


int MPI_Comm_group( MPI_Comm comm, MPI_Group* group )
{
  int i;

  Check_Communicator(comm);

  group->size = comm_world_group.size;
  group->ranks = comm_world_group.ranks; /* Copy memory address */

  return MPI_SUCCESS;
}

int MPI_Group_incl( MPI_Group old_grp, int n, int* ranks, MPI_Group* new_grp )
{
  int i;
  
  new_grp->size = n;
  new_grp->ranks = malloc(sizeof(int)*n);
  
  for(i=0;i<n;i++)
    new_grp->ranks[i] = old_grp.ranks[ ranks[i] ];

  return MPI_SUCCESS;
}

#ifndef INFINIBAND

/* MPI-2 One-sided communications */

int MPI_Win_create(void *base, MPI_Aint size, int disp_unit, MPI_Info info, 
                   MPI_Comm comm, MPI_Win *win)
{
  /* Ignore info */

  Check_Communicator(comm);
  
  MP_Win_create(base, size, disp_unit, win);

  return MPI_SUCCESS;
}

int MPI_Win_free(MPI_Win *win)
{
  MP_Win_free(win);

  return MPI_SUCCESS;
}
  
int MPI_Put(void *orig_addr, int orig_cnt, MPI_Datatype orig_datatype, 
            int targ_rank, MPI_Aint targ_disp, int targ_cnt, 
            MPI_Datatype targ_datatype, MPI_Win win)
{
  int mid;

  if(orig_datatype != targ_datatype) {
    fprintf(stderr, "ERROR - Origin datatype must match target datatype\n");
    exit(-1);
  }
  if(orig_cnt != targ_cnt) {
    fprintf(stderr, "ERROR - Origin count must match target count\n");
    exit(-1);
  }

  MP_APut(orig_addr, orig_cnt, targ_rank, targ_disp, win, &mid);

  return MPI_SUCCESS;
}

int MPI_Get(void *orig_addr, int orig_cnt, MPI_Datatype orig_datatype, 
            int targ_rank, MPI_Aint targ_disp, int targ_cnt, 
            MPI_Datatype targ_datatype, MPI_Win win)
{
  int mid;

  if(orig_datatype != targ_datatype) {
    fprintf(stderr, "ERROR - Origin datatype must match target datatype\n");
    exit(-1);
  }
  if(orig_cnt != targ_cnt) {
    fprintf(stderr, "ERROR - Origin count must match target count\n");
    exit(-1);
  }
  
  MP_AGet(orig_addr, orig_cnt, targ_rank, targ_disp, win, &mid);
  
  return MPI_SUCCESS;
}

int MPI_Accumulate(void *orig_addr, int orig_cnt, MPI_Datatype orig_datatype, 
                   int targ_rank, MPI_Aint targ_disp, int targ_cnt, 
                   MPI_Datatype targ_datatype, MPI_Op op, MPI_Win win)
{
  int opcode;
  int mid;

  if(orig_datatype != targ_datatype) {
    fprintf(stderr, "ERROR - Origin datatype must match target datatype\n");
    exit(-1);
  }
  if(orig_cnt != targ_cnt) {
    fprintf(stderr, "ERROR - Origin count must match target count\n");
    exit(-1);
  }

  if( op == MPI_SUM ) opcode = 0;
  else if( op == MPI_MAX ) opcode = 3;
  else if( op == MPI_MIN ) opcode = 6;
  else if( op == MPI_REPLACE ) opcode = 9; /* MPI-2 op just for accumulate */
  else {
    fprintf(stderr,"ERROR in MPI_Accumulate(), unsupported MPI_Op\n");
    exit(-1);
  }

  if( ( (orig_datatype == MPI_INT || orig_datatype == MPI_INTEGER) 
        && sizeof(int) == 4) 
      || (orig_datatype == MPI_LONG && sizeof(long) == 4) ) {
    opcode += 1;
  } else if( orig_datatype == MPI_FLOAT ||
             orig_datatype == MPI_REAL   ||
             orig_datatype == MPI_REAL4 ) {
    opcode += 2;
  } else if( orig_datatype == MPI_DOUBLE ||
             orig_datatype == MPI_REAL8 ||
             orig_datatype == MPI_DOUBLE_PRECISION ) {
    opcode += 0;
  } else {
    fprintf(stderr,"ERROR in MPI_Accumulate(), unsupported MPI_Datatype\n");
    exit(-1);
  }
  
  MP_AAccumulate(orig_addr, orig_cnt, targ_rank, targ_disp, opcode, win, &mid);
  
  return MPI_SUCCESS;
}

int MPI_Win_fence(int assert, MPI_Win win)
{
  /* Ignoring assert */

  MP_Win_fence(win);

  return MPI_SUCCESS;
}

int MPI_Win_start(MPI_Group group, int assert, MPI_Win win)
{
  int* procs;
  int i;

  procs = malloc(sizeof(int) * nprocs);
  for(i=0;i<nprocs;i++)     procs[i] = 0;
  for(i=0;i<group.size;i++) procs[ group.ranks[i] ] = 1;
    
  MP_Win_start(procs, win);

  return MPI_SUCCESS;
}

int MPI_Win_complete(MPI_Win win)
{
  MP_Win_complete(win);

  return MPI_SUCCESS;
}

int MPI_Win_post(MPI_Group group, int assert, MPI_Win win)
{
  int* procs;
  int i;

  procs = malloc(sizeof(int) * nprocs);
  for(i=0;i<nprocs;i++)     procs[i] = 0;
  for(i=0;i<group.size;i++) procs[ group.ranks[i] ] = 1;
    
  MP_Win_post(procs, win);

  return MPI_SUCCESS;
}

int MPI_Win_wait(MPI_Win win)
{
  MP_Win_wait(win);

  return MPI_SUCCESS;
}

int MPI_Win_test(MPI_Win win, int* flag)
{
  MP_Win_test(win, flag);

  return MPI_SUCCESS;
}

int MPI_Win_lock(int lock_type, int rank, int assert, MPI_Win win)
{
  MP_Win_lock(lock_type, rank, win);

  return MPI_SUCCESS;
}

int MPI_Win_unlock(int rank, MPI_Win win)
{
  MP_Win_unlock(rank, win);

  return MPI_SUCCESS;
}

#endif /* INFINIBAND */
