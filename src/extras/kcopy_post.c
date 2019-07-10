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
/*     * kernel_copy.c - Module to test the Linux kernel copy module for     */
/*                       SMP message passing.                                */
/*     - Dave Turner - July 2004+                                            */
/*****************************************************************************/
#include    "netpipe.h"
#include    <errno.h>
#include    <sys/stat.h>
#include    <fcntl.h>

#include "kcopy.h"   /* Contains the kcopy_hdr struct and KCOPY_ enums */

struct kcopy_hdr k_hdr;
static int dd;   /* The device descriptor for /dev/kcopy */
static void *v_pages;

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
   p->tr = 0;
   p->rcv = 1;
}

void Setup(ArgStruct *p)
{

   int i, j, proc, mypid, lc;
   char command[100];
   FILE *pd;


   if( p->nprocs > MAXNSMP ) {
      fprintf(stderr,"ERROR: %d procs requested is more than MAXNSMP=%d"
                     " defined in src/netpipe.h\n", p->nprocs, MAXNSMP);
      exit(0);
   }

      /* Allocate space for the list of PID numbers */

   p->pid = (int *) malloc( p->nprocs * sizeof( int ) );

   mypid = getpid();

      /* Block until nprocs .np.pid# files are present */

   sprintf( command, "touch .np.%d", mypid);

   system( command );

   lc = 0;


   while( lc != p->nprocs ) {

      pd = popen( "ls -a1 | grep .np. | wc -l", "r");

      fscanf( pd, "%d", &lc );

      pclose( pd );
   }

      /* Now read the other PID numbers and set myproc */

   pd = popen( "ls -a1 | grep .np.", "r");

   for( proc=0; proc<p->nprocs; proc++ ) {

      fscanf( pd, ".np.%d\n", &p->pid[proc] );

      if( p->pid[proc] == mypid ) p->myproc = proc;
   }

   pclose( pd );

   if( p->myproc < 0 ) {
      fprintf(stderr,"ERROR: Myproc was not set properly (mypid=%d)\n",mypid);
      exit(0);
   }

      /* PID of proc 0 is the unique identifier for this run */

   k_hdr.uid    = p->pid[0];
   k_hdr.myproc = p->myproc;
   k_hdr.nprocs = p->nprocs;
   k_hdr.l_adr  = NULL;
   k_hdr.r_adr  = NULL;
   k_hdr.r_proc = 0;
   k_hdr.nbytes = 0;
   k_hdr.tag    = 3;

      /* Set the first half to be transmitters */

   if( p->myproc < p->nprocs/2 ) p->tr = 1;
   else p->rcv = 1;

   p->dest = p->source = p->nprocs - 1 - p->myproc;

   establish(p);                               /* Establish connections */


      /* Remove the .np.pid.adr files */

   if( p->master ) system( "rm -f .np.*" );

}   


void establish(ArgStruct *p)
{
   int i, nbytes;

      /* Open the connection to the /dev/kcopy device */

   dd = open( "/dev/kcopy", O_WRONLY );

   if( dd < 0 ) {   /* An error has occured */
      fprintf(stderr, "ERROR: Proc %d could not open the /dev/kcopy device\n", p->myproc);
      fprintf(stderr, "       errno=%d   %s\n", errno, strerror(errno) );
   }

   k_hdr.job = -1;
   k_hdr.nbytes = 1;   /* Signal to initialize the kcopy queue */

      /* Pass along 10 user-space pages in case we need it */

#define PAGE_SIZE 4096
   posix_memalign( &v_pages, PAGE_SIZE, 10*PAGE_SIZE);
   k_hdr.r_adr = v_pages;

   k_hdr.job = ioctl( dd, KCOPY_INIT, &k_hdr );   /* Initialize kcopy_ioctl */

      /* Test the Sync call. */

   Sync( p );
}


   /* Put nbytes from sbuf on myproc to dbuf on the destination proc. */

int put(ArgStruct *p, int dest, void *sbuf, void *dbuf, int nbytes )
{
   int nleft;

   k_hdr.l_adr  = sbuf;
   k_hdr.r_adr  = dbuf;
   k_hdr.r_proc = dest;
   k_hdr.nbytes = nbytes;

   nleft = ioctl( dd, KCOPY_PUT, &k_hdr );   /* Do the kcopy SMP_PUT */

   if( nleft != 0 ) {
      fprintf(stderr,"ERROR: Proc %d failed to write %d of %d bytes\n",
              p->myproc, nleft, nbytes);
   }

   return nleft;
}

int ptr_dump(ArgStruct *p, int dest, void *sbuf, void *dbuf, int nbytes )
{
   int nleft;

   k_hdr.l_adr  = sbuf;
   k_hdr.r_adr  = dbuf;
   k_hdr.r_proc = dest;
   k_hdr.nbytes = nbytes;

   nleft = ioctl( dd, KCOPY_PTR_DUMP, &k_hdr );   /* Do the kcopy SMP_PUT */

   return nleft;
}

   /* Global sync with the master proc, then local sync with your comm pair */

void Sync(ArgStruct *p)
{
   int err = 0;

   err = ioctl( dd, KCOPY_SYNC, &k_hdr );  /* Let the kcopy device do the sync */

   if( err == -1 ) {
      fprintf(stderr,"ERROR: Sync timed out for proc %d\n", p->myproc);
      exit(0);
   }
}

   /* Post a msg to the kcopy.o queue and let it handle the rest */

void post( ArgStruct *p, kcopy_comm_t command )
{
/*fprintf(stderr,"posting a message\n");*/
   p->prot.nleft = p->bufflen;

/*   if( command == KCOPY_POST_SEND ) k_hdr.r_proc = p->dest;*/
/*   else k_hdr.r_proc = p->source;*/
/*ptr_dump(p, k_hdr.r_proc, (void *) &p->prot.nleft, NULL, sizeof(int) );*/

   p->prot.posted = 1;

   k_hdr.r_adr = &p->prot;  /* Completion notification structure */

   if( command == KCOPY_POST_SEND ) {
      k_hdr.l_adr = p->s_ptr;
      k_hdr.r_proc = p->dest;
   } else {
      k_hdr.l_adr = p->r_ptr;
      k_hdr.r_proc = p->source;
   }
   k_hdr.nbytes = p->bufflen;

   ioctl( dd, command, &k_hdr);
/*fprintf(stderr,"done\n");*/
}

   /* Post the recv to the kcopy.o module */

void PrepareToReceive(ArgStruct *p)
{
/*fprintf(stderr,"preposting a recv\n");*/
   post( p, KCOPY_POST_RECV );
/*fprintf(stderr,"done\n");*/
}

   /* Post the send and wait for completion */

void SendData(ArgStruct *p)
{
   double t0 = myclock();

/*fprintf(stderr,"posting a send %p\n", &p->prot);*/

   post( p, KCOPY_POST_SEND );

   while( p->prot.nleft != 0 ) {    /* Spin on completion */
      sched_yield();
      if( p->prot.nleft%2 == 3 ) printf("%d", p->prot.nleft);
      if( (myclock() - t0) > 5 ) {
         fprintf(stderr,"%d waited %0.2lf seconds to send %d bytes\n",
                 p->myproc, myclock()-t0, p->bufflen);
         exit(0);
      }
   }
   p->prot.posted = 0;
/*fprintf(stderr,"done\n");*/
}

int TestForCompletion( ArgStruct *p )
{
   if( p->prot.nleft == 0 ) return 1;
   else return 0;

/*
   volatile char *cbuf = (char *) p->r_ptr;

   if( cbuf[p->bufflen-1] == p->expected ) return 1;
   else return 0;
*/
}

   /* post the recv if not already done and spin for completion */

void RecvData(ArgStruct *p)
{
   double t0 = myclock();

/*fprintf(stderr,"posting a recv %p\n", &p->prot);*/

   if( ! p->prot.posted ) post( p, KCOPY_POST_RECV );

   while( p->prot.nleft != 0 ) {    /* Spin on completion */
      sched_yield();
      if( p->prot.nleft%2 == 3 ) printf("%d", p->prot.nleft);
      if( (myclock() - t0) > 5 ) {
         fprintf(stderr,"%d waited %0.2lf seconds to send %d bytes\n",
                 p->myproc, myclock()-t0, p->bufflen);
         exit(0);
      }
   }

   p->prot.posted = 0;
/*fprintf(stderr,"done\n");*/
}

   /* Gather routine (used to gather times) */

void Gather( ArgStruct *p, double *buf)
{
   k_hdr.nbytes = sizeof(double);
   k_hdr.l_adr  = &buf[p->myproc];
   k_hdr.r_adr  = buf;  /* Actually dest adr for my proc */

   ioctl( dd, KCOPY_GATHER, &k_hdr);
}

   /* Broadcast nrepeat from master 0 to all other procs.
    * For this module, we also need to exchange rbuf with our comm pair.
    */

void Broadcast(ArgStruct *p, unsigned int *ibuf)
{
   void **parray;

/*fprintf(stderr,"%d entering Broadcast (nrepeat=%d)\n", p->myproc,*ibuf);*/

   k_hdr.nbytes = sizeof(int);
   k_hdr.l_adr  = ibuf;

   ioctl( dd, KCOPY_BCAST, &k_hdr);

/*fprintf(stderr,"%d has nrepeat=%d\n", p->myproc, *ibuf);*/

      /* Now exchange the receive buffer pointer rbuf with our comm pair */

   parray = (void **) malloc( p->nprocs * sizeof(void *) );

   k_hdr.nbytes = sizeof(void *);
   k_hdr.l_adr  = &p->r_buff;
   k_hdr.r_adr  = parray;  /* Actually dest adr for my proc */

   ioctl( dd, KCOPY_GATHER, &k_hdr);

   p->dr_buff = parray[p->dest];
   p->dr_ptr  = p->dr_buff + p->roffset;

/*fprintf(stderr,"%d has r_buff=%p and dest %d dr_buff=%p\n",*/
/*        p->myproc, p->r_buff, p->dest, p->dr_buff);*/

      /* Now exchange the send buffer pointer sbuf with our comm pair */

   k_hdr.l_adr = &p->s_buff;

   ioctl( dd, KCOPY_GATHER, &k_hdr);

   p->ds_buff = parray[p->dest];
   p->ds_ptr  = p->ds_buff + p->soffset;

   free( parray );
/*fprintf(stderr,"%d has s_buff=%p and dest %d ds_buff=%p\n",*/
/*        p->myproc, p->s_buff, p->dest, p->ds_buff);*/
}


void CleanUp(ArgStruct *p)
{
   free( v_pages );

      /* Tell the kcopy.o module to clean up the kmalloced arrays */

   ioctl( dd, KCOPY_QUIT, &k_hdr);

   sleep(1);       /* Give the other procs a chance to CleanUp before exiting */

   close( dd );    /* Close the link to the device driver */
}

ModuleSwapPtrs(ArgStruct *p)
{
   void *tmp_ptr;

/*fprintf(stderr,"%d swapping d_ptrs %p %p\n", p->myproc, p->ds_ptr, p->dr_ptr);*/

   tmp_ptr = p->dr_buff;
   p->dr_buff = p->ds_buff;
   p->ds_buff = tmp_ptr;

      /* Remove offsets before flip-flop */
   
   p->ds_ptr -= p->soffset;
   p->dr_ptr -= p->roffset;

   tmp_ptr = p->dr_ptr;
   p->dr_ptr = p->ds_ptr;
   p->ds_ptr = tmp_ptr;
   
      /* Add on offsets */
   
   p->ds_ptr += p->soffset;
   p->dr_ptr += p->roffset;

/*fprintf(stderr,"%d swapping d_ptrs %p %p\n", p->myproc, p->ds_ptr, p->dr_ptr);*/
}

void Reset(ArgStruct *p) { }

