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

#define MPLITE_MAIN
#include "globals.h"
#undef MPLITE_MAIN
#include "mplite.h"

#ifdef NEVER_SET      /* Barrier sync using a circular pass */

void MP_Sync()
{
   int soi=sizeof(int), next, prev, token = 777;

   if( nprocs == 1) return;

   dbprintf("\n%d in MP_Sync()\n",myproc);
   next = (myproc+1)%nprocs;
   prev = (myproc+nprocs-1)%nprocs;
   if( myproc == 0 ) {
      MP_Send(&token, soi, next, 0);
      MP_Recv(&token, soi, prev, 0);
      /* All nodes are here, now send out the 'go' signal */
      MP_Send(&token, soi, next, 0);
      MP_Recv(&token, soi, prev, 0);
   } else {
      MP_Recv(&token, soi, prev, 0);
      MP_Send(&token, soi, next, 0);
      /* All nodes are here, now send out the 'go' signal */
      MP_Recv(&token, soi, prev, 0);
      MP_Send(&token, soi, next, 0);
   }
   dbprintf("%d exiting MP_Sync()\n\n",myproc);
}
#endif

/* gettimeofday() provides decent resolution (~1 ms at worst).
 * getrusage() counts CPU time instead, which is a problem since it
 * doesn't count the idle time while waiting on a packet.
 */

#if defined (_CRAYMPP)
  #include <intrinsics.h>
  double MP_Time()
  {
     return (double) _rtc() / CLK_TCK;
  }

#else
  double MP_Time()
  {
     struct timeval f;

     gettimeofday(&f, (struct timezone *)NULL);

     return ( (double)f.tv_sec + ((double)f.tv_usec)*0.000001);
  }
#endif

   /* Common functions to get processor information.  nprocs and myproc
    * are set in the MP_Init() functions for each module. */

void MP_Nprocs(int* n)
{
   *n = nprocs;
}

void MP_Myproc(int* m)
{
   *m = myproc;
}

/* type-specific versions of byte-based functions
 */

void MP_dSend(void *buf, int count, int dest, int tag)
   { MP_Send( buf, count*sizeof(double), dest, tag); }
void MP_iSend(void *buf, int count, int dest, int tag)
   { MP_Send( buf, count*sizeof(int), dest, tag); }
void MP_fSend(void *buf, int count, int dest, int tag)
   { MP_Send( buf, count*sizeof(float), dest, tag); }

void MP_dRecv(void *buf, int count, int source, int tag)
   { MP_Recv( buf, count*sizeof(double), source, tag); }
void MP_iRecv(void *buf, int count, int source, int tag)
   { MP_Recv( buf, count*sizeof(int), source, tag); }
void MP_fRecv(void *buf, int count, int source, int tag)
   { MP_Recv( buf, count*sizeof(float), source, tag); }

void MP_dASend(void *buf, int count, int dest, int tag, int *msg_id)
   { MP_ASend( buf, count*sizeof(double), dest, tag, msg_id); }
void MP_iASend(void *buf, int count, int dest, int tag, int *msg_id)
   { MP_ASend( buf, count*sizeof(int),    dest, tag, msg_id); }
void MP_fASend(void *buf, int count, int dest, int tag, int *msg_id)
   { MP_ASend( buf, count*sizeof(float),  dest, tag, msg_id); }

void MP_dARecv(void *buf, int count, int source, int tag, int *msg_id)
   { MP_ARecv( buf, count*sizeof(double), source, tag, msg_id); }
void MP_iARecv(void *buf, int count, int source, int tag, int *msg_id)
   { MP_ARecv( buf, count*sizeof(int), source, tag, msg_id); }
void MP_fARecv(void *buf, int count, int source, int tag, int *msg_id)
   { MP_ARecv( buf, count*sizeof(float), source, tag, msg_id); }

/* Masked Broadcast from root to other nodes in the mask using binary tree algorithm */

void MP_Bcast_masked(void *vec, int nbytes, int root, int *mask)
{
   int myvproc, nvprocs, vproc[MAX_PROCS];
   int n, b, igot = 0, source, dest;
   int msg_id, tag = 1711;

   if( mask[myproc] != 1 ) return;

   if( mask[root] == 0 )
      errorx(root, "MP_Bcast_masked() - root node must be in the mask");

   if( nprocs == 1 ) return;

   dbprintf("\n%d in MP_Bcast_masked(%d, %d, %d, mask)\n",myproc,vec,nbytes,root);

   nvprocs = 0;
   vproc[nvprocs++] = root;
   if (myproc == root) myvproc = 0;

   for( n=0; n<nprocs; n++) 
   {
      if (n != root)
      {
         if( n == myproc ) myvproc = nvprocs;
         if( mask[n] == 1 ) vproc[nvprocs++] = n;
      }
   }

   if( nvprocs <= 1 ) return;

   b = 1;
   while( b < nvprocs ) b *= 2;
   if( myproc == root ) igot = 1;

      /* Prepost my receive */

   do {
      b /= 2;
      if ( igot == 0 && myvproc%b == 0 )
      {
         source = myvproc-b;
         MP_ARecv(vec, nbytes, vproc[source], tag, &msg_id);
         igot = 1;
      }
   } while ( b != 1 );

      /* Now do all my sends */

   igot = 0;
   b = 1;

   while( b < nvprocs ) b *= 2;
   if( myproc == root ) igot = 1;

   do {
      b /= 2;

      if( igot )
      {
         dest = myvproc+b;
         if( dest < nvprocs ) MP_Send(vec, nbytes, vproc[dest], tag);
      }
      else if ( myvproc%b == 0 )
      {
         source = myvproc-b;
       MP_Wait(&msg_id);
         igot = 1;
      }
   } while ( b != 1 );

   dbprintf("%d exiting MP_Broadcast_Global()\n",myproc);
}

/* Broadcast from 0 to all other nodes */

void MP_Broadcast(void *ptr, int nbytes)
   { MP_Bcast_masked(ptr, nbytes, 0, MP_all_mask); }

void MP_dBroadcast(void *ptr, int count)
   { MP_Bcast_masked(ptr, count * sizeof(double), 0, MP_all_mask); }

void MP_iBroadcast(void *ptr, int count)
   { MP_Bcast_masked(ptr, count * sizeof(int), 0, MP_all_mask); }

void MP_fBroadcast(void *ptr, int count)
   { MP_Bcast_masked(ptr, count * sizeof(float), 0, MP_all_mask); }

/* Broadcast from different root */

void MP_Bcast(void *ptr, int nbytes, int root)
   { MP_Bcast_masked(ptr, nbytes, root, MP_all_mask); }

void MP_dBcast(void *ptr, int count, int root)
   { MP_Bcast_masked(ptr, count * sizeof(double), root, MP_all_mask); }

void MP_iBcast(void *ptr, int count, int root)
   { MP_Bcast_masked(ptr, count * sizeof(int), root, MP_all_mask); }

void MP_fBcast(void *ptr, int count, int root)
   { MP_Bcast_masked(ptr, count * sizeof(float), root, MP_all_mask); }

/* Masked Broadcast from different root */

void MP_dBcast_masked(void *ptr, int count, int root, int *mask)
   { MP_Bcast_masked(ptr, count * sizeof(double), root, mask); }

void MP_iBcast_masked(void *ptr, int count, int root, int *mask)
   { MP_Bcast_masked(ptr, count * sizeof(int), root, mask); }

void MP_fBcast_masked(void *ptr, int count, int root, int *mask)
   { MP_Bcast_masked(ptr, count * sizeof(float), root, mask); }

/* Global operations
 *   - mask[0..nprocs-1] = 1 for procs that are participating in the operation
 */

void MP_Global(void *vec, int n, int op_type, int *mask)
{
   int dim, mxprocs, source, dest, np, vp, bytes_per, i, *trial;
   int myvproc=myproc, nvprocs, *vproc, rid, got_a_vec;
   int tag=10000;

   double* dvec = (double *) vec;
   int* ivec = (int *) vec;
   float* fvec = (float *) vec;
   void* work;
   double* dwork;
   int* iwork;
   float* fwork;

   /* Bail if my proc is not to take part in this global operation */

   if( nprocs == 1 || mask[myproc] != 1 ) return;
   dbprintf(">MP_Global(%p, %d, %d)\n",vec,n,op_type);

/*  Add pipelining and sync at a later time to overlap comp with comm
 */

   /* Setup myvproc and nvprocs for masked operations */

   vproc = malloc(nprocs*sizeof(int));
   if( ! vproc ) errorx(0,"ERROR in mplite.c - malloc(vproc) failed");

   nvprocs = 0;
   for( i=0; i<nprocs; i++) {
      if( i == myproc ) myvproc = nvprocs;
      if( mask[i] == 1 ) vproc[nvprocs++] = i;
   }
   if( nvprocs <= 1 ) {
      free( vproc );
      dbprintf("%d exiting MP_Global() since nvprocs <= 1\n",myproc);
      return;
   }

   /* determine the type involved in the operation */

   if (op_type%3 == 1) {        /* op_type --> int */
      bytes_per = sizeof(int);
   } else if (op_type%3 == 2) { /* op_type --> float */
      bytes_per = sizeof(float);
   } else {                     /* op_type --> double */
      bytes_per = sizeof(double);
   }
  
   work = malloc(n*bytes_per);

   if( ! work ) errorx(n*bytes_per,"ERROR in mplite.c - malloc(work) failed");

   dwork = (double *) work;
   iwork = (int *) work;
   fwork = (float *) work;

   dim=0; mxprocs=1; while( mxprocs < nvprocs ) { dim++; mxprocs *= 2; }

   trial = malloc(mxprocs * sizeof(int));

   if( ! trial ) errorx(0,"ERROR in mplite.c - malloc(trial) failed");

   for(i=0; i<nvprocs; i++)      trial[i] = 1;
   for(i=nvprocs; i<mxprocs; i++) trial[i] = 0;

   for(np=1; np<mxprocs; np=np*2){

      source = myvproc^np;
      tag++;
       
         /* Prepost the recvs */

      if( trial[source] != 0 ) {
         if( source >= nvprocs ) source = nvprocs-1;
         MP_ARecv(work, n*bytes_per, vproc[source], tag, &rid);
      }

      dest = myvproc^np;             /* Send the data out in a binary tree */
      if (dest < nvprocs) {
         MP_Send(vec, n*bytes_per, vproc[dest], tag);
      }
      if (myvproc == nvprocs-1) {
         for(vp=nvprocs; vp<mxprocs; vp++){
            dest = vp^np;
            if( trial[vp] != 0 && dest < nvprocs-1) {
               MP_Send(vec, n*bytes_per, vproc[dest], tag);
      }  }  }

      got_a_vec = trial[myvproc^np];
      if( got_a_vec ) {
         MP_Wait( &rid );
      }

      switch(op_type) {
        case 0:                                        /* MP_dSum() */
          if( got_a_vec ) for(i=0;i<n;i++) {
            dvec[i] += dwork[i];
          } break;
        case 1:                                        /* MP_iSum() */
          if( got_a_vec ) for(i=0;i<n;i++) {
            ivec[i] += iwork[i];
          } break;
        case 2:                                        /* MP_fSum() */
          if( got_a_vec ) for(i=0;i<n;i++) {
            fvec[i] += fwork[i];
          } break;
        case 3:                                        /* MP_dMax() */
          if( got_a_vec ) for(i=0;i<n;i++) {
            if (dwork[i] > dvec[i]) dvec[i] = dwork[i];
          } break;
        case 4:                                        /* MP_iMax() */
          if( got_a_vec ) for(i=0;i<n;i++) {
            if (iwork[i] > ivec[i]) ivec[i] = iwork[i];
          } break;
        case 5:                                        /* MP_fMax() */
          if( got_a_vec ) for(i=0;i<n;i++) {
            if (fwork[i] > fvec[i]) fvec[i] = fwork[i];
          } break;
        case 6:                                        /* MP_dMin() */
          if( got_a_vec ) for(i=0;i<n;i++) {
            if (dwork[i] < dvec[i]) dvec[i] = dwork[i];
          } break;
        case 7:                                        /* MP_iMin() */
          if( got_a_vec ) for(i=0;i<n;i++) {
            if (iwork[i] < ivec[i]) ivec[i] = iwork[i];
          } break;
        case 8:                                        /* MP_fMin() */
          if( got_a_vec ) for(i=0;i<n;i++) {
            if (fwork[i] < fvec[i]) fvec[i] = fwork[i];
          } break;
        default:
          printf("ERROR - MP_Global() op_type of %d is unkown\n",op_type);
      }
      for(i=0; i<mxprocs; i++) trial[i] += trial[i^np];
   }
   
   free(work);  free(vproc);  free(trial);

   dbprintf("%d exiting MP_Global()\n",myproc);
}

void MP_dSum(double *ptr, int nelements)
{ MP_Global(ptr, nelements, 0, MP_all_mask); }

void MP_iSum(int *ptr, int nelements)
{ MP_Global(ptr, nelements, 1, MP_all_mask); }

void MP_fSum(float *ptr, int nelements)
{ MP_Global(ptr, nelements, 2, MP_all_mask); }

void MP_dMax(double *ptr, int nelements)
{ MP_Global(ptr, nelements, 3, MP_all_mask); }

void MP_iMax(int *ptr, int nelements)
{ MP_Global(ptr, nelements, 4, MP_all_mask); }

void MP_fMax(float *ptr, int nelements)
{ MP_Global(ptr, nelements, 5, MP_all_mask); }

void MP_dMin(double *ptr, int nelements)
{ MP_Global(ptr, nelements, 6, MP_all_mask); }

void MP_iMin(int *ptr, int nelements)
{ MP_Global(ptr, nelements, 7, MP_all_mask); }

void MP_fMin(float *ptr, int nelements)
{ MP_Global(ptr, nelements, 8, MP_all_mask); }

/* Masked versions of the globals */

void MP_dSum_masked(double *ptr, int nelements, int *mask)
{ MP_Global(ptr, nelements, 0, mask); }

void MP_iSum_masked(int *ptr, int nelements, int *mask)
{ MP_Global(ptr, nelements, 1, mask); }

void MP_fSum_masked(float *ptr, int nelements, int *mask)
{ MP_Global(ptr, nelements, 2, mask); }

void MP_dMax_masked(double *ptr, int nelements, int *mask)
{ MP_Global(ptr, nelements, 3, mask); }

void MP_iMax_masked(int *ptr, int nelements, int *mask)
{ MP_Global(ptr, nelements, 4, mask); }

void MP_fMax_masked(float *ptr, int nelements, int *mask)
{ MP_Global(ptr, nelements, 5, mask); }

void MP_dMin_masked(double *ptr, int nelements, int *mask)
{ MP_Global(ptr, nelements, 6, mask); }

void MP_iMin_masked(int *ptr, int nelements, int *mask)
{ MP_Global(ptr, nelements, 7, mask); }

void MP_fMin_masked(float *ptr, int nelements, int *mask)
{ MP_Global(ptr, nelements, 8, mask); }

void MP_Global_gather(void *vec, int n, int op_type, int *mask)
{
   int dim, mxprocs, source, dest, np, vp, bytes_per, i, *trial;
   int myvproc=myproc, nvprocs, *vproc, rid, got_a_vec, offset;
   int malloc_fct, snd_fct, rcv_fct;
   /* Bail if my proc is not to take part in this global operation */

   if( nprocs == 1 || mask[myproc] != 1 ) return;
   dbprintf(">MP_Global_gather(%d, %d, %d)\n",vec,n,op_type);

   /* Setup myvproc and nvprocs for masked operations */

   vproc = malloc(nprocs*sizeof(int));
   if( ! vproc ) errorx(0,"ERROR in mplite.c - malloc(vproc) failed");

   nvprocs = 0;
   for( i=0; i<nprocs; i++) {
      if( i == myproc ) myvproc = nvprocs;
      if( mask[i] == 1 ) vproc[nvprocs++] = i;
   }
   if( nvprocs <= 1 ) {
      free( vproc );
      return;
   }

   /* determine the type involved in the operation */
   
   if(op_type==0)      bytes_per = sizeof(double); /* op_type --> double */
   else if(op_type==1) bytes_per = sizeof(int);    /* op_type --> int */
   else if(op_type==2) bytes_per = sizeof(float);  /* op_type --> float */
   else                bytes_per = 1;              /* op_type --> byte */

   malloc_fct = nvprocs;

   dim=0; mxprocs=1; while( mxprocs < nvprocs ) { dim++; mxprocs *= 2; }

   trial = malloc(mxprocs * sizeof(int));

   if( ! trial ) errorx(0,"ERROR in mplite.c - malloc(trial) failed");

   for(i=0; i<nvprocs; i++)      trial[i] = 1;
   for(i=nvprocs; i<mxprocs; i++) trial[i] = 0;

   for(np=1; np<mxprocs; np=np*2){

      source = myvproc^np;
      /* np gives the number of procs in a "group" which currently have the
       * same data (np=1: each proc has unique data; np=2: groups of two have
       * the same data; ... np=nvprocs: all procs in gather op have same data).
       * 
       * If nvprocs is not a power of 2, we need to adjust accordingly
       * *rank of last node in my group plus one (nvprocs=rank of last node+1)
       * |          *how far from the end of the group myvproc is
       * |          |     *which number of the group myvproc is (0..np-1)
       * |          |     |                                               */
      if((myvproc + (np - (myvproc % np) ) ) > nvprocs) {
        snd_fct = nvprocs%np;
      } else {
        snd_fct = np;
      }
      
      if((source + (np - (source % np) ) ) > nvprocs) {
        rcv_fct = nvprocs%np;
      } else {
        rcv_fct = np;
      }
      
      /* Prepost the recvs */
      if( trial[source] != 0 ) {
         if( source >= nvprocs ) source = nvprocs-1;
         offset = ( source - (source % np) )*bytes_per*n;
         MP_ARecv((char *)vec+offset, rcv_fct*n*bytes_per, vproc[source], 0, &rid);
      }

      dest = myvproc^np;             /* Send the data out in a binary tree */
      offset = ( myvproc - (myvproc % np) )*bytes_per*n;
      if (dest < nvprocs) {
        MP_Send((char *)vec+offset, snd_fct*n*bytes_per, vproc[dest], 0);
      }
      if (myvproc == nvprocs-1) {
         for(vp=nvprocs; vp<mxprocs; vp++){
            dest = vp^np;
            if( trial[vp] != 0 && dest < nvprocs-1) {
               MP_Send((char *)vec+offset, snd_fct*n*bytes_per, vproc[dest], 0);
      }  }  }

      got_a_vec = trial[myvproc^np];
      if( got_a_vec ) {
         MP_Wait( &rid );
      }
      for(i=0; i<mxprocs; i++) trial[i] += trial[i^np];
   }
   
   free(vproc);  free(trial);
   
   dbprintf("%d exiting MP_Global_gather()\n",myproc);

}

void MP_dGather(double *ptr, int nelements)
{ MP_Global_gather(ptr, nelements, 0, MP_all_mask); }

void MP_iGather(int *ptr, int nelements)
{ MP_Global_gather(ptr, nelements, 1, MP_all_mask); }

void MP_fGather(float *ptr, int nelements)
{ MP_Global_gather(ptr, nelements, 2, MP_all_mask); }

void MP_Gather(void *ptr, int nelements)
{ MP_Global_gather(ptr, nelements, 3, MP_all_mask); }

void MP_dGather_masked(double *ptr, int nelements, int *mask)
{ MP_Global_gather(ptr, nelements, 0, mask); }

void MP_iGather_masked(int *ptr, int nelements, int *mask)
{ MP_Global_gather(ptr, nelements, 1, mask); }

void MP_fGather_masked(float *ptr, int nelements, int *mask)
{ MP_Global_gather(ptr, nelements, 2, mask); }

void MP_Gather_masked(void *ptr, int nelements, int *mask)
{ MP_Global_gather(ptr, nelements, 3, mask); }

/* Synchronous send and receive - ARecv preposts then sends ready signal
 *   - This guarantees a bypass of any receive buffer
 */

void MP_SSend(void *buf, int nbytes, int dest, int tag)
{
   int token;

   dbprintf("> MP_SSend(&%d %d %d %d)\n", buf, nbytes, dest, tag);
   MP_Recv(&token, sizeof(int), dest, tag);
   MP_Send(buf, nbytes, dest, tag);
   dbprintf("< MP_SSend()\n");
}

void MP_SRecv(void *buf, int nbytes, int src, int tag)
{
   int token = 888, msg_id;

   dbprintf("> MP_SRecv(&%d %d %d %d)\n", buf, nbytes, src, tag);
   MP_ARecv(buf, nbytes, src, tag, &msg_id);
   MP_Send(&token, sizeof(int), src, tag);
   MP_Wait(&msg_id);
   dbprintf("< MP_SRecv()\n");
}

/* Synchronous shift - Shake hands then block shift the data to prevent
 * overflow of the buffers. Guarantee prepost to provide good performance
 * and bypass buffers if possible.
 */

void MP_SShift(void *buf_out, int dest, void *buf_in, int src, int nbytes)
{
   int token = 888, nsize, nb, nbmax = (buffer_size/32)*8, tag=11000;
   int msg_id, msg_token_id;
   char *ptr_in, *ptr_out;

   dbprintf(" > MP_SShift(&%d %d &%d %d %d)\n",
             buf_out, dest, buf_in, src, nbytes);
   nbmax = 1000000;

   ptr_in = buf_in;
   ptr_out = buf_out;

   for( nb=0; nb<nbytes; nb+=nbmax) {

      nsize = MIN(nbytes-nb, nbmax);

         /* Prepost the receive for each block of data. */

      MP_ARecv(ptr_in, nsize, src, tag+1, &msg_id);

         /* Use asynchronous communications to sync.  Otherwise MPI_Send()
          * can force synchronization and burn you. */

      MP_ARecv(&token, sizeof(int), dest, tag, &msg_token_id);
      MP_Send( &token, sizeof(int), src, tag);
      MP_Wait( &msg_token_id);

      MP_Send(ptr_out, nsize, dest, tag+1);
      MP_Wait( &msg_id );

      ptr_out += nsize;
      ptr_in += nsize;
      tag += 2;
   }
   dbprintf(" < MP_SShift()\n");
}


/* Manage the .mplite.status file
 *   - initialize_status_file() - creates the status file
 *   - set_status() changes the current status for my node
 *   - check_status() checks the overall status and returns -1 if any
 *     node has logged an ERROR, meaning all nodes should abort
 */

void initialize_status_file()
{
   FILE *fd = NULL;
   int node, ntries = 0;

   if( myproc == 0 ) {
      fd = fopen(".mplite.status", "w");
      if( !fd ) errorw(0,"Couldn't open the .mplite.status file for writing");

      fprintf(fd, "Status: Running     \n");
      fprintf(fd, " Node   Status     Messages                               \n");
      for( node=0; node<nprocs; node++) 
         fprintf(fd, " %4d                                                                          \n", node);
      fclose(fd);
   } else {
      while( fd == NULL && ntries < 30 ) {
         fd = fopen(".mplite.status", "r");
         ntries++;
         sleep(1);
      }
      if( !fd ) errorw(0,"Couldn't open the .mplite.status file for reading");
      else fclose(fd);
      sleep(2);
   }

   set_status(0, "starting", "");
}

void set_status(int abort, char *status, char *message)
{ 
   FILE *fd;

   fd = fopen(".mplite.status", "r+");
   if( !fd ) {
      errorw(0,"Couldn't open the .mplite.status file for changing");
   } else {

      if( abort < 0 ) fprintf(fd, "Status: ABORT    ");

      fseek(fd, 80*(myproc+1), SEEK_SET);
      fprintf(fd, "                                                                               ");
      fseek(fd, 80*(myproc+1), SEEK_SET);
      fprintf(fd, " %4d  %-10s %-60s", myproc, status, message);
      fseek(fd, 80*nprocs - 1, SEEK_SET);
      fclose(fd);

      if( abort < 0 ) exit(-1);
   }
}

void check_status()
{
   char status[10];
   FILE *fd;
   int fserr;

   fd = fopen(".mplite.status","r");
   if( !fd ) {
      errorw(0,"Couldn't open the .mplite.status file for checking");
   } else {

      fserr = fscanf(fd, "%*s %s", status);
      fclose(fd);
      if( !strcmp(status,"ABORT") ) {
         set_status(0, "aborting","An ABORT signal was set by another process");
         fprintf(mylog, "ABORTING: Received an abort signal from another node\n");
         exit(-1);
      }
   }
}
   

void errorw(int err_num, char* text)
{
   fprintf(mylog, "\nWARNING(%d) - %s\n\n", err_num, text); fflush(mylog);
}

void errorx(int err_num, char* text)
{
   fprintf(stderr,"*** Node %d ERROR(%d) - %s\n", myproc, err_num, text);
   if( mylog ) {
      fprintf(mylog, "\nERROR(%d) - %s\n", err_num, text); fflush(mylog);
   }
   set_status(-1, "ERROR", text);   /* This will exit */
}

void errorxs(int err_num, char* format_statement, char* string_ptr)
{
   char format_line[200];

   set_status(-1, "ERROR", string_ptr);
   strcpy(format_line, "\nERROR(%d) - ");
   strcat(format_line, format_statement);
   strcat(format_line, "\n");
   fprintf(stderr, format_line, err_num, string_ptr);
   if( ! mylog ) {
      fprintf(mylog,  format_line, err_num, string_ptr); fflush(mylog);
   }
   exit(-1);
}

void range_check(int nbytes, int node)
{
#if defined (NO_SIGNALS)
   static int warn_once = 1;
#endif

   if( node >= nprocs ) {
      fprintf(stderr,
         "ERROR - Send/Recv to/from node %d is out of the range 0-%d\n",
         node, nprocs-1);
      fprintf(mylog, 
         "ERROR - Send/Recv to/from node %d is out of the range 0-%d\n",
         node, nprocs-1); fflush(mylog);
      exit(-1);
   }

/*   if( nbytes <= 0 ) {*/
   if( nbytes < 0 ) {
      fprintf(stderr,"ERROR - Send/Recv of %d bytes is out of range\n",nbytes);
      fprintf(mylog, "ERROR - Send/Recv of %d bytes is out of range\n",nbytes);
   }

#if defined (NO_SIGNALS)
   if( warn_once && nbytes > buffer_size/2 ) {
      lprintf("WARNING - %d byte message being sent through a %d size buffer\n",
           nbytes, buffer_size);
      warn_once = 0;
   }
#endif
}

void stall_check( double *t0, int nbytes, int src, int tag)
{
   char cbuf[100];
   double t, MP_Time();
   static double t_old;
   static int write_once;

   if( *t0 == 0.0 ) {
      write_once = 1;
      *t0 = MP_Time();
      t_old = 0.0;
   }
   t = MP_Time() - *t0;

       /* The Cray T3E can roll a job out, then restart it later.  This 
        * can mess up this routine, so the timer is reset if the last 
        * check occurred more than 60 seconds ago.
        */

   if( (t_old - t) > 60 ) {
      lprintf("Stall_check() detected a job roll-out and reset the timer\n");
      *t0 = MP_Time();
      t = 0.0;
   }
   t_old = t;

   if( t > BAIL_TIME ) {
      sprintf(cbuf, "Bailing after waiting %d secs for %d bytes from node %d",
              (int) t, nbytes, src);
      lprintf("\n%s\n\n", cbuf);
      fprintf(stderr, "\n%s\n\n", cbuf);
      dump_msg_state();
      set_status(-1, "stalled", cbuf);
   } else if ( t > PANIC_TIME && write_once ) {  /* log any long waits */
      dbprintf("\n*** WARNING: Blocking %d seconds for message (& %d %d %d)\n\n",
               (int) t, nbytes, src, tag);
      write_once = 0;
      
   }
}



/* Tracing routines for dumping the start/stop times for each message 
 *   -DTRACE must be specified at compile time to turn tracing on.
 */

   int t_min = 000, t_cur = -1;
   double t_0;
#if defined (TRACE)
   struct trace_element t_el[10000];
#endif

int trace_start( int nbytes, int dest)
{
#if defined (TRACE)
   if( labs(nbytes) > t_min && ++t_cur < 10000 ) {
      t_el[t_cur].node = dest;
      t_el[t_cur].nbytes = nbytes;
      t_el[t_cur].start = MP_Time();
   }
#endif
   return t_cur;
}

void trace_end( int trace_id, int stats, int send_flag)
{
#if defined (TRACE)
   if( trace_id >= 0 && trace_id < 10000 ) {
     t_el[trace_id].end = MP_Time();
     t_el[trace_id].npackets = stats;
     if( send_flag > 1 )
        t_el[trace_id].node += nprocs;   /* signifying a buffered send */
   }
#endif
}

void trace_set0()
{
   MP_Sync();

   t_0 = MP_Time();
}

void trace_dump()
{

#if defined (TRACE)
   int i, n;
   FILE *fd = NULL;
   char filename[100], *arrow;
   double dt;

   sprintf(filename, ".trace.%d", myproc);
   fd = fopen(filename,"w");
   if( !fd ) errorxs(0,"Couldn't open the trace file %s",filename);

   n = MIN(t_cur, 10000);
   fprintf(fd,"  Trace Dump from %s of the first %d comms"
              " larger than %d bytes\n\n", myhostname, n, t_min);

   for( i=0; i<n; i++) {
      t_el[i].start -= t_0;
      t_el[i].end -= t_0;

      if( t_el[i].nbytes < 0 ) {             /* receive */
         arrow = "<--";
         t_el[i].nbytes = - t_el[i].nbytes;
      } else if( t_el[i].node >= nprocs ) {  /* buffered send */
         t_el[i].node -= nprocs;
         arrow = "*->";
      } else {                               /* unbuffered send */
         arrow = "-->";
      }

      dt = ( t_el[i].end - t_el[i].start ) * 1000000;

      if( t_el[i].nbytes == 0 ) {
        fprintf(fd,"%9.6f - %9.6f  nd %d ***************************",
           t_el[i].start, t_el[i].end, myproc);
      } else if (t_el[i].nbytes > 0 ) {
        fprintf(fd,"%9.6f - %9.6f  nd %d  %s  nd %d   %8d bytes",t_el[i].start,
            t_el[i].end, myproc, arrow, t_el[i].node, t_el[i].nbytes);
      }
      if( t_el[i].npackets > 1 )
        fprintf(fd," / %d usec (%d pkts)\n", (int) dt, t_el[i].npackets);
      else fprintf(fd," / %d usec\n", (int) dt);
   }
   fclose(fd);
#endif
}


/* MP_Get_Last_Tag() returns the tag from the last MP_Recv() call, which
 * can be useful after using a tag = -1 'anytag' to receive a message.
 */ 

void MP_Get_Last_Tag(int *tag)
{
   *tag = last_tag;
}

/* MP_Get_Last_Source() returns the source of the last MP_Recv() call, which
 * can be useful after using a source = -1 to receive from any source.
 */

void MP_Get_Last_Source(int *src)
{
   *src = last_src;
}

/* MP_Get_Last_Size() returns the size in bytes from the last MP_Recv() call,
 * which is needed to fill out the status structure for asynchronous calls.
 */

void MP_Get_Last_Size(int *nbytes)
{
   *nbytes = last_size;
}


void MP_Set( char *var, int value)
{
   if( !strcmp(var, "debug") ||
       !strcmp(var, "DEBUG") ||
       !strcmp(var, "Debug") ) {
      if( value >= 0 && value <= 3 ) {
         debug_level = value;
         printf("*** Node %d switching debug mode to level %d\n",
                 myproc,debug_level);
      } else {
         errorw(value, "MP_Set(debug, #) - value out of the range 0-3\n");
      }
   } else {
      errorw(0,"MP_Set(???,value) - Unknown parameter.\n");
   }
}

void MP_Set_Debug(int value) {
   debug_level = value;
   printf("*** Node %d switching debug mode to level %d\n",myproc,debug_level);
}


/* Cartesian routines for dividing the nodes onto a 2D mesh and 
 * determining neighbors for passing.
 */

struct MP_cart_struct {     /* structure for the cartesian commands */
   int ndims;
   int dim[10];
};
static struct MP_cart_struct *mp_cart;

/* If the dims don't multiply up to nprocs, an error is reported.
 * If dims[0] is set to 0, the nodes will be divided among ndims 
 * to minimize the difference between the dimensions.
 */

void MP_Cart_Create(int ndims, int *dim)
{
   int n, np, bf, d, i;

   if( ndims < 1 || ndims > 10 ) 
      errorx(0, "MP_Cart_create() - ndims must be 1 or larger");

   mp_cart = malloc( sizeof( struct MP_cart_struct ) );

   mp_cart->ndims = ndims;

   if( dim[0] == 0 ) {

      for( n=0; n<ndims; n++) dim[n] = 1;

      np = nprocs;   d = 0;

      while( np != 1 ) {

         bf = 1;  for( i=2; i<=np; i++) if( np%i == 0 ) bf = i;

         dim[d] *= bf;

         d = (d+1)%ndims;
         np /= bf;

      }

   } else {

      np = 1;  for( n=0; n<ndims; n++) np *= dim[n];

      if( np != nprocs ) 
         errorx(0, "MP_Cart_Create() - dimensions do not multiply up to nprocs");
   }

   for( n=0; n<ndims; n++)  mp_cart->dim[n] = dim[n];
}


/* Return the coordinates for the given rank */

void MP_Cart_Coords( int iproc, int *coord)
{
   int r, n;

   if( mp_cart == NULL ) errorx(0, "You must use MP_Cart_create() first");

   r = iproc;

   for( n=0; n<mp_cart->ndims; n++) {

      coord[n] = r % mp_cart->dim[n];

      r = r / mp_cart->dim[n];

   }
}


/* Return the dimension info and my coordinate rank.  Redundant and useless */

void MP_Cart_Get( int *dim, int *coord)
{
   int n;

   if( mp_cart == NULL ) errorx(0, "You must use MP_Cart_create() first");

   for( n=0; n<mp_cart->ndims; n++) {

      dim[n] = mp_cart->dim[n];

   }

   MP_Cart_Coords( myproc, coord);

}


/* Return the source and destination nodes given a direction and distance
 * to shift.
 */

void MP_Cart_Shift( int dir, int dist, int *source, int *dest)
{
   int n, d = 1, dmod, base;

   if( mp_cart == NULL ) errorx(0, "You must use MP_Cart_create() first");

   if( dir < 0 || dir > mp_cart->ndims-1 )
      errorx( 0, "MP_Cart_shift() - the direction is greater than ndims");


   for( n=0; n<dir; n++) d = d * mp_cart->dim[n];   /* how much to jump */

   dmod = d * mp_cart->dim[dir];

   base = (myproc / dmod) * dmod;

   *source = base + (myproc-base - dist*d + labs(dist)*dmod) % dmod;

   *dest   = base + (myproc-base + dist*d + labs(dist)*dmod) % dmod;
}


/* Return the rank given the cartesian coordinates */

void MP_Cart_Rank( int *coord, int *rank )
{
   int n, m = 1;

   if( mp_cart == NULL ) errorx(0, "You must use MP_Cart_create() first");

   *rank = 0;

   for( n=0; n<mp_cart->ndims; n++) {

      *rank += m * coord[n];

      m *= mp_cart->dim[n];

   }
}





