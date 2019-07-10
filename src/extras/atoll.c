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
/*          atoll.c         ---- Atoll module for NetPIPE                    */
/*          Dave Turner - Ames Laboratory - May of 2003                      */
/*          David Slogsnat - University of Mannheim                          */
/*                                                                           */
/* Parts of this code were taken and modified from an example program        */
/* provided with the Atoll documentation.                                    */
/*****************************************************************************/

#include    "netpipe.h"



int err, nsent = 0, ns_miss = 0, nrecv = 0, nr_miss = 0;
char *hdr;

#include "atoll_sliced.c"

#define SLICED 

int send_message(ArgStruct *p, void *buf, int nbytes, int tag)
{
   nsent++;
   

#ifdef SLICED

   err = Atoll_send_sliced(p->prot.handle,tag, 
                       buf, nbytes, hdr, 0, ATOLL_SEND_DMA);

   return err;

#else
   while (1) {

      err = Atoll_send(p->prot.handle,tag, 
                       buf, nbytes, hdr, 0, ATOLL_SEND_DMA);

      if (err == ATOLL_SUCCESS)  break;

      if (err==ATOLL_ERR_NO_DESC_MEM || err==ATOLL_ERR_NO_DMA_MEM) {
         ns_miss++;
//         usleep(10);    //wait and try again
         continue;
      }  else {
         printf("Atoll_send returned error %d\n",err);
         return err;
      }
   }
   return 0;
#endif


}




   /* Tag is currently not used to choose a message */

int recv_message(ArgStruct *p, void *buf, int nbytes, int tag)
{
   int len, hlen, i;
   int tag_in;

   nrecv++;
   while (1) {

#ifdef SLICED
     
     err = Atoll_recv_sliced(p->prot.id_self, buf, nbytes, NULL, 0, &tag_in, ATOLL_RECV_DMA, &len, &hlen);

#else

     err = Atoll_recv_data(p->prot.id_self, buf, nbytes, &tag_in, 
                       ATOLL_RECV_DMA, &len);
#endif
     
     if (err == ATOLL_SUCCESS) {
         if ( len == nbytes ) {
            return 0;
         } else {
            printf("message returned with len %d, expected was %d, \n",len,nbytes);
            return 2;
         }
      } else if (err == ATOLL_ERR_NO_MSG) {
         nr_miss++;
         continue;
      } else {
         Atoll_perror("-Atoll_recv_message returned :",err);
         return err;
      }
   }
   return 1;


}


void Init(ArgStruct *p, int* pargc, char*** pargv)
{
   p->tr = 0;      /* The transmitter will specify the other host with -h */
   p->rcv = 1;
}

void Setup(ArgStruct *p)
{
   int t=0;
   FILE *fd;
   char *myport, *nborport, *port0=".atoll.port0", *port1=".atoll.port1";

      /* Open my local host port */

   err = Atoll_open(&p->prot.id_self);

   if( err ) 
      Atoll_perror("Error openning self", err);
   else 
      printf("got my ID  %d\n",(int) p->prot.id_self );

      /* The transmitter will put its hostname and port_id into a file */

   if( p->rcv ) {   

           /* The receiver is always started before the transmitter */

      system( "rm -f .atoll.port0 .atoll.port1" );
      myport = port1;
      nborport = port0;

   } else {         /* transmitter */

      myport = port0;
      nborport = port1;

   }

   fd = fopen(myport, "w");

   fprintf(fd, "%d", p->prot.id_self);

   fclose( fd );

   p->prot.id_nbor = 0;

   while ( ++t < 15 ) {        /* bail after 1 minute */

      system( "sync; sync; sync;" );
      fd = fopen( nborport, "r" );

      if( fd != NULL ) {


         break;

      }
//      printf("fopen(%s,r) failed, time #%d\n", nborport, t);
      sleep(1);
   }

   if( fd ) {
      fscanf( fd, "%d", &p->prot.id_nbor );
      printf("got nbor ID  %d\n",(int) p->prot.id_nbor );
   } else {
      printf("ERROR: failed to open %s\n", nborport);
      exit(1);
   }

        /* Establish connections */

   err = Atoll_connect(p->prot.id_self, p->prot.id_nbor, &(p->prot.handle));
   if (err) Atoll_perror("Failed to connect", err);

   printf("ID %d got connected to %d\n",(int) p->prot.id_self, (int) p->prot.id_nbor);

}   


void Sync(ArgStruct *p)
{
   char syncme[] = "SyncMe";
   char response[] = "      ";

   err = send_message(p, syncme, sizeof(syncme), 9);
   if( err ) printf("Send error in atoll.c Sync() - %d\n", err);

   err = recv_message(p, response, sizeof(syncme), 9);
   if( err ) printf("Receive error in atoll.c Sync() - %d\n", err);

   if (strncmp(syncme, response, strlen(syncme))) {

      printf("Corruption error in atoll.c Sync() - %s\n", response);
      exit(9);
   }
}


void PrepareToReceive(ArgStruct *p)
{
}


void SendData(ArgStruct *p)
{
   err = send_message(p, p->s_ptr, p->bufflen, 1);
   if( err ) printf("Send error in atoll.c SendData() - %d\n", err);
}


void RecvData(ArgStruct *p)
{
   err = recv_message(p, p->r_ptr, p->bufflen, 1);
   if( err ) printf("Receive error in atoll.c RecvData() - %d\n", err);
}


void SendTime(ArgStruct *p, double *t)
{
   uint32_t ltime, ntime;

    /*
      Multiply the number of seconds by 1e8 to get time in 0.01 microseconds
      and convert value to an unsigned 32-bit integer.
      */
   ltime = (uint32_t)(*t * 1.e8);

    /* Send time in network order */

   ntime = htonl(ltime);

   err = send_message(p, &ntime, sizeof(uint32_t), 2);
   if( err ) printf("Send error in atoll.c SendTime() - %d\n", err);
}


void RecvTime(ArgStruct *p, double *t)
{
    uint32_t ltime, ntime;

   err = recv_message(p, &ntime, sizeof(uint32_t), 2);
   if( err ) printf("Receive error in atoll.c RecvTime() - %d\n", err);

   ltime = ntohl(ntime);

        /* Result is ltime (in microseconds) divided by 1.0e8 to get seconds */

   *t = (double)ltime / 1.0e8;
}


void SendRepeat(ArgStruct *p, int nrepeat)
{
   uint32_t lrpt, nlrpt;

   lrpt = nrepeat;

     /* Send repeat count as a long in network order */

   nlrpt = htonl(lrpt);

   err = send_message(p, &nlrpt, sizeof(uint32_t), 3);
   if( err ) printf("Send error in atoll.c SendRepeat() - %d\n", err);
}


void RecvRepeat(ArgStruct *p, int *nrepeat)
{
   uint32_t lrpt, nlrpt;

   err = recv_message(p, &nlrpt, sizeof(uint32_t), 3);
   if( err ) printf("Receive error in atoll.c RecvRepeat() - %d\n", err);

   lrpt = ntohl(nlrpt);

   *nrepeat = lrpt;
}


void CleanUp(ArgStruct *p)
{
   err = Atoll_disconnect( p->prot.handle);
   err = Atoll_close( p->prot.id_self );
   printf("\n %d messages sent with %d misses\n", nsent, ns_miss);
   printf("\n %d messages recv with %d misses\n", nrecv, nr_miss);
}


void FreeBuff(char *buff1, char *buff2)
{
  if(buff1 != NULL)

    free(buff1);


  if(buff2 != NULL)

    free(buff2);
}


void MyMalloc(ArgStruct *p, int bufflen)
{
    /* Allocate receive buffer */

    if((p->r_buff=(char *)malloc(bufflen))==(char *)NULL)
    {
        fprintf(stderr,"couldn't allocate memory for receive buffer\n");
        exit(-1);
    }

    /* Allocate send buffer */

    if((p->s_buff=(char *)malloc(bufflen))==(char *)NULL)
    {
        fprintf(stderr,"Couldn't allocate memory for send buffer\n");
        exit(-1);
    }

    /* Save original buffer addresses in case we do alignment */

    p->r_buff_orig = p->r_buff;
    p->s_buff_orig = p->s_buff;
}

void Reset(ArgStruct *p) { }

void AfterAlignmentInit(ArgStruct *p) { }

void InitBufferData(ArgStruct *p, int nbytes) { }
