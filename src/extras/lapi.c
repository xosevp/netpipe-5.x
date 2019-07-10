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
/* This code was originally developed by Quinn Snell, Armin Mikler,          */
/* John Gustafson, and Guy Helmer.                                           */
/*                                                                           */
/* It is currently being developed by Dave Turner with many contributions    */
/* from ISU students (Bogdan Vasiliu, Adam Oline, Xuehua Chen, Brian Smith). */
/*****************************************************************************/

/* DDT - The LAPI module only runs on 2 procs currently */

#include "netpipe.h"
#include <lapi.h>

lapi_handle_t  t_hndl;
lapi_cntr_t    l_cntr;
lapi_cntr_t    t_cntr;
lapi_cntr_t    c_cntr;
lapi_info_t    t_info;  /* LAPI info structure */
void           *global_raddr[2];
void           *global_saddr[2];
void           *tgt_addr[2];
void           *comm_addr[2];

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
}

void Setup(ArgStruct *p)
{
   int   one=1, loop, rc, val, cur_val;
   int   task_id;       /* My task id */
   int   num_tasks;     /* Number of tasks in my job */
   char* t_buf;         /* Buffer to manipulate */
   char  err_msg_buf[LAPI_MAX_ERR_STRING];
/*   int bulk_on, bulk_size;*/

   bzero(&t_info, sizeof(lapi_info_t));

   t_info.err_hndlr = NULL;   /* Not registering error handler function */

   if ((rc = LAPI_Init(&t_hndl, &t_info)) != LAPI_SUCCESS) {
      LAPI_Msg_string(rc, err_msg_buf);
      printf("Error Message: %s, rc = %d\n", err_msg_buf, rc);
      exit (rc);
   }

      /* Get task number within job */

   rc = LAPI_Qenv(t_hndl, TASK_ID, &task_id);

      /* Get number of tasks in job */

   rc = LAPI_Qenv(t_hndl, NUM_TASKS, &num_tasks);

   if (num_tasks != 2) {
      printf("Error Message: Run with MP_PROCS set to 2\n");
      exit(1);
   }

      /* Probe to see if bulk transfer using the NIC is turned on */

/*   LAPI_Qenv( t_hndl, BULK_XFER, &bulk_on);*/

/*   LAPI_Qenv( t_hndl, BULK_MIN_MSG_SIZE, &bulk_size);*/

/*   if( bulk_on == 1 && task_id == 0 ) */
/*      printf("Bulk transfer mode is turned on with minimum size of %d bytes", bulk_size);*/

   p->nprocs = num_tasks;

   /* Turn off parameter checking - default is on */

   rc = LAPI_Senv(t_hndl, ERROR_CHK, 0);

   /* Initialize counters to be zero at the start */

   rc = LAPI_Setcntr(t_hndl, &l_cntr, 0);

   rc = LAPI_Setcntr(t_hndl, &t_cntr, 0);

   rc = LAPI_Setcntr(t_hndl, &c_cntr, 0);

   /* Exchange addresses for target counter, repeats, and rbuff offset */

   rc = LAPI_Address_init(t_hndl,&t_cntr,tgt_addr);

   p->tr = p->rcv = 0;
   if( task_id == 0 ) {
      p->myproc = 0;
      p->tr = 1;
      p->prot.nbor=1;
   } else {
      p->myproc = 1;
      p->rcv = 1;
      p->prot.nbor=0;
   }
}

void Sync(ArgStruct *p)
{
   LAPI_Gfence(t_hndl);
}

void PrepareToReceive(ArgStruct *p)
{
}

void SendData(ArgStruct *p)
{
   int rc;
   int offset = ((p->s_ptr - p->soffset) - p->s_buff) + p->roffset;
   void* dest = global_raddr[p->prot.nbor] + offset;

      /* We calculate the destination address because buffer alignment most
       * likely changed the start of the buffer from what malloc returned
       */

   rc = LAPI_Put(t_hndl, p->prot.nbor, p->bufflen*sizeof(char), dest,
                 (void *)p->s_ptr,tgt_addr[p->prot.nbor], &l_cntr,&c_cntr);

      /* Wait for local Put completion */

   rc = LAPI_Waitcntr(t_hndl, &l_cntr, 1, NULL); 
}

/* DDT - I have not tested the CPU workload measurement for LAPI */

int TestForCompletion( ArgStruct *p )
{
   int rc,val;

      /* Poll for receive.  We have to use polling
       * as LAPI_Waitcntr does not guarantee making progress
       * on receives.
       */

   rc = LAPI_Probe(t_hndl); /* Poll the adapter once */
   rc = LAPI_Getcntr(t_hndl, &t_cntr, &val);

   if (val < 1) return 0;  /* Message not complete */
   else         return 1;  /* Message is complete */
}

void RecvData(ArgStruct *p)
{
   volatile unsigned char* cbuf = (unsigned char*)p->r_ptr;
   int rc,val,cur_val;

      /* Poll for receive.  We have to use polling
       * as LAPI_Waitcntr does not guarantee making progress
       * on receives.
       */

   rc = LAPI_Getcntr(t_hndl, &t_cntr, &val);
   while (val < 1) {
      rc = LAPI_Probe(t_hndl); /* Poll the adapter once */
      rc = LAPI_Getcntr(t_hndl, &t_cntr, &val);
   }

      /* To clear the t_cntr value */

   rc = LAPI_Waitcntr(t_hndl, &t_cntr, 1, &cur_val); 

      /* In bi-directional mode, it's possible for two puts from the sending
       * node to be outstanding, and in that case ordering is not guaranteed.
       * Thus, even though the counter was decremented, it was not necessarily
       * decremented for this particular incoming put. So spin on the last
       * byte to make sure this put has completed */
 
   while( cbuf[p->bufflen-1] != p->expected ) {
      sched_yield();
   }
}

   /* Gather at proc 0 */

void Gather(ArgStruct *p, double* buf)
{
   int rc, val, cur_val, bufflen = sizeof(double);

   if( p->master ) {

      rc = LAPI_Address_init(t_hndl,buf,comm_addr);
      rc = LAPI_Getcntr(t_hndl, &t_cntr, &val);
      while (val < 1) {
          rc = LAPI_Probe(t_hndl); /* Poll the adapter once */
          rc = LAPI_Getcntr(t_hndl, &t_cntr, &val);
      }
      /* To clear the t_cntr value */
      rc = LAPI_Waitcntr(t_hndl, &t_cntr, 1, &cur_val);

   } else {

      rc = LAPI_Address_init(t_hndl,buf,comm_addr);
      rc = LAPI_Put(t_hndl,p->prot.nbor,bufflen,
              comm_addr[p->prot.nbor],buf,tgt_addr[p->prot.nbor],
              &l_cntr,&c_cntr);
      /* Wait for local Put completion */
      rc = LAPI_Waitcntr(t_hndl, &l_cntr, 1, NULL);

   }
}

   /* Broadcast from proc 0 */

void Broadcast(ArgStruct *p, unsigned int *buf)
{
   int rc, val, cur_val, bufflen = sizeof(int);

   if( p->master ) {

      rc = LAPI_Address_init(t_hndl,buf,comm_addr);
      rc = LAPI_Put(t_hndl,p->prot.nbor,bufflen,
              comm_addr[p->prot.nbor],buf,tgt_addr[p->prot.nbor],
              &l_cntr,&c_cntr);
      /* Wait for local Put completion */
      rc = LAPI_Waitcntr(t_hndl, &l_cntr, 1, NULL);

   } else {

      rc = LAPI_Address_init(t_hndl,buf,comm_addr);
      rc = LAPI_Getcntr(t_hndl, &t_cntr, &val);
      while (val < 1) {
          rc = LAPI_Probe(t_hndl); /* Poll the adapter once */
          rc = LAPI_Getcntr(t_hndl, &t_cntr, &val);
      }
         /* To clear the t_cntr value */
      rc = LAPI_Waitcntr(t_hndl, &t_cntr, 1, &cur_val);

   }
}


void  CleanUp(ArgStruct *p)
{
   int rc;
   rc = LAPI_Gfence(t_hndl); /* Global fence to sync before terminating job */
   rc = LAPI_Term(t_hndl);   
}        


void Reset(ArgStruct *p)
{
}


void AfterAlignmentInit(ArgStruct* p)
{
   int rc, val, cur_val;

   rc = LAPI_Address_init(t_hndl,p->r_buff,global_raddr);

   if(!p->cache || p->bidir)
      rc = LAPI_Address_init(t_hndl,p->s_buff,global_saddr);
}

void MyMalloc(ArgStruct *p, int bufflen)
{
   int rc;

   if((p->r_buff=(char *)malloc(bufflen+MAX(p->soffset,p->roffset)))==(char *)NULL) {
      fprintf(stderr,"couldn't allocate memory for receive buffer\n");
      exit(-1);
   }

   if(!p->cache || p->bidir) {

      if((p->s_buff=(char *)malloc(bufflen+MAX(p->soffset,p->roffset)))==(char *)NULL) {
         fprintf(stderr,"Couldn't allocate memory for send buffer\n");
         exit(-1);
      }
   }
}

void FreeBuff(char *buff1, char *buff2)
{
   if(buff1 != NULL) free(buff1);

   if(buff2 != NULL) free(buff2);
}

void ModuleSwapPtrs(ArgStruct *p)
{
   int   n;
   void* tmp_p;
   int   tmp_i;

   for(n=0;n<2;n++) {
      tmp_p = global_raddr[n];
      global_raddr[n] = global_saddr[n];
      global_saddr[n] = tmp_p;
   }
}
