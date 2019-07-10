
//
// scivlib.c is an sci virtual library for connecting, sending, and receiving data
// scroll down for additional comments on the protocols used
// there are many different versions of this file with different protocols used
// this version uses the two-segment synchronization, raw FIFO protocol
// we will call that protocol version 0.1, as it is the most basic protocol (slow)
//

//int rn[2] = { 4, 68 };
//int rn[2] = { 8, 72 };

#define alignlen 128
//#define seglen 65536
#define seglen (2*1024*1024)
//#define blanklen (100000-maxlen*2-alignlen*2-12)
//#define blanklen (512*1024)
//#define blanklen 65536
#define blanklen alignlen
//#define scivlib_datalen (seglen - alignlen)
#define maxlen ((seglen/2+8) & -alignlen)
//#define maxlen ((65536+8) & -alignlen)
//#define maxlen ((16384+8) & -alignlen)
//#define maxlen ((3328+8) & -alignlen)
//#define maxlen ((1024*3-4-128))
//#define maxlen ((1024-4-128))
//#define maxlen ((256+8) & -alignlen)

#define readflushlimit 2/16

#define verbose 0
#define verboseo 0

#include "sisci_error.h"
#include "sisci_api.h"
#include "sisci_demolib.h"

#define reusepos (1<<31)

//src/SCI_SOCKET/scilib/SISCI/scilib_sisci.h

//#include "scilib.h"

FILE* scifile;
FILE* scifile2;

void uwait(int rep) {
  int i;
  for(;rep;rep--) {
    for(i=1;i;i<<=1);
  }
}

//
// segment1 layout: start1 end1 start2 end2 data1
// segment2 layout: start2 end2 start1 end1 data2
//                  ==ournode== =othernode= local
//
// start1 = read position of cpu1, written by cpu1
//   end1 = write position of cpu1, written by cpu2
// start2 = read position of cpu2, written by cpu2
//   end2 = write position of cpu2, written by cpu1
//   data = corresponding data, circular fifo
//
// every pair of cpus will have a pair of fifos (waste of memory for large cluster?)
//  - typically MP programs are root-based so everything talks to a single root node
//
// maxlen limits the size of a single "packet" ... anything larger will need to
// be broken up. large packets fill the fifo up completely before the receiver
// can verify, leaving the sender in a wait-state. this is fixed by letting the
// receiver process data more often as it comes in.
//
// 4 bytes reserved somewhere for receiver to write fifo read pointer
//



#define SCIVLIB_MAXSOCKETS 256


    sci_remote_segment_t rs[SCIVLIB_MAXSOCKETS];
    sci_local_segment_t ls[SCIVLIB_MAXSOCKETS];
    sci_desc_t sd1[SCIVLIB_MAXSOCKETS];
    sci_desc_t sd2[SCIVLIB_MAXSOCKETS];
    sci_error_t err;
    sci_segment_cb_reason_t reason;
    sci_map_t remotemap[SCIVLIB_MAXSOCKETS];
    sci_map_t localmap[SCIVLIB_MAXSOCKETS];
    sci_sequence_t seq[SCIVLIB_MAXSOCKETS];
    sci_sequence_status_t   sequenceStatus;
    int adapter[SCIVLIB_MAXSOCKETS];
    int sourceid[SCIVLIB_MAXSOCKETS];
    unsigned char *dstbuf[SCIVLIB_MAXSOCKETS] = {0};
    unsigned char *buf[SCIVLIB_MAXSOCKETS] = {0};
    int nbytes = seglen;
    volatile static unsigned char op = 1;
    int rank = 0;
    int segid[SCIVLIB_MAXSOCKETS] = {0};
    unsigned int reuseflagr[SCIVLIB_MAXSOCKETS] = {0};
    unsigned int reuseflagw[SCIVLIB_MAXSOCKETS] = {0};

    sci_remote_segment_t rssync[SCIVLIB_MAXSOCKETS];
    sci_local_segment_t lssync[SCIVLIB_MAXSOCKETS];
    sci_desc_t sdsync1[SCIVLIB_MAXSOCKETS];
    sci_desc_t sdsync2[SCIVLIB_MAXSOCKETS];
    sci_map_t remotemapsync[SCIVLIB_MAXSOCKETS];
    sci_map_t localmapsync[SCIVLIB_MAXSOCKETS];
    sci_sequence_t seqsync[SCIVLIB_MAXSOCKETS];
    unsigned int *dstbufsync[SCIVLIB_MAXSOCKETS] = {0};
    unsigned int *bufsync[SCIVLIB_MAXSOCKETS] = {0};
    int segidsync[SCIVLIB_MAXSOCKETS] = {0};

static int virtualport[SCIVLIB_MAXSOCKETS] = { 0 };

static int writeptr[SCIVLIB_MAXSOCKETS] = { 0 };
static int write_total[SCIVLIB_MAXSOCKETS] = { 0 };

static int readptr[SCIVLIB_MAXSOCKETS] = { 0 };
static int lastflush[SCIVLIB_MAXSOCKETS] = { 0 };
static int read_total[SCIVLIB_MAXSOCKETS] = { 0 };

static int lbd[SCIVLIB_MAXSOCKETS] = { 0 };  // loopback device

static int write_sem[SCIVLIB_MAXSOCKETS] = {0};
static int read_sem[SCIVLIB_MAXSOCKETS] = {0};


int hexdump_sem = 0;

static void hexdump(unsigned char* buf, int len, int start) {
    int m,n,j;

    while(hexdump_sem) {
      continue;
    }
    hexdump_sem++;

    m=n=0;
    for(;len>0;m=n) {
      n += (len>=16)?16:len;
      if(m==n) break;
      printf("%08X: ",m+start);
      for(j=0;j<n-m;j++) {
        printf("%c%02X",(j==8)?'-':' ',buf[j+m]);
      }
      for(j=n-m;j<16;j++) {
        printf("   ");
      }
      printf("   ");
      for(j=0;j<n-m;j++) {
        printf("%c",((buf[j+m]&0x7f)<0x20)?'.':buf[j+m]&0x7f);
      }
      printf("\n");
      len -= 16;
    }
    fflush(0);

    hexdump_sem--;

}


static void hexdumpf(FILE* file, unsigned char* buf, int len, int start) {
    int m,n,j;

    while(hexdump_sem) {
      continue;
    }
    hexdump_sem++;

    m=n=0;
    for(;len>0;m=n) {
      n += (len>=16)?16:len;
      if(m==n) break;
      fprintf(file, "%08X: ",m+start);
      for(j=0;j<n-m;j++) {
        fprintf(file, "%c%02X",(j==8)?'-':' ',buf[j+m]);
      }
      for(j=n-m;j<16;j++) {
        fprintf(file, "   ");
      }
      fprintf(file, "   ");
      for(j=0;j<n-m;j++) {
        fprintf(file, "%c",((buf[j+m]&0x7f)<0x20)?'.':buf[j+m]&0x7f);
      }
      fprintf(file, "\n");
      len -= 16;
    }
    fflush(0);

    hexdump_sem--;

}


int newfd = 4;
int scivlib_getnewfd() {
  return ++newfd;
}

int initialized = 0;

int scivlib_init(int mp, int remotenodeid, int port)   // returns "socket" number (fd)
{
    sci_error_t err;
    int fd;

    port <<= 4;
    port ^= mp;

    printf("scivlib_init(%d,%8x,%d)\n", mp, remotenodeid, port);

#if 0
    if(rn[mp] == remotenodeid) {
        return -1;
        fd = scivlib_getnewfd();

        printf("hmm... this is myself (%d)\n", fd);

        readptr[fd] = alignlen;
        lastflush[fd] = alignlen;
        read_total[fd] = 0;

        write_sem[fd] = 0;
        read_sem[fd] = 0;

        writeptr[fd] = 0;
        write_total[fd] = 0;

        reuseflagr[fd] = reusepos;
        reuseflagw[fd] = 0;

        dstbuf[fd] = buf[fd] = (unsigned char*)malloc(nbytes);
        memset(buf[fd], 0, nbytes);

        lbd[fd] = 1;

        return fd;
    }
#endif

    rank = mp;
    fd = scivlib_getnewfd();

#if verboseo
  if(rank) scifile = fopen("zzwrite1.txt", "a");
  else scifile = fopen("zzwrite0.txt", "a");
  if(rank) scifile2 = fopen("zzread1.txt", "a");
  else scifile2 = fopen("zzread0.txt", "a");
#endif

    lbd[fd] = 0;

    err = 0;
    if(!initialized) {
        SCIInitialize(0, &err);
        initialized = 1;
    }

    if(err != SCI_ERR_OK) {
        fprintf(stderr,"SCIInitialize failed - Error code: 0x%x\n",err);
        //exit(err);
    }

    SCIOpen(&sd1[fd], 0, &err);
    SCIOpen(&sd2[fd], 0, &err);
    SCIOpen(&sdsync1[fd], 0, &err);
    SCIOpen(&sdsync2[fd], 0, &err);

    if(err == SCI_ERR_OK) {
       // printf("SCIOpen() = OK!\n");
    } else {
        printf("SCIOpen() = FAIL!\n");
        if (err == SCI_ERR_INCONSISTENT_VERSIONS) {
            fprintf(stderr,"Version mismatch between SISCI user library and SISCI driver\n");
        } 
        fprintf(stderr,"SCIOpen failed - Error code 0x%x\n", err); fflush(0);
        exit(err);
    }

//    segid[fd] = 0x1000 ^ mp ^ (fd<<16) ^ (port<<24);     // (p->prot.nodeids[mp^1] << 16) | p->prot.nodeid;
    segid[fd] = 0x1000 ^ mp ^ (port<<16);     // (p->prot.nodeids[mp^1] << 16) | p->prot.nodeid;
segid[fd] = port;
segidsync[fd] = segid[fd] | 0x10000;

      //  printf("%d: try xLocal segment (id=0x%x, size=%d) is created. \n", mp, segid, nbytes);
    SCICreateSegment(sd1[fd], &ls[fd], segid[fd], nbytes, 0, 0, 0, &err);
    if (err == SCI_ERR_OK) {
        printf("%d/%d: xLocal segment (id=0x%x, size=%d) is created. \n", mp, port, segid[fd], nbytes);
    } else {
        printf("xSCICreateSegment failed - Error code 0x%x [errno=%d]\n",err, errno); fflush(0);
        exit(1);
    }

    SCICreateSegment(sdsync1[fd], &lssync[fd], segidsync[fd], 8, 0, 0, 0, &err);
    if (err == SCI_ERR_OK) {
//        printf("%d/%d: xLocal segment (id=0x%x, size=%d) is created. \n", mp, port, segid[fd], nbytes);
    } else {
        printf("xSCICreateSegment failed - Error code 0x%x [errno=%d]\n",err, errno); fflush(0);
        exit(1);
    }

    SCIPrepareSegment(ls[fd], 0, 0, &err);
    if (err == SCI_ERR_OK) {
       // printf("Local segment (id=0x%x, size=%d) is prepared. \n", segid, nbytes);
    } else {
        fprintf(stderr,"SCIPrepareSegment failed - Error code 0x%x\n", err);
        exit(1);
    }

    SCIPrepareSegment(lssync[fd], 0, 0, &err);
    if (err == SCI_ERR_OK) {
       // printf("Local segment (id=0x%x, size=%d) is prepared. \n", segid, nbytes);
    } else {
        fprintf(stderr,"SCIPrepareSegment failed - Error code 0x%x\n", err);
        exit(1);
    }

    buf[fd] = SCIMapLocalSegment(ls[fd], &localmap[fd], 0, nbytes, 0, 0, &err);
    memset(buf[fd], 0, nbytes);
    if (err == SCI_ERR_OK) {
       // printf("Local segment (id=0x%x buf=%p) is mapped to user space.\n", segid, dstbuf);
    } else {
        fprintf(stderr,"SCIMapLocalSegment failed - Error code 0x%x\n",err);
        exit(1);
    }
//    *(int*)(buf[fd]) = alignlen;

    bufsync[fd] = SCIMapLocalSegment(lssync[fd], &localmapsync[fd], 0, 8, 0, 0, &err);
    memset(bufsync[fd], 0, 8);
    if (err == SCI_ERR_OK) {
       // printf("Local segment (id=0x%x buf=%p) is mapped to user space.\n", segid, dstbuf);
    } else {
        fprintf(stderr,"SCIMapLocalSegment failed - Error code 0x%x\n",err);
        exit(1);
    }

    SCISetSegmentAvailable(ls[fd], 0, 0, &err);
    if (err == SCI_ERR_OK) {
       // printf("Local segment (id=%d:0x%x) is available for remote connections. \n", p->prot.nodeid, segid);
    } else {
        fprintf(stderr,"SCISetSegmentAvailable failed - Error code 0x%x\n", err);
        exit(1);
    }

    SCISetSegmentAvailable(lssync[fd], 0, 0, &err);
    if (err == SCI_ERR_OK) {
       // printf("Local segment (id=%d:0x%x) is available for remote connections. \n", p->prot.nodeid, segid);
    } else {
        fprintf(stderr,"SCISetSegmentAvailable failed - Error code 0x%x\n", err);
        exit(1);
    }

    printf("%d/%d: xRemote connect to (id=0x%x) attempt.\n", mp, port, segid[fd]^1);
    while(1) {
      SCIConnectSegment(sd2[fd], &rs[fd], remotenodeid, segid[fd]^1, 0, 0, 0, SCI_INFINITE_TIMEOUT, 0, &err);
      if (err == SCI_ERR_OK) break;
    }

    printf("%d/%d: xRemote connect to sync (id=0x%x) attempt.\n", mp, port, segidsync[fd]^1);
    while(1) {
      SCIConnectSegment(sdsync2[fd], &rssync[fd], remotenodeid, segidsync[fd]^1, 0, 0, 0, SCI_INFINITE_TIMEOUT, 0, &err);
      if (err == SCI_ERR_OK) break;
    }

    dstbuf[fd] = (void*)SCIMapRemoteSegment(rs[fd], &remotemap[fd], 0, nbytes, 0, 0, &err);
    if (err == SCI_ERR_OK) {
//        printf("%d/%d: Remote segment (id=0x%x) is mapped to user space. \n", mp, port, segid[fd]^1);
    } else {
        fprintf(stderr,"%d: SCIMapRemoteSegment failed - Error code 0x%x\n", mp, err);
        exit(1);
    }

    dstbufsync[fd] = (void*)SCIMapRemoteSegment(rssync[fd], &remotemapsync[fd], 0, 8, 0, 0, &err);
    if (err == SCI_ERR_OK) {
//        printf("%d/%d: Remote segment (id=0x%x) is mapped to user space. \n", mp, port, segid[fd]^1);
    } else {
        fprintf(stderr,"%d: SCIMapRemoteSegment failed - Error code 0x%x\n", mp, err);
        exit(1);
    }


//    SCICreateMapSequence(remotemap[fd], &seq[fd], SCI_FLAG_FAST_BARRIER, &err);
    SCICreateMapSequence(remotemap[fd], &seq[fd], 0, &err);
    if (err != SCI_ERR_OK) {
        fprintf(stderr,"SCICreateMapSequence failed - Error code 0x%x\n", err);
        exit(1);
    }

    SCICreateMapSequence(remotemapsync[fd], &seqsync[fd], 0, &err);
    if (err != SCI_ERR_OK) {
        fprintf(stderr,"SCICreateMapSequence failed - Error code 0x%x\n", err);
        exit(1);
    }

//    while(SCIStartSequence(seq[fd], 0, &err) != SCI_SEQ_OK);

    readptr[fd] = 0;
    lastflush[fd] = 0;
    read_total[fd] = 0;

    writeptr[fd] = 0;
    write_total[fd] = 0;

    reuseflagr[fd] = reusepos;
    reuseflagw[fd] = 0;

    write_sem[fd] = 0;
    read_sem[fd] = 0;

    return fd;
}


void scivlib_shutdown(int fd)
{
    sci_error_t err;

    SCIRemoveSequence(seq[fd], 0, &err);
    if (err != SCI_ERR_OK) {
        fprintf(stderr,"SCIRemoveSequence failed - Error code 0x%x\n", err);
        exit(1);
    }


    SCIUnmapSegment(remotemap[fd], 0, &err);
    if (err == SCI_ERR_OK) {
       // printf("The remote segment is unmapped\n");
    } else {
        fprintf(stderr,"SCIUnmapSegment failed - Error code 0x%x\n",err);
        exit(1);
    }

    SCIDisconnectSegment(rs[fd], 0, &err);
    if (err == SCI_ERR_OK) {
       // printf("The segment is disconnected\n");
    } else {
        fprintf(stderr,"SCIDisconnectSegment failed - Error code 0x%x\n",err);
        exit(1);
    }

    SCIUnmapSegment(localmap[fd], 0, &err);
    if (err == SCI_ERR_OK) {
       // printf("The local segment is unmapped\n");
    } else {
        fprintf(stderr,"SCIUnmapSegment failed - Error code 0x%x\n",err);
        exit(1);
    }


    SCIRemoveSegment(ls[fd], 0, &err);
    if (err == SCI_ERR_OK) {
       // printf("The local segment is removed\n");
    } else {
        fprintf(stderr,"SCIRemoveSegment failed - Error code 0x%x\n",err);
        exit(1);
    }

    SCIClose(sd1[fd], 0, &err);
    SCIClose(sd2[fd], 0, &err);
    SCIClose(sdsync1[fd], 0, &err);
    SCIClose(sdsync2[fd], 0, &err);
    SCITerminate();
}


/******* thecore *******/


//
// writing works as follows:
//
// * each processor has a buffer of the same size <seglen> which is
//   shared with the other processor. meaning, there are two segments
//   per pair of processors connected to each other in this fashion.
//
// * to communicate, writes are performed to "push" the data over to
//   another processor. "pulling" is inherently slow in typical shared
//   memory systems, so we don't do that here.
//
// * the first <alignlen> bytes of each segment is reserved for control
//   variables. the rest of the data is for semi-raw communication. we
//   actually follow a maildrop protocol for this communication.
//
//     |<------->|<----------------------------------------------->|
//       control                        data
//
// * the only control variable right now is a pushed variable to tell
//   your partner processor how much free space you have left in your
//   buffer. for example, we have two nodes A and B connected to each
//   other. each has their own respective local data segment, A and B
//   (same name to reduce confusion; although I realize this can raise
//   confusion, I hope it's more easily understood by using node and
//   segment names interchangably) which are both shared with their
//   partner. the amount of free space remaining in B is only known by
//   B, since he will be the one reading and removing data from his
//   own buffer. When A wants to write to B, however, A needs to know
//   how much data he can write (in case B is full). so, B pushes some
//   data into A's control data portion of its shared memory that tells
//   A how much more he can safely write. thus, for A to check how much
//   data he can write, he need only check a local memory variable,
//   instead of trying to pull some memory from B (slow!!). specifically,
//   this variable is an integer stored in the first four bytes of that
//   control region. it is simply a zero-based index saying how far a
//   processor has "read" into its data buffer. all data up to that read
//   pointer has already been read, thus, it can be overwritten. this
//   is basically a circular FIFO queue, with the head being at this read
//   pointer, and the tail being at MOST, just behind the read pointer.
//
// * the "maildrop protocol" is fairly simple and mainly used to maximize
//   performance. typically, node A would want to write to B. thus, A
//   would perform a write(), send(), MPI_Send(), or any other various
//   operation that boils down into sending raw bytes to their partner
//   processor. the problem with sending raw bytes is we don't know how
//   long the message was, so we don't know how many bytes are valid to
//   read. we could add such a variable to the control variables section,
//   however, it turns out that pushing another variable into a seperate
//   memory page than the data write causes considerable slowdown. thus,
//   the pushed data length is sent with the message itself. a simple
//   solution is to push the data length, the message, and then the data
//   length again. the purpose of appending the data length to the message
//   is so that the receiving side knows that the message sent has been
//   completely sent. thus, the receiving node knows if there is new data
//   in its data buffer by simply checking if *readptr is non-zero (there
//   is a message of that length either being written or already written),
//   and then comparing this with (readptr + 4 + *readptr) to see if it
//   is the same as the message length. if both of these cases are true,
//   then there is a message ready and waiting. otherwise, there isn't.
//   this protocol obviously adds eight bytes of overhead to every message,
//   which could be high for small messages. however, the alternatives
//   typically either have much higher latencies or waste memory. this
//   protocol has high performance in bandwidth and latency for both
//   "ping-pong" and "push" type network applications.
//
//     "ping-pong" is where A sends a message to B, who then replies
//     back to A, who then replies back to B, etc. this is obviously
//     heavily dependent on latency.
//
//     "push" is where each node may wish to continually push as much
//     data as possible to its partner. this is obviously heavily
//     dependent on bandwidth.
//
// * after memory is read, it should be zeroed out so that next time a
//   packet appears immediately before that memory location, the reader
//   doesn't mistakenly think that another packet is avaiable (when we
//   make a full circle in the FIFO). it may not be enough to simply
//   zero out the data length, as a partial write with a shorter message
//   length could appear to be complete, whereas the shorter message
//   length number simply matched with some other random data later on.
//
// * because shared memory logic relies heavily on small memory pages,
//   every write is followed by a pointer realignment <alignlen> to ensure
//   high speed communication (a small write that crosses a page boundary
//   can hinder speed considerably). this wastes memory, however, I think
//   the performance gain is worth it.
//
// * the most notable special situation that arises in a circular FIFO
//   queue is deciding what do to when crossing the circular position in
//   the FIFO queue (i.e. what to do when you have to write past the end
//   and start over at the beginning). to simplify the FIFO queue and
//   maintain speed, a packet will never be split between the end of the
//   memory segment and the front of the memory segment. packets have a
//   length of at most <maxlen> bytes (after which, they get broken down
//   anyway). thus, as long as we reserve (maxlen+8+alignlen) bytes at
//   the end of the buffer, we should always be able to guarantee enough
//   space to write a full packet. if there isn't enough space to reserve,
//   we would automatically assume that a write could not take place there,
//   and start again at the front of the buffer.
//
//     the read logic is simple: if you read past the end of the
//     buffer, start reading again at the front of the buffer after
//     you parse a whole packet.
//
//     the write logic is slightly more complex. we definitely don't
//     want to write into a memory region that hasn't been read yet.
//     so, we need to clearly decide what is safe to write to and
//     what to stay away from.
//
//                       , readptr                   , writeptr
//                      v                           v
//     |<------->|<====>|<------------------------->|<============>|
//       control   safe             data                  safe
//
//     we need to make sure that after we perform a write operation,
//     the next few bytes of memory after a realignment adjustment
//     are zeroed out, so we don't have a runaway reader. if the
//     realignment adjustment causes us to not have enough space to
//     reserve at the end of the memory region, we need to make sure
//     that the start of the memory region currently does not have
//     data in it. otherwise, the same runaway reader problem exists.
//     thus, before we can write, if we're near the end of the data
//     buffer, we need to see if we're going to wrap around. and if
//     we are, we need to make sure that the read pointer is past
//     the first <alignlen> bytes so that the memory is properly
//     zeroed. we need to check this even before we start writing!
//
//  TODO: avail < nbytes
//






//
// new protocol to assure data correctly/completely flushed by
// using two segments:
//
// 1.  an 8 byte segment for synchronization,
//
//       00 00 00 00 11 11 11 11
//
//     the remote node will write data to the local node's sync segment
//     indicating the circular FIFO's write position (i.e. how far into
//     the FIFO it has written with valid data). the local node can poll
//     this value to see if it has changed, and may read up to this
//     position in the circular FIFO to get valid data.
//
//     the second number in this synchronization segment is to indicate
//     how far the local node has read through its own FIFO. this is to
//     prevent the remote node from overwriting data that the local node
//     has not yet read.
//
// 2.  an nbytes segment for data transfer,
//
//     the data segment, at least for the first version of this protocol,
//     will be a pure FIFO. there will be no starting/ending markers,
//     alignment markers, or blank data; the synchronization segment is
//     sufficient for the data transfer. it may be that adding alignment
//     changes can speed up the transfer, in which case a more hybrid
//     method (between this and the previous protocol) will be used. in
//     fact, it should almost be assumed that adding alignment data will
//     speed up data transfers; however, for simplicity, I will start
//     with a pure FIFO.
//
// * buf[fd] points to local node's data buffer
// * bufsync[fd] points to local node's synchronization data
// * dstbuf[fd] points to remote node's data buffer
// * dstbufsync[fd] points to remote node's synchronization data
//
// * dstbufsync[fd][0] (will cache locally in writeptr[fd], of course) is
//   local node's write pointer into dstbuf[fd]). this will be updated
//   at the end of every scivlib_write().
//
// * dstbufsync[fd][1] is local node's read pointer into buf[fd]. this
//   need not be updated after every read, since there should be plenty
//   of space in the FIFO to write more data; it can be updated after
//   a certain percentage of data has been read.


int getnextsegmentptr(int cur, int len) {
  cur += len;
  cur = (cur + alignlen - 1) & -alignlen;
  if(cur > seglen - maxlen*2 - alignlen - 12) cur = alignlen;
  return cur;
}



void scivlib_write( int dst, void *_buf, int _nbytes )
{
    int cur = 0;
    int nbytes;

    while(write_sem[dst]) {
        continue;
    }
    write_sem[dst]++;

    cur = 0;

#if verbose
    printf("scivlib_write(%08x, dst=%d, %d bytes);\n", write_total[dst], dst, _nbytes);
    hexdump(_buf, _nbytes, write_total[dst]);
#endif

    write_total[dst] += _nbytes;


    while(_nbytes > 0) {

        nbytes = _nbytes;
        if(nbytes > maxlen) { nbytes = maxlen; }
        if(writeptr[dst] + nbytes > seglen) { nbytes = seglen - writeptr[dst]; }

          // dont write past bufsync[fd][1] ...
          // if writeptr[fd] < bufsync[fd][1] but writeptr[fd] + nbytes >= bufsync[fd][1] then it would overwrite

        while(writeptr[dst] < bufsync[dst][1] && (writeptr[dst] + nbytes >= bufsync[dst][1])) { continue; }

        while(SCIStartSequence(seq[dst], 0, &err) != SCI_SEQ_OK) { continue; }

        memcpy(dstbuf[dst] + writeptr[dst], (char*)_buf + cur, nbytes);

        if(SCICheckSequence(seq[dst], 0, &err) != SCI_SEQ_OK) {
          fprintf(stderr, "fatal error - check sequence failed!!\n");
          fflush(0);
          exit(-1);
        }


        writeptr[dst] = (writeptr[dst] + nbytes) % seglen;

        _nbytes -= nbytes;
        cur += nbytes;


        while(SCIStartSequence(seqsync[dst], 0, &err) != SCI_SEQ_OK) { continue; }

        dstbufsync[dst][0] = writeptr[dst];

        if(SCICheckSequence(seqsync[dst], 0, &err) != SCI_SEQ_OK) {
          fprintf(stderr, "fatal error - check sequence failed!!\n");
          fflush(0);
          exit(-1);
        }

    }

    write_sem[dst]--;

}


void scivlib_read( int src, void *_buf, int _nbytes )
{
    int cur = 0;
    int avail;
    int originbytes = _nbytes;
    int bs;

    while(read_sem[src]) {
        continue;
    }
    read_sem[src]++;

    cur = 0;
    originbytes = _nbytes;

#if verbose
    printf("scivlib_read(%08x, src=%d, %d bytes); [%08x %08x]\n", read_total[src], src, _nbytes, bufsync[src][0], readptr[src]);
#endif


    while(_nbytes > 0) {

        avail = 0;
        while(avail <= 0) { bs = bufsync[src][0]; avail = (bs - readptr[src] + seglen) % seglen; }
        if(bs < readptr[src]) { avail = seglen - readptr[src]; }
        if(_nbytes < avail) { avail = _nbytes; }

        memcpy((char*)_buf + cur, buf[src] + readptr[src], avail);

        cur += avail;
        _nbytes -= avail;
        readptr[src] = (readptr[src] + avail) % seglen;

        if((readptr[src] - lastflush[src] + seglen) % seglen > seglen*readflushlimit) {  // dont waste bandwidth on the

          while(SCIStartSequence(seqsync[src], 0, &err) != SCI_SEQ_OK) { continue; }

          dstbufsync[src][1] = readptr[src];                                              //  ring for small packets

          if(SCICheckSequence(seqsync[src], 0, &err) != SCI_SEQ_OK) {
            fprintf(stderr, "fatal error - check sequence failed!!\n");
            fflush(0);
            exit(-1);
          }

          lastflush[src] = readptr[src];

        }

    }

#if verbose
    hexdump(_buf, originbytes, read_total[src]);
#endif

    read_total[src] += originbytes;

    read_sem[src]--;

}


int scivlib_waitevent(int *socklist, int num, int *ready) {
    sci_error_t err;
    int srcnode;
    int localadapter;
    int i;
    int avail;
    int numready = 0;
    int numrounds = 0;

//printf("%d: wait for %d things -- {", rank, num);
//    for(i=0;i<num;i++) printf(" %d", socklist[i]);
//printf(" }\n");

//    SCIWaitForLocalSegmentEvent( ls, &srcnode, &localadapter, SCI_INFINITE_TIMEOUT, 0, &err);

    for(i=0;i<num;i++) {
        ready[i] = 0;
    }

    numrounds = 0;
    while(!numready) {
#if 0
        numrounds++;
        if(numrounds > 1000000) {
            for(i=0;i<num;i++) {
                if(socklist[i] >= 0) {
                    ready[i] = 1;
                   numready++;
                }
            }
            return numready;
        }
#endif
        for(i=0;i<num;i++) {
//            while((avail = *(int*)(buf[0] + readptr[0])) == 0);
//            while(avail != *(int*)(buf[0] + readptr[0] + avail + 4));
            if(socklist[i] < 0) continue;
            if((avail = *(int*)(buf[socklist[i]] + readptr[socklist[i]])) != 0) {
                if(avail == *(int*)(buf[socklist[i]] + readptr[socklist[i]] + avail + 4)) {
                    ready[i] = 1;
                    numready++;
                }
            }
        }

//        ready[rank^1] = 1;
//        usleep(1);
    }

//    printf("%d: waitevent: at least %d bytes avail from node %d\n", rank, avail, rank^1);

    return numready;
}
