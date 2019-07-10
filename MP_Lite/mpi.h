#ifndef _MP_LITE_MPI_H_
#define _MP_LITE_MPI_H_

#include "mplite.h" /* Required for malloc/free wrappers */

/* mpi.h file to allow MPI programs to run on top of MP_Lite */


#define MPI_SUCCESS 0
#define MPI_UNDEFINED -1

#define MPI_Comm int
#define MPI_COMM_WORLD 0
#define MPI_ANY_SOURCE (-1)
#define MPI_ANY_TAG (-1)

   /* MPI datatype definitions */

#define MPI_Datatype int
#define MPI_CHAR           (1)
#define MPI_UNSIGNED_CHAR  (2)
#define MPI_BYTE           (3)
#define MPI_SHORT          (4)
#define MPI_UNSIGNED_SHORT (5)
#define MPI_INT            (6)
#define MPI_UNSIGNED       (7)
#define MPI_LONG           (8)
#define MPI_UNSIGNED_LONG  (9)
#define MPI_FLOAT          (10)
#define MPI_DOUBLE         (11)
#define MPI_LONG_DOUBLE    (12)
#define MPI_LONG_LONG_INT  (13)
#define MPI_PACKED         (14)

   /* Datatype defs for f77 compatibility */

#define MPI_COMPLEX           (23)
#define MPI_DOUBLE_COMPLEX    (24)
#define MPI_LOGICAL           (25)
#define MPI_REAL              (26)
#define MPI_DOUBLE_PRECISION  (27)
#define MPI_INTEGER           (28)
#define MPI_2INTEGER          (29)
#define MPI_2COMPLEX          (30)
#define MPI_2DOUBLE_COMPLEX   (31)
#define MPI_2REAL             (32)
#define MPI_2DOUBLE_PRECISION (33)
#define MPI_CHARACTER         ( 1)

#define MPI_REAL4             (40)
#define MPI_REAL8             (41)
#define MPI_INTEGER1          (42)
#define MPI_INTEGER2          (43)
#define MPI_INTEGER4          (44)


   /* MPI operator definitions */

#define MPI_Op int
#define MPI_MAX     (100)
#define MPI_MIN     (101)
#define MPI_SUM     (102)
#define MPI_PROD    (103)
#define MPI_LAND    (104)
#define MPI_BAND    (105)
#define MPI_LOR     (106)
#define MPI_BOR     (107)
#define MPI_LXOR    (108)
#define MPI_BXOR    (109)
#define MPI_MINLOC  (110)
#define MPI_MAXLOC  (111)
#define MPI_REPLACE (112)


   /* MPI lock types */

#define MPI_LOCK_SHARED    1
#define MPI_LOCK_EXCLUSIVE 2

/*#define MPI_Status int*/
typedef struct {
    int nbytes;
    int MPI_SOURCE;
    int MPI_TAG;
    int MPI_ERROR;
} MPI_Status;
#define MPI_STATUS_IGNORE (MPI_Status *)0

#define MPI_Request int

double MPI_Wtime();

typedef struct {
  int size;
  int* ranks;
} MPI_Group;

/* MPI-2 One-sided stuff */
#define MPI_Win  int
#define MPI_Aint int
#define MPI_Info int

// MPI prototypes

int MPI_Init( int *argc, char ***argv);
int MPI_Comm_rank( MPI_Comm comm, int *rank);
int MPI_Comm_size(MPI_Comm comm, int *size);
int MPI_Send( void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm);
int MPI_Recv( void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status);

int MPI_Barrier(MPI_Comm comm);
int MPI_Bcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm);
int MPI_Gather(void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, 
               int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm);
int MPI_Finalize(void);

#endif /* _MP_LITE_MPI_H_ */
