//****************************************************************************
// "NetPIPE" -- Network Protocol Independent Performance Evaluator.          *
// Copyright 1997, 1998 Iowa State University Research Foundation, Inc.      *
// NetPIPE is free software distrubuted under the GPL license.               *
// It is currently being developed Fall 2014- by Dave Turner at KSU          *
// Email: DrDaveTurner@gmail.com                                             *
//****************************************************************************
// TODO: This module does not currently work with NetPIPE-5.x

// This module will generate a full queue system on top of an MPI-3 shared
// memory segment to provide extremely low latency messages between 
// processes on the same node.  It will not be a robust system as it will
// not handle stranded messages, nor make optimal use of memory.  The use
// of ANY_SOURCE is also not included at this time.  Adding these would 
// make this system fully robust but should not change the performance much.

// Each proc will have its own queue for outgoing messages, and will be the
// only proc that can write to that area except when other procs mark a
// fully-read-message flag for garbage cleanup.  This eliminates the need for
// costly locks on each queue.  When a new message is added to the queue,
// it is written completely before the pointer to it is added.

// The shared-memory segment is mapped into each proc's memory space
// using different base pointers.  Reads will need to adjust their
// pointers by the difference between the source and dest node base pointers
// for that queue.

#include    "netpipe.h"
#include    <mpi.h>

// Pointers to my queue and arrays pointing to my place in other queues

struct qtype {
   void * base;
   void * head;                  // Global doubly linked list for controlling my queue
   void * tail;    
   void * qstart;                // Upper and lower bound of my queue
   void * qend;
   struct msgtype  *** from_src; // Links to my incoming msgs in other queues
   struct msgtype  *** to_dest;  // Starting link for dest msgs in my queue
   struct msgtype  ** dlast;     // Array of ptrs to last msg for each dest
   long * offset;                // Array of pointer offsets for dest
                                 // procs to adjust source queue pointers
   int garb_counter;
} *myq;

void print_myq(ArgStruct *p, struct qtype *q )
{
   int i;
   fprintf(stderr,"\n%d q dump  base=%p  head=%p tail=%p qstart=%p qend=%p\n",
                  p->myproc, q->base, q->head, q->tail, q->qstart, q->qend);
   for( i=0; i<p->nprocs; i++ ) {
      fprintf(stderr,"%d dest=%d   from_src=%p   to_dest=%p   dlast=%p\n",
                      p->myproc, i, q->from_src[i], q->to_dest[i], q->dlast[i]);
      fprintf(stderr,"%d dest=%d   *from_src=%p   *to_dest=%p\n",
                      p->myproc, i, *q->from_src[i], *q->to_dest[i]);
   }
   fprintf(stderr,"\n");
}

// Each message has the header info below followed by the message data.
// Once the message is written completely, the last step is to set the
// dest_ptr of the prev msg to point to this message.  No locks needed.

struct msgtype {
   char * data;       // Pointer to the data for this msg
   void * next;       // Pointer to next msg in my queue, all dests
   void * prev;       // Pointer to prev msg in my queue, all dests
   void * dnext;      // Pointer to next msg to this same destination
   void * dprev;      // Pointer to prev msg to this same destination
   int dest;          // Destination proc
   int nbytes;        // Number of bytes in message
   int tag;           // Tag for this message
   int flag;          // Garbage cleanup flag for this msg.
                      // Dest proc marks 0 -> 1 for src proc to cleanup.
                      // This is the only time other procs write to my queue.
};

static int send_tag = 0, recv_tag = 0;


// Initialize vars in Init() that may be changed by parsing the command args

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
  p->source = 0;  // Default source node

  MPI_Init(pargc, pargv);
}

void Setup(ArgStruct *p)
{
   char s[255], *ptr;
   FILE *fd;
   int i, np, mp = -1, node, fs;
   MPI_Win win;
   MPI_Aint wsize;
   void ** qbase, **ptrptr;
   int disp_unit;

   MPI_Comm_rank(MPI_COMM_WORLD, &p->myproc);
   MPI_Comm_size(MPI_COMM_WORLD, &np);


   gethostname(s,253);
   if( s[0] != '.' ) {                 // just print the base name
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

   p->dest = (p->nprocs - 1) - p->myproc;

       /* You can change the communication pairs using the <-H hostfile>
        * where hostfile has a listing of the new process numbers,
        * with the first and last communicating, etc.
        * In this code, it changes each procs myproc and dest.
        */

// DDT - This could use better error checking on the input file

   if( p->hostfile != NULL ) {

      if( (fd = fopen( p->hostfile, "r")) == NULL ) {
         fprintf(stderr, "Could not open the hostfile %s\n", p->hostfile );
         exit(0);
      }

      p->host = (char **) malloc( p->nprocs * sizeof(char *) );
      for( i=0; i<p->nprocs; i++ )
         p->host[i] = (char *) malloc( 100 * sizeof(char) );

      for( i=0; i<p->nprocs; i++ ) {
         fs = fscanf( fd, "%s", p->host[i]);
         if( atoi(p->host[i]) == p->myproc ) mp = i;
      }

      if( mp < 0 ) {
         fprintf(stderr, "%d NetPIPE: Error reading the hostfile, mp=%d\n",
                 p->myproc, mp);
         exit(0);
      }

      p->dest = atoi( p->host[ p->nprocs - 1 - mp ] );
//fprintf(stderr,"%d proc mp=%d dest=%d\n", p->myproc, mp, p->dest);
      p->myproc = mp;

   }

       /* The first half are transmitters by default.  */

   p->tr = p->rcv = 0;
   if( p->myproc < p->nprocs/2 ) p->tr = 1;
   else p->rcv = 1;

       /* p->source may already have been set to -1 (MPI_ANY_SOURCE)
        * by specifying a -z on the command line.  If not, set the source
        * node normally. */

   if( p->source == 0 ) p->source = p->dest;

// Allocate the MPI-3 shared window

   long window_size = 1000000000;   // Choose 1 GB for a window size
   void * mywin;

   MPI_Win_allocate_shared( window_size, sizeof(double), MPI_INFO_NULL, MPI_COMM_WORLD, 
                            &mywin, &win);

// Get the base pointers for all procs

   qbase = (void **) malloc( p->nprocs * sizeof( void * ) );

   for( i=0; i < p->nprocs; i++ ) {

      MPI_Win_shared_query( win, i, &wsize, &disp_unit, &qbase[i] );

printf("%d qbase[%d] = %p\n", p->myproc, i, qbase[i]);
   }

// Put the local address for this queue in the first element so the dest
// nodes can use it to create an offset pointer for when they access this queue

   ptrptr = (void **) qbase[p->myproc];

   *ptrptr = qbase[p->myproc];

   MPI_Barrier(MPI_COMM_WORLD);

// Initialize my queue
//    - myq struct contains info for writing to my queue and recving from other queues
//    - Each queue has nprocs dest queue pointers then the outgoing queue.

   myq = malloc( sizeof( struct qtype ) );

   myq->base = mywin;

   myq->head = NULL;
   myq->tail = NULL;

   myq->to_dest  = (struct msgtype ***) malloc( p->nprocs * sizeof( void * ) );
   myq->from_src = (struct msgtype ***) malloc( p->nprocs * sizeof( void * ) );
   myq->dlast    = (struct msgtype **) malloc( p->nprocs * sizeof( void * ) );
   myq->offset   = (long *) malloc( p->nprocs * sizeof( long ) );

// Grab every source's own pointer to create my offsets for reading from those queues

   for( i=0; i < p->nprocs; i++ ) {

      ptrptr = (void **) qbase[i];

      myq->offset[i] = ( (char *)qbase[i] - (char *)*ptrptr );// Their pointer minus mine

fprintf(stderr,"%d  qb=%p - *qb=%p ==> offset=%ld\n", i, qbase[i], *ptrptr, myq->offset[i]);
   }

// Initialize the to_dest and from_src arrays

   for( i=0; i < p->nprocs; i++ ) {

      myq->to_dest[i]  = (struct msgtype **)(qbase[p->myproc] + (i+1)*sizeof(void*) );

      myq->from_src[i] = (struct msgtype **)(qbase[i] + 
                            (p->myproc + 1) * sizeof(void*) );

      *myq->to_dest[i] = NULL;
      myq->dlast[i]    = NULL;
      fprintf(stderr,"%d from_src[%d]=%p   offset=%x   to_dest[%d]=%p\n",
              p->myproc, i, myq->from_src[i], (unsigned int)myq->offset[i], i, myq->to_dest[i]);
      //fprintf(stderr,"%d dest=%d   *from_src=%p   *to_dest=%p\n",
                      //p->myproc, i, *myq->from_src[i], *myq->to_dest[i]);
   }

   myq->qstart = myq->base + (p->nprocs + 1)*sizeof(void*);
   myq->qend   = myq->base + window_size;

   myq->garb_counter = 0;

//print_myq(p, myq );

   MPI_Barrier(MPI_COMM_WORLD);
}   

void Sync(ArgStruct *p)
{
    MPI_Barrier(MPI_COMM_WORLD);
}


// Put a message in my queue for the dest proc to grab

void SendData(ArgStruct *p)
{
   struct msgtype *msg, *m;
   int i, padding;

// Determine the address for this new message

   msg = myq->tail;
   if( msg == NULL ) msg = myq->qstart;
   else              msg = msg->next;

// Make certain there is room for the message before qend

   if( (char *) msg + sizeof( struct msgtype ) + p->bufflen > (char *) myq->qend ) {
      msg = myq->qstart;   // Wrap around to the start of the queue
   }

// Check to see if we're running into the head of the circular queue

   if( myq->head != NULL && (void *) msg < myq->head && 
       (char *) msg + sizeof( struct msgtype ) + p->bufflen > (char *) myq->head ) {
      fprintf(stderr,"Proc %d ran out of room in the queue\n", p->myproc);
      exit(-1);
   }

// Fill the message header

   msg->nbytes = p->bufflen;
   send_tag = (++send_tag % 100);
   msg->tag    = send_tag;
   msg->dest   = p->dest;   // Not really needed at this point
   //msg->src   = p->source;   // Not really needed at this point

   padding = 8 -  msg->nbytes % 8;  if( padding == 8 ) padding = 0;

   msg->flag = 0;           // Set the garbage flag

   msg->next = (char *) msg + sizeof( struct msgtype ) + msg->nbytes + padding; 
   msg->prev = myq->tail;
   msg->dnext = NULL;

//fprintf(stderr,"%d SendData(%p %d bytes %d tag to proc %d) msg=%p\n", 
               //p->myproc, p->s_ptr, p->bufflen, send_tag, p->dest, msg);

// Copy the data to the queue

   msg->data = (char *) msg + sizeof( struct msgtype );

   memcpy( msg->data, p->s_ptr, msg->nbytes );

//fprintf(stderr,"%d SendData msg=%p memcpy done\n", p->myproc, msg);

// Now add to the dlast linked list to activate the message

   m = myq->dlast[ p->dest ];

   if( m == NULL ) {
      *myq->to_dest[ p->dest ] = msg;
   } else {
      m->dnext = msg;
   }
   myq->dlast[ p->dest ] = msg;

// Adjust the head and tail if needed

   myq->tail = msg;   // This message is now the new tail of the linked list

   if( myq->head == NULL ) myq->head = msg;

//fprintf(stderr,"%d SendData() added msg to myq->dlast\n", p->myproc);

// Do garbage collection occasionally

   if( ++myq->garb_counter == 100 ) {

      myq->garb_counter = 0;

      for( i = 0; i < p->nprocs; i++ ) myq->dlast[i] = NULL;

//fprintf(stderr,"%d SendData() doing garbage cleanup\n", p->myproc);

      void * newhead = NULL, * newtail = NULL;

      for( msg = myq->head; /* break after tail msg */ ; msg = msg->next ) {

         if( msg->flag == 1 ) {   // A completed read, do cleanup

// Take it out of the linked lists

            m = msg->dprev;
            if( m == NULL ) *myq->to_dest[ msg->dest ] = msg->dnext;
            else                              m->dnext = msg->dnext;

            m = msg->prev;
            if( m == NULL ) myq->head = msg->next; 
            else              m->next = msg->next;

            msg->flag = 2;  // Signal spinning recv that this message has been cleaned

         } else {

            if( newhead == NULL ) newhead = msg;

            newtail = msg;
            myq->dlast[msg->dest] = msg;   // Reset dlast for all destinations
         }

         if( msg == myq->tail ) break;
      }

      myq->head = newhead;
      myq->tail = newtail;
//print_myq(p, myq );
//sleep(5);
   }

   //MPI_Send( p->s_ptr, p->bufflen, MPI_BYTE, p->dest, tag, MPI_COMM_WORLD);
   //tag = (tag+1) % 100;
}


// Get an incoming message from a source queue on the shared segment
// making sure to use the pointer offsets.

void RecvData(ArgStruct *p)
{
   volatile struct msgtype *msg, *last_valid, *new_msg;

   if( p->source < 0 ) {
      fprintf(stderr,"Receive from MPI_ANY_SOURCE not supported at this time\n");
      exit(-2);
   }

   recv_tag = (++recv_tag % 100);

//fprintf(stderr,"%d RecvData(%p %d bytes %d tag from proc %d)\n", 
               //p->myproc, p->r_ptr, p->bufflen, recv_tag, p->source);


// Go through message list or spin on last until a match is found

   msg = NULL;
   last_valid = NULL;

   for( ; ; ) {    // Spin until a matching message has been found then break

// Spin to find the next message to check

      new_msg = NULL;

      if( msg == NULL ) {   // Spin to find a first message

         new_msg = *myq->from_src[ p->source ];

      } else {   // Spin through the queue of messages to me

         new_msg = msg->dnext;

      }

      if( new_msg != NULL ) {   // Found a new message, now test it for a match

               // Adjust to my set of pointers first

         msg = (struct msgtype *) ((char *) new_msg + myq->offset[ p->source ]);

         if( msg->flag == 0 ) last_valid = msg;

         if( msg->nbytes == p->bufflen && msg->tag == recv_tag && msg->flag == 0 ) { 
            break;   // We found our message
         }

      }

               // If it is a deleted message, go back to last valid message

      if( msg != NULL && msg->flag == 2 ) msg = last_valid;

      //sleep(1);
//fprintf(stderr,"\n%d RecvData spinning try %p msg next\n\n", p->myproc, msg);

// May want a block of NOP's here eventually  - cyclesleep( ncycles )

   }
//fprintf(stderr,"%d RecvData %p msg not null - %d bytes\n", p->myproc, msg, msg->nbytes);

// Copy data out of queue

   memcpy( p->r_ptr, (char *) msg->data + myq->offset[p->source], msg->nbytes);

// Mark message as garbage for source proc to clean up

   msg->flag = 1;

//fprintf(stderr,"%d RecvData got msg data\n", p->myproc);

//MPI_Recv(p->r_ptr, p->bufflen, MPI_BYTE, p->source, tag, MPI_COMM_WORLD, &status);
}

void PrepostRecv(ArgStruct *p) { }

int TestForCompletion( ArgStruct *p ) { return 1; }

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

void Reset(ArgStruct *p) { }

