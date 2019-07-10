/* MP_Lite message-passing library
 * Copyright (C) 1997-2004 Dave Turner and Xuehua Chen
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

/* 
 * This file contains two kinds of FIFOs. One is provided by
 * the operating system, and the other is implemented using
 * shared memory. You can switch between different FIFOs by 
 * defining FIFO differently. 
 */
#include "globals.h"

#define SYS_FIFO  1           /* System provided FIFO */
#define SHM_FIFO  2           /* Our implemenation of FIFO using shared memory */

#define FIFO SHM_FIFO         /* FIFO to be used */
//#define FIFO SYS_FIFO         /* FIFO to be used */


#if FIFO == SHM_FIFO          

/* 
 * Use FIFOs implemented by shared memory 
 */
#define FIFO_MAX 40         /* The maximum number of entries in a fifo */
typedef struct Fifo
{
  volatile int recvpos;      /* The position to receive next entry */
  volatile int sendpos;      /* The position to send next entry    */ 
  uint64_t offset[FIFO_MAX];   /* An array to store the entries      */  
} Fifo;

static Fifo       *fifo_pool_shm;
static Fifo       **fifo_in;    /* point to Fifos for receiving entries */
static Fifo       **fifo_out;   /* point to Fifos for sending entries   */ 

// Give shm.c the Fifo size so it can create room in pool_shm

int sizeofFifo()
{
   return ( (nsmp*nsmp * sizeof(Fifo) + 4095)/4096 ) * 4096;
}

// initialize the fifos

void init_fifo( char *ptr )
{
  int i, j;
  Fifo *tmp_fifo;

  fifo_pool_shm = (Fifo *) ptr;

  //lprintf("init_fifo( %p )\n", fifo_pool_shm);

  fifo_in  = (Fifo **) malloc (sizeof(Fifo*)*nsmp*nsmp);
  fifo_out = (Fifo **) malloc (sizeof(Fifo*)*nsmp*nsmp);

  for (i = 0;  i < nsmp; i ++) {
    for (j = 0; j < nsmp; j ++) {
      * (fifo_in + i * nsmp + j) = tmp_fifo = fifo_pool_shm + i * nsmp + j;
      tmp_fifo->sendpos = tmp_fifo->recvpos = 0;
      * (fifo_out + j * nsmp + i) = tmp_fifo = fifo_pool_shm + i * nsmp + j;
      tmp_fifo->sendpos = tmp_fifo->recvpos = 0;
    }
  }
}

// Put the offset address into the FIFO for processor dest

void FIFOwrite(int dest, uint64_t offset)
{
  Fifo *w_fifo;
  int j;

  w_fifo = *(fifo_out + smp_rank[myproc] * nsmp + dest);

  for (j = 0; j < nsmp; j ++) {
    if (w_fifo->sendpos + 1 != w_fifo->recvpos &&
                  (w_fifo->sendpos + 1 != FIFO_MAX || w_fifo->recvpos != 0))
    {

      w_fifo->offset[w_fifo->sendpos] = offset;

      if (w_fifo->sendpos == FIFO_MAX -1)
        w_fifo->sendpos = 0;
      else
        w_fifo->sendpos ++;
      break;
    }
    sched_yield();
  }
}

// Return the next message that comes into the FIFO queue

uint64_t FIFOread(int src)
{
  Fifo * r_fifo;
  uint64_t offset;

  r_fifo = *(fifo_in + smp_rank[myproc] * nsmp + src);

  while (r_fifo->recvpos == r_fifo->sendpos) sched_yield();

  offset = r_fifo->offset[r_fifo->recvpos];

  if (r_fifo->recvpos == FIFO_MAX - 1)
    r_fifo->recvpos = 0;
  else
    r_fifo->recvpos ++;

  return offset;
}


#elif FIFO == SYS_FIFO
/* 
 * Use FIFO provided by the OS 
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define FIFO_SIZE 40           /* Set the maximum size of the FIFOs' pathnames */
#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)  /* FIFOs' open mode */
static char **fifo;                                        /* FIFOs' pathnames */
static int *writefd, *readfd;

/*
 * allocate fifos 
 */
void allocate_fifo( )
{
  writefd=malloc(sizeof(int)*nsmp*nsmp);
  readfd =malloc(sizeof(int)*nsmp*nsmp);
  fifo = malloc(sizeof(char *)*nsmp*nsmp);
}

/*
 * initialization fifos
 */
void init_fifo( pool_ptr )
{
  int i, j;
  writefd=malloc(sizeof(int)*nsmp*nsmp);
  readfd =malloc(sizeof(int)*nsmp*nsmp);
  fifo = malloc(sizeof(char *)*nsmp*nsmp);
  for (i = 0; i < nsmp; i++)
  {
    if(i != smp_rank[myproc])
    {
      *(fifo + smp_rank[myproc] * nsmp + i) = malloc(FIFO_SIZE);
      snprintf(*(fifo + smp_rank[myproc] * nsmp + i), FIFO_SIZE,
                                   "/tmp/fifo.%d.%d", smp_rank[myproc], i);
      if( mkfifo( *(fifo + smp_rank[myproc] * nsmp + i),  FILE_MODE )   <  0 )
                             /* Create FIFOs needed for all the processes */
      {
        if (errno == EEXIST)
        {
          fprintf(stderr,"%s already exist", *(fifo + smp_rank[myproc] * nsmp + i) );
          exit(1);
        }
      }

      *(fifo + i * nsmp + smp_rank[myproc]) = malloc(FIFO_SIZE);
      snprintf( *(fifo + i * nsmp + smp_rank[myproc]), FIFO_SIZE,
                                             "/tmp/fifo.%d.%d", i, smp_rank[myproc]);
    }
  }
  MP_Sync();
  for (i=0; i<nsmp; i++)
  {
    if(i==smp_rank[myproc])
      *(writefd+smp_rank[myproc]*nsmp+i)=0;
    else
    {
      if(smp_rank[myproc]<i)
      {
        *(writefd + smp_rank[myproc]*nsmp+i)=open(*(fifo+smp_rank[myproc]*nsmp+i), O_WRONLY, 0);
        *(readfd + smp_rank[myproc]*nsmp+i)=open(*(fifo+i*nsmp+smp_rank[myproc]), O_RDONLY, 0);
      }
      else
      {
        *(readfd+smp_rank[myproc]*nsmp+i)=open(*(fifo+i*nsmp+smp_rank[myproc]), O_RDONLY, 0);
        *(writefd+smp_rank[myproc]*nsmp+i) = open(*(fifo+smp_rank[myproc]*nsmp+i), O_WRONLY, 0);
      }
    }
  }
}

/*
 * A wrapper of write
 */
void FIFOwrite(int dest, void *address)
{
   int irtn;
   irtn = write( *(writefd + smp_rank[myproc]*nsmp + dest),
                         &address, sizeof(address));
}

/*
 * A wrapper of read 
 */
void FIFOread(int src, void ** address)
{
   int irtn;
   irtn = read(*(readfd + src + smp_rank[myproc]*nsmp),
}

/*
 * remove fifos
 */
void freefifo( )
{
  int i;
  for (i = 0; i < nsmp; i++)
  {
    if(i != smp_rank[myproc])
      unlink(*(fifo + smp_rank[myproc]*nsmp + i));
  }
}
#endif
