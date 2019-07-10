#include  "netpipe.h"

extern double *pTime;
extern int    *pNrepeat; 

int Setup(ArgStruct *p)
{
   int npes;
   
#ifndef _CRAYT3E
   start_pes(0);
#endif 

   if((npes=shmem_n_pes())!=2) {

      printf("Error Message: Run with npes set to 2\n");
      exit(1);
   }

   p->prot.flag=(int *) shmalloc(sizeof(int));
   pTime = (double *) shmalloc(sizeof(double));
   pNrepeat = (int *) shmalloc(sizeof(int));

   if((p->prot.ipe=_my_pe()) == 0) {
      p->tr=1;
      p->prot.nbor=1;
      *p->prot.flag=1;

   } else {

      p->tr=0;
      p->prot.nbor=0;
      *p->prot.flag=0;
   }

   return 0;
}

void Sync(ArgStruct *p)
{
   shmem_barrier_all();
}

void PrepostRecv(ArgStruct *p) { }

void SendData(ArgStruct *p)
{
   if(p->bufflen%8==0)
      shmem_put64(p->buff, p->buff1, p->bufflen/8,p->prot.nbor);
   else
      shmem_putmem(p->buff, p->buff1,p->bufflen,p->prot.nbor);
}

void RecvData(ArgStruct *p)
{
   int i=0;

   while(p->buff[p->bufflen-1]!='b'+p->prot.ipe) {

#ifndef _CRAYT3E
/*      shmem_udcflush_line(p->buff + p->bufflen - 1);  */
        sched_yield(); 
#else
     if(++i%10000000==0) printf(""); 
#endif

   }

   p->buff[p->bufflen-1]='b'+p->prot.nbor;  
}

void SendTime(ArgStruct *p, double *t)
{
   *pTime=*t;

   shmem_double_put(pTime,pTime,1,p->prot.nbor);
   shmem_int_put(p->prot.flag,p->prot.flag,1,p->prot.nbor);
}

void RecvTime(ArgStruct *p, double *t)
{
   int i=0;

   while(*p->prot.flag!=p->prot.ipe)
   {

#ifndef _CRAYT3E
/*      shmem_udcflush_line(p->prot.flag);   */
        sched_yield();  
#else
      if(++i%10000000==0) printf("");
#endif

   }
   *t=*pTime; 
   *p->prot.flag=p->prot.nbor;
}

void SendRepeat(ArgStruct *p, int rpt)
{
   *pNrepeat= rpt;
   shmem_int_put(pNrepeat,pNrepeat,1,p->prot.nbor);
   shmem_int_put(p->prot.flag,p->prot.flag,1,p->prot.nbor);
}

void RecvRepeat(ArgStruct *p, int *rpt)
{
   int i=0;

   while(*p->prot.flag!=p->prot.ipe)
   {

#ifndef _CRAYT3E
/*      shmem_udcflush_line(p->prot.flag);  */
        sched_yield();  
#else
      if(++i%10000000==0) printf("");
#endif 

   }
   *rpt=*pNrepeat;
   *p->prot.flag=p->prot.nbor;
}

int  CleanUp(ArgStruct *p)
{
   return 0;    /* Damn SGI compilers want this */
}

void FreeBuff(char *buff1, char* buff2)
{
   shfree(buff1);
   shfree(buff2);
}

int MyMalloc(ArgStruct *p, int bufflen)
{
   int i;
   if((p->buff=(char *)shmalloc(bufflen))==(char *)NULL)
   {
      fprintf(stderr,"couldn't allocate memory\n");
      return -1;
   }
   /* memset(p->buff, p->tr, bufflen);    */
   p->buff[bufflen-1]='b'+p->tr; 
   if((p->buff1=(char *)malloc(bufflen))==(char *)NULL)
   {
      fprintf(stderr,"Couldn't allocate memory\n");
      return -1;
   }
   /* memset(p->buff1, p->tr, bufflen);    */
   return 0;
}

void PoolSetup(ArgStruct *p, int bufalign, int bufoffset)
{
    if ( (p->pool = (char *)shmalloc(MEMSIZE)) == NULL)
    {
        perror("malloc");
        exit(-1);
    }

    if ( (p->pool1 =(char *)shmalloc(MEMSIZE)) == NULL)
    {
        perror("malloc");
        exit(-1);
    }

    if (bufalign != 0)
    {
        p->pool_align  = p->pool + (bufalign - ( (long)p->pool % bufalign )
                           + bufoffset) % bufalign;
        p->pool1_align = p->pool1 + (bufalign - ( (long)p->pool1 % bufalign )
                           + bufoffset) % bufalign;
    }
    else
    {
        p->pool_align  = p->pool;
        p->pool1_align = p->pool1;
    }
}
