// Netpipe module for mpi-2 one-sided communications by Adam Oline
// TODO: This does not currently work with NetPIPE 5.x
// TODO: malloc a single buffer for both send and recv buffers
// TODO: Put Fence() in Sync() and Reset()??? Removed from netpipe.c

/*
  struct protocolstruct
  {
    int nbor, iproc;
    int use_get;
    int no_fence;
  } prot;
*/

#include "netpipe.h"
#include <mpi.h>

MPI_Win* send_win;
MPI_Win* recv_win;

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
  p->prot.use_get = 0;  /* Default to put   */
  p->prot.no_fence = 0; /* Default to fence */

  MPI_Init(pargc, pargv);
}

void Setup(ArgStruct *p)
{
  int nprocs;

  MPI_Comm_rank(MPI_COMM_WORLD, &p->prot.iproc);

  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

  if ( nprocs < 2 )
    {
      printf("Need at least 2 processes, we have %d\n", nprocs);
      exit(-2);
    }

  {
    char s[255], *ptr;
    gethostname(s,253);
    if( s[0] != '.' ) {                 /* just print the base name */
      ptr = strchr( s, '.');
      if( ptr != NULL ) *ptr = '\0';
    }
    printf("%d: %s\n",p->prot.iproc,s);
    fflush(stdout);
  }

  /* TODO: Finish changing netpipe such that it can run with > 2 procs */
  /* 0 <--> (nprocs - 1)
   * 1 <--> (nprocs - 2)
   * ...
   */
 
  p->tr = p->rcv = 0;
  if (p->prot.iproc == 0) {
    p->tr = 1;
    p->prot.nbor = nprocs-1;
    p->myproc = 0;
  } else if( p->prot.iproc == nprocs-1 ) {
    p->rcv = 1;
    p->prot.nbor = 0;
    p->myproc = 1;
  }

}

void Sync(ArgStruct *p)
{
   MPI_Barrier(MPI_COMM_WORLD);
}

int Fence(ArgStruct *p)
{
   MPI_Win_fence(0, *recv_win);

   if( p->cache && p->bidir )

      MPI_Win_fence(0, *send_win);
}

void PrepostRecv(ArgStruct *p)
{

}

void SendData(ArgStruct *p)
{
  int buf_offset = ((p->s_ptr - p->soffset) - p->s_buff) + p->roffset;

  if( p->prot.use_get ) {

    /* First call to fence signals other proc to start get. Second call
     * to fence waits for get to complete. */

    MPI_Win_fence(0, *send_win);

    if( !p->bidir && !p->prot.no_fence ) MPI_Win_fence(0, *send_win);
    
  } else {

    MPI_Put(p->s_ptr, p->bufflen, MPI_BYTE, p->prot.nbor, buf_offset, 
            p->bufflen, MPI_BYTE, *recv_win);
  
    /* If bi-directional mode, we don't need this fence, it will be
     * called in RecvData */

    if ( !p->bidir && !p->prot.no_fence ) MPI_Win_fence(0, *recv_win);
  
  }

}

/* MPI implementations are not required to guarantee message progress
 * without a sync call so this may not work with all implementations.
 */

int TestForCompletion(ArgStruct *p)
{
   if( p->r_ptr[p->bufflen-1] == (p->tr ? p->expected+1 : p->expected-1) ) {
          return 1;
   } else return 0;
}

void RecvData(ArgStruct *p)
{
  volatile unsigned char *cbuf = (unsigned char *) p->r_ptr;

  int buf_offset = ((p->r_ptr - p->roffset) - p->r_buff) + p->soffset;
     
  if( p->prot.use_get ) {

    /* If bi-directional or no-cache mode, we have separate windows for the
     * send and receive buffers, and for gets we need to use the send window
     * (if neither mode, then send_win == recv_win) */
     
    if( !p->bidir ) MPI_Win_fence(0, *send_win);

    MPI_Get(p->r_ptr, p->bufflen, MPI_BYTE, p->prot.nbor, buf_offset, 
            p->bufflen, MPI_BYTE, *send_win);
    
    if( !p->prot.no_fence ) MPI_Win_fence(0, *send_win);
    
  } else {

    /* Waiting for incoming MPI_Put */

    if( !p->prot.no_fence ) MPI_Win_fence(0, *recv_win);

  }
 
  /* If no_fence, then we forego the second call to MPI_Win_fence and instead
   * try to poll on the last byte of the buffer. Since MPI implementations are
   * not required to guarantee message progress without a synchronization call,
   * this may stall with some implementations. */

  if( p->prot.no_fence ) {

    while(cbuf[p->bufflen-1] != (p->tr ? p->expected+1 : p->expected-1))
      sched_yield();

  }
  
}

   /* Gather the times from all procs to proc 0 for output */

void Gather(ArgStruct *p, double *buf)
{
   MPI_Gather( &buf[p->myproc], 1, MPI_DOUBLE, 
                buf,            1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

   /* Proc 0 sets nrepeats then broadcasts it to all procs */

void Broadcast(ArgStruct *p, unsigned int *nrepeat)
{
   MPI_Bcast(nrepeat, 1, MPI_INT, 0, MPI_COMM_WORLD);
}

void CleanUp(ArgStruct *p)
{
  MPI_Finalize();
}


void MyMalloc_Module( ArgStruct *p, int bufflen )
{
  /* After mallocs and alignment, we need to create MPI Windows */
   
  recv_win = (MPI_Win*)malloc(sizeof(MPI_Win));

  MPI_Win_create(p->r_buff, bufflen), 1, NULL, MPI_COMM_WORLD, recv_win);
  
  if( !p->cache || p->bidir ) {
     
     send_win = (MPI_Win*)malloc(sizeof(MPI_Win));

     MPI_Win_create(p->s_buff, bufflen, 1, NULL, MPI_COMM_WORLD, send_win);

  } else {

     send_win = recv_win;

  }
}

void FreeBuffs_Module( ArgStruct *p )    // Free MPI windows
{
  if( recv_win != send_win ) free(send_win);

  free(recv_win);
}


void Module_SwapSendRecvPtrs( ArgStruct *p )
{
   void* tmp;

   tmp = send_win;
   send_win = recv_win;
   recv_win = tmp;
}

