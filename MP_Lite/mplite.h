   /* Prototypes for the MP_Lite message passing library */

void MP_Init(int *argc, char ***argv);
void MP_Finalize();

void MP_Nprocs( int *nprocs);
void MP_Myproc( int *myproc);

void MP_ASend(void *buf, int nbytes, int dest, int tag, int *msg_id);
void MP_Send(void *buf, int nbytes, int dest, int tag);

void MP_ARecv(void *buf, int nbytes, int src, int tag, int *msg_id);
void MP_Recv(void *buf, int nbytes, int src, int tag);

void MP_Wait(int *msg_id);
void MP_Block(int *msg_id);

void MP_Sync();

int MP_AProbe(int nbytes, int source, int tag, int *flag);
int MP_Probe(int nbytes, int source,int tag);
int MP_Test(int *msg_id, int *flag);
int MP_Testall(int count, int *msg_ids, int *flag);


    /* Global operations from mplite.c */

void MP_dSend(void *buf, int count, int dest, int tag);
void MP_iSend(void *buf, int count, int dest, int tag);
void MP_fSend(void *buf, int count, int dest, int tag);
void MP_dRecv(void *buf, int count, int source, int tag);
void MP_iRecv(void *buf, int count, int source, int tag);
void MP_fRecv(void *buf, int count, int source, int tag);
void MP_dASend(void *buf, int count, int dest, int tag, int *msg_id);
void MP_iASend(void *buf, int count, int dest, int tag, int *msg_id);
void MP_fASend(void *buf, int count, int dest, int tag, int *msg_id);
void MP_dARecv(void *buf, int count, int source, int tag, int *msg_id);
void MP_iARecv(void *buf, int count, int source, int tag, int *msg_id);
void MP_fARecv(void *buf, int count, int source, int tag, int *msg_id);
void MP_Broadcast(void *ptr, int nbytes);
void MP_dBroadcast(void *ptr, int count);
void MP_iBroadcast(void *ptr, int count);
void MP_fBroadcast(void *ptr, int count);
void MP_Bcast(void *ptr, int nbytes, int root);
void MP_dBcast(void *ptr, int count, int root);
void MP_iBcast(void *ptr, int count, int root);
void MP_fBcast(void *ptr, int count, int root);
void MP_dSum(double *ptr, int nelements);
void MP_iSum(int *ptr, int nelements);
void MP_fSum(float *ptr, int nelements);
void MP_dMax(double *ptr, int nelements);
void MP_iMax(int *ptr, int nelements);
void MP_fMax(float *ptr, int nelements);
void MP_dMin(double *ptr, int nelements);
void MP_iMin(int *ptr, int nelements);
void MP_fMin(float *ptr, int nelements);
void MP_dGather(double *ptr, int nelements);
void MP_iGather(int *ptr, int nelements);
void MP_fGather(float *ptr, int nelements);
void MP_Gather(void *ptr, int nelements);
void MP_dSum_masked(double *ptr, int nelements, int *mask);
void MP_iSum_masked(int *ptr, int nelements, int *mask);
void MP_fSum_masked(float *ptr, int nelements, int *mask);
void MP_dMax_masked(double *ptr, int nelements, int *mask);
void MP_iMax_masked(int *ptr, int nelements, int *mask);
void MP_fMax_masked(float *ptr, int nelements, int *mask);
void MP_dMin_masked(double *ptr, int nelements, int *mask);
void MP_iMin_masked(int *ptr, int nelements, int *mask);
void MP_fMin_masked(float *ptr, int nelements, int *mask);
void MP_dGather_masked(double *ptr, int nelements, int *mask);
void MP_iGather_masked(int *ptr, int nelements, int *mask);
void MP_fGather_masked(float *ptr, int nelements, int *mask);
void MP_Gather_masked(void *ptr, int nelements, int *mask);

void MP_SSend(void *buf, int nbytes, int dest, int tag);
void MP_SRecv(void *buf, int nbytes, int src, int tag);
void MP_SShift(void *buf_out, int dest, void *buf_in, int src, int nbytes);
void MP_Get_Last_Tag(int *tag);
void MP_Get_Last_Source(int *src);
void MP_Get_Last_Size(int *nbytes);
void MP_Set( char *var, int value);
void MP_Set_Debug(int value);

void MP_Cart_Create(int ndims, int *dims);
void MP_Cart_Coords( int rank, int *coord);
void MP_Cart_Get( int *dim, int *coord);
void MP_Cart_Shift( int dir, int dist, int *source, int *dest);
void MP_Cart_Rank( int *coord, int *rank );

void MP_Broadcast(void *ptr, int nbytes);
void MP_dBroadcast(void *ptr, int count);
void MP_iBroadcast(void *ptr, int count);
void MP_fBroadcast(void *ptr, int count);
void MP_Bcast (void *ptr, int nbytes,int root);
void MP_dBcast(void *ptr, int count,int root);
void MP_iBcast(void *ptr, int count,int root);
void MP_fBcast(void *ptr, int count,int root);
void MP_Global(void *vec, int n, int op_type, int *mask);

   /* Timing operations from timer.c */

double MP_Time();
void MP_Reset_Time(void);
void MP_Enter(char *name);
void MP_Leave(double nflops);
void MP_Time_Report(char *timefile);
void MD_Time_Report(char *timefile, int nsteps);
void MP_Zero_Time(char *name);
void MP_memcpy(void *dest, const void *src, int n);
void MP_memmove(void *dest, const void *src, int n);

    /* SMP prototypes from shm.c */

void MP_IPRecv( void **data_ptr, int nbytes, int src, int tag);
void MP_IPSend( void *data_ptr, int nbytes, int dest, int tag);
void *MP_shmMalloc( int nbytes);
void MP_shmFree( void *shmptr );
void MP_Cleanup( void *data_ptr);

   /* One-sided communications prototypes from tcp.c */

int MP_Win_create(void* base, int size, int disp_unit, int* win);
int MP_shMalloc(void** base, int size, int disp_unit, int* win);
int MP_Win_free(int* win);

void MP_Win_fence(int win);
void MP_Win_start(int* group, int win);
void MP_Win_complete(int win);
void MP_Win_post(int* group, int win);
void MP_Win_wait(int win);
void MP_Win_test(int win, int* flag);
void MP_Win_lock_exclusive(int rank, int win);
void MP_Win_lock_shared(int rank, int win);
void MP_Win_lock(int lock_type, int rank, int win);
void MP_Win_unlock(int rank, int wid);

void MP_APut(void* buf, int nelts, int dest, int dest_off, int win, int* mid);
void MP_Put(void* buf, int nelts, int dest, int dest_off, int win);
void MP_AGet(void* buf, int nelts, int src, int src_off, int win, int* mid);
void MP_Get(void* buf, int nelts, int src, int src_off, int win);
void MP_AAccumulate(void* buf, int nelts, int dest, int dest_off, int op_type, 
                    int win, int* mid);

void MP_AdSumAccumulate(void* buf, int n, int dest, int off, int win,int* mid);
void MP_AiSumAccumulate(void* buf, int n, int dest, int off, int win,int* mid);
void MP_AfSumAccumulate(void* buf, int n, int dest, int off, int win,int* mid);
void MP_AdMaxAccumulate(void* buf, int n, int dest, int off, int win,int* mid);
void MP_AiMaxAccumulate(void* buf, int n, int dest, int off, int win,int* mid);
void MP_AfMaxAccumulate(void* buf, int n, int dest, int off, int win,int* mid);
void MP_AdMinAccumulate(void* buf, int n, int dest, int off, int win,int* mid);
void MP_AiMinAccumulate(void* buf, int n, int dest, int off, int win,int* mid);
void MP_AfMinAccumulate(void* buf, int n, int dest, int off, int win,int* mid);

void MP_Accumulate(void* buf, int nelts, int dest, int dest_off, int op_type, 
                    int win);
void MP_dSumAccumulate(void* buf, int n, int dest, int off, int win);
void MP_iSumAccumulate(void* buf, int n, int dest, int off, int win);
void MP_fSumAccumulate(void* buf, int n, int dest, int off, int win);
void MP_dMaxAccumulate(void* buf, int n, int dest, int off, int win);
void MP_iMaxAccumulate(void* buf, int n, int dest, int off, int win);
void MP_fMaxAccumulate(void* buf, int n, int dest, int off, int win);
void MP_dMinAccumulate(void* buf, int n, int dest, int off, int win);
void MP_iMinAccumulate(void* buf, int n, int dest, int off, int win);
void MP_fMinAccumulate(void* buf, int n, int dest, int off, int win);

   /* TCP prototypes from tcp.c */

void MP_Bedcheck();


/* Added by Adam Oline, wrappers for memory allocation that are required
 * to allow malloc and free to be (somewhat) safely called inside the sigio 
 * handler.
 *
 * This is not a full-proof solution because a user can still link with
 * code that does not #include this file, as well as pre-compiled system 
 * libraries.
 */

#if ! defined(MP_MEM)

#include <stdlib.h>

#define malloc  MP_Malloc
#define calloc  MP_Calloc
#define realloc MP_Realloc
#define free    MP_Free

void* MP_Malloc(size_t size);
void* MP_Realloc(void *ptr, size_t size);
void* MP_Calloc(size_t nmemb, size_t size);
void MP_Free(void *ptr);

#endif /* MP_MEM */

/* Prototypes for MP_Remap() */

void MP_Remap_Setup( int nrows, int ncols, int *myproc );
int MP_Remap( int node );

