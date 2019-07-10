/*****************************************************************************/
/* "NetPIPE" -- Network Protocol Independent Performance Evaluator.          */
/* Copyright 1997, 1998 Iowa State University Research Foundation, Inc.      */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation.  You should have received a copy of the     */
/* GNU General Public License along with this program; if not, write to the  */
/* Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   */
/*                                                                           */
/*     * memcpy.c           ---- single process memory copy                  */
/*****************************************************************************/
#include "netpipe.h"
#include <sys/shm.h>
/*#undef SPLIT_MEMCPY*/

#ifdef USE_MP_MEMCPY
void MP_memcpy();
#endif


void Init(ArgStruct *p, int* pargc, char*** pargv)
{
   p->tr = 1;
   p->rcv = 0;
   p->myproc = 0;
   p->nprocs = 1;

   p->prot.exchange = 0;
}

static int sync_id, *isync, sync_num = 0;
static int myshm_id, nborshm_id;
static void *myshm;

void Setup(ArgStruct *p)
{
  /* Print out message about results */

   if( p->myproc == 0 ) {
      fprintf(stderr, "\n");
      fprintf(stderr, "  *** Note about memcpy module results ***  \n");
      fprintf(stderr, "\n");
      fprintf(stderr, "The memcpy module is sensitive to the L1 and L2 cache sizes,\n" \
             "the size of the cache-lines, and the compiler.  The following\n" \
             "may help to interpret the results:\n" \
             "\n" \
             "* With cache effects and no perturbations (NPmemcpy -p 0),\n" \
             "    the plot will show 2 peaks.  The first peak is where data is\n" \
             "    copied from L1 cache to L1, peaking around half the L1 cache\n" \
             "    size.  The second peak is where data is copied from the L2 cache\n" \
             "    to L2, peaking around half the L2 cache size.  The curve then\n" \
             "    will drop off as messages are copied from RAM through the caches\n" \
             "    and back to RAM.\n" \
             "\n" \
             "* Without cache effects and no perturbations (NPmemcpy -I -p 0).\n" \
             "    Data always starts in RAM, and is copied through the caches\n" \
             "    up in L1, L2, or RAM depending on the message size.\n"\
             "\n" \
             "* Compiler effects (NPmemcpy)\n" \
             "    The memcpy() function in even current versions of glibc is\n"\
             "    poorly optimized.  Performance is great when the message size\n" \
             "    is divisible by 4 bytes, but other sizes revert to a byte-by-byte\n" \
             "    copy that can be 4-5 times slower.  This produces sharp peaks\n" \
             "    in the curve that are not seen using other compilers.\n" \
             );
      fprintf(stderr, "\n");
   }

   if( p->nprocs == 1 ) return;

   p->dest = (p->nprocs - 1) - p->myproc;
 
      /* Create the shared-memory segment */

   sync_id = shmget(500, (p->nprocs + 1)*sizeof(int), 0660 | IPC_CREAT );

   while( sync_id < 0 ) {

      sched_yield();

      sync_id = shmget(500, (p->nprocs + 1)*sizeof(int), 0660 | IPC_CREAT );
   }

/*fprintf(stderr, "Processor %d has sync_id %d\n", p->myproc, sync_id);*/

      /* Attach the segment to the isync array */

   if( (isync = shmat(sync_id, NULL, 0)) == (void *) -1 ) {

     fprintf(stderr, "Processor %d has shmat() error %d: %s\n",
             p->myproc, errno, strerror(errno));
     exit(0);
   }

/*   isync[p->nprocs] = 0;*/
   isync[p->myproc] = 0;

   sleep(1);

   Sync( p );

   if( p->prot.exchange ) {   /* Create my segment for the data buffer */

      myshm_id = shmget(600+p->myproc, 2*MEMSIZE, 0660 | IPC_CREAT );

      while( myshm_id < 0 ) {

         sched_yield();

         myshm_id = shmget(600+p->myproc, 2*MEMSIZE, 0660 | IPC_CREAT );
      }

/*fprintf(stderr, "Processor %d has myshm_id %d\n", p->myproc, myshm_id);*/

         /* Attach the segment to the myshm pointer */

      if( (myshm = shmat(myshm_id, NULL, 0)) == (void *) -1 ) {

        fprintf(stderr, "Processor %d has shmat() error %d: %s\n",
                p->myproc, errno, strerror(errno));
        exit(0);
      }


      Sync( p );    /* Barrier, then connect to my neighbors segment */


      nborshm_id = shmget(600+p->dest, 2*MEMSIZE, 0660 | IPC_CREAT );

      while( nborshm_id < 0 ) {

         sched_yield();

         nborshm_id = shmget(600+p->dest, 2*MEMSIZE, 0660 | IPC_CREAT );
      }

/*fprintf(stderr, "Processor %d has nborshm_id %d\n", p->myproc, nborshm_id);*/

         /* Attach my buffers to my neighbors segment */

      if( (p->r_buff = shmat(nborshm_id, NULL, 0)) == (void *) -1 ) {

        fprintf(stderr, "Processor %d has shmat() error %d: %s\n",
                p->myproc, errno, strerror(errno));
        exit(0);
      }

      p->s_buff = ((char *) p->r_buff) + MEMSIZE;

      Sync( p );
   }
}   

void PrepareToReceive(ArgStruct *p) { }

void SendData(ArgStruct *p)
{
    int nleft, nbytes = p->bufflen;
    char *src = p->s_ptr, *dest = p->r_ptr;

#ifdef USE_MP_MEMCPY

    MP_memcpy(dest, src, nbytes);

#else

    memcpy(dest, src, nbytes);

#endif
}

/* Some systems such as the Cray XD1 offload the memcpy to the
 * communication chip.  This would allow the CPU to do work while
 * a memcpy procedes.  At this time, I don't know how it implements
 * this asynchronously, so I can't provide a means to test for it.
 */
int TestForCompletion( ArgStruct *p )
{
   return 1;
}

void RecvData(ArgStruct *p)
{
    int nbytes = p->bufflen, nleft;
    char *src = p->s_ptr, *dest = p->r_ptr;

#ifdef USE_MP_MEMCPY

    MP_memcpy(src, dest, nbytes);

#else

    memcpy(src, dest, nbytes);

#endif
}

   /* Just do a global sync since this should be fast */

void Sync(ArgStruct *p)
{
   int i, loop_count = 0;

   if( p->nprocs == 1 ) return;

fprintf(stderr, "%d > Sync\n", p->myproc);

      /* Signal that I have arrived */

   sync_num = (sync_num+1) % 10;
   isync[ p->myproc ] = (isync[ p->myproc ]+1) % 10;

   for( i=0; i<p->nprocs; i++ ) {

      while( isync[i] != sync_num && isync[i] != (sync_num+1)%10 ) {
         loop_count++;
         sched_yield();
/*         if( isync[i]%10 == 11 ) printf("%d",isync[i]);*/
/*         if( loop_count == 10000000 ) {*/
/*            fprintf(stderr,"%d stalled in Sync %d (%d %d)\n",*/
/*                    p->myproc, sync_num, isync[0], isync[1]);*/
/*            exit(0);*/
/*         }*/
      }
   }

fprintf(stderr, "%d < Sync\n", p->myproc);
}

void Broadcast(ArgStruct *p, int *buf)
{
   int nbytes = sizeof(int);

   /* Need a broadcast to do multiple memcpy's */
}

void Gather(ArgStruct *p, double *buf)
{
   int nbytes = sizeof(double);

   /* Need a Gather to do multiple memcpy's */
}

void CleanUp(ArgStruct *p)
{
   struct shmid_ds ds;
   int err = 0;

   if( p->nprocs == 1 ) return;

   Sync( p );

      /* Detach the shared-memory segment */

   err = shmctl( sync_id, IPC_RMID, &ds);

   err = shmctl( myshm_id, IPC_RMID, &ds);

   err = shmctl( nborshm_id, IPC_RMID, &ds);
}

void Reset(ArgStruct *p) { }
