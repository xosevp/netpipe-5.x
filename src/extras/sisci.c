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
/*     * MPI.c              ---- MPI calls source                            */
/*****************************************************************************/
#include "netpipe.h"
#include <errno.h>

#include <stdio.h>
#include <netinet/in.h>
#include <netdb.h>

#include "scivlib.c"

int scifd = 0;

/*
  #include "sisci_error.h"
  #include "sisci_api.h"
  #include "sisci_demolib.h"
  #include "scilib.h"

  struct protocolstruct {
    int nodeid;
    int* nodeids;
  } prot;

*/

/* aro */
int is_host_local(char* hostname)
{
  struct hostent* hostinfo;
  char* addr;
  char buf[1024];
  char cmd[80];
  FILE* output;

  hostinfo = gethostbyname(hostname);

  if(hostinfo == NULL) {
    fprintf(stderr, "Could not resolve hostname [%s] to IP address", hostname);
    fprintf(stderr, "Reason: ");

    switch(h_errno)
      {
      case HOST_NOT_FOUND:
        printf("host not found\n");
        break;

      case NO_ADDRESS:
        printf("no IP address available\n");
        break;

      case NO_RECOVERY:
        printf("name server error\n");
        break;

      case TRY_AGAIN:
        printf("temporary error on name server, try again later\n");
        break;

      }

    return -1;
  }

  addr = (char*)inet_ntoa(*(struct in_addr *)hostinfo->h_addr_list[0]);

  sprintf(cmd, "/sbin/ifconfig | grep %s", addr);

  output = popen(cmd, "r");

  if(output == NULL) {
    fprintf(stderr, "running /sbin/ifconfig failed\n");
    return -1;
  }

  if(fgets(buf, 1024, output) == NULL) {
    pclose(output);
    return 0;
  } else {
    pclose(output);
    return 1;
  }
}


int TestForCompletion(ArgStruct *p) {
  return 0;
}

/* Initialize vars in Init() that may be changed by parsing the command args */

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
  int i,j;

  p->source = 0;  /* Default source node */

}

void Setup(ArgStruct *p)
{
   char s[255], *ptr;
   FILE *fd;
   int i, np, mp = -1, node;

  p->myproc = 0;
  np = p->nprocs;
  p->nprocs = np;

   p->prot.nodeids = (int*)malloc(p->nprocs * sizeof(int));

/* DDT - This could use better error checking on the input file */

   p->hostfile = "siscihosts";

   if( p->hostfile != NULL ) {

      if( (fd = fopen( p->hostfile, "r")) == NULL ) {
         fprintf(stderr, "Could not open the hostfile %s\n", p->hostfile );
         exit(0);
      }

      p->host = (char **) malloc( p->nprocs * sizeof(char *) );
      for( i=0; i<p->nprocs; i++ )
         p->host[i] = (char *) malloc( 100 * sizeof(char) );

      mp = -1;
      for( i=0; i<p->nprocs; i++ ) {
         fscanf( fd, "%s %d\n", p->host[i], &(p->prot.nodeids[i]));
         if( is_host_local(p->host[i]) ) mp = i;
         if( is_host_local(p->host[i]) ) printf("%s yes\n", p->host[i]); else printf("%s no\n", p->host[i]);
      }

      if( mp < 0 ) {
         fprintf(stderr, "%d NetPIPE: Error reading the hostfile, mp=%d\n",
                 p->myproc, mp);
         exit(0);
      }

      p->dest = p->nprocs - 1 - mp;
      p->myproc = mp;
   }

  p->prot.nodeid = p->prot.nodeids[mp];

  printf("%d: %d procs detected\n", mp, p->nprocs);

  for(i=0;i<p->nprocs;i++) {
    printf("%d: %s %d\n", mp, p->host[i], p->prot.nodeids[i]);
}

       /* The first half are transmitters by default.  */

   p->tr = p->rcv = 0;
   if( p->myproc < p->nprocs/2 ) p->tr = 1;
   else p->rcv = 1;

       /* p->source may already have been set to -1 (MPI_ANY_SOURCE)
        * by specifying a -z on the command line.  If not, set the source
        * node normally. */

   if( p->source == 0 ) p->source = p->dest;

   if( p->bidir && p->myproc == 0 ) {
      fprintf(stderr,"MPI implementations do not have to guarantee message progress.\n");
      fprintf(stderr,"You may need to run using -a to avoid locking up.\n\n");
   }

   gethostname(s,253);
   if( s[0] != '.' ) {                 /* just print the base name */
      ptr = strchr( s, '.');
      if( ptr != NULL ) *ptr = '\0';
   }
   fprintf(stderr,"%d: %s\n",p->myproc,s);

   if( np != p->nprocs && p->nprocs != 2 ) {
      if( p->myproc == 0 ) {
         fprintf(stderr,"MPI_Comm_size doesn't match default or command line nprocs,\n");
         fprintf(stderr,"nprocs will be reset to %d in mpi.c#setup()\n",np);
      }
   }
   p->nprocs = np;

   if (p->nprocs < 2 || p->nprocs%2 == 1) {
      fprintf(stderr, "tcp.c: nprocs = %d must be even and at least 2\n", p->nprocs);
      exit(0);
   }

   p->dest = p->nprocs - 1 - p->myproc;

       /* You can change the communication pairs using the <-H hostfile>
        * where hostfile has a listing of the new process numbers,
        * with the first and last communicating, etc.
        * In this code, it changes each procs myproc and dest.
        */


    printf("my id ==> %d\n", mp);

    scifd = scivlib_init( mp, p->prot.nodeids[mp^1], 0 );

}

void Sync(ArgStruct *p)
{
}

static int recvPosted = 0;


int readFully( ArgStruct *p, int src, void *buf, int nbytes);
int writeFully( ArgStruct *p, int dest, void *buf, int nbytes);



void PrepareToReceive(ArgStruct *p)
{
    if (recvPosted)
    {
        fprintf(stderr,"Can't prepare to receive: outstanding receive!\n");
        exit(-1);
    }
    recvPosted = -1;
}

void SendData(ArgStruct *p)
{
    writeFully(p, p->dest, p->s_ptr, p->bufflen);
//printf("op %02x ||| write %02x\n", op, ((unsigned char*)p->s_ptr)[0]); fflush(0);
}

void RecvData(ArgStruct *p)
{
    readFully(p, p->source, p->r_ptr, p->bufflen);
//printf("op %02x ||| read %02x\n", op, ((unsigned char*)p->r_ptr)[0]); fflush(0);
}

void CleanUp(ArgStruct *p)
{
    scivlib_shutdown(scifd);
}

void Reset(ArgStruct *p) { }

//void AfterAlignmentInit(ArgStruct *p) { }


#define _BLOCK_SIZE (1024*1024*2)

int writeFully( ArgStruct *p, int src, void *_buf, int _nbytes )
{
#if 1
    scivlib_write( scifd, _buf, _nbytes );
    return _nbytes;
#else
  int i;
  for(i=0;i<_nbytes;i+=_BLOCK_SIZE) {
    scivlib_write( scifd, (char*)_buf+i, ((_nbytes-i)<_BLOCK_SIZE)?(_nbytes-i):_BLOCK_SIZE );
  }
#endif
}

int readFully( ArgStruct *p, int dest, void *_buf, int _nbytes )
{
#if 1
    scivlib_read( scifd, _buf, _nbytes );
    return _nbytes;
#else
  int i;
  for(i=0;i<_nbytes;i+=_BLOCK_SIZE) {
    scivlib_read( scifd, (char*)_buf+i, ((_nbytes-i)<_BLOCK_SIZE)?(_nbytes-i):_BLOCK_SIZE );
  }
#endif
}


   /* Generic Gather routine (used to gather times) */

void Gather( ArgStruct *p, double *buf)
{
   int proc;

   if( p->master ) {   /* Gather the data from the other procs */

      for( proc=1; proc<p->nprocs; proc++ ) {

         buf++;

         readFully(p, proc, buf, sizeof(double));

      }

   } else {   /* Write my section to the master proc 0 */

      buf++;

      writeFully(p, 0, buf, sizeof(double));

   }
}

   /* Broadcast from master 0 to all other procs (used for nrepeat) */

void Broadcast(ArgStruct *p, unsigned int *buf)
{
   int proc;

   if( p->master ) {

      for( proc=1; proc<p->nprocs; proc++ ) {

         writeFully(p, proc, buf, sizeof(int));

      }

   } else {

      readFully(p, 0, buf, sizeof(int));

   }
}


#if 0
void MyMalloc(ArgStruct *p, int bufflen, int soffset, int roffset)
{
  void* buf1;
  void* buf2;
  sci_error_t err;
  char* localMapAddr;
  sci_map_t localMap;
  int len;
  char* newmem;

  int i,j;


  len = bufflen+MAX(soffset,roffset);

  if((buf1=(char *)malloc(len))==(char *)NULL)
   {
      fprintf(stderr,"couldn't allocate memory\n");
      exit(-1);
   }


   if(!p->cache)

     if((buf2=(char *)malloc(bufflen+soffset))==(char *)NULL)
       {
         fprintf(stderr,"Couldn't allocate memory\n");
         exit(-1);
       }

   if(p->cache) {
     p->r_buff = buf1;
   } else { /* Flip-flop buffers so send <--> recv between nodes */
     p->r_buff = p->tr ? buf1 : buf2;
     p->s_buff = p->tr ? buf2 : buf1;
   }

}

void FreeBuff(char *buff1, char* buff2)
{

  if(buff1 != NULL)
    free(buff1);

  if(buff2 != NULL)
    free(buff2);
}

#endif
