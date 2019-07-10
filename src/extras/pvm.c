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
/*     * pvm.c              ---- PVM calls source                            */
/*****************************************************************************/
#include    "netpipe.h"
#include    <pvm3.h>

#ifndef lint
static const char rcsid[] =
    "$Id: pvm.c,v 1.5 2004/09/24 05:25:15 turner Exp $";
#endif


/**********************************************************************/
/* Initialialization that needs to occur before                       */
/* command args are parsed                                            */
/**********************************************************************/
void
Init(ArgStruct *p, int* pargc, char*** pargv)
{
   p->tr = 0;     /* The transmitter will be set using the -h host flag. */
   p->rcv = 1;
}

/**********************************************************************/
/* Set up the communcations system.                                   */
/*    In pvm, this means to join the parallel machine                 */
/**********************************************************************/
void
Setup(ArgStruct *p)
{
    p->prot.mytid = pvm_mytid();
#ifdef DEBUG
    printf("My task id is %d \n",p->prot.mytid);
#endif

    if( p->myproc < 0 ) {
       fprintf(stderr, "You must specify -H hostfile\n");
       pvm_exit();
       exit(-1);
    }

    if( p->myproc == 0 ) {
       p->master = 1;
       p->tr = 1;
       p->rcv = 0;
    } else {
       p->master = 0;
       p->tr = 0;
       p->rcv = 1;
    }
    
    establish(p);
}   

/**********************************************************************/
/* Establish a link with the other processor                          */
/*    In pvm, this means to send a simple message, but to keep the    */
/*    communication line open.  Since the assumption is that we are   */
/*    starting it by hand on the other machine, we don't know what    */
/*    the other task id is.                                           */
/**********************************************************************/

void establish(ArgStruct *p)
{
    int inum;      /* instance number in group       */
    int tid;       /* task id                        */
    int buffer_id; /* Received buffer (if receiver)  */

    /* Use group name and barrier to sync up before going on */
    
    inum = pvm_joingroup("netpipe");

    pvm_barrier("netpipe", 2);
    
    /*
        If we are the transmitting side, go find the other one and send
        it a message containing our tid. If we are the receiving side,
        just wait for a message.
    */
    if ( p->tr ) {
#ifdef DEBUG
        printf("this is the transmitter\n");
#endif
        
        /* Since we know there are only two procs, ours will be either 
         * instance 0 or instance 1 in the group "netpipe". So, check
         * each one to find the other tid */

        tid = pvm_gettid("netpipe", 0);
        if( tid == p->prot.mytid ) {
           p->prot.othertid = pvm_gettid("netpipe", 1);
        } else {
           p->prot.othertid = tid;
           tid = pvm_gettid("netpipe", 1);
        }
        
        if( tid != p->prot.mytid ) {
           printf("Error, didn't find my own task id\n");
           exit(-1);
        }
        
            /* Send the receiver a message.  Tell pvm to keep the channel open */

#ifdef DEBUG
        printf("The receiver tid is %d \n",p->prot.othertid);
#endif

           /* PVMDATA is defined in netpipe.h, choose wisely (PvmDataInPlace)
            * Also use PvmRouteDirect, otherwise the data goes through the
            * pvmd daemons which kills performance.
            */

        pvm_setopt( PvmRoute, PvmRouteDirect );
        pvm_initsend( PVMDATA );

        pvm_pkint( &p->prot.mytid, 1, 1 );
        pvm_send( p->prot.othertid, 1 );
    } else {
#ifdef DEBUG
        printf("This is the receiver \n");
#endif
                
            /* Receive any message from any task */

        buffer_id = pvm_recv(-1, -1);

        if ( buffer_id < 0 ) {
        printf("Error on receive in receiver\n");
        exit(-1);
        }
        pvm_upkint( &p->prot.othertid, 1, 1 );
    }
}

/**********************************************************************/
/* Prepare to receive                                                 */
/*    In pvm, you cannot set up a reception buffer ahead of time      */
/**********************************************************************/
void
PrepareToReceive(ArgStruct *p)
{
}

/**********************************************************************/
/* Synchronize                                                        */
/*     In pvm, this is not necessary                                  */
/**********************************************************************/
void
Sync(ArgStruct *p)
{
}

/**********************************************************************/
/* Send a buffer full of information                                  */
/*    In pvm, we use pvm_pkbyte and then send it.                     */
/**********************************************************************/
void
SendData(ArgStruct *p)
{
#ifdef DEBUG
    printf(" In send \n");
#endif
    pvm_initsend( PVMDATA );
    pvm_pkbyte( p->s_ptr, p->bufflen, 1 );
    pvm_send( p->prot.othertid, 1 );
#ifdef DEBUG
    printf(" message sent.  Size=%d\n",p->bufflen);
#endif
}

/**********************************************************************/
/* Receive a buffer full of information                               */
/**********************************************************************/
void
RecvData(ArgStruct *p)
{
#ifdef DEBUG
    printf(" In receive \n");
#endif
    pvm_recv( -1, -1);
    pvm_upkbyte( p->r_ptr, p->bufflen, 1);
#ifdef DEBUG
    printf(" message received .  Size=%d \n", p->bufflen);
#endif
}

/**********************************************************************/
/* Broadcast from proc 0 to proc 1                                    */
/**********************************************************************/
void Broadcast(ArgStruct *p, unsigned int *buf)
{
   if( p->myproc == 0 ) {
      
      pvm_initsend( PVMDATA );
      pvm_pkbyte( buf, sizeof(int), 1 );
      pvm_send( p->prot.othertid, 1 );
      
   } else {
      
      pvm_recv( -1, -1 );
      pvm_upkbyte( buf, sizeof(int), 1 );

   }
}

/**********************************************************************/
/* Gather from proc 0 to proc 1                                       */
/**********************************************************************/
void Gather(ArgStruct *p, double *buf)
{
   if(p->myproc == 0 ) {

      pvm_recv( -1, -1 );
      pvm_upkbyte( buf, sizeof(double), 1 );
      
   } else {
      
      pvm_initsend( PVMDATA );
      pvm_pkbyte( buf, sizeof(double), 1 );
      pvm_send( p->prot.othertid, 1 );
      
   }
}

/**********************************************************************/
/* Close down the connection.
/**********************************************************************/
void CleanUp(ArgStruct *p) { }


void Reset(ArgStruct *p) { }
