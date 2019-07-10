
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>

#include <sys/user.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <vipl.h>

#define MAX_MSGS        100         /* Maximum outstanding messages */

#define RDMAW_THRESHOLD 16300       /* rdmaw threshold */
#define RDMA_DESC_NUM   300         /* number of RDMAW descriptors */

#define FAST_RDMAW                  /* a little faster on gigabit ethernet */
#define POLLING_COUNT   100         /* polling times before waiting */

#define MIN_SPLIT       2048        /* Channel-bonding threshold */

/* VIA connection descriptor */
struct via_conn 
{
    int                peer;        /* peer node */
    int                nic;         /* which nic to be used */
    VIP_VI_HANDLE      viHand;      /* vi handle */
    VIP_CONN_HANDLE    connHand;    /* connection handle */
    VIP_NET_ADDRESS    *localAddr;  /* local address */
    VIP_NET_ADDRESS    *remoteAddr; /* remote address */
    int                seq;         /* send sequence # */
    int                seqExp;      /* expected receive sequence # */
    int                sync;        /* synchronization channel flag */
};

/* Range of send/recv sequence number, must > 0 */
#define MIN_SEQ         1           /* lower bound */
#define MAX_SEQ         65535       /* upper bound */

/* Headers */

typedef struct via_hdr {
    int         op;
    int         nbytes;
    int         tag;
} via_hdr_t;

/* RDMAW request-to-send header */
typedef struct via_hdr_rts {
    int         op;
    int         nbytes;
    int         tag;
    int         srcId;
} via_hdr_rts_t;

/* RDMAW clear-to-send header */
typedef struct via_hdr_cts {
    int               op;
    int               nbytes;
    int               tag;
    int               srcId;
    int               dstId;
    char              *addr;
    VIP_MEM_HANDLE    mHand[MAX_NICS];
} via_hdr_cts_t;

/* Header code */
#define    OP_SEND         0x01    /* normal send */    
#define    OP_RDMAW_RTS    0x02    /* request to send */
#define    OP_RDMAW_CTS    0x03    /* clear to send */


/* Reserved descriptors */
#ifdef GIGANET
#define    DESC_POST_NUM   200     /* number of decriptors pre-posted */
#define    MBUF_SIZE       1024
#else
#define    DESC_POST_NUM   10      /* number of decriptors pre-posted */
#define    MBUF_SIZE       16384
#endif

#define    DESC_SHARE_NUM  10      /* number of shared descriptors */

#define    TIMEOUT         5000    /* in microsecond */


#define MAX_DREG_ENTRY 1
#define DREG_ENTRY_INVALID -1
#define DREG_ENTRY_CACHED  -2

/* Dynamic memory registration entry */
struct dreg_entry
{
    int                status;
    char               *buffer[MAX_NICS];
    int                nbytes;
    VIP_MEM_HANDLE     mHandle[MAX_NICS];
    long               timestamp;
};

#define BUF_SIZE      (MBUF_SIZE - sizeof(VIP_DESCRIPTOR)) /* data size */

struct mbuf {
    VIP_DESCRIPTOR    desc;
    char              buf[BUF_SIZE];
};

// check error
#define via_check_error(rc, err_msg) \
    if (rc != VIP_SUCCESS) errorx(rc, err_msg)


VIP_DESCRIPTOR *via_rdma_desc_request(int nic);
void via_rdma_desc_release(VIP_DESCRIPTOR *p, int nic);

void via_do_send(struct MP_msg_entry *msg);
void via_do_recv(struct via_conn *c);
void via_do_recv_from_any();

void via_dreg_init();
void via_dreg_clean();
void via_register_mem(struct MP_msg_entry *msg);
void via_deregister_mem(struct MP_msg_entry *msg);

void via_init(void);
void via_clean();
struct via_conn *via_create_vi(int node, int nic, int sync);
void via_destroy_vi(struct via_conn *c);
void via_set_local_address(struct via_conn *c);
void via_set_remote_address(struct via_conn *c);
void via_alloc_memory(int nic);
void via_prepare_receive(struct via_conn *c);
void via_conn_setup(void);
void via_accept_connection(struct via_conn *c);
void via_connect_to(struct via_conn *c);
void via_disconnect();

VIP_DESCRIPTOR *via_desc_request(int nic);
void via_desc_release(VIP_DESCRIPTOR *p, int nic);

void via_op(struct via_conn *c, VIP_DESCRIPTOR *p);
void via_get_op_send(struct via_conn *c, VIP_DESCRIPTOR *p);
void via_get_op_rdmaw_rts(struct via_conn *c, VIP_DESCRIPTOR *p);
void via_get_op_rdmaw_cts(struct via_conn *c, VIP_DESCRIPTOR *p);
void via_get_op_rdmaw_done(int dstId);

void via_post_send(struct via_conn *c, VIP_DESCRIPTOR *p);
void via_post_recv(struct via_conn *c, VIP_DESCRIPTOR *p);

int via_rdmaw(struct via_conn *c, struct MP_msg_entry *msg, 
        via_hdr_cts_t *hdr_cts);

int via_send(struct via_conn *c, void *buf, size_t len);
int via_recv(struct via_conn *c, void *buf, size_t len);

VIP_DESCRIPTOR *via_recv_one(struct via_conn *c);
void via_send_one(struct via_conn *c, VIP_DESCRIPTOR *p);

void via_send_with_header(struct via_conn *c, via_hdr_t *hdr, char *buf, int len);

void via_msg_split(struct MP_msg_entry *msg, int length);
struct MP_msg_entry *via_buffering(struct via_conn *c, VIP_DESCRIPTOR *p);

void via_error_callback(VIP_PVOID c, VIP_ERROR_DESCRIPTOR *p);

void get_devices();
void get_myproc();

VIP_DESCRIPTOR *via_send_wait(struct via_conn *c);
VIP_DESCRIPTOR *via_recv_wait(struct via_conn *c);

int sem_create(key_t key, int initval);
int sem_open(key_t key);
void sem_rm(int id);
void sem_close(int id);
void sem_wait(int id);
void sem_signal(int id);
void sem_op(int id, int value);

void err_sys(char *msg);
void err_dump(char *msg);
