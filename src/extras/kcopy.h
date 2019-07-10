/* Dave Turner  -  Ames Lab  -  July 2004+
 *
 * Header file for the kcopy.o module and apps that use it.
 */

  struct protocolstruct
  {
    volatile int nbytes, source, tag, nleft;  /* nleft --> 0 on completion */
    int posted;
  } prot;


typedef enum kcopy_comm_type
{
KCOPY_INIT,
KCOPY_QUIT,
KCOPY_PUT,
KCOPY_GET,
KCOPY_POST_SEND,
KCOPY_POST_RECV,
KCOPY_SYNC,
KCOPY_GATHER,
KCOPY_BCAST,
KCOPY_PTR_DUMP,
KCOPY_FIX
} kcopy_comm_t;


struct kcopy_hdr {
   void *l_adr;
   void *r_adr;              /* For posts, this contains the completion adr */
   int myproc;
   int nprocs;
   int r_proc;
   int nbytes;
   int tag;
   kcopy_comm_t comm_type;   /* Enumerated above */
   int job;                  /* Job number returned by ioctl() after KCOPY_INIT */
   int uid;                  /* Unique identifier for this MPI job */
};

