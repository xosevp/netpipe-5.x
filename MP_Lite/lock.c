/* MP_Lite message-passing library
 * Copyright (C) 1998-2004 Dave Turner and Xuehua Chen
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 * Contact: Dave Turner - turner@ameslab.gov
 */


/***************************************************
 * This file defines different lock mechanisms     *
 * and polling methods                             *
 ***************************************************/
#include "globals.h"
#include <sched.h>

/*--------------------------------------------------*/
/* Different lock mechanisms are implemented here   */
/* To switch to different lock mechanism, you can   */
/* define LOCK differently                          */
/*--------------------------------------------------*/

/* 
 * Two lock mechanisms, BAKERY and MUTEX, still needs to be 
 * modified to be able to work  
 */
 
#define SEMLOCK  1  /* Semaphore based lock */
#define BAKERY   2  /* Need to add lfence */
#define HARDWARE 3  /* 
		     * shared memory based lock, implemenation is platform  
		     * dependent, atomic operations need to be implemented
                     */
#define MUTEX    4  /*
                     * process shared mutex, available on IRIX
                     * not available on Linux, BSD and AIX right now
                     */
#if defined(X86) || defined(POWERPC)

#define LOCK HARDWARE

#else

#define LOCK SEMLOCK

#endif

#if LOCK == SEMLOCK
/* 
 * Use semaphores to implement lock and unlock operations
 */

static union    sem_union arg;
static int      sem_wt;
static struct   semid_ds seminfo;
static struct   sembuf op;

/*
 * A generic semaphore operation
 */
void sem_op(int id, int value)
{
  op.sem_op = value;
  if (semop(id, &op, 1) < 0)
  {
    fprintf(stderr,"semop error");
    quitall();
  }
}
                                                                                                                                                                           
/* 
 * A semaphore operation of wait
 */
void sem_wait(int id)
{
  sem_op(id, -1);
}
                                                                                                                                                                           
/*
 * A semaphore operation of signal
 */ 
void sem_signal(int id)
{
  sem_op(id, 1);
}

/* 
 * lock the shared pool
 */
void lock_pool()
{
  sem_wait(sem_wt);
}

/*
 * unlock the shared pool
 */
void unlock_pool()
{
  sem_signal(sem_wt);
}

/*
 * allocate a semaphore variable of lock
 */
void allocate_lock()
{
   if( myproc == 0 ) {
      sem_wt = shmget(511, 1, 0660 | IPC_CREAT);
   } else {
      sem_wt = shmget(511, 1, 0660);
   }
}

/*
 * initialization of a semaphore variable of lock
 */
void init_lock()
{
  if (smp_rank[myproc] == 0) {
    arg.val=1;
    semctl(sem_wt, 0, SETVAL, arg);
  }
}

/*
 * deallocate the lock
 */
void free_lock() 
{
  int err;

  if( (err = semctl(sem_wt, 0, IPC_RMID, NULL)) <0)
    errorx(errno, "semctl(sem_wt,IPC_RMID) error in free_lock()");
}


#elif LOCK == BAKERY
/*
 * Use Bakery Algorithm to impelment locks 
 * The implemenation is based from  
 * Operating System Concepts Fifth Edition (ISBN 0-471-36414-2) 
 * page 162- 163
 */

static int      choosing_id,
                number_id;
static int      *choosing;
static int      *number;

int max_number()
{
  int i, max;
  max = number[smp_rank[0]];
  for (i = 1; i < nprocs; i++)
  {
    if (max < number[smp_rank[i]])
    {
      max = number[smp_rank[i]];
    }
  }
  return max;
}

void lock_pool()
{
  int i;
  choosing[smp_rank[myproc]] = 1;
  number[smp_rank[myproc]] = max_number() + 1;
  choosing[smp_rank[myproc]] = 0;
                                                                                                                                                                           
  for (i = 0; i < nprocs; i++)
  {
    while (choosing[i])
      sched_yield();
    while (number[i] &&
            (number[i] < number[smp_rank[myproc]] ||
              (number[i] == number[smp_rank[myproc]] &&
                               smp_rank[i] < smp_rank[myproc])  )  )
      sched_yield();
  }
}

void unlock_pool()
{
  number[smp_rank[myproc]] = 0;
}

void allocate_lock()
{
   if( myproc == 0 ) {
      choosing_id = shmget(505, nsmp*sizeof(int), 0660 | IPC_CREAT );
      number_id = shmget(  506, nsmp*sizeof(int), 0660 | IPC_CREAT );
   } else {
      choosing_id = shmget(505, nsmp*sizeof(int), 0660 );
      number_id = shmget(  506, nsmp*sizeof(int), 0660 );
   }
}

void init_lock()
{
  choosing = myshmat(choosing_id);
  number = myshmat(number_id);
  choosing[smp_rank[myproc]] = 0;
  number[smp_rank[myproc]] = 0;
}

void free_lock()
{
  struct shmid_ds ds;
  int err;

  if( (err = shmctl(choosing_id, IPC_RMID, &ds)) <0)
    errorx(errno, "shmctl(choosing_id,IPC_RMID) error in free_lock()");
  if( (err = shmctl(number_id, IPC_RMID, &ds)) <0)
    errorx(errno, "shmctl(number_id,IPC_RMID) error in free_lock()");
}



#elif LOCK == HARDWARE 
/*  
 * Use atomic instructions to implement locks 
 *
 *  define X86 or POWERPC through the makefile
 */ 

#if defined( X86 )

/* 
 * Implementation of shared memory locks on x86 processors 
 */
static int              mutex_id;
static int * volatile   mutex;

static int              copy_mutex_id;
static int * volatile   copy_mutex;

#define LOCK_PREFIX "lock ; "
#define SMPVOL volatile
                                                                                                                                                                           
struct __dummy { unsigned long a[100]; };
#define ADDR (*(struct __dummy *) addr)
#define CONST_ADDR (*(const struct __dummy *) addr)
                                                                                                                                                                           
__inline__ static int set_bit(int nr, volatile int * volatile addr)
{
  int oldbit;
   __asm__ __volatile__(LOCK_PREFIX
     "btsl %2,%1\n\tsbbl %0,%0"
     :"=r" (oldbit),"=m" (ADDR)
     :"ir" (nr));
  return oldbit;
}
                                                                                                                                                                           
__inline__ static int clear_bit(int nr, volatile int * volatile addr)
{
  int oldbit;
                                                                                                                                                                           
  __asm__ __volatile__(LOCK_PREFIX
    "btrl %2,%1\n\tsbbl %0,%0"
    :"=r" (oldbit),"=m" (ADDR)
    :"ir" (nr));
  return oldbit;
}
                                                                                                                                                                           
/*
 * This routine doesn't need to be atomic.
 */
__inline__ int static test_bit(int nr, volatile int * volatile addr)
{
  return ((1UL << (nr & 31)) & (((const volatile unsigned int *) addr)[nr >> 5])) != 0;
}

static int hardware_lock(void)
{
  while(set_bit(0, mutex))
    while(test_bit(0, mutex));
  return 0;
} 
  
static int hardware_copy_lock(void)
{
  while(set_bit(0, copy_mutex))
    while(test_bit(0, copy_mutex));
  return 0;
}

static int hardware_unlock(void)
{
  return clear_bit(0, mutex);
}

static int hardware_copy_unlock(void)
{
  return clear_bit(0, copy_mutex);
}

void lock_pool()
{
  hardware_lock();
}

void unlock_pool()
{
  hardware_unlock();
}

void lock_copy()
{
  hardware_copy_lock();
}

void unlock_copy()
{
  hardware_copy_unlock();
}

void allocate_lock()
{
   if( myproc == 0 ) {
      mutex_id = shmget(     507, sizeof(int)+127, 0660 | IPC_CREAT );
      copy_mutex_id = shmget(508, sizeof(int)+127, 0660 | IPC_CREAT );
   } else {
      mutex_id = shmget(     507, sizeof(int)+127, 0660 );
      copy_mutex_id = shmget(508, sizeof(int)+127, 0660 );
   }
}

void init_lock()
{
  mutex = myshmat(mutex_id);
  mutex = (int *) ((((long) mutex) + 127) & -128);
  copy_mutex = myshmat(copy_mutex_id);
  copy_mutex = (int *) ((((long) copy_mutex) + 127) & -128);
  *mutex = *copy_mutex = 0;
}

void free_lock()
{
  struct shmid_ds ds;
  int err;

  if( (err = shmctl(mutex_id, IPC_RMID, &ds)) <0)
    errorx(errno, "shmctl(mutex_id,IPC_RMID) error in free_lock()");
  if( (err = shmctl(copy_mutex_id, IPC_RMID, &ds)) <0)
    errorx(errno, "shmctl(copy_mutex,IPC_RMID) error in free_lock()");
}

#elif defined( POWERPC )

/* 
 * Implementation of shared memory locks on PowerPC processors.
 * This implementation is based on the kernel source for linux 
 * 2.4.20 on ppc.
 * Linux/include/asm-ppc64/spinlock.h 
 * The resource can be found at Linux Cross Reference.
 */
typedef struct {
  volatile unsigned int lock;
} spinlock_t;

int lock_id;
spinlock_t * lock;

static __inline__ void spin_lock(spinlock_t *lock)
{
        unsigned int tmp;
                                                                                                                                                                            
        __asm__ __volatile__(
        "b              2f              # spin_lock\n\
1:      or              1,1,1           # spin at low priority\n\
        lwzx            %0,0,%1\n\
        cmpwi           0,%0,0\n\
        bne+            1b\n\
        or              2,2,2           # back to medium priority\n\
2:      lwarx           %0,0,%1\n\
        cmpwi           0,%0,0\n\
        bne-            1b\n\
        stwcx.          %2,0,%1\n\
        bne-            2b\n\
        isync"
        : "=&r"(tmp)
        : "r"(&lock->lock), "r"(1)
        : "cr0", "memory");
}

static __inline__ void spin_unlock(spinlock_t *lock)
{
        __asm__ __volatile__("lwsync    # spin_unlock": : :"memory");
        lock->lock = 0;
}

void allocate_lock()
{
   if( myproc == 0 ) {
      lock_id = shmget(509, sizeof(int)+127, 0660 | IPC_CREAT );
   } else {
      lock_id = shmget(509, sizeof(int)+127, 0660 );
   }
}

void init_lock()
{
  lock = myshmat(lock_id);
  lock = (spinlock_t *) ((((int) lock) + 127) & -128);
  lock->lock = 0;
}

void free_lock()
{
  struct shmid_ds ds;
  int err;
                                                                                                                                                                            
  if( (err = shmctl(lock_id, IPC_RMID, &ds)) <0)
    errorx(errno, "shmctl(lock_id,IPC_RMID) error in free_lock()");
}

void lock_pool()
{
  spin_lock(lock);
}
                                                                                                                                                                            
void unlock_pool()
{
  spin_unlock(lock);
}

#endif /* For PowerPC processors */ 

#elif LOCK == MUTEX
/* 
 * Use process shared mutex to implement locks  
 */

#include <pthread.h> 
static int  mutex_id;
static pthread_mutex_t *mutex;

/*
 * lock the shared memory pool
 */
void lock_pool()
{
  pthread_mutex_lock(mutex);
}

/*
 * unlock the shared memory pool
 */
void unlock_pool()
{
  pthread_mutex_unlock(mutex);
}

/* allocate a lock */

void allocate_lock()
{
   if( myproc == 0 ) {
      mutex_id = shmget(510, sizeof(pthread_mutex_t), 0660 | IPC_CREAT );
   } else {
      mutex_id = shmget(510, sizeof(pthread_mutex_t), 0660 );
   }
}

/*
 * intialize the lock
 */
void init_lock()
{
  pthread_mutexattr_t mattr;

  mutex = myshmat(mutex_id);
  if ( smp_rank[myproc] == 0 )
  {
    pthread_mutexattr_init(&mattr);

    /* make the mutex shared between processes */
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);  
    pthread_mutex_init(mutex, &mattr);
  }
}

/*
 * deallocate the lock 
 */
void free_lock()
{
  struct shmid_ds ds;
  int err;

  if( (err = shmctl(mutex_id, IPC_RMID, &ds)) <0)
    errorx(errno, "shmctl(mutex_id,IPC_RMID) error in free_lock()");
}
#endif

/*-----------------------------------------------*/
/* Different poll method                         */
/*-----------------------------------------------*/

#define YIELD    1
#define SIGNAL   2
#define PAUSE    3 /* has a shorter latency than NOP when used on Pentium 4 */
#define NOP      4

#define POLL PAUSE

/*  Doesn't work on AIX for shm_lockfifo.c, so comment out since using 
    shm_lockfree.c anyway.

__inline__ void short_wait()
{
#if POLL == YIELD
  sched_yield();
#elif POLL == NOP
  asm volatile (
    "nop"
  );
#elif POLL == PAUSE   
  asm volatile (
    "pause"   
  );
#endif
}

*/
