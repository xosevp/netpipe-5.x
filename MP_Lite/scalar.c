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

/* This file is for the scalar version which:
 *    - provides NULL stubs for all MP_Lite commands
 *      so that the codes can be run in scalar mode 
 */

#define SCALAR_FILE
#include "globals.h"
#include "mplite.h"
#undef SCALAR_FILE

void MP_Init(int *argc, char ***argv)
{ 
   myproc=0;
   nprocs=1;
   MP_all_mask = malloc(sizeof(int));
   if( ! MP_all_mask ) errorx(0,"scalar.c - malloc(MP_all_mask) failed");
   MP_all_mask[0] = 1;
}
void MP_Finalize() { }
void MP_Wait(int *msg_id) { }
void MP_Sync() {}

void MP_ASend(void *buf, int nbytes, int dest, int tag, int *msg_id)
{
   if( dest != 0 )
      printf("WARNING - MP_ASend() is trying to send to node %d"
             " in a serial run\n", dest);
}

void MP_Send(void *buf, int nbytes, int dest, int tag)
{
   if( dest != 0 )
      printf("WARNING - MP_Send() is trying to send to node %d"
             " in a serial run\n", dest);
}

void MP_IPSend( void *data_ptr, int len, int dest, int tag)
{
   if( dest != 0 )
      printf("WARNING - MP_IPSend() is trying to send to node %d"
             " in a serial run\n", dest);
}



void MP_ARecv(void *buf, int nbytes, int src, int tag, int *msg_id)
{
   if( src != 0 )
      printf("WARNING - MP_ARecv() is trying to receive from node %d"
             " in a serial run\n", src);
}

void MP_Recv(void *buf, int nbytes, int src, int tag)
{
   if( src != 0 )
      printf("WARNING - MP_Recv() is trying to receive from node %d"
             " in a serial run\n", src);
}

void MP_IPRecv( void **data_ptr, int len, int src, int tag)
{
   if( src != 0 )
      printf("WARNING - MP_IPRecv() is trying to receive from node %d"
             " in a serial run\n", src);
}


void *MP_shmMalloc( int nbytes)
{
   return malloc( nbytes );
}

void MP_shmFree( void *shmem_ptr )
{
   free( shmem_ptr );
}

void MP_Bedcheck() {}
void dump_msg_state() {}
