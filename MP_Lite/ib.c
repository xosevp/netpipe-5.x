/* MP_Lite message-passing library
 * Copyright (C) 1997-2004 Dave Turner and Adam Oline
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

/* Infiniband support for MP_Lite, Adam Oline, March 2003 - July 2003 
 *
 *
 * IMPLEMENTATION OVERVIEW
 *
 * This file contains the event completion implementation of message-passing
 * over Infiniband hardware, specifically using Mellanox's VAPI layer to
 * communicate with the hardware.
 * 
 * We are currently using the Eager/Rendezvous model for message transfer.
 *
 * Eager transfers:
 *
 *   Each node has one queue pair dedicated to eager communications with each
 *   of the other nodes. A predetermined number of eager receive and send
 *   buffers for each of the other nodes are registered with the Infiniband
 *   adapter at initialization, and any data to be transferred using the eager
 *   protocol is copied to the send buffers on the sending node, sent through
 *   the queue pair, and copied from the receive buffers at the receiving node.
 *
 *   Each node has per-node counts of the number of sends started, the number
 *   of local send buffers freed, and the number of remote receive buffers
 *   freed. The count of send buffers freed is updated locally, and the count
 *   of receive buffers freed is updated by other nodes using RDMA writes.
 *   Before a node may send a message, it must wait until there is at least one
 *   free local send buffer and one free remote receive buffer. So each node
 *   starts with the count of sends started equal to 0, and the count of local
 *   send buffers freed and remote receive buffers freed are equal to the total
 *   number of each type of buffer. The counters maintain running totals, and
 *   are never reset to 0, until they reach the maximum value, at which point
 *   we must wrap around. As long as the number of sends is not equal to the
 *   number of buffers, send or receive, that have been freed, we can initiate
 *   a new send, because at least one send buffer is available locally and one
 *   receive buffer is available remotely.
 *   
 *   Once a message has been sent into the queue pair by the sending node, a
 *   completion will be generated and the local node updates its count of freed
 *   send buffers. At the receiving node, a completion will be generated when
 *   the data has completely arrived. The node copies the data out of the
 *   receive buffer, updates the number of receive buffers freed, and sends
 *   this number via RDMA to the node which sent the data.
 *
 *   Note that this implementation does not do any extra buffering of eager
 *   messages received which have no currently posted receive header
 *   information, i.e., the receiver doesn't know what to do with them. The
 *   buffering is limited to the finite number of eager receive buffers into
 *   which the messages are deposited by the VAPI layer. So deadlock is
 *   possible in the following situation: 
 *   
 *   -Each node is set up with 4 eager receive buffers for every other node to
 *   send to.
 *   -The receiving node posts 5 receives with tags in the order 1,
 *   2, 3, 4, 5 and then waits for a message with tag 1 to arrive before
 *   waiting for any other message.
 *   -The sending node sends 5 distinct messages with tags in the order 5, 4, 
 *   3, 2, 1.
 *
 *   Then the first four of those messages, with tags 5, 4, 3, 2 will actually
 *   be sent, and the last message with tag 1 will stall at the sender because
 *   there are no more buffers free at the receiver. The receiving node, which
 *   is waiting for a message with tag 1 before checking for any other
 *   messages, will stall forever because the mesage will not arrive until at
 *   least one buffer is freed. A more robust implementation would include some
 *   sort of buffering so out-of-order communcations like this will not cause
 *   deadlock.
 *
 * Rendezvous transfers:
 *
 *   Each node has one queue pair dedicated to rendezvous communications with
 *   each of the other nodes, and this queue pair is distinct from the queue
 *   pair used for eager communications. Our rendezvous protocol works as
 *   follows: The receiver sends a message to the sender indicating that the
 *   receiver is ready-to-receive (RTR) N bytes of data with tag X at memory
 *   address P, and that this transaction has an id M.  The receiver expects
 *   the sender to reply with an acknowledgement of the message, which tells the
 *   receiver that the sender copied this header data out of its RDMA
 *   writable memory, and the receiver can send another RTR message.  The
 *   sender may then send the data when he is ready. Because we are using RDMA
 *   transfers, the sender specifies the destination memory address (P,
 *   received in the initial message from the receiver). The id M received in 
 *   the original RTR message is sent along with the data (using the RDMA
 *   immediate data field). A completion will be generated at the
 *   receiving node when the data transfer finishes, and the rendezvous receive
 *   for that data can be marked as done, using the id M to determine which
 *   receive has completed.
 *
 * Other important implementation points:
 *
 *   * Registering memory with VAPI is expensive
 *
 *   This can really affect the performance of the rendezvous communications.
 *   The naive approach is to simply register the buffer that the application
 *   has provided to us with VAPI, wait until the data is transferred through
 *   the VAPI layer, and then unregister the buffer with VAPI. If the
 *   application is going to be giving us a different buffer every single time,
 *   then this is fine. But, it is likely that some applications will tend to
 *   use the same buffers repeatedly for communications. In this case, we are
 *   doing a lot of unnecessary registering and unregistering operations. 
 *
 *   So, in this implementation we are doing "lazy" buffer unregistration. The
 *   basic idea is that, every time a new buffer is given to us by the
 *   application, and it falls into the rendezvous communications category, we
 *   register it with VAPI, and also add it and its VAPI memory region handle
 *   to a persistent data structure (here a red-black tree). When the current
 *   communications involving that buffer finish, we do not unregister it.
 *   Every time we get a buffer from the application, we first check it against
 *   this persistent data structure to see if it was already registered. If so,
 *   then we can skip the registration process, and we save quite a bit of
 *   time. In tests done locally, using NetPIPE between two Infiniband cards
 *   connected back-to-back, we saw throughput increase by 80% for messages of
 *   size ~1 megabyte after adding the lazy buffer unregistration.
 *
 *   While the current implementation does not evict any of the buffers from
 *   the persistent data structure, this may also be a good feature for a more
 *   robust implementation, since a long-running process could eat up a lot of
 *   memory if the data structure can grow unbounded.
 *
 *   * Using VAPI event completion is not as efficient as polling
 *
 *   The VAPI layer includes a nify mechanism for detecting when data has
 *   arrived called event completion. It's very similiar to signal handling in
 *   unix, but native to the Infiniband cards and VAPI. The programmer may
 *   register a function with VAPI to be called when a send or receive leaves
 *   from or arrives at a queue pair. The programmer's function is called in a
 *   new thread, passed some relevant data about the queue pair, and allowed to
 *   process the messages on the queue however it likes. So, for example, in
 *   this implementation we use the event completion mechanism to automatically
 *   copy the data from the eager buffers to the application's buffers,
 *   increment counters, etc.
 *
 *   Unfortunately, as we discovered with this implementation, the event
 *   completion mechanism is inherently slower than polling, at least in the
 *   current firmware and hardware, and that probably won't change. Before the
 *   event completion is generated, the VAPI layer has to actually recognize
 *   that some data has arrived. Once it does, it spawns a new thread to handle
 *   the event completion, which takes quite a bit of time. Alternatively, if
 *   the application itself polls on its data buffers, it can detect when data
 *   has arrived much sooner than if it waited for the event completion to be
 *   generated. Currently, this implementation has a latency of ~40 usec for
 *   small messages on our testbed, using event completion.  Running NetPIPE on
 *   our testbed using its Infiniband module shows us ~36 usec for event
 *   completion and ~16 usec for polling. So our implementation likely has 20
 *   usec of latency due soley to the delay in the event completion mechanism.
 *
 *   There are actually two distinct methods of polling. The first is just
 *   checking either a flag variable or even the last byte of a buffer in which
 *   data is expected to arrive. The key point here is to make sure the polling
 *   code somehow invalidates the memory it is checking so that the compiler
 *   will insert instructions to reload the memory every time it is checked.
 *   Use of the "volatile" modifier or simply calling a function between every
 *   poll are possible solutions.  We do the latter in this implementation, and
 *   call 'printf("")'. This has worked well, with GCC at least, since the
 *   compiler sees that the code will jump away and so inserts a memory load
 *   instruction. The printf call is cheap enough that it doesn't appear to
 *   hurt performance by a significant amount.
 *
 *   The second method of polling is to use the function VAPI_poll_cq to poll
 *   the completion queue directly. If the VAPI communications are signaled
 *   (comp_type == VAPI_SIGNALED), then a work completion entry will be added
 *   to the completion queue when a work request completes, and this function
 *   can be used to check for any new entries. NetPIPE tests indicate that
 *   using this method of polling only lags behind direct buffer polling by ~1
 *   usec. The only drawback is that you must use a type of communication that
 *   can be signaled, which means using sends and receives, or remote dma with
 *   immediate data, and these all have small message latencies around ~15
 *   usec. We can reduce latency even more by using remote dma writes, without
 *   immediate data, for communications, which give a minimum latency of ~6 usec
 *   in NetPIPE for our testbed. This method requires the use of direct buffer
 *   polling.
 *
 *   So, to get the minimum latency possible with Infiniband and VAPI, one will
 *   want to use remote dma writes for communications, and directly poll on the
 *   last byte of data to determine when it has completely arrived. The main
 *   issue that comes up here is what if the last byte of data is the same as
 *   what the buffer is already set to? E.g., the buffer is initialized to all
 *   zeroes, and the last byte of data is also a zero. The solution is to pad
 *   the data on the sending side with one more non-zero byte and check this
 *   byte instead of the last byte of data on the receiving side. Obviously,
 *   using remote dma writes for communications will require more setup time
 *   to exchange buffer addresses and memory keys, but the payoff is worth it
 *   in reduced latency.
 *
 * KNOWN ISSUES
 *
 * This code:
 *
 *    None (Uh, right... :)
 *
 * Mellanox drivers (BUILD_ID: thca-x86-0.0.6-rc7-build-001):
 *
 *   1. Invalid Completion Queue handle passed to completion event handler
 *
 *      This problem seems to only occur after running a code that uses this
 *      module (like NetPIPE), recompiling this code, and then running it
 *      again.  When the completion event handler is called by the drivers, a
 *      completion queue handle is passed as an argument.  Sometimes, aftter
 *      calling VAPI_poll_cq with this handle, the return value is an error
 *      indicating that the handle is not valid.  Upon further investigation,
 *      the invalid handle, which is actually an unsigned long, is the same as
 *      one of the completion queue handles from the previous run, before
 *      recompiling.  If the code is run again, without recompiling, then it
 *      runs fine.
 *
 *      I suspect that for whatever reason, the completion queue handles are
 *      remaining somewhere in the kernel modules' memory, and they are not
 *      properly reset the next time the code is run.
 *
 *      UPDATE:  Now trying Mellanox's Extended VAPI (EVAPI) completion event
 *      handler registration and clear functions.  So far, using these instead
 *      of the VAPI registration function seems to work fine.
 */

/* System includes
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <limits.h>
#include <sys/time.h>

/* MP_Lite includes 
 */
#include "globals.h"
#include "mplite.h"

/* Infiniband includes 
 */
#include "vapi.h"
#include "evapi.h"
#include "vapi_common.h"

/* Macros 
 */

/* Explicitly handle overflow of counters by wrapping around to zero
 */
#define COUNT_MAX INT_MAX
#define INC_WITH_WRAP(n) ( n == COUNT_MAX ? n=0 : n++ )
#define ADD_WITH_WRAP(n,m) ( COUNT_MAX-n < m ? ((m-(COUNT_MAX-n))-1) : (n+m) )

/* Tunable constants 
 */

/* Number of buffers allocated for each node for eager transfers.
 * Each buffer is EAGER_BUFFER_SIZE bytes big
 */
#define EAGER_BUFFERS_PER_NODE         4

/* Eager buffer size determines cutoff for beginning
 * rendezvous transfers
 */
#if 1
#define EAGER_BUFFER_SIZE            32768
#else
/* This essentially sets the eager buffer size limit to zero,
 * so only rendezvous is used
 */
#define EAGER_BUFFER_SIZE          (sizeof(struct MP_msg_header))
#endif

/* If defined, when a memory region is registered with IB for
 * a rendezvous send, do not deregister it after the send
 * completes. The memory region info will be maintained in
 * a data structure, and if the same memory region is needed
 * in the future, we will not have the penalty of reregistering.
 * The data struct containing the memory region info may
 * be of a fixed size, in which case deregistration of memory 
 * regions could be forced as new memory regions are registered.
 */
#define RNDZV_PERSISTENT_MEM_REGS


/* Number of unique IDs for keeping track of rendezvous transfers.
 * Because this ID is stored in the immediate data field of the
 * VAPI work requests and work completions, it must be limited to
 * a 32 bit value, as well as taking into consideration other uses
 * of the same 32 bit space.
 */
#define MP_TRANS_ID_SPACE          512

/* Use this macro to conveniently change the method used for 
 * blocking waits. Currently sched_yield() seems to give the best
 * performance. One could also use sleep(n) or usleep(n), etc.
 * Be aware that a function of some sort must be called, otherwise
 * the compiler will likely "optimize" the loop to not reload the
 * flags from memory and thus the loop will be inifinite.
 */ 
#define MP_BLOCK_METHOD       sched_yield()
 void busywait(){}
#define MP_BLOCK_METHOD       busywait()

/* End of tunable constants 
 */

/* Values for the rr & sr id element for VAPI send and receive requests 
 */
#define MP_SYNC                  0
#define MP_EAGER_RECEIVE_BUF     1
#define MP_EAGER_SEND_BUF        2
#define MP_UPDATE_BUFFERS_FREED  3
#define MP_RNDZV_SEND_RTR        4
#define MP_RNDZV_SEND_RTR_ACK    5
#define MP_RNDZV_SEND_DATA       6
#define MP_RNDZV_RECEIVE         7

/* Values for the ib_flag of the MP_Lite msg struct
 * for eager messages. 
 */
#define MP_WAITING_EAGER_RECV_COMPLETION   0
#define MP_EAGER_RECV_COMPLETE             1
#define MP_EAGER_RECV_MATCHED              2
#define MP_WAITING_EAGER_SEND_POST         3
#define MP_WAITING_EAGER_SEND_COMPLETION   4
#define MP_EAGER_SEND_COMPLETE             5

/* Values for the ib_flag of the MP_Lite msg struct
 * for rendezvous message.
 */
#define MP_RNDZV_INCOMPLETE        0
#define MP_RNDZV_COMPLETE          1

/* Struct used for rendezvous Ready-to-Receive messages 
 */
struct rndzv_msg_entry {
  void* addr;
  VAPI_rkey_t rkey;
  int tag;
  int nbytes;
  int trans_id; /* Transaction ID */
};

/* Enumeration and struct for persistent memory region list 
 */
typedef enum { RED, BLACK } rbtree_node_color;

struct rndzv_mr_tree_node_t {
  rbtree_node_color color;             /* Red-Black node color     */
  void* addr;                          /* Address is the key       */
  VAPI_mr_hndl_t hndl;                 /* Memory region handle     */
  VAPI_mrw_t     prop;                 /* Memory region properties */
  struct rndzv_mr_tree_node_t* p;      /* Parent pointer           */
  struct rndzv_mr_tree_node_t* left;   /* Left child pointer       */
  struct rndzv_mr_tree_node_t* right;  /* Right child pointer      */
};

/* Local prototypes 
 */

/* Initialization and shutdown
 */
void ib_init();
void bring_up_qps(void);
void modifyQP_RESET_to_INIT(const VAPI_qp_hndl_t* p_qp_hndl);
void modifyQP_INIT_to_RTR(const VAPI_qp_hndl_t* p_qp_hndl,
                          VAPI_qp_num_t qp_num, int lid);
void modifyQP_RTR_to_RTS(const VAPI_qp_hndl_t* p_qp_hndl);
void doSendRecvTest(int node, VAPI_qp_hndl_t qp_hndl, VAPI_cq_hndl_t s_cq_hndl,
                    VAPI_cq_hndl_t r_cq_hndl);
void ib_cleanup();

/* Event handling
 */
void ib_async_event_handler(VAPI_hca_hndl_t hca, VAPI_event_record_t* record,
                            void* data);
void ib_comp_event_handler(VAPI_hca_hndl_t hca, VAPI_cq_hndl_t cq, void* data);
int  ib_req_comp(VAPI_hca_hndl_t hca, VAPI_cq_hndl_t cq, VAPI_wc_desc_t* wc);

/* Memory regions
 */
void register_memory_region(void* start, int nbytes, VAPI_mr_hndl_t* hndl,
                            VAPI_mrw_t* mr_out, VAPI_mrw_acl_t acl);
void deregister_memory_region(VAPI_mr_hndl_t hndl);
void persist_mr_add(void* addr, VAPI_mr_hndl_t hndl, VAPI_mrw_t* prop);
int  persist_mr_get(void* addr, VAPI_mr_hndl_t* hndl, VAPI_mrw_t* prop);
void persist_mr_edit(void* addr, VAPI_mr_hndl_t hndl, VAPI_mrw_t* prop);
void persist_mr_del(void* addr, VAPI_mr_hndl_t* hndl, VAPI_mrw_t* prop);
int  persist_mr_delrand(void** addr, VAPI_mr_hndl_t* hndl, VAPI_mrw_t* prop);

/* Regular and Red-Black binary search tree
 */
void* tree_search(struct rndzv_mr_tree_node_t* node, void* addr);
void* tree_minimum(struct rndzv_mr_tree_node_t* node);
void* tree_maximum(struct rndzv_mr_tree_node_t* node);
void* tree_predecessor(struct rndzv_mr_tree_node_t* node);
void* tree_successor(struct rndzv_mr_tree_node_t* node);
void  tree_insert(struct rndzv_mr_tree_node_t** root, 
                  struct rndzv_mr_tree_node_t* newnode);
void* tree_delete(struct rndzv_mr_tree_node_t** root, 
                  struct rndzv_mr_tree_node_t* delnode);

void left_rotate(struct rndzv_mr_tree_node_t** root, 
                 struct rndzv_mr_tree_node_t* node);
void right_rotate(struct rndzv_mr_tree_node_t** root, 
                 struct rndzv_mr_tree_node_t* node);

void rb_insert(struct rndzv_mr_tree_node_t** root,
               struct rndzv_mr_tree_node_t* node);
void* rb_delete(struct rndzv_mr_tree_node_t** root,
               struct rndzv_mr_tree_node_t* delnode);
void rb_delete_fixup(struct rndzv_mr_tree_node_t** root,
                     struct rndzv_mr_tree_node_t* delnode);

/* Eager protocol
 */
void ib_eager_asend(struct MP_msg_entry* msg);
void ib_eager_arecv(struct MP_msg_entry* msg);
void ib_eager_handle_recv(VAPI_wc_desc_t* wc, int node);
void ib_eager_handle_send(VAPI_wc_desc_t* wc, int node);
void ib_eager_check_sends(int node, int* status);
void ib_eager_finish_recv(struct MP_msg_entry* msg);
void ib_eager_finish_send(struct MP_msg_entry* msg);
void ib_post_eager_recv(int src);
void ib_post_eager_send(struct MP_msg_entry* msg);
void ib_inc_eager_recv_freed(int node);

/* Rendezvous protocol
 */
void ib_rndzv_asend(struct MP_msg_entry* msg);
void ib_rndzv_arecv(struct MP_msg_entry* msg);
void ib_rndzv_finish(struct MP_msg_entry* msg);
void ib_rndzv_send_rtr_msg(void* addr, VAPI_rkey_t rkey, int tag, int nbytes, 
                           int trans_id, int node);
void ib_rndzv_send_rtr_ack(int node);
void ib_rndzv_send_data(void* data, int nbytes, int lkey, int trans_id,
                        int node, void* dest_addr, VAPI_rkey_t rkey, int tag);
void ib_post_rndzv_recv(int node);
void ib_rndzv_handle_recv(VAPI_wc_desc_t* wc, int node);
void ib_rndzv_handle_send(VAPI_wc_desc_t* wc, int node);
int  ib_get_trans_id(int node);
void ib_free_trans_id(int node, int id);

/* Functions pulled out of tcp.c and modified for use here
 */
void post(struct MP_msg_entry* msg, void** q);
void recv_from_q(struct MP_msg_entry* msg);
void send_to_q(struct MP_msg_entry* msg);

/* Shared filesystem communications functions
 */
void shfs_init();
void shfs_sync(int dest);
void shfs_barrier();
void shfs_send(void* buf, int nbytes, int dest);
void shfs_recv(void* buf, int nbytes, int src);


/* Global variables
 */
struct itimerval gprof_itimer;

void **recv_q, **send_q, **msg_q;

int portnum[MAX_PROCS+1];      /* Port numbers for TCP connections           */
int sd[MAX_PROCS];             /* TCP socket descriptors                     */

char** eager_send_buffers;     /* Array to hold list of eager send buffers   */
char** eager_recv_buffers;     /* Array to hold list of eager recv buffers   */
int* eager_next_send_buffer;   /* Index of next available send buffer        */
int** eager_recv_buffer_in_use;/* Array to keep track of which buffers in use*/
                               /* 0 => Free, 1 => In Use                     */
int* eager_sends;              /* Number of eager sends posted to each node  */
int* eager_recvs;              /* Number of eager recvs posted from each node*/
int* eager_loc_send_freed;     /* Number of local send buffers freed         */
int* eager_loc_recv_freed;     /* Number of local receive buffers freed      */
int* eager_rem_recv_freed;     /* Number of recv buffers freed at each node  */
void** eager_ib_recv_q;        /* Queue of eager receives posted for InfBand */
void** eager_user_recv_q;      /* Queue of eager receives posted for user    */
void** eager_send_q;           /* Queue of eager sends; user and InfBand shr */

VAPI_hca_id_t   hca_id;           /* HCA Identifier (string)                 */
VAPI_cq_hndl_t  sync_cq_hndl;     /* Synchronization completion queue handle */
VAPI_cq_hndl_t* send_cq_hndl;     /* Send completion queue handles           */
VAPI_cq_hndl_t* recv_cq_hndl;     /* Receive completion queue handles        */
EVAPI_compl_handler_hndl_t* send_comp_handler_hndl;
EVAPI_compl_handler_hndl_t* recv_comp_handler_hndl;
VAPI_qp_hndl_t* eager_qp_hndl;    /* Queue pair handle array for eager xfer  */
VAPI_qp_prop_t* eager_qp_prop;    /* Queue pair props array for eager xfer   */
VAPI_mr_hndl_t* eager_s_mr_hndl;  /* Eager send memory region handles        */
VAPI_mr_hndl_t* eager_r_mr_hndl;  /* Eager receive memory region handles     */
VAPI_lkey_t*    eager_s_lkey;     /* Eager send memory region lkeys          */
VAPI_lkey_t*    eager_r_lkey;     /* Eager receive memory region lkeys       */
VAPI_mr_hndl_t  eager_rem_mr_hndl;/* Memory region handle for RDMA access    */
VAPI_mr_hndl_t  eager_loc_mr_hndl;/* MR handle for local recv buffer freed   */
int             eager_rem_mr_rkey;/* Local memory region rkey for RDMA access*/
int             eager_loc_mr_lkey;/* Local MR key for recv bufer freed       */
int*            eager_rem_rkeys;  /* Other nodes' MR rkeys for RDMA access   */
void**          eager_rem_addrs;  /* Other nodes' rem_recv_freed addresses   */
VAPI_qp_hndl_t  sync_s_qp_hndl;   /* Queue pair handle for sync send         */
VAPI_qp_hndl_t  sync_r_qp_hndl;   /* Queue pair handle for sync receive      */
VAPI_qp_num_t   sync_s_qp_num;    /* Queue pair number for sync send         */
VAPI_qp_num_t   sync_r_qp_num;    /* Queue pair number for sync receive      */
char            sync_token;       /* Buffer to hold sync token               */
VAPI_mr_hndl_t  sync_mr_hndl;     /* Memory region handle for token buffer   */
int             sync_mr_lkey;     /* Local access key for sync token mem reg */
VAPI_rr_desc_t* sync_rr;          /* Receive request for syncs               */
VAPI_sr_desc_t* sync_sr;          /* Send request for syncs                  */
VAPI_hca_hndl_t hca_hndl;         /* HCA object handle                       */
VAPI_pd_hndl_t  pd_hndl;          /* Protection domain handle                */
IB_mtu_t        ib_mtu;           /* MTU for Infiniband HCA                  */
int             ib_port_num;      /* Physical port number                    */
IB_lid_t        ib_lid;           /* Local identifier (port addr in subnet)  */
VAPI_cqe_num_t  req_num_cqe;      /* Requested num of completion q elements  */
VAPI_cqe_num_t  sync_num_cqe;     /* Returned num of sync compl q elements   */
VAPI_cqe_num_t  send_num_cqe;     /* Returned num of send compl q elements   */
VAPI_cqe_num_t  recv_num_cqe;     /* Returned num of recv compl q elements   */
int             max_wr;           /* Max outstanding work reqs on compl q's  */
int             send_owr;         /* Outstanding work requests on send q     */
int             recv_owr;         /* Outstanding work requests on recv q     */
int             sync_send_node;   /* Number of node I send to in sync        */
int             sync_recv_node;   /* Number of node I recv from in sync      */
pthread_mutex_t io_mutex;         /* pThreads mutex for serializing file I/O */
pthread_mutex_t hndl_e_recv_mutex;/* pThreads mutex for handling eager recvs */
pthread_mutex_t msg_id_mutex;     /* pThreads mutex for changing curr msg id */
pthread_mutex_t post_msg_mutex;   /* pThreads mutex for postings msgs to Qs  */
pthread_mutex_t post_e_recv_mutex;/* pThreads mutex for posting eager recvs  */

struct itimerval itimer;

/* Additional globals required for rendezvous
 */
VAPI_qp_hndl_t* rndzv_qp_hndl;    /* Queue pair handles    */
VAPI_qp_prop_t* rndzv_qp_prop;    /* Queue pair properties */

/* These are values that will be used in the immediate data
 * field of RDMA writes. The imm. data field stores 32 bit 
 * values, so keep that in mind when setting values below
 */
#define  MP_RNDZV_READY_TO_RECEIVE  0
#define  MP_TRANS_MIN_ID            1
#define  MP_TRANS_MAX_ID            (MP_TRANS_MIN_ID+MP_TRANS_ID_SPACE-1)

/* Error checking to make sure we're not going over
 * the maximum 32 bit value
 */
#if MP_TRANS_MAX_ID > 0xffffffff
#error
#error *****************************************
#error * MP_TRANS_ID_SPACE is set too large,   *
#error * you must descrease its value in ib.c. *
#error *****************************************
#error
#endif

/* Keep track of the available and in-use transaction ids
 * for each node. 0 in list means the id is available, 1 
 * means the id is in use. current_trans_id keeps track
 * of the last transaction id used.
 */

short** trans_id_list;
int* current_trans_id;

/* MP_msg_entry message queues for keeping track of current
 * rendezvous transfers
 */
void** rndzv_recv_q;
void** rndzv_send_q;
void** rndzv_posted2ib_q;

/* Mutex for checking the rndzv_send_q for a message in
 * ib_rndzv_handle_recv and ib_rdnzv_asend
 */
pthread_mutex_t rndzv_send_q_mutex;

/* The array and memory region info is used for incoming
 * Ready-to-Receive messages from other nodes. When we
 * get a completion of this type we copy the data from the 
 * corresponding entry in the array.
 */
struct rndzv_msg_entry* rndzv_msgs_in;
VAPI_mr_hndl_t          rndzv_msgs_in_mr_hndl;
int                     rndzv_msgs_in_mr_rkey;

/* This will be an array from which we send outgoing
 * Ready-to-Receive messages.
 */
struct rndzv_msg_entry* rndzv_msgs_out;
VAPI_mr_hndl_t          rndzv_msgs_out_mr_hndl;
int                     rndzv_msgs_out_mr_lkey;

/* Set to 0 each time we send a RTR message to a node. That node
 * acknowledges the RTR message by setting it to 1 via RDMA write,
 * and we know we can send the next RTR message.
 */
int*                    rndzv_msgs_ack;
VAPI_mr_hndl_t          rndzv_msgs_ack_mr_hndl;
int                     rndzv_msgs_ack_mr_rkey;
int                     rndzv_msgs_ack_mr_lkey;

/* Address and rkey info for each node's incoming RTR acks
 */
void**                  rndzv_msgs_ack_rem_addrs;
int*                    rndzv_msgs_ack_rem_rkeys; 

/* Address and rkey info for each node's incoming RTR
 * message area.
 */
int*                    rndzv_msgs_rem_rkeys;
void**                  rndzv_msgs_rem_addrs;

/* nilnode is used in the binary search tree impletation
 * as a subsitution for NULL in order to simplify the code.
 * rndzv_mr_tree_root is the root pointer of the tree.
 */
struct rndzv_mr_tree_node_t nilnode = { BLACK };
struct rndzv_mr_tree_node_t* rndzv_mr_tree_root=&nilnode;

/* End of rendezvous-specific globals
 */

/* Variables for collecting statistics
 */
int eager_sends_immediate;      /* Eager sends sent immediately              */
int eager_sends_queued;         /* Eager sends queued                        */
int eager_recvs_immediate;      /* Eager receives received immediately       */
int eager_recvs_queued;         /* Eager receives queued                     */
int eager_waits_recv_blocked;   /* Waits on eager receives that had to block */
int eager_waits_send_blocked;   /* Waits on eager sends that had to block    */
int eager_waits_send_posted;    /* Waits on eager sends that posted the send */

int rndzv_sends_immediate;      /* Rendezvous sends sent immediately         */
int rndzv_sends_queued;         /* Rendezvous sends queued                   */
int rndzv_recvs_blocking_rtr;   /* Rendezvous RTR sends that blocked         */
int rndzv_waits_blocked;        /* Waits on rndzv sends + recvs that blocked */

int already_blocking = 0;

void Block_Signals()
{
  pthread_mutex_lock(&io_mutex);
}

void Unblock_Signals()
{
  pthread_mutex_unlock(&io_mutex);
}

/* Not implemented for now, but need the definition because stall_check()
 * in mpcore.c calls it.
 */
void dump_msg_state() {};

/* Function definitions
 */
void MP_Init(int* pargc, char*** pargv)
{
   FILE *fd;
   int iproc, i, j, p, n, r, node, next, prev, tmp;
   int mypid, nic, nics, one = 1, socket_flags, is_ip, ndots = 0;
   int port = TCP_PORT, listen_socket;
   char *ptr, garb[100], command[100];
   char cbuf[100]; 
   struct hostent *host_entry;
   struct in_addr ipadr;

   errno = -1;

   /* Set some global variable defaults
    */
   strncpy(hca_id, "InfiniHost0", HCA_MAXNAME);
   sync_token = '\0';
   ib_mtu = MTU1024;
   ib_port_num = 1;
   req_num_cqe = 1000;
   max_wr = 1000;

   gethostname(myhostname,100);
   ptr = strstr(myhostname,".");
   if (ptr != NULL && ptr != myhostname) strcpy(ptr,"\0");
   mypid = getpid();


   /* Open a .node.myhostname.mypid log file for each node.  After node 
    * numbers are assigned, this will be moved to .node# .
    */

   sprintf(filename, ".node.%s.%d", myhostname, mypid);
   mylog = fopen( filename, "w" );
   if( !mylog ) errorxs(0,"Couldn't open the log file %s",filename);

   /* Open .mplite.config and read in nprocs and hostnames.
    * Set myproc if possible, interface[0..nprocs-1][0..nics-1]
    */

   fd = fopen(".mplite.config","r");
   if( !fd ) errorx(0,"Couldn't open the .mplite.config file");

   fgets(garb, 90, fd);
   sscanf(garb,"%d",&nprocs);
   fgets(garb, 90, fd);
   sscanf(garb,"%d",&nics);
   fgets(garb, 90, fd);
   sscanf(garb, "%s", prog_name);

   if( nics > 1 ) 
     errorx(0, "Infiniband MP_Lite does not currently support multiple hca's");

   for(i=0; i<nprocs; i++) {
      interface[i][0] = malloc(100*sizeof(char));
      fscanf(fd,"%d %s\n",&iproc, interface[i][0]);

      /* If these are IP numbers, convert into dot.style.interface.addresses.
       * If it is an alias, asign the real hostname instead.
       */

      is_ip = true;
      for( j=0; j<strlen(interface[i][0]); j++) {
        if( ! isdigit(interface[i][0][j]) && interface[i][0][j] != '.' )
          is_ip = false;
        if( interface[i][0][j] == '.' ) ndots++;
      }
      if( ndots < 3 ) is_ip = false;
      
      if( is_ip ) {
        
        inet_aton( interface[i][0], &ipadr );
        host_entry = gethostbyaddr( (const void*) &ipadr, 4, AF_INET );
        strcpy( interface[i][0], host_entry->h_name );
        
      } else {   /* Convert any aliases */
        
        host_entry = gethostbyname( interface[i][0] );
        strcpy( hostname[i], host_entry->h_name );
        
      }
      
      /* Cut the hostname down
       */
      
      strcpy(hostname[i], interface[i][0]);
      ptr = strstr(hostname[i],".");
      if (ptr != NULL && ptr != hostname[i]) strcpy(ptr,"\0");

      /* Assign myproc
       */

      if( !strcmp(hostname[i],myhostname) )
         if( myproc < 0 ) myproc = i;

   }
   fclose(fd);

   if( myproc < 0 ) {
      errorxs(myproc,"%s matched no entry in .mplite.config",myhostname);
   }

      /* Create a listen socket. */

   listen_socket = create_listen_socket( port, buffer_size );

   /* At this point, all nodes know their myproc.
    */

   /* Handle first contact 
    *   --> Node 0 contacts port TCP_PORT on each node.
    *   --> If a time-out occurs, node 0 checks for an appropriate 
    *       .changeport... file to get the initial port number.
    *   --> Then node 0 connects to each other node and sends the list
    *       main port numbers.
    */

   initialize_status_file();

   if( myproc == 0 ) {
      portnum[0] = port;
      for( node=1; node<nprocs; node++) {

         portnum[node] = TCP_PORT;

         sprintf(cbuf, "node %d port %d", node, portnum[node]);
         set_status(0,"contacting", cbuf);

         sd[node] = connect_to(node, portnum[node], 0, buffer_size);

         if( sd[node] <= 0 ) {  /* Bad port, contact via .changeport file */
            lprintf("\n--> Failed to connect, use .changeport - ");

            /* Check for a .changeport.hostname[node].port# file. */

            while( ++portnum[node] < TCP_PORT_MAX ) {

               sprintf(cbuf, "node %d port %d", node, portnum[node]);
               set_status(0,"changeport", cbuf);

               sprintf(filename, ".changeport.%s.%d\0", hostname[node], 
                                 portnum[node]);
               fd = fopen( filename, "r" );
               if( fd ) {
                  fclose( fd );

                  lprintf(" connected via a file to %d\n", portnum[node]);
                  fprintf(stderr," connected via a file to %d\n",portnum[node]);

                  sprintf( command, "rm -f %s\0", filename);
                  system( command );

                  break;
               }

            }

            if( portnum[node] == TCP_PORT_MAX ) {  /* No .changeport file */

               errorx(node, "Bad port and no .changeport file");

            } else {

               set_status(0,"contacting", cbuf);

               sd[node] = connect_to(node, portnum[node], 0, buffer_size);
               if( sd[node] <= 0 ) {
                  errorx(node, "Found .changeport file, but couldn't connect");
               }

            }
         }

      }

      lprintf("\nNode 0 sending port numbers to all the nodes\n");
      for( node=1; node<nprocs; node++) {
         portnum[nprocs] = node;
         n = send(sd[node], (char *)portnum, (nprocs+1)*sizeof(int), 0);
      }

   } else {    /* not master node */

      sd[0] = accept_a_connection_from(0, listen_socket, 0);

      /*
        TCP_NODELAY may or may not (AIX) be propagated to accepted
        sockets, so attempt to set it again.
      */

      r = setsockopt(sd[0], IPPROTO_TCP, TCP_NODELAY, 
                     (char *)&one, sizeof(one));
      if( r != 0) errorx(errno,"MP_Init()-setsockopt(NODELAY)");
      


      lprintf("Getting port numbers from node 0 - ");

      n = recv(sd[0], (char *)portnum, (nprocs+1)*sizeof(int), 0);

      if( portnum[nprocs] != myproc ) {
         lprintf("Node 0 thinks I'm node %d\n", portnum[nprocs]);
         errorx(portnum[nprocs], "Node 0 thinks I'm the wrong node number");
      }
      sprintf(cbuf, "node %d port %d connected to node 0", 
              myproc, portnum[myproc]);
      set_status(0,"connectd", cbuf);
      lprintf("done");
   }

   /* Now we all know myproc, so move .node.myhostname.mypid to .node#
    * where # is myproc.
    */

   sprintf(command, "mv .node.%s.%d .node%d", myhostname, mypid, myproc);
   system( command );

   lprintf("\n%s PID %d is node %d of %d connected to port %d\n\n", 
            myhostname, mypid, myproc, nprocs, portnum[myproc]);
   for(i=0; i<nprocs; i++) {
      lprintf("   %d ==> ", i);
      lprintf(" %s ", interface[i][0]);
      lprintf(" port %d\n", portnum[i]);
   }

   /* Do the rest of the handshaking  
    *   - Each node accepts connections from lesser nodes
    *   - Each node then initiates connections to greater nodes
    *   - A 'go' signal is sent around in between to keep things in sync
    */

   if( myproc > 0 ) {
      for( node=1; node<myproc; node++)
         sd[node] = accept_a_connection_from(node, listen_socket, 0);

      n = recv(sd[myproc-1], (char *)&j, sizeof(int), 0); /* go signal */
        if( n < 0 ) errorx(errno,"MP_Init() - recv() go signal error");

      for( node=myproc+1; node<nprocs; node++)
        sd[node] = connect_to(node, portnum[node], 0, buffer_size);

   }

   if( myproc < nprocs-1 ) {            /* send go signal for next proc */
      n = send(sd[myproc+1], (char *)&j, sizeof(int), 0);
        if( n < 0 ) errorx(errno,"MP_Init() - send() go signal error");
   }
   close(listen_socket);
   
   /* Set up message lists
    */

   for(i=0; i<MAX_MSGS; i++) {
     msg_list[i] = malloc(sizeof(struct MP_msg_entry));
     if( ! msg_list[i] ) errorx(0, "ib.c MP_msg_entry malloc() failed");
     *msg_list[i] = msg_null;
   }

   msg_q  = (void **) malloc( (nprocs+1)*sizeof(void *) ) + 1;

   for( i = -1; i<nprocs; i++) {
      msg_q[i] = NULL;
   }

   /* NULL => Default (fast mutex, no check for recursive locks, etc)
    */
   pthread_mutex_init(&io_mutex,           NULL); 
   pthread_mutex_init(&hndl_e_recv_mutex,  NULL);
   pthread_mutex_init(&msg_id_mutex,       NULL);
   pthread_mutex_init(&post_msg_mutex,     NULL);
   pthread_mutex_init(&post_e_recv_mutex,  NULL);
   pthread_mutex_init(&rndzv_send_q_mutex, NULL);

   shfs_init();

   ib_init();

   /* For gprof-thread stuff
    */
   getitimer(ITIMER_PROF, &gprof_itimer);

   trace_set0();

   /* Cntl-c or kill -HUP will dump the message queue state then exit.
    */

#if defined (POSIX_SIGNALS)

   sigact.sa_handler = dump_and_quit;
   sigaction(SIGINT, &sigact, NULL);
   sigaction(SIGHUP, &sigact, NULL);

#elif defined (_BSD_SIGNALS)

   signal(SIGINT, dump_and_quit);
   signal(SIGHUP, dump_and_quit);

#endif

   /* Set up a default mask for the global operations that include all procs
    */
   MP_all_mask = malloc(nprocs*sizeof(int));
   if( ! MP_all_mask ) errorx(0,"malloc(MP_all_mask) failed");
   for( i=0; i<nprocs; i++) MP_all_mask[i] = 1;

   /* Initialize the twiddle increment
    */
   twiddle( 0.0 );

   /* Initialize statistics counters
    */
   n_send = nb_send = n_send_buffered = nb_send_buffered = n_send_packets = 0;
   n_recv = nb_recv = n_recv_buffered = nb_recv_buffered = n_recv_packets = 0;

   eager_sends_immediate = eager_sends_queued = 0;
   eager_recvs_immediate = eager_recvs_queued = 0;
   eager_waits_recv_blocked = eager_waits_send_blocked = 0;
   eager_waits_send_posted = 0;

   rndzv_sends_immediate = rndzv_sends_queued = 0;
   rndzv_recvs_blocking_rtr = rndzv_waits_blocked = 0;

   if( myproc == 0 ) 
      fprintf(stderr, "----------  Done handshaking  ----------\n");
   lprintf("Done with all the handshaking, exiting MP_Init()\n");
   lprintf("------------------------------------------------\n");
   set_status(0, "running", "");

}

void MP_Finalize()
{
   int node, nic;
   int i;

      /* Close all sockets.  The side with the dynamic port must be closed
       * first, leaving it in a TIMEWAIT state for an ack for ~30 seconds.
       * Otherwise, the known ports would be tied up, forcing negotiation
       * for the next mprun if it was done within ~30 seconds.
       */

   MP_Sync();

   for( node=myproc+1; node<nprocs; node++) 
      close(sd[node]);

   MP_Sync();

   for( node=0; node<myproc; node++)
      close(sd[node]);

   sleep(1);

   ib_cleanup();

   pthread_mutex_destroy(&io_mutex);
   pthread_mutex_destroy(&hndl_e_recv_mutex);
   pthread_mutex_destroy(&msg_id_mutex);
   pthread_mutex_destroy(&post_msg_mutex);
   pthread_mutex_destroy(&post_e_recv_mutex);
   pthread_mutex_destroy(&rndzv_send_q_mutex);

   set_status(0, "completed", "");
   lprintf("------------------------------------------------\n");
   dump_msg_state();
   lprintf("************************************************\n");

   lprintf("%d sends with %d partially buffered\n"
           "   (%f%% of traffic buffered, %d packets)\n", n_send,
           n_send_buffered, (100.0*nb_send_buffered)/nb_send, n_send_packets);
   lprintf("%d recvs with %d partially buffered\n"
           "   (%f%% of traffic buffered, %d packets)\n", n_recv, 
           n_recv_buffered, (100.0*nb_recv_buffered)/nb_recv, n_recv_packets);
   lprintf("Eager stats: %d immediate sends, %d queued sends\n"
           "             %d immediate recvs, %d queued receives\n"
           "             %d waits on send blocked, %d waits on recv blocked\n"
           "             %d sends were posted in the call to wait\n",
           eager_sends_immediate, eager_sends_queued, eager_recvs_immediate,
           eager_recvs_queued, eager_waits_send_blocked, eager_waits_recv_blocked,
           eager_waits_send_posted);
   lprintf("Rendezvous stats: %d immediate sends, %d queued sends\n"
           "                  %d ready-to-receive sends blocked\n"
           "                  %d waits on send or receive blocked\n",
           rndzv_sends_immediate, rndzv_sends_queued, rndzv_recvs_blocking_rtr,
           rndzv_waits_blocked);

   lprintf("************************************************\n");
   fclose(mylog);
   trace_dump();         /* Only active if compiled with -DTRACE */
}

void MP_Sync()
{
  VAPI_ret_t      ret;
  VAPI_wc_desc_t  wc;
  int             i;

  dbprintf("> MP_Sync()\n");

  /* Post two more receives.  We should have another already posted, either
   * from init or the previous sync, and we'll use two receives in this
   * call.
   */

  for(i=0; i<2; i++) {

    ret = VAPI_post_rr(hca_hndl, sync_r_qp_hndl, sync_rr);
    if(ret != VAPI_OK) {
      fprintf(stderr, "MP_Sync() - Error posting receive %d: %s\n", i,
              VAPI_strerror(ret));
      lprintf("MP_Sync() - Error posting receive %d: %s\n", i,
              VAPI_strerror(ret));
      dump_and_quit();
    }
    
  }

  /* Send token around twice
   */
  for(i=0; i<2; i++) {

    /* If I'm proc 0 start the sync, otherwise wait for receive
     */
    if(myproc == 0) {
      
      /* Post first send
       */
      
      ret = VAPI_post_sr(hca_hndl, sync_s_qp_hndl, sync_sr);
      if(ret != VAPI_OK) {
        fprintf(stderr, "MP_Sync() - Error posting send %d: %s\n", i, 
                VAPI_strerror(ret));
        lprintf("MP_Sync() - Error posting send %d: %s\n", i,
                VAPI_strerror(ret));
        dump_and_quit();
      }
      
      /* Wait for first send to complete
       */
      do
        ret = VAPI_poll_cq(hca_hndl, sync_cq_hndl, &wc);
      while(ret == VAPI_CQ_EMPTY);
      
      if(ret != VAPI_OK) {
        fprintf(stderr,
                "MP_Sync() - Error polling for send %d completion: %s\n", i,
                VAPI_strerror(ret));
        lprintf("MP_Sync() - Error polling for send %d completion: %s\n", i,
                VAPI_strerror(ret));
        dump_and_quit();
      }
    
      /* Wait for the incoming token
       */
      do
        ret = VAPI_poll_cq(hca_hndl, sync_cq_hndl, &wc);
      while(ret == VAPI_CQ_EMPTY);
    
      if(ret != VAPI_OK) {
        fprintf(stderr,
                "MP_Sync() - Error polling for receive %d completion: %s\n", i,
                VAPI_strerror(ret));
        lprintf("MP_Sync() - Error polling for receive %d completion: %s\n", i,
                VAPI_strerror(ret));
        dump_and_quit();
      }
    
    } else {

      /* Wait for the incoming token
       */
      do
        ret = VAPI_poll_cq(hca_hndl, sync_cq_hndl, &wc);
      while(ret == VAPI_CQ_EMPTY);
    
      if(ret != VAPI_OK) {
        fprintf(stderr,
                "MP_Sync() - Error polling for receive %d completion: %s\n", i,
                VAPI_strerror(ret));
        lprintf("MP_Sync() - Error polling for receive %d completion: %s\n", i,
                VAPI_strerror(ret));
        dump_and_quit();
      }
    
      /* Post first send
       */
      ret = VAPI_post_sr(hca_hndl, sync_s_qp_hndl, sync_sr);
      if(ret != VAPI_OK) {
        fprintf(stderr, "MP_Sync() - Error posting receive %d: %s\n", i,
                VAPI_strerror(ret));
        lprintf("MP_Sync() - Error posting receive %d: %s\n", i,
                VAPI_strerror(ret));
        dump_and_quit();
      }

      /* Wait for first send to complete
       */
      do
        ret = VAPI_poll_cq(hca_hndl, sync_cq_hndl, &wc);
      while(ret == VAPI_CQ_EMPTY);
    
      if(ret != VAPI_OK) {
        fprintf(stderr,
                "MP_Sync() - Error polling for send %d completion: %s\n", i,
                VAPI_strerror(ret));
        lprintf("MP_Sync() - Error polling for send %d completion: %s\n", i,
                VAPI_strerror(ret));
        dump_and_quit();
      }
    
    }

  }

  dbprintf("< MP_Sync()\n");
}

void MP_Wait(int *msg_id)
{
  struct MP_msg_entry* msg;

  dbprintf(">MP_Wait(%d)\n", *msg_id);
  if( *msg_id == -1 ) return;

  msg = msg_list[*msg_id];

  if(msg->send_flag == 3) {

    /* Message was sent to myself, do nothing here
     */
    dbprintf("  I sent this message to myself\n");

  } else if(msg->recv_flag == 1 && msg->src == myproc) {

    /* Receive a message from to myself
     */
    dbprintf("  Receiving a message I sent to myself\n");

    recv_from_q(msg);


  } else if(msg->nbytes < EAGER_BUFFER_SIZE-sizeof(struct MP_msg_header) ) {

    /* Eager send or receive 
     */
    if(msg->send_flag == 1) {

      /* Send 
       */
      ib_eager_finish_send(msg);

    } else {

      /* Receive 
       */
      ib_eager_finish_recv(msg);

    }

  } else {

    /* Rendezvous send or receive 
     */

    ib_rndzv_finish(msg);

  }


  /* These can be retrieved using the MP_Get_Last_Source()/Tag()/Size() 
   * functions. */

  last_src = msg->src;
  last_tag = msg->tag;
  last_size = msg->nbytes;
  
  if( msg->send_flag > 0 ) n_send_packets += msg->stats;
  else                     n_recv_packets += msg->stats;

  if( msg->send_flag != 3 ) {
      msg->buf = NULL;       /* Mark the msg header as free for reuse */
  }

  *msg_id = -1;
  trace_end( msg->trace_id, msg->stats, msg->send_flag);
  dbprintf("<MP_Wait()\n");
}

void MP_ASend(void *buf, int nbytes, int dest, int tag, int *msg_id)
{
  VAPI_ret_t          ret;
  struct MP_msg_entry* msg;

  dbprintf("\n>MP_ASend(& %d %d %d)\n", nbytes, dest, tag);

  range_check(nbytes, dest);

  msg = create_msg_entry( buf, nbytes, myproc, dest, tag);
  *msg_id = msg->id;
  msg->trace_id = trace_start( nbytes, dest);

  if( dest == myproc ) {      /* Handle sending to self seperately */

    send_to_q( msg );

  } else if(msg->nbytes < (EAGER_BUFFER_SIZE-sizeof(struct MP_msg_header))) { 

    /* Do an eager send */

    ib_eager_asend(msg);

  } else {

    /* Do a rendezvous receive */

    ib_rndzv_asend(msg);

  }

  n_send++; nb_send += nbytes;
  dbprintf("<MP_ASend()\n");

}

void MP_Send(void *buf, int nbytes, int dest, int tag)
{
  int msg_id;

  MP_ASend(buf, nbytes, dest, tag, &msg_id);
  MP_Wait(&msg_id);
}

void MP_ARecv(void *buf, int nbytes, int src, int tag, int *msg_id)
{
  VAPI_ret_t       ret;
  struct MP_msg_entry* msg;

  dbprintf("\n>MP_ARecv(& %d %d %d)\n", nbytes, src, tag );
 
  range_check(nbytes, src);

  msg = create_msg_entry( buf, nbytes, src, -1, tag);
  *msg_id = msg->id;
  msg->trace_id = trace_start( -nbytes, src);

  if( src == myproc ) {    /* Let MP_Wait() handle it */


  } else if(msg->nbytes < (EAGER_BUFFER_SIZE-sizeof(struct MP_msg_header))) { 

    /* Do an eager receive */
    
    ib_eager_arecv(msg);

  } else {

    /* Do a rendezvous receive */
    
    ib_rndzv_arecv(msg);

  }

  n_recv++; nb_recv += nbytes;
  dbprintf("<MP_ARecv()\n");

}

void MP_Recv(void *buf, int nbytes, int dest, int tag)
{
  int msg_id;

  MP_ARecv(buf, nbytes, dest, tag, &msg_id);
  MP_Wait(&msg_id);
}

void ib_init()
{
  /* Assuming we have 1 HCA, 1 PD, nprocs-1 QP's */

  int                  i;           /* Loop index                            */
  int                  j;           /* Loop index                            */
  VAPI_ret_t           ret;         /* Return value                          */
  VAPI_qp_init_attr_t  qp_init_attr;/* Initialization attributes             */
  VAPI_qp_attr_mask_t  qp_attr_mask;/* Specifies qp attributes to modify     */
  VAPI_qp_attr_t       qp_attr;     /* Queue pair attributes                 */
  VAPI_qp_prop_t       sync_qp_prop;/* Queue pair properties                 */
  VAPI_hca_port_t      hca_port;    /* HCA Port attributes                   */
  VAPI_mrw_t           mr_out;      /* Returned memory region properties     */
  void*                addr;        /* Generic pointer                       */

  dbprintf("Infiniband Initialization\n");

  /* Open HCA just in case it was not opened by system earlier */

  ret = VAPI_open_hca(hca_id, &hca_hndl); 

  /* Get HCA handle */

  ret = EVAPI_get_hca_hndl(hca_id, &hca_hndl);
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error opening Infiniband HCA: %s\n", VAPI_strerror(ret));
    lprintf("Error opening Infiniband HCA: %s\n", VAPI_strerror(ret));
    exit(-1);
  } 
  dbprintf("  Opened HCA\n");

  /* Get HCA properties */

  ret = VAPI_query_hca_port_prop(hca_hndl, (IB_port_t)ib_port_num, 
                                 (VAPI_hca_port_t *)&hca_port);
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error querying Infiniband HCA: %s\n", VAPI_strerror(ret));
    lprintf("Error querying Infiniband HCA: %s\n", VAPI_strerror(ret));
    exit(-1);
  }
  dbprintf("  Queried HCA\n");

  ib_lid = hca_port.lid;
  dbprintf("    lid = %d\n", ib_lid);

  /* Allocate Protection Domain */

  ret = VAPI_alloc_pd(hca_hndl, &pd_hndl);
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error allocating PD: %s\n", VAPI_strerror(ret));
    lprintf("Error allocating PD: %s\n", VAPI_strerror(ret));
    exit(-1);
  }
  dbprintf("  Allocated Protection Domain\n");

  /* Create sync completion queue */
  
  ret = VAPI_create_cq(hca_hndl, req_num_cqe, &sync_cq_hndl, &sync_num_cqe);
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error creating sync CQ: %s\n", VAPI_strerror(ret));
    lprintf("Error creating sync CQ: %s\n", VAPI_strerror(ret));
    exit(-1);
  } 
  dbprintf("  Created sync Completion Queue with %d elements\n", sync_num_cqe);

  /* Create send and receive completion queues */
  
  send_cq_hndl = malloc( sizeof(VAPI_cq_hndl_t) * nprocs );
  recv_cq_hndl = malloc( sizeof(VAPI_cq_hndl_t) * nprocs );

  for(i=0; i<nprocs; i++) if(i != myproc) {
    ret = VAPI_create_cq(hca_hndl, req_num_cqe, &send_cq_hndl[i],
                         &send_num_cqe);
    if(ret != VAPI_OK) {
      fprintf(stderr, "Error creating send CQ: %s\n", VAPI_strerror(ret));
      lprintf("Error creating send CQ: %s\n", VAPI_strerror(ret));
      exit(-1);
    }
    dbprintf("  Created Send Completion Queue for node %d with %d elements\n", 
             i, send_num_cqe);

    ret = VAPI_create_cq(hca_hndl, req_num_cqe, &recv_cq_hndl[i], 
                         &recv_num_cqe);
    if(ret != VAPI_OK) {
      fprintf(stderr, "Error creating receive CQ: %s\n", VAPI_strerror(ret));
      lprintf("Error creating receive CQ: %s\n", VAPI_strerror(ret));
      exit(-1);
    }
    dbprintf("  Created Recv Completion Queue for node %d with %d elements\n", 
             i, recv_num_cqe);

  }

  /* Now we need to create (nprocs-1) Queue Pairs, so we have a direct 
   * Reliable Connection to each node over Infiniband.  There may be other
   * possibilities, such as Reliable Datagrams, which only require one
   * QP per node, but will involve more work implementing in MP_Lite
   */

  /* Create arrays to hold QP handles and properties for the eager 
   * communications channels
   */
  eager_qp_hndl = malloc( sizeof(VAPI_qp_hndl_t) * nprocs );
  eager_qp_prop = malloc( sizeof(VAPI_qp_prop_t) * nprocs );

  /* Create arrays to hold QP handles and properties for the rendezvous
   * communications channels
   */
  rndzv_qp_hndl = malloc( sizeof(VAPI_qp_hndl_t) * nprocs );
  rndzv_qp_prop = malloc( sizeof(VAPI_qp_prop_t) * nprocs );

  for(i=0; i<nprocs; i++) if(i != myproc) {

    /* Set initialization attributes */

    qp_init_attr.pd_hndl            = pd_hndl;/* Protection domain handle   */
    qp_init_attr.rdd_hndl           = 0;/* Reliable datagram domain handle  */
    qp_init_attr.rq_cq_hndl         = recv_cq_hndl[i];/* CQ handle for RQ   */
    qp_init_attr.sq_cq_hndl         = send_cq_hndl[i];/* CQ handle for SQ   */
    qp_init_attr.cap.max_oust_wr_rq = max_wr;/* Max outstanding WR on RQ    */
    qp_init_attr.cap.max_oust_wr_sq = max_wr;/* Max outstanding WR on SQ    */
    qp_init_attr.cap.max_sg_size_rq = 1;/* Max scatter/gather entries on RQ */
    qp_init_attr.cap.max_sg_size_sq = 1;/* Max scatter/gather entries on SQ */
    qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR;/* Signaling type  */
    qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR;/* Signaling type  */
    qp_init_attr.ts_type            = IB_TS_RC; /* Transmission type        */

    /* Create eager queue pair for node i */

    ret = VAPI_create_qp(hca_hndl, 
                         &qp_init_attr,
                         &eager_qp_hndl[i], 
                         &eager_qp_prop[i]);

    if(ret != VAPI_OK) {
      fprintf(stderr, "Error creating eager QP: %s\n", VAPI_strerror(ret));
      lprintf("Error creating eager QP: %s\n", VAPI_strerror(ret));
      exit(-1);
    } 
    
    dbprintf("  Created eager Queue Pair for node %d\n", i);
    dbprintf("    QP num: %d\n", eager_qp_prop[i].qp_num);
    dbprintf("    Max oustanding WRs in the SQ: %d\n", 
             eager_qp_prop[i].cap.max_oust_wr_sq);
    dbprintf("    Max oustanding WRs in the RQ: %d\n", 
             eager_qp_prop[i].cap.max_oust_wr_rq);
    dbprintf("    Max scatter/gather elts in a WR in the SQ: %d\n", 
             eager_qp_prop[i].cap.max_sg_size_sq);
    dbprintf("    Max scatter/gather elts in a WR in the RQ: %d\n", 
             eager_qp_prop[i].cap.max_sg_size_rq);

    /* Create rendezvous queue pair for node i */

    ret = VAPI_create_qp(hca_hndl,
                         &qp_init_attr,
                         &rndzv_qp_hndl[i],
                         &rndzv_qp_prop[i]);

    if(ret != VAPI_OK) {
      fprintf(stderr, "Error creating rndzv QP: %s\n", VAPI_strerror(ret));
      lprintf("Error creating rndzv QP: %s\n", VAPI_strerror(ret));
      exit(-1);
    } 
    
    dbprintf("  Created rndzv Queue Pair for node %d\n", i);
    dbprintf("    QP num: %d\n", rndzv_qp_prop[i].qp_num);
    dbprintf("    Max oustanding WRs in the SQ: %d\n", 
             rndzv_qp_prop[i].cap.max_oust_wr_sq);
    dbprintf("    Max oustanding WRs in the RQ: %d\n", 
             rndzv_qp_prop[i].cap.max_oust_wr_rq);
    dbprintf("    Max scatter/gather elts in a WR in the SQ: %d\n", 
             rndzv_qp_prop[i].cap.max_sg_size_sq);
    dbprintf("    Max scatter/gather elts in a WR in the RQ: %d\n", 
             rndzv_qp_prop[i].cap.max_sg_size_rq);

  }

  /* Create arrays to hold 2 (or 1 if nprocs==2) QP handles and 
   * properties for the synchronization channels.  We will use one QP
   * for sending a token and second (or possibly the first) to receive
   * the token.  This is modeled after tcp_sync in tcp.c.
   */

  /* Set initialization attributes */
  qp_init_attr.pd_hndl            = pd_hndl;/* Protection domain handle*/
  qp_init_attr.rdd_hndl           = 0;/* Reliable datagram domain handle  */
  qp_init_attr.rq_cq_hndl         = sync_cq_hndl;/* CQ handle for RQ      */
  qp_init_attr.sq_cq_hndl         = sync_cq_hndl;/* CQ handle for SQ      */
  qp_init_attr.cap.max_oust_wr_rq = 10;/* Max outstanding WR on RQ */
  qp_init_attr.cap.max_oust_wr_sq = 10;/* Max outstanding WR on SQ */
  qp_init_attr.cap.max_sg_size_rq = 1;/* Max scatter/gather entries on RQ */
  qp_init_attr.cap.max_sg_size_sq = 1;/* Max scatter/gather entries on SQ */
  qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR;/* Signaling type  */
  qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR;/* Signaling type  */
  qp_init_attr.ts_type            = IB_TS_RC; /* Transmission type        */
  
  
  ret = VAPI_create_qp(hca_hndl, &qp_init_attr, 
                       &sync_s_qp_hndl, 
                       &sync_qp_prop);
  
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error creating Queue Pair: %s\n", VAPI_strerror(ret));
    lprintf("Error creating Queue Pair: %s\n", VAPI_strerror(ret));
    exit(-1);
  }

  sync_s_qp_num = sync_qp_prop.qp_num;
  dbprintf("  Created synchronization send Queue Pair\n");
  dbprintf("    QP num: %d\n", sync_qp_prop.qp_num);
  dbprintf("    Max oustanding WRs in the SQ: %d\n", 
           sync_qp_prop.cap.max_oust_wr_sq);
  dbprintf("    Max oustanding WRs in the RQ: %d\n", 
           sync_qp_prop.cap.max_oust_wr_rq);
  dbprintf("    Max scatter/gather elts in a WR in the SQ: %d\n", 
           sync_qp_prop.cap.max_sg_size_sq);
  dbprintf("    Max scatter/gather elts in a WR in the RQ: %d\n", 
           sync_qp_prop.cap.max_sg_size_rq);
  
  if(nprocs > 2) {
    
    ret = VAPI_create_qp(hca_hndl, &qp_init_attr, 
                         &sync_r_qp_hndl, 
                         &sync_qp_prop);
    
    if(ret != VAPI_OK) {
      fprintf(stderr, "Error creating Queue Pair: %s\n", VAPI_strerror(ret));
      lprintf("Error creating Queue Pair: %s\n", VAPI_strerror(ret));
      exit(-1);
    }

    sync_r_qp_num = sync_qp_prop.qp_num;
    dbprintf("  Created synchronization recv Queue Pair\n");
    dbprintf("    QP num: %d\n", sync_qp_prop.qp_num);
    dbprintf("    Max oustanding WRs in the SQ: %d\n", 
             sync_qp_prop.cap.max_oust_wr_sq);
    dbprintf("    Max oustanding WRs in the RQ: %d\n", 
             sync_qp_prop.cap.max_oust_wr_rq);
    dbprintf("    Max scatter/gather elts in a WR in the SQ: %d\n", 
             sync_qp_prop.cap.max_sg_size_sq);
    dbprintf("    Max scatter/gather elts in a WR in the RQ: %d\n", 
             sync_qp_prop.cap.max_sg_size_rq);
    
  } else {
    
    sync_r_qp_hndl = sync_s_qp_hndl;
    sync_r_qp_num  = sync_s_qp_num;

    dbprintf("  Only 2 nodes, so only one QP needed for sync\n");

  }
  
  /* Initialize Queue Pairs */

  bring_up_qps();

  /* Set completion event handler */

  /* VAPI_set_comp_event_handler(hca_hndl, ib_comp_event_handler, NULL); */

  send_comp_handler_hndl = malloc(sizeof(EVAPI_compl_handler_hndl_t) * nprocs);
  recv_comp_handler_hndl = malloc(sizeof(EVAPI_compl_handler_hndl_t) * nprocs);


  for(i=0; i<nprocs; i++) if(i != myproc) {

    EVAPI_set_comp_eventh(hca_hndl, send_cq_hndl[i], ib_comp_event_handler, 
                          NULL, &send_comp_handler_hndl[i]);
    EVAPI_set_comp_eventh(hca_hndl, recv_cq_hndl[i], ib_comp_event_handler, 
                          NULL, &recv_comp_handler_hndl[i]);

    VAPI_req_comp_notif(hca_hndl, send_cq_hndl[i], VAPI_NEXT_COMP);
    VAPI_req_comp_notif(hca_hndl, recv_cq_hndl[i], VAPI_NEXT_COMP);

  }

  /* Set up MP_Sync() */
  
  register_memory_region(&sync_token, sizeof(char), &sync_mr_hndl,
                         &mr_out, VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE);
  sync_mr_lkey = mr_out.l_key;

  /* Allocate and set receive request */
  sync_rr = malloc(sizeof(VAPI_rr_desc_t));
  sync_rr->id = MP_SYNC;
  sync_rr->opcode = VAPI_RECEIVE;
  sync_rr->comp_type = VAPI_SIGNALED;

  /* Allocate and set scatter/gather list entry */
  sync_rr->sg_lst_p        = malloc(sizeof(VAPI_sg_lst_entry_t));
  sync_rr->sg_lst_len      = 1;
  sync_rr->sg_lst_p->addr  = (VAPI_virt_addr_t)(u_int32_t)&sync_token;
  sync_rr->sg_lst_p->len   = sizeof(char);
  sync_rr->sg_lst_p->lkey  = mr_out.l_key;

  /* Allocate and set send recquest */
  sync_sr = malloc(sizeof(VAPI_sr_desc_t));
  sync_sr->id = MP_SYNC;
  sync_sr->opcode = VAPI_SEND;
  sync_sr->comp_type = VAPI_SIGNALED;
  sync_sr->remote_qkey = 0;
  sync_sr->set_se = FALSE;

  /* Allocate and set scatter/gather list entry */
  sync_sr->sg_lst_p        = malloc(sizeof(VAPI_sg_lst_entry_t));
  sync_sr->sg_lst_len      = 1;
  sync_sr->sg_lst_p->addr  = (VAPI_virt_addr_t)(u_int32_t)&sync_token;
  sync_sr->sg_lst_p->len   = sizeof(char);
  sync_sr->sg_lst_p->lkey  = mr_out.l_key;

  /* Prepost a receive */
  ret = VAPI_post_rr(hca_hndl, sync_r_qp_hndl, sync_rr);
  if(ret != VAPI_OK) {
    fprintf(stderr,"Error posting initial sync recv: %s\n",VAPI_strerror(ret));
    lprintf("Error posting initial sync recv: %s\n",VAPI_strerror(ret));
    dump_and_quit();
  }

  shfs_barrier();

  /* Set up eager-transfer buffers */

  dbprintf("Creating eager buffers:\n");
  dbprintf("  %d buffers per node\n", EAGER_BUFFERS_PER_NODE);
  dbprintf("  each buffer of size %d bytes\n", EAGER_BUFFER_SIZE);
  
  /* Here we allocate space for nprocs elements in each array,
   * but we will only use nprocs-1 elements.  Since we are, for 
   * the most part, dealing with primitive data types, we are not 
   * wasting much memory.  This approach seems preferable to 
   * obfuscating the code with macros or extra math to give us the
   * correct indexes
   */
  eager_sends             = malloc( sizeof(long) * nprocs  );
  eager_recvs             = malloc( sizeof(long) * nprocs  );
  eager_loc_send_freed    = malloc( sizeof(long) * nprocs  );
  eager_loc_recv_freed    = malloc( sizeof(long) * nprocs  );
  eager_rem_recv_freed    = malloc( sizeof(long) * nprocs  );
  eager_send_buffers      = malloc( sizeof(char*) * nprocs );
  eager_recv_buffers      = malloc( sizeof(char*) * nprocs );
  eager_next_send_buffer  = malloc( sizeof(int) * nprocs   );
  eager_recv_buffer_in_use= malloc( sizeof(int*) * nprocs  );
  eager_s_mr_hndl         = malloc( sizeof(VAPI_mr_hndl_t) * nprocs );
  eager_r_mr_hndl         = malloc( sizeof(VAPI_mr_hndl_t) * nprocs );
  eager_s_lkey            = malloc( sizeof(VAPI_lkey_t) * nprocs );
  eager_r_lkey            = malloc( sizeof(VAPI_lkey_t) * nprocs );
  eager_ib_recv_q         = malloc( sizeof(void*) * nprocs );
  eager_user_recv_q       = malloc( sizeof(void*) * nprocs );
  eager_send_q            = malloc( sizeof(void*) * nprocs );
  eager_rem_rkeys         = malloc( sizeof(int) * nprocs );
  eager_rem_addrs         = malloc( sizeof(void*) * nprocs );

  /* Allocate space for the rendezvous-related stuff */

  rndzv_msgs_in            = malloc( sizeof(struct rndzv_msg_entry) * nprocs );
  rndzv_msgs_out           = malloc( sizeof(struct rndzv_msg_entry) * nprocs );
  rndzv_msgs_rem_rkeys     = malloc( sizeof(int)   * nprocs );
  rndzv_msgs_rem_addrs     = malloc( sizeof(void*) * nprocs );

  rndzv_msgs_ack           = malloc( sizeof(int) * nprocs );
  rndzv_msgs_ack_rem_addrs = malloc( sizeof(void*) * nprocs );
  rndzv_msgs_ack_rem_rkeys = malloc( sizeof(int) * nprocs );

  trans_id_list            = malloc( sizeof(short*) * nprocs );
  current_trans_id         = malloc( sizeof(int) * nprocs );
  rndzv_recv_q             = malloc( sizeof(void*) * nprocs );
  rndzv_send_q             = malloc( sizeof(void*) * nprocs );
  rndzv_posted2ib_q        = malloc( sizeof(void*) * nprocs );

  for(i=0; i<nprocs; i++) if(i != myproc) {
    
    /* Initialize send and recv queues to NULL 
     */
    eager_ib_recv_q[i] = eager_user_recv_q[i] = eager_send_q[i] = NULL;
    rndzv_send_q[i] = rndzv_recv_q[i] = rndzv_posted2ib_q[i] = NULL;

    /* Allocate memory for receive buffer in-use array, initialize all entries
     * to "free"
     */
    eager_recv_buffer_in_use[i] = malloc( sizeof(int)*EAGER_BUFFERS_PER_NODE);
    for(j=0; j<EAGER_BUFFERS_PER_NODE; j++)
      eager_recv_buffer_in_use[i][j] = 0;

    /* Allocate memory for each node's transaction id list, and mark all
     * ids available
     */
    trans_id_list[i] = malloc( sizeof(short) * MP_TRANS_ID_SPACE );
    for(j=0; j<MP_TRANS_ID_SPACE; j++)
      trans_id_list[i][j] = 0;
    current_trans_id[i] = 0;

    /* Set rendezvous ack list to all 1's, so each node can initially
     * send a RTR message right away
     */
    rndzv_msgs_ack[i] = 1;

  }

  /* Register memory region for sending our count of receive buffers
   * freed via RDMA
   */
  register_memory_region(eager_loc_recv_freed,
                         sizeof(long) * nprocs,
                         &eager_loc_mr_hndl,
                         &mr_out,
                         VAPI_EN_LOCAL_WRITE);
  eager_loc_mr_lkey = mr_out.l_key;

  /* Register memory region for allowing other nodes to do RDMA or Atomic
   * writes to our eager_rem_recv_freed variable.
   */
  register_memory_region(eager_rem_recv_freed,
                         sizeof(long) * nprocs,
                         &eager_rem_mr_hndl,
                         &mr_out,
                         VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE |
                         VAPI_EN_REMOTE_ATOM);
  eager_rem_mr_rkey = mr_out.r_key;

  /* Register memory region for allowing other nodes to do RDMA writes to
   * our incoming rendezvous Ready-to-Receive message area.
   */
  register_memory_region(rndzv_msgs_in,
                         sizeof(struct rndzv_msg_entry) * nprocs,
                         &rndzv_msgs_in_mr_hndl,
                         &mr_out,
                         VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE |
                         VAPI_EN_REMOTE_ATOM);
  rndzv_msgs_in_mr_rkey = mr_out.r_key;

  /* Register memory region to allow this node to send RDMA rendezvous
   * Ready-to-Receive messages to other nodes.
   */
  register_memory_region(rndzv_msgs_out,
                         sizeof(struct rndzv_msg_entry) * nprocs,
                         &rndzv_msgs_out_mr_hndl,
                         &mr_out,
                         VAPI_EN_LOCAL_WRITE);
  rndzv_msgs_out_mr_lkey = mr_out.l_key;

  /* Register memory region for allowing other nodes to write to our
   * RTR acknowledgement array, as well as using our node's index in
   * the array for sending the acknowledgement out.
   */
  register_memory_region(rndzv_msgs_ack,
                         sizeof(int) * nprocs,
                         &rndzv_msgs_ack_mr_hndl,
                         &mr_out,
                         VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE);
  rndzv_msgs_ack_mr_rkey = mr_out.r_key;
  rndzv_msgs_ack_mr_lkey = mr_out.l_key;


  /* Exchange rkeys and addresses with all the other nodes */

  for(i=0; i<nprocs; i++) if(i != myproc) {
    shfs_send(&eager_rem_mr_rkey, sizeof(int), i);
    addr = &eager_rem_recv_freed[i];
    shfs_send(&addr, sizeof(void*), i);
    
    shfs_send(&rndzv_msgs_in_mr_rkey, sizeof(int), i);
    addr = &rndzv_msgs_in[i];
    shfs_send(&addr, sizeof(void*), i);

    shfs_send(&rndzv_msgs_ack_mr_rkey, sizeof(int), i);
    addr = &rndzv_msgs_ack[i];
    shfs_send(&addr, sizeof(void*), i);
              
    dbprintf("  Sent eager rkey %d, addr %p,\n"
             "       rndzv msg rkey %d, addr %p,\n"
             "       rndzv ack rkey %d, addr %p, to node %d\n", 
             eager_rem_mr_rkey, &eager_rem_recv_freed[i], 
             rndzv_msgs_in_mr_rkey, &rndzv_msgs_in[i], 
             rndzv_msgs_ack_mr_rkey, &rndzv_msgs_ack[i], i);
  }
  
  shfs_barrier();

  for(i=0; i<nprocs; i++) if(i != myproc) {
    shfs_recv(&eager_rem_rkeys[i],          sizeof(int),   i);
    shfs_recv(&eager_rem_addrs[i],          sizeof(void*), i);
    shfs_recv(&rndzv_msgs_rem_rkeys[i],     sizeof(int),   i);
    shfs_recv(&rndzv_msgs_rem_addrs[i],     sizeof(void*), i);
    shfs_recv(&rndzv_msgs_ack_rem_rkeys[i], sizeof(int),   i);
    shfs_recv(&rndzv_msgs_ack_rem_addrs[i], sizeof(void*), i);


    dbprintf("  Received eager rkey %d, addr %p,\n"
             "           rndzv msg rkey %d, addr %p,\n"
             "           rndzv ack rkey %d, addr %p, from node %d\n", 
             eager_rem_rkeys[i], eager_rem_addrs[i], 
             rndzv_msgs_rem_rkeys[i], rndzv_msgs_rem_addrs[i], 
             rndzv_msgs_ack_rem_rkeys[i], rndzv_msgs_ack_rem_addrs[i], i);
  }

  

  for(i=0; i<nprocs; i++) if(i != myproc) {

    eager_sends[i] = 0;
    eager_recvs[i] = 0;

    eager_loc_send_freed[i] = EAGER_BUFFERS_PER_NODE;
    eager_loc_recv_freed[i] = EAGER_BUFFERS_PER_NODE;
    eager_rem_recv_freed[i] = EAGER_BUFFERS_PER_NODE;

    eager_send_buffers[i] = malloc( EAGER_BUFFER_SIZE*EAGER_BUFFERS_PER_NODE );
    eager_next_send_buffer[i] = 0;

    eager_recv_buffers[i] = malloc( EAGER_BUFFER_SIZE*EAGER_BUFFERS_PER_NODE );

    /* Set up memory regions for send buffers */

    register_memory_region( eager_send_buffers[i],
                            EAGER_BUFFER_SIZE*EAGER_BUFFERS_PER_NODE, 
                            &eager_s_mr_hndl[i],
                            &mr_out,
                            VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE);
    eager_s_lkey[i] = mr_out.l_key;

    /* Set up memory regions for receive buffers */

    register_memory_region( eager_recv_buffers[i],
                            EAGER_BUFFER_SIZE*EAGER_BUFFERS_PER_NODE, 
                            &eager_r_mr_hndl[i],
                            &mr_out,
                            VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE);
    eager_r_lkey[i] = mr_out.l_key;

    /* Post receive for each buffer */

    dbprintf("  Posting preliminary eager receives for node %d\n", i);

    for(j=0; j<EAGER_BUFFERS_PER_NODE; j++)

      ib_post_eager_recv(i);

    /* Post one rendezvous receive for each node */

    ib_post_rndzv_recv(i);
  }  
  
  dbprintf("\nInfiniband initialization done\n");

}

void ib_cleanup()
{
  int            i;
  VAPI_ret_t     ret;
#if defined (RNDZV_PERSISTENT_MEM_REGS)
  VAPI_mr_hndl_t mr_hndl;
  VAPI_mrw_t     mr_prop;
  void*          addr;
#endif

  dbprintf("Infiniband Cleanup\n");

  /* Wait for all incoming RDMA sends to complete, otherwise we'll
   * cause errors for the sender if we start shutting down too
   * soon.
   */

  dbprintf("  Waiting for incoming RDMA sends\n");

  for(i=0;i<nprocs;i++) if(i != myproc) {
    
    dbprintf("    Proc %d: eager_sends=%d, eager_rem_recv_freed=%d\n",
             i, eager_sends[i], eager_rem_recv_freed[i]);
    /* If we did N eager sends to node i, then we should expect node i
     * to have freed (N + EAGER_BUFFERS_PER_NODE) buffers
     */
    while( ADD_WITH_WRAP(eager_sends[i], EAGER_BUFFERS_PER_NODE) 
           != eager_rem_recv_freed[i] ) {
      MP_BLOCK_METHOD;
    }
  }

  MP_Sync();

  /* Clear completion event handlers 
   */
  for(i=0; i<nprocs; i++) if(i != myproc) {

    ret=EVAPI_clear_comp_eventh(hca_hndl, send_comp_handler_hndl[i]);
    if(ret != VAPI_OK) {
      fprintf(stderr, "Error clearing send completion event handler\n");
      lprintf("Error clearing send completion event handler\n");
    }

    ret=EVAPI_clear_comp_eventh(hca_hndl, recv_comp_handler_hndl[i]);
    if(ret != VAPI_OK) {
      fprintf(stderr, "Error clearing receive completion event handler\n");
      lprintf("Error clearing receive completion event handler\n");
    }

  }

  /* Deregister sync memory regions 
   */
  if(sync_mr_hndl != VAPI_INVAL_HNDL) {
    dbprintf("  Deregistering sync memory handle\n");
    deregister_memory_region(sync_mr_hndl);
    sync_mr_hndl = VAPI_INVAL_HNDL;
  }
  
  /* Deregister eager memory regions 
   */
  for(i=0; i<nprocs; i++) if(i != myproc) {

    if(eager_s_mr_hndl[i] != VAPI_INVAL_HNDL) {

      dbprintf("  Deregistering eager send memory region for node %d\n", i);
      deregister_memory_region(eager_s_mr_hndl[i]);
      eager_s_mr_hndl[i] = VAPI_INVAL_HNDL;

      dbprintf("  Deregistering eager recv memory region for node %d\n", i);
      deregister_memory_region(eager_r_mr_hndl[i]);
      eager_r_mr_hndl[i] = VAPI_INVAL_HNDL;
    }

  }
  
  /* Deregister eager RDMA-related memory regions 
   */
  if(eager_rem_mr_hndl != VAPI_INVAL_HNDL) {
    dbprintf("  Deregistering eager remote memory region\n");
    deregister_memory_region(eager_rem_mr_hndl);
    eager_rem_mr_hndl = VAPI_INVAL_HNDL;
  }

  if(eager_loc_mr_hndl != VAPI_INVAL_HNDL) {
    dbprintf("  Deregistering local eager recv buffers freed memory region\n");
    deregister_memory_region(eager_loc_mr_hndl);
    eager_loc_mr_hndl = VAPI_INVAL_HNDL;
  }

  /* Deregister rendezvous memory regions 
   */
  if(rndzv_msgs_in_mr_hndl != VAPI_INVAL_HNDL) {
    dbprintf("  Deregistering incoming rendezvous control messages mr\n");
    deregister_memory_region(rndzv_msgs_in_mr_hndl);
    rndzv_msgs_in_mr_hndl = VAPI_INVAL_HNDL;
  }

  if(rndzv_msgs_out_mr_hndl != VAPI_INVAL_HNDL) {
    dbprintf("  Deregistering outgoing rendezvous control messages mr\n");
    deregister_memory_region(rndzv_msgs_out_mr_hndl);
    rndzv_msgs_out_mr_hndl = VAPI_INVAL_HNDL;
  }

  if(rndzv_msgs_ack_mr_hndl != VAPI_INVAL_HNDL) {
    dbprintf("  Deregistering rendezvous ack mr\n");
    deregister_memory_region(rndzv_msgs_ack_mr_hndl);
    rndzv_msgs_ack_mr_hndl = VAPI_INVAL_HNDL;
  }

#if defined(RNDZV_PERSISTENT_MEM_REGS)
  while( persist_mr_delrand(&addr, &mr_hndl, &mr_prop) ) {

    dbprintf("  Deregistering mr hndl %d\n", mr_hndl);

    ret=VAPI_deregister_mr(hca_hndl, mr_hndl);
    if(ret != VAPI_OK) {
      fprintf(stderr,"Error deregistering persistent mr: %s\n",VAPI_strerror(ret));
      lprintf("Error deregistering persistent mr: %s\n",VAPI_strerror(ret));
    }
    
  }
#endif

  /* Destroy Queue Pairs 
   */
  if(sync_s_qp_hndl != VAPI_INVAL_HNDL) {
    dbprintf("  Destroying synchronization send QP\n");
    ret = VAPI_destroy_qp(hca_hndl, sync_s_qp_hndl);
    if(ret != VAPI_OK) {
      fprintf(stderr,"Error destroying Queue Pair: %s\n",VAPI_strerror(ret));
      lprintf("Error destroying Queue Pair: %s\n",VAPI_strerror(ret));
    }
    sync_s_qp_hndl = VAPI_INVAL_HNDL;
  }

  if(nprocs > 2) {

    if(sync_r_qp_hndl != VAPI_INVAL_HNDL) {
      dbprintf("  Destroying synchronization recv QP\n");
      ret = VAPI_destroy_qp(hca_hndl, sync_r_qp_hndl);
      if(ret != VAPI_OK) {
        fprintf(stderr,"Error destroying Queue Pair: %s\n",VAPI_strerror(ret));
        lprintf("Error destroying Queue Pair: %s\n",VAPI_strerror(ret));
      }
      sync_r_qp_hndl = VAPI_INVAL_HNDL;
    }

  } else {
    
    sync_r_qp_hndl = VAPI_INVAL_HNDL;
    
  }
  
  for(i=0; i<nprocs; i++) if(myproc != i) {

    if(eager_qp_hndl[i] != VAPI_INVAL_HNDL) {
      dbprintf("  Destroying node %d's eager QP\n", i);
      ret = VAPI_destroy_qp(hca_hndl, eager_qp_hndl[i]);
      if(ret != VAPI_OK) {
        fprintf(stderr,"Error destroying eager QP: %s\n",VAPI_strerror(ret));
        lprintf("Error destroying eager QP: %s\n",VAPI_strerror(ret));
      }
      eager_qp_hndl[i] = VAPI_INVAL_HNDL;
    }

  }

  for(i=0; i<nprocs; i++) if(myproc != i) {

    if(rndzv_qp_hndl[i] != VAPI_INVAL_HNDL) {
      dbprintf("  Destroying node %d's rndzv QP\n", i);
      ret = VAPI_destroy_qp(hca_hndl, rndzv_qp_hndl[i]);
      if(ret != VAPI_OK) {
        fprintf(stderr,"Error destroying rndzv QP: %s\n",VAPI_strerror(ret));
        lprintf("Error destroying rndzv QP: %s\n",VAPI_strerror(ret));
      }
      rndzv_qp_hndl[i] = VAPI_INVAL_HNDL;
    }

  }

  /* Destroy completion queues 
   */
  for(i=0; i<nprocs; i++) if(i != myproc) {

    if(recv_cq_hndl[i] != VAPI_INVAL_HNDL) {
      dbprintf("  Destroying Receive CQ for node %d\n", i);
      ret = VAPI_destroy_cq(hca_hndl, recv_cq_hndl[i]);
      if(ret != VAPI_OK) {
        fprintf(stderr, "Error destroying receive CQ: %s\n", 
                VAPI_strerror(ret));
        lprintf("Error destroying receive CQ: %s\n", VAPI_strerror(ret));
      }
      recv_cq_hndl[i] = VAPI_INVAL_HNDL;
    }
    
    if(send_cq_hndl[i] != VAPI_INVAL_HNDL) {
      dbprintf("  Destroying Send CQ for node %d\n", i);
      ret = VAPI_destroy_cq(hca_hndl, send_cq_hndl[i]);
      if(ret != VAPI_OK) {
        fprintf(stderr, "Error destroying send CQ: %s\n", VAPI_strerror(ret));
        lprintf("Error destroying send CQ: %s\n", VAPI_strerror(ret));
      }
      send_cq_hndl[i] = VAPI_INVAL_HNDL;
    }
    
  }

  /* Deallocate protection domain 
   */
  if(pd_hndl != VAPI_INVAL_HNDL) {
    dbprintf("  Deallocating PD\n");
    ret = VAPI_dealloc_pd(hca_hndl, pd_hndl);
    if(ret != VAPI_OK) {
      fprintf(stderr, "Error deallocating PD: %s\n", VAPI_strerror(ret));
      lprintf("Error deallocating PD: %s\n", VAPI_strerror(ret));
    } 
    pd_hndl = VAPI_INVAL_HNDL;
  }

  /* Application code should not close HCA, just release handle 
   */
  if(hca_hndl != VAPI_INVAL_HNDL) {
    dbprintf("  Releasing HCA\n");
    ret = EVAPI_release_hca_hndl(hca_hndl);
    if(ret != VAPI_OK) {
      fprintf(stderr, "Error releasing Infiniband HCA: %s\n", 
              VAPI_strerror(ret));
      lprintf("Error releasing Infiniband HCA: %s\n", 
              VAPI_strerror(ret));
    } 
    hca_hndl = VAPI_INVAL_HNDL;
  }
}



/* XXX DEBUG FUNCTION */
void poll_if_stuck(VAPI_cq_hndl_t queue, int stop)
{
  VAPI_ret_t        ret;
  VAPI_wc_desc_t    wc;
  static int        reset=1;
  static double     start_time;
  double            current_time;

  if(stop) {
    
    reset=1;
    return;

  }

  if(reset) {

    reset=0;
    start_time=MP_Time();

  } else {

    current_time=MP_Time();

    if(current_time - start_time > 10.0) {

      reset=1;

      fprintf(stderr, 
              "\nWe have stalled for 10 seconds, polling manually...\n");

      ret = VAPI_CQ_EMPTY;
      ret = VAPI_poll_cq(hca_hndl, queue, &wc);
      
      if(ret != VAPI_CQ_EMPTY) {
        
        if(ret != VAPI_OK) {
          fprintf(stderr, "Error polling cq for completion: %s\n", 
                  VAPI_strerror(ret));
          exit(-1);
        } else {
          
          fprintf(stderr, "We appear to have found a completion that slipped"
                  " by the handler\n");
          fprintf(stderr, "\n");
          fprintf(stderr, "** Completion info **\n");
          fprintf(stderr, "      ID: %d\n", wc.id);
          fprintf(stderr, "  Opcode: %s\n", VAPI_cqe_opcode_sym(wc.opcode));
          fprintf(stderr, "  Status: %s\n", VAPI_wc_status_sym(wc.status));
          fprintf(stderr, "Byte len: %d\n", wc.byte_len);

          exit(-1);
          
        }
        
      }
      
    }



  }


}

/* This function is not currently needed, as far as I know, but in case
 * something bad happens, this may be necessary to catch it.
 */
void ib_async_event_handler(VAPI_hca_hndl_t hca, VAPI_event_record_t* record,
                            void* data)
{

  dbprintf("\n ^^^ Async event handler ^^^\n");

  dbprintf("Got asynchronous event: %s\n", 
           VAPI_event_record_sym(record->type));
  fprintf(stderr, "Got asynchronous event: %s\n", 
          VAPI_event_record_sym(record->type));

  dbprintf("^^^ Leaving async event handler ^^^\n");
}

void ib_eager_asend(struct MP_msg_entry* msg)
{
  int status;

  /* Check for other queued sends to the same destination first 
   */
  ib_eager_check_sends( msg->dest, &status );

  /* It's possible that the loc_send or rem_recv freed
   * counters would be incremented after returning from checking
   * the queue for sends.  If in the above function we didn't try
   * any sends because either of the counters reached its limit,
   * then don't try checking again below, just post to queue.  This
   * will maintain ordered sends.
   */
    
  /* Check if we can send this message immediately 
   */
  if( status == 1
      &&
      eager_sends[ msg->dest ] != eager_loc_send_freed[ msg->dest ] 
      &&
      eager_sends[ msg->dest ] != eager_rem_recv_freed[ msg->dest ] ) {

    /* Do send now 
     */
    dbprintf("  Doing eager send to node %d immediately\n", msg->dest);

    msg->ib_flag = MP_WAITING_EAGER_SEND_COMPLETION;
    post( msg, eager_send_q ); /* MUTEX no need to protect, only called
                                  here and below, and only in main thread */

    /* Post send to VAPI layer 
     */
    ib_post_eager_send(msg);

    eager_sends_immediate++;

  } else {
    
    /* Do send later 
     */
    dbprintf("  Adding eager send request for node %d to queue, it\n"
             "   will be completed when resources are available\n", msg->dest);
    
    msg->ib_flag = MP_WAITING_EAGER_SEND_POST;
    post( msg, eager_send_q ); /* MUTEX see above */

    eager_sends_queued++;
  }
  
}

/* According to Infiniband specs, work requests generally complete
 * in the order they were submitted to the work queue, aside from 
 * a few exceptions.  The messages of type VAPI_SEND should thus
 * complete in the order they are submitted to the work queue,
 * so we can assume the next free send buffer is always the
 * next buffer in numerical order.  This is different from
 * processing eager receives, where the next free receive
 * buffer is not necessarily the next buffer in numerical order.
 */ 
void ib_post_eager_send(struct MP_msg_entry* msg)
{
  VAPI_ret_t           ret;
  VAPI_sr_desc_t       sr;
  VAPI_sg_lst_entry_t  sg_entry;
  VAPI_wc_desc_t       wc;
  char*                send_buf;

  dbprintf("  Posting send to VAPI layer (%d %d %d)\n", msg->nbytes,
           msg->dest, msg->tag ); 
  
  send_buf = eager_send_buffers[ msg->dest ] + 
    (eager_next_send_buffer[ msg->dest ] * EAGER_BUFFER_SIZE);

  /* XXX ASYNC - Once we put a send into the queue waiting
   * for VAPI post, there should not be any further updates to 
   * eager_next_send_buffer[msg->dest] until we are cleared to send the
   * first one waiting in queue.  So, this should be ok whether called
   * inside or outside the completion handler.
   *
   * Could use a mutex here if necessary.
   */
  eager_next_send_buffer[ msg->dest ] =
    (eager_next_send_buffer[ msg->dest ] + 1) % EAGER_BUFFERS_PER_NODE;

  MP_memcpy( send_buf, msg->phdr, msg->nhdr );
      
  MP_memcpy( send_buf + msg->nhdr, msg->buf, msg->nbytes );
          
  sr.opcode = VAPI_SEND;
  sr.comp_type = VAPI_SIGNALED;
  sr.id = MP_EAGER_SEND_BUF;
  sr.sg_lst_len = 1;
  sr.sg_lst_p = malloc( sizeof(VAPI_sg_lst_entry_t) );
  sr.sg_lst_p->lkey = eager_s_lkey[ msg->dest ];
  sr.sg_lst_p->len  = msg->nbytes + msg->nhdr;
  sr.sg_lst_p->addr = (VAPI_virt_addr_t)(u_int32_t)send_buf;
      
  ret = VAPI_post_sr(hca_hndl, eager_qp_hndl[ msg->dest ], &sr);
  if(ret != VAPI_OK) {
    fprintf(stderr, "ib_asend - Error posting eager send request: %s\n",
            VAPI_strerror(ret));
    lprintf("ib_asend - Error posting eager send request: %s\n",
            VAPI_strerror(ret));
    exit(-1);
  } 

  /* XXX ASYNC - See above explanation, we should be safe */

  INC_WITH_WRAP( eager_sends[ msg->dest ] );

}

void ib_eager_arecv(struct MP_msg_entry* msg)
{

  struct MP_msg_entry* cur_msg;
  VAPI_ret_t           ret;
  char*                recv_buf;
  int                  got_match;

  /* Get mutex lock so we don't try to process a message concurrently
   * with event completion handler thread.  If we don't find an eager
   * recv message in the ib eager recv q below, then the message
   * has not arrived yet, and when it does the event completion handler
   * will find our eager recv message in the user eager recv q and
   * process it.
   */

  pthread_mutex_lock(&hndl_e_recv_mutex);

  /* Check eager_ib_recv_q[src] for a completed matching receive */


  for(cur_msg = eager_ib_recv_q[msg->src], got_match=0;
      cur_msg != NULL; 
      cur_msg = cur_msg->next) {
    
    if(cur_msg->ib_flag == MP_EAGER_RECV_COMPLETE &&
       cur_msg->nbytes <= msg->nbytes &&
       (msg->tag == -1 || cur_msg->tag == msg->tag) ) {

      got_match = 1;

      break;
    }

  }

  if(got_match) {

    dbprintf("  Found matching msg in eager ib recv q, processing it now\n");

    /* Link ib recv msg to user recv msg */

    msg->ib_ptr = cur_msg;

    cur_msg->ib_flag = MP_EAGER_RECV_MATCHED;

    /* Copy data */

    MP_memcpy(msg->buf, cur_msg->buf+sizeof(struct MP_msg_header),
              cur_msg->nbytes);

    /* Mark eager buffer as not in use */

    eager_recv_buffer_in_use[msg->src][cur_msg->ib_index] = 0;

    /* Post new eager receive */

    ib_post_eager_recv(msg->src);

    /* Increment count of freed receive buffers */

    ib_inc_eager_recv_freed(msg->src);

    /* Mark user's msg as done */

    msg->ib_flag = MP_EAGER_RECV_COMPLETE;

    eager_recvs_immediate++;

  } else {

    dbprintf("  No matching msg in eager ib recv q, we will process it when it"
             " arrives\n");

    msg->ib_flag = MP_WAITING_EAGER_RECV_COMPLETION;

    eager_recvs_queued++;
  }

  /* Post receive message to eager user receive queue */
  
  /* XXX OPT - don't post to recv q if we already processed
   * message above
   */

  post( msg, eager_user_recv_q ); /* MUTEX no need to protect, this is only
                                     place eager_user_recv_q is posted to
                                     and this function is only called in
                                     main thread */

  pthread_mutex_unlock(&hndl_e_recv_mutex);

}

void ib_eager_handle_recv(VAPI_wc_desc_t* wc, int node)
{
  struct MP_msg_entry* ib_msg;
  struct MP_msg_entry* user_msg;
  struct MP_msg_header hdr;

  dbprintf("***  Handling eager receive from node %d\n", node);

  if( wc->status != VAPI_SUCCESS ) {
    fprintf(stderr, "*** No success, status = %s\n", 
            VAPI_wc_status_sym(wc->status));
    lprintf("*** eager_handler_recv: No success, status = %s\n", 
            VAPI_wc_status_sym(wc->status));
    exit(-1);
  }

  /* Get mutex lock so we don't try to process a message concurrently 
   * with the user-called MP_ARecv.  If the event completion handler 
   * does not find the eager recv msg posted by the user, then the user
   * hasn't called MP_ARecv yet, and when he or she does, it will 
   * pick up the msg in the ib eager recv q
   */

  pthread_mutex_lock(&hndl_e_recv_mutex);
 
  for(ib_msg = eager_ib_recv_q[node]; ib_msg != NULL; ib_msg = ib_msg->next) {

    if(ib_msg->ib_flag == MP_WAITING_EAGER_RECV_COMPLETION) {
      dbprintf("***    Found an eager receive waiting for completion, "
               "buffer %d\n", ib_msg->ib_index);

      /* Copy tag and nbytes from header to message in queue */

      MP_memcpy(&hdr, ib_msg->buf, sizeof(hdr));
      
      ib_msg->tag = hdr.tag;  ib_msg->nbytes = hdr.nbytes;

      ib_msg->ib_flag = MP_EAGER_RECV_COMPLETE;

      dbprintf("***      Header: nbytes=%d, tag=%d int val=%d\n", hdr.nbytes, hdr.tag, *((int*)(ib_msg->buf+sizeof(hdr))));

      break; /* Break from for loop */
    }

  }

  if(ib_msg == NULL) {
    fprintf(stderr, "*** Error, no eager messages waiting for completion\n");
    lprintf("*** Error, no eager messages waiting for completion\n");
    exit(-1);
  }


  /* Check eager_user_recv_q[src] to see if user already posted the 
   * corresponding receive information for this msg.
   */

  for(user_msg = eager_user_recv_q[node]; user_msg != NULL; 
      user_msg = user_msg->next) {

    if(user_msg->ib_flag == MP_WAITING_EAGER_RECV_COMPLETION &&
       ib_msg->nbytes <= user_msg->nbytes &&
       (user_msg->tag == -1 || ib_msg->tag == user_msg->tag) ) {

      dbprintf("***    Found matching msg in user recv q, copying data\n");

      /* Link this IB msg with user's msg */

      user_msg->ib_ptr = ib_msg;

      ib_msg->ib_flag = MP_EAGER_RECV_MATCHED;

      /* Copy data */

      MP_memcpy(user_msg->buf, ib_msg->buf+sizeof(struct MP_msg_header),
                ib_msg->nbytes);

      /* Mark ib eager receive buffer as not in use */

      eager_recv_buffer_in_use[node][ib_msg->ib_index] = 0;

      /* Post new eager receive */

      dbprintf("***   Posting new eager receive\n");

      ib_post_eager_recv(user_msg->src);
      
      /* Increment count of freed receive buffers */

      dbprintf("***   Incrementing count of freed receive buffers\n");

      ib_inc_eager_recv_freed(user_msg->src);

      /* Mark user's msg as done (do this after posting new eager receive
       * so that the main thread will not try to remove a message from
       * the eager_ib_recv_q at the same time).
       */
      user_msg->ib_flag = MP_EAGER_RECV_COMPLETE;

      break; /* Break from loop */
    }

  }

  pthread_mutex_unlock(&hndl_e_recv_mutex);

  if(user_msg == NULL) {
    
    dbprintf("***    No matching msg in user recv q, take care of this eager "
             "msg later.\n");

  }

}


void ib_eager_finish_recv(struct MP_msg_entry* msg)
{

  struct MP_msg_entry* cur_msg;
  struct MP_msg_entry* prev_msg;
  int                  got_match=0;
  
  dbprintf("  Finishing eager receive from node %d\n", msg->src);

  /* Block until msg is marked as complete */

  if( msg->ib_flag != MP_EAGER_RECV_COMPLETE ) {

     eager_waits_recv_blocked++;
     
     while( msg->ib_flag != MP_EAGER_RECV_COMPLETE ) {
        MP_BLOCK_METHOD;
     }
  }

  /* Remove msg from eager user receive queue */

  for(prev_msg = NULL, cur_msg = eager_user_recv_q[msg->src];
      cur_msg != NULL;
      prev_msg = cur_msg, cur_msg = cur_msg->next) {

    if(cur_msg == msg) break; /* Found matching msg */

  }
  
  if(cur_msg == NULL) {
    fprintf(stderr, "Error - No matching msg in eager user recv q\n");
    lprintf("Error - No matching msg in eager user recv q\n");
    exit(-1);
  }

  if(prev_msg == NULL) /* msg is at head of queue */
    
    eager_user_recv_q[msg->src] = msg->next;

  else /* msg is not at head of queue */
    
    prev_msg->next = msg->next;


  /* Remove corresponding msg from eager_ib_recv_q[src] */

  for(prev_msg = NULL, cur_msg = eager_ib_recv_q[msg->src];
      cur_msg != NULL;
      prev_msg = cur_msg, cur_msg = cur_msg->next) {

    if((void*)cur_msg == msg->ib_ptr) break; /* Found matching msg */

  }
  
  if(cur_msg == NULL) {
    fprintf(stderr, "Error - No matching msg in eager ib recv q\n");
    lprintf("Error - No matching msg in eager ib recv q\n");
    exit(-1);
  }

  if(prev_msg == NULL) /* msg is at head of queue */
    
    eager_ib_recv_q[msg->src] = cur_msg->next;

  else /* msg is not at head of queue */
    
    prev_msg->next = cur_msg->next;
  
  /* Mark IB msg as free */
  
  cur_msg->buf = NULL;

  INC_WITH_WRAP( eager_recvs[ msg->src ] );

}

void ib_eager_handle_send(VAPI_wc_desc_t* wc, int node)
{
  struct MP_msg_entry* msg;

  dbprintf("***  Handling eager send to node %d\n", node);

  if( wc->status != VAPI_SUCCESS ) {
    fprintf(stderr, "*** No success, status = %s\n", 
            VAPI_wc_status_sym(wc->status));
    lprintf("*** No success, status = %s\n", 
            VAPI_wc_status_sym(wc->status));
    exit(-1);
  }

  /* Walk through queue to find msg that matches and is waiting
   * for completion.
   */
  for(msg = eager_send_q[node]; msg != NULL; msg=msg->next) {
    
    if(msg->ib_flag == MP_WAITING_EAGER_SEND_COMPLETION) {
      dbprintf("***    Found an eager send waiting for completion\n");

      msg->ib_flag = MP_EAGER_SEND_COMPLETE;

      /* MP_Wait will clean up this message when it is called. In the
       * meantime, we increment the eager_loc_send_freed counter
       * so that the next send can go out. This is currently the only
       * location that eager_loc_send_freed is modified, so it is safe
       * to do in the event handler without a mutex.
       */
      INC_WITH_WRAP( eager_loc_send_freed[ node ] ); 
      
      break; /* Break from for loop */

    }

  }

  if(msg == NULL) {
    fprintf(stderr, "Error, no eager messages waiting for completion\n");
    lprintf("Error, no eager messages waiting for completion\n");
    exit(-1);
  }

  /* XXX OPT - Use a flag to check whether any sends have been queued
   * for posting to VAPI. 
   */
  
  /* XXX - Shouldn't have to call this, if we're 
   * expecting a completion every time we get an update
   * ib_eager_check_sends(node); 
   */

}

/* Check the eager_send_q for any sends that need posting to VAPI */

void ib_eager_check_sends(int node, int* status)
{
  /* status == 0 => counter limit reached, sends may still be on queue
   * status == 1 => no sends on queue
   */

  struct MP_msg_entry* msg;

  dbprintf("    Checking for eager sends for node %d in queue\n", node);

  *status = 1;

  /* Walk through queue to find any messages waiting to be posted
   * to the VAPI layer.
   */
  for(msg = eager_send_q[node]; msg != NULL; msg = msg->next) {

    /* Need to check every time to make sure it's ok to send.  If not,
     * then no sense checking for more messages, so break from loop.
     */
    
    if( eager_sends[node] == eager_loc_send_freed[node] 
        ||
        eager_sends[node] == eager_rem_recv_freed[node] ) {
      
      *status = 0;
      break;

    }

    if(msg->ib_flag == MP_WAITING_EAGER_SEND_POST) {

      dbprintf("    Found eager send waiting for post to VAPI (%d %d %d)\n",
                 msg->nbytes, msg->dest, msg->tag);

      msg->ib_flag = MP_WAITING_EAGER_SEND_COMPLETION;
      ib_post_eager_send(msg);
      
    }

  }

}

void ib_eager_finish_send(struct MP_msg_entry* msg)
{
  struct MP_msg_entry* cur_msg;
  struct MP_msg_entry* prev_msg;

  dbprintf("  Finishing eager send to node %d\n", msg->dest);

  /* If the message has not been posted to VAPI yet, do a busy wait
   * until a receive slot is free at destination node and a send
   * slot is free here, and then post to VAPI.
   */

  if(msg->ib_flag == MP_WAITING_EAGER_SEND_POST) {
    
    dbprintf("    Message is still waiting to be posted to VAPI layer\n");

    while( eager_sends[ msg->dest ] == eager_loc_send_freed[ msg->dest ]
           ||
           eager_sends[ msg->dest ] == eager_rem_recv_freed[ msg->dest ] ) {
      MP_BLOCK_METHOD;
    }

    dbprintf("    At least one local send and one remote receive slot is \n"
             "     free, posting to VAPI layer now.\n");
    
    msg->ib_flag = MP_WAITING_EAGER_SEND_COMPLETION;
    ib_post_eager_send(msg);

    eager_waits_send_posted++;

  }

  /* Busy wait for completion handler to set ib_flag */
  
  if(msg->ib_flag != MP_EAGER_SEND_COMPLETE) {

     eager_waits_send_blocked++;
     
     while(msg->ib_flag != MP_EAGER_SEND_COMPLETE) {
       MP_BLOCK_METHOD;
     }
  }

  /* Find previous message in queue so we can remove this message */
  
  for(prev_msg = NULL, cur_msg = eager_send_q[msg->dest];
      cur_msg != NULL;
      prev_msg = cur_msg, cur_msg = cur_msg->next) {
    
    if( cur_msg == msg ) break;  /* Found our message */

  }

  if(cur_msg == NULL) {
    fprintf(stderr, "Error - could not find this message in eager_send_q\n");
    lprintf("Error - could not find this message in eager_send_q\n");
    exit(-1);
  }
  
  /* Remove msg from queue */

  if(prev_msg==NULL) { /* msg was at head of queue */

    eager_send_q[msg->dest] = msg->next;

  } else { /* msg was not at head of queue */

    prev_msg->next = msg->next;

  }


}

/* ib_rndzv_asend is called by MP_ASend to initiate the sending
 * part of a rendezvous transfer. First we need to register the memory
 * from which the data will be sent on our end. We may or may not already
 * have gotten a Ready-to-Receive message from the receiver, so we
 * check for such a message in the rndzv_send_q. If we find a matching
 * message, then we start the send now. If we do not find a matching
 * message, add a message indicating the transfer is ready to go,
 * and let the completion handler do the actual send when the RTR
 * message comes in. We need to be careful here with the threaded
 * model so that an incoming RTR is not added to the message 
 * queue at the same time we are looking for it, causing both the
 * main thread and the event completion thread to miss it.
 */
void ib_rndzv_asend(struct MP_msg_entry* msg)
{
  VAPI_mr_hndl_t*        mr_hndl_ptr;
  VAPI_mrw_t             mr_out;
  struct MP_msg_entry*   cur_msg;
  struct MP_msg_entry*   prev_msg;

  dbprintf("  ib_rndzv_asend (%d %d %d)\n", msg->dest, msg->nbytes, msg->tag);

  /* Register the memory for the data we will be sending out with IB.
   */
  mr_hndl_ptr = malloc( sizeof(VAPI_mr_hndl_t) );

  register_memory_region(msg->buf, msg->nbytes, mr_hndl_ptr, &mr_out,
                         VAPI_EN_LOCAL_WRITE);

  /* Save memory region handle for deregistration
   */
  msg->ib_ptr = mr_hndl_ptr;

  pthread_mutex_lock(&rndzv_send_q_mutex);

  for(prev_msg=NULL, cur_msg=rndzv_send_q[msg->dest]; 
      cur_msg != NULL; 
      prev_msg=cur_msg, cur_msg=cur_msg->next)

    if(cur_msg->ib_index != -1 &&
       cur_msg->nbytes >= msg->nbytes &&
       (cur_msg->tag == msg->tag || cur_msg->tag == -1))
      break;

  if(cur_msg == NULL) {

    /* The message does not already exist in the queue, thus we haven't
     * received the RTR for this send. Add the message to the queue and
     * expect the completion handler to send the data when the RTR is
     * received. This message should have ib_index == -1 until the RTR
     * is received, so we use this to indicate that the message in the
     * queue is not yet ready to send.
     */
    dbprintf("    Did not find message in rndzv_send_q, adding this msg.\n");

    msg->ib_key = mr_out.l_key;
    msg->ib_flag = MP_RNDZV_INCOMPLETE;
    post( msg, rndzv_send_q ); /* MUTEX no need to protect, post
                                  to rndzv_send_q is called in
                                  main thread and event handler, but
                                  already protected by mutex */

    rndzv_sends_queued++;

  } else {

    /* We found a message in the queue, so we can do the send now. Replace
     * message in queue with the message passed into this function. Mark
     * the old message free, and expect MP_Wait to cleanup.
     */
    dbprintf("    Found corresponding message in rndzv_send_q, "
             "sending data now\n");

    post( msg, rndzv_posted2ib_q ); /* MUTEX no need to protect, post
                                       to rndzv_posted2ib_q is called in
                                       main thread and event handler, but
                                       already protected by mutex */



    ib_rndzv_send_data(msg->buf, msg->nbytes, mr_out.l_key, cur_msg->ib_index,
                       msg->dest, cur_msg->dest_ptr, cur_msg->ib_key, 
                       msg->tag);

    rndzv_sends_immediate++;
    
    if( prev_msg == NULL ) {

      /* Message is at head of queue
       */
      msg->next = cur_msg->next;
      rndzv_send_q[msg->dest] = msg;

    } else {

      /* Message is not at head of queue
       */

      msg->next = cur_msg->next;
      prev_msg->next = msg;

    }

    /* Mark old RTR message as free
     */
    cur_msg->buf = NULL;

    /* Since the data has not necessarily been sent yet, we can't
     * deregister the memory region. MP_Wait will block on ib_flag
     * before deregistering, so let the completion event handler
     * mark this message complete when the completion is generated.
     */
    
  }

  pthread_mutex_unlock(&rndzv_send_q_mutex);
}

/* ib_rndzv_arecv is called by MP_ARecv to initiate the receiving
 * process of a rendezvous transfer. First we register the memory region
 * the data will be received into with Infiniband. Second we add a message
 * to rndzv_recv_q with information about this transfer. Next we make sure the
 * sender is ready to receive a Ready-to-Receive message from
 * us by checking that rndzv_msgs_ack[node] == 1, and wait if we need to. 
 * We then set rndzv_msgs_ack[node] = 0 and send the RTR. We are done after
 * this, but we do expect an acknowledgement to be sent back after the sender 
 * has processed our RTR, in the form of an incoming RDMA write to set 
 * rndzv_msgs_ack[node] back to 1. This acknowledgement will happen in
 * the background, and it is unsignaled on our end.
 */
void ib_rndzv_arecv(struct MP_msg_entry* msg)
{
  VAPI_mr_hndl_t*        mr_hndl_ptr;
  VAPI_mrw_t             mr_out;
  int                    trans_id;

  dbprintf("  ib_rndzv_arecv (%d %d %d)\n", msg->src, msg->nbytes, msg->tag);

  /* Register the memory that the data will be received into with IB.
   * We enable remote writing because the sender will use an RDMA write
   * to send the data.
   */  
  mr_hndl_ptr = malloc( sizeof(VAPI_mr_hndl_t) );

  register_memory_region(msg->buf, msg->nbytes, mr_hndl_ptr, &mr_out,
                         VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE);

  /* Post a rendezvous receive for the eventual data transfer
   */
  ib_post_rndzv_recv(msg->src);

  /* Get a transaction id number for this transfer 
   */
  trans_id = ib_get_trans_id(msg->src);

  /* Add message to rendezvous receive queue, which includes a pointer
   * to the memory region handle so we can deregister the memory
   * region later. Set ib_flag to MP_RNDZV_INCOMPLETE to indicate that 
   * this message has not been completed yet. Also keep track of 
   * transaction id so we can identify this message when the transfer 
   * completes.
   */
  msg->ib_ptr   = mr_hndl_ptr;
  msg->ib_flag  = MP_RNDZV_INCOMPLETE;
  msg->ib_index = trans_id;

  post( msg, rndzv_recv_q ); /* MUTEX no need to protect, this is the only
                              place that rndzv_recv_q is posted to, and
                              this function is only called by main thread */

  /* Wait until the sending node is ready for an RTR message, and send it
   */
  if( rndzv_msgs_ack[msg->src] == 0 ) {

     rndzv_recvs_blocking_rtr++;
     
     while( rndzv_msgs_ack[msg->src] == 0 ) { MP_BLOCK_METHOD; }
  }

  rndzv_msgs_ack[msg->src] = 0;

  ib_rndzv_send_rtr_msg(msg->buf, mr_out.r_key, msg->tag, msg->nbytes,
                        trans_id, msg->src);

}

/* ib_rndzv_finish is called by MP_Wait() to complete a rendezvous send or
 * receive. We simply wait for msg->ib_flag to have the value MP_RNDZV_COMPLETE 
 * and then walk through rndzv_recv_q[msg->src] or rndzv_send_q[msg->dest] 
 * until we find this message in the list and remove it. Mark the message as 
 * free and we are done.
 */
void ib_rndzv_finish(struct MP_msg_entry* msg)
{
  VAPI_ret_t           ret;
  struct MP_msg_entry* cur_msg;
  struct MP_msg_entry* prev_msg;
  void**               queue;
  int                  node;

  /* Wait for receive to be marked as done
   */
  dbprintf("Waiting for rndzv msg to be marked as done\n");

  if(msg->ib_flag != MP_RNDZV_COMPLETE) {

     rndzv_waits_blocked++;
             
     while(msg->ib_flag != MP_RNDZV_COMPLETE) { MP_BLOCK_METHOD; }
  }

  /* Deregister memory region
   */
  dbprintf("Deregistering memory region associated with this msg\n");

  deregister_memory_region( *((VAPI_mr_hndl_t*)(msg->ib_ptr)) );

  /* Deallocate memory used for memory region handle
   */
  free(msg->ib_ptr);

  /* Set queue and node based on whether this message is a send or receive
   */
  queue = msg->send_flag == 1 ? rndzv_send_q : rndzv_recv_q;
  node  = msg->send_flag == 1 ? msg->dest    : msg->src;

  /* Walk through queue to find this message and remove it
   */
  dbprintf("Walking through queue to remove this message\n");

  for(prev_msg = NULL, cur_msg = queue[node]; 
      cur_msg != NULL; 
      prev_msg = cur_msg, cur_msg = cur_msg->next)
    
    if(cur_msg == msg) break; /* Found message */

  if(cur_msg == NULL) {
    fprintf(stderr, "ib_rndzv_finish: Error, msg not in queue\n");
    lprintf("ib_rndzv_finish_recv: Error, msg not in queue\n");
    exit(-1);
  }

  if(prev_msg == NULL)

    /* This message is at the head of the queue
     */
    queue[node] = msg->next;

  else
    
    /* This message is not at the head of the queue
     */
    prev_msg->next = msg->next;

}

/* ib_rndzv_send_rtr_msg sends a Ready-to-Receive message to the destination
 * node along with data indicating the address to write to, the rkey access
 * key, the message tag, size of data, and the transaction id to send with
 * the transfer. It is the responsibiliy of the caller of this function to 
 * ensure a rendezvous receive has been preposted at the destination node.
 */
void ib_rndzv_send_rtr_msg(void* addr, VAPI_rkey_t rkey, int tag, int nbytes, 
                           int trans_id, int node)
{
  VAPI_ret_t           ret;
  VAPI_sr_desc_t       sr;
  VAPI_sg_lst_entry_t  sg_entry;

  dbprintf("    Sending rndzv rtr message to node %d:\n"
           "       addr=%p, rkey=%d, tag=%d, nbytes=%d, trans_id=%d\n", 
           node, addr, rkey, tag, nbytes, trans_id);

  sr.opcode     = VAPI_RDMA_WRITE_WITH_IMM;
  sr.imm_data   = MP_RNDZV_READY_TO_RECEIVE;
  sr.set_se     = FALSE;
  sr.comp_type  = VAPI_SIGNALED;
  
  sr.remote_addr = (VAPI_virt_addr_t)(u_int32_t) rndzv_msgs_rem_addrs[node];
  sr.r_key       = rndzv_msgs_rem_rkeys[node];
  sr.id          = MP_RNDZV_SEND_RTR;

  sr.sg_lst_len = 1;
  sr.sg_lst_p   = &sg_entry;

  /* Copy the rendezvous message into the registered memory area */

  rndzv_msgs_out[node].addr     = addr;
  rndzv_msgs_out[node].rkey     = rkey;
  rndzv_msgs_out[node].tag      = tag;
  rndzv_msgs_out[node].nbytes   = nbytes;
  rndzv_msgs_out[node].trans_id = trans_id;

  sg_entry.lkey = rndzv_msgs_out_mr_lkey;
  sg_entry.len  = sizeof( rndzv_msgs_out[node] );
  sg_entry.addr = (VAPI_virt_addr_t)(u_int32_t) &rndzv_msgs_out[node];
  
  ret = VAPI_post_sr(hca_hndl, rndzv_qp_hndl[ node ], &sr);
  if(ret != VAPI_OK) {
    fprintf(stderr, 
            "ib_rndzv_send_rtr_msg - Error posting RDMA write req: %s\n",
            VAPI_strerror(ret));
    lprintf("ib_rndzv_send_rtr_msg - Error posting RDMA write req: %s\n",
            VAPI_strerror(ret));
    exit(-1);
  } 

}

/* ib_rndzv_send_rtr_ack sends acknowledgement of a Ready-to-Receive message
 * to the destination node. This is done by doing an RDMA write of the
 * integer value 1 into the appropriate location at the destination node.
 * This function should only be called after having received an RTR message.
 */
void ib_rndzv_send_rtr_ack(int node)
{
  VAPI_ret_t          ret;
  VAPI_sr_desc_t      sr;
  VAPI_sg_lst_entry_t sg_entry;

  dbprintf("    Sending acknowledgement of last RTR message to node %d\n",
           node);

  /* Send ack using RDMA with no immediate data 
   * ( => no work completion at dest )
   */
 
  sr.opcode      = VAPI_RDMA_WRITE;
  sr.set_se      = FALSE;

  sr.comp_type   = VAPI_SIGNALED;

  sr.remote_addr = (VAPI_virt_addr_t)(u_int32_t)rndzv_msgs_ack_rem_addrs[node];
  sr.r_key       = rndzv_msgs_ack_rem_rkeys[node];
  sr.id          = MP_RNDZV_SEND_RTR_ACK;

  sr.sg_lst_len = 1;
  sr.sg_lst_p = &sg_entry;
  
  /* Set the ack value of 1 at my index in the ack array. We just want
   * to send the value 1, and since the array is already registered
   * with IB, its easy to do it this way.
   */

  rndzv_msgs_ack[myproc] = 1;

  sg_entry.lkey = rndzv_msgs_ack_mr_lkey;
  sg_entry.len  = sizeof(int);
  sg_entry.addr = (VAPI_virt_addr_t)(u_int32_t) &rndzv_msgs_ack[myproc];

  ret = VAPI_post_sr(hca_hndl, rndzv_qp_hndl[ node ], &sr);
  if(ret != VAPI_OK) {
    fprintf(stderr, 
            "ib_rndzv_send_rtr_ack - Error posting RDMA write req: %s\n",
            VAPI_strerror(ret));
    lprintf("ib_rndzv_send_rtr_ack - Error posting RDMA write req: %s\n",
            VAPI_strerror(ret));
    exit(-1);
  } 

}

/* ib_rndzv_send_data sends the actual data to the destination node, via RDMA,
 * using the destination memory address and rkey that we received in the
 * RTR message. The transaction id we got from the receiver in the RTR message
 * is also sent in the immediate data field, so the receiver will be able
 * to determine which transfer this message is completing.
 */
void ib_rndzv_send_data(void* data, int nbytes, int lkey, int trans_id,
                        int node, void* dest_addr, VAPI_rkey_t rkey, int tag)
{
  VAPI_ret_t           ret;
  VAPI_sr_desc_t       sr;
  VAPI_sg_lst_entry_t  sg_entry;

  dbprintf("    Sending %d bytes of data to node %d via rndzv:\n" 
           "      tag=%d, trans_id=%d\n"
           "      src  addr,lkey=(%p %d)\n"
           "      dest addr,rkey=(%p %d)\n",
           nbytes, node, tag, trans_id, data, lkey, dest_addr, rkey);

  sr.opcode     = VAPI_RDMA_WRITE_WITH_IMM;
  sr.imm_data   = trans_id; /* Receive uses this to classify RDMA write */
  sr.set_se     = FALSE;
  sr.comp_type  = VAPI_SIGNALED;
  
  sr.remote_addr = (VAPI_virt_addr_t)(u_int32_t) dest_addr;
  sr.r_key       = rkey;
  sr.id          = MP_RNDZV_SEND_DATA;

  sr.sg_lst_len = 1;
  sr.sg_lst_p   = &sg_entry;

  sg_entry.lkey = lkey;
  sg_entry.len  = nbytes;
  sg_entry.addr = (VAPI_virt_addr_t)(u_int32_t) data;
  
  ret = VAPI_post_sr(hca_hndl, rndzv_qp_hndl[ node ], &sr);
  if(ret != VAPI_OK) {
    fprintf(stderr, 
            "ib_rndzv_send_data - Error posting RDMA write req: %s\n",
            VAPI_strerror(ret));
    lprintf("ib_rndzv_send_data - Error posting RDMA write req: %s\n",
            VAPI_strerror(ret));
    exit(-1);
  } 

}

/* ib_post_rndzv_recv posts a generic rendezvous receive to the VAPI layer.
 * This receive may be for accepting an incoming RTR message or an
 * incoming data transfer. The receive is signaled because we expect
 * the incoming receive to have immediate data, which we will use to
 * classify the message (In fact, if the incoming RDMA write did not
 * have immediate data, then it would not generate a completion on this
 * end anyway).
 */
void ib_post_rndzv_recv(int node)
{
  VAPI_ret_t           ret;
  VAPI_rr_desc_t       rr;
  VAPI_sg_lst_entry_t  sg_lst;
  struct MP_msg_entry* msg;

  dbprintf("    Posting rendezvous receive for node %d\n", node);

  rr.opcode    = VAPI_RECEIVE;
  rr.comp_type = VAPI_SIGNALED;
  rr.id        = MP_RNDZV_RECEIVE;

  rr.sg_lst_len = 0;
  rr.sg_lst_p   = NULL;

  /* Post message to VAPI layer */

  ret = VAPI_post_rr(hca_hndl, rndzv_qp_hndl[node], &rr);
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error posting rndzv recv for node %d: %s\n", 
            node, VAPI_strerror(ret));
    lprintf("Error posting rndzv recv %d for node %d: %s\n", 
            node, VAPI_strerror(ret));
    exit(-1);
  } 
  
  dbprintf("      posted rendezvous receive to VAPI\n");

}

/* ib_get_trans_id returns an available transaction id number. If
 * it is called within the event completion handler, it needs to 
 * protected with mutexes so the transaction id list stays consistent.
 */
int ib_get_trans_id(int node)
{
  int last_id;
  int curr_id;

  dbprintf("  Getting a transaction id for node %d\n", node);

  curr_id = last_id = current_trans_id[node];
  curr_id = (curr_id + 1) % MP_TRANS_ID_SPACE;

  while(curr_id != last_id) {

    if(trans_id_list[node][curr_id] == 0) {
      trans_id_list[node][curr_id] = 1;
      current_trans_id[node] = curr_id;
      dbprintf("  returning id %d\n", curr_id);
      return MP_TRANS_MIN_ID + curr_id;
    } else {
      curr_id = (curr_id + 1) % MP_TRANS_ID_SPACE;
    }

  }

  fprintf(stderr, "Error - ran out of space in the transaction id list.\n"
          "Increase the size of MP_TRANS_ID_SPACE in ib.c and recompile.\n");
  lprintf("Error - ran out of space in the transaction id list.\n"
          "Increase the size of MP_TRANS_ID_SPACE in ib.c and recompile.\n");
  exit(-1);
  
}

/* ib_free_trans_id is called to mark a transaction id in the list for
 * a node as available.
 */
void ib_free_trans_id(int node, int id)
{
  dbprintf("  Setting transaction id %d for node %d to be available\n", 
           id, node);

  trans_id_list[node][id - MP_TRANS_MIN_ID] = 0;
}

/* ib_rndzv_handle_recv is called by the event completion handler
 * when we get a signaled completion of a receive posted to the
 * rendezvous IB queue pair. We must determine whether the completion
 * is from an incoming Ready-to-Receive message, or an incoming
 * data transfer, by looking at the value of the immediate data
 * field in the work completion. 
 *
 * For RTR messages, we check for the existence of an MP_msg_entry message 
 * in the rndzv_send_q which would have been put there by the ib_rndzv_asend 
 * call corresponding to this message. If it does not exist, because the
 * main thread hasn't called the function yet, then we create the message 
 * and expect ib_rndzv_asend to find it when it is called. If it does exist, 
 * then we do the data send. After copying the data that was written to 
 * rndzv_msgs_in[node], we send an acknowledgement message back to the 
 * receiver so he knows he can send the next RTR message when and if he 
 * gets to it.
 *
 * For data transfers, we find the corresponding MP_msg_entry message in
 * rndzv_recv_q, and mark its ib_flag as MP_RNDZV_COMPLETE to indicate the 
 * receive finished. MP_Wait will cleanup things when it is called.
 */
void ib_rndzv_handle_recv(VAPI_wc_desc_t* wc, int node)
{
  struct MP_msg_entry* msg;
  struct rndzv_msg_entry rmsg;

  dbprintf("*** Rendezvous message received from node %d\n", node);
  
  if(wc->status != VAPI_SUCCESS) {
    fprintf(stderr, "*** Error receiving rndzv msg: status: %s\n",
            VAPI_wc_status_sym(wc->status));
    lprintf("*** Error receiving rndzv msg: status: %s\n",
            VAPI_wc_status_sym(wc->status));
    exit(-1);
  }
  
  if( wc->imm_data == MP_RNDZV_READY_TO_RECEIVE ) {

    /* Process Ready-to-Receive message
     */
    memcpy(&rmsg, &rndzv_msgs_in[node], sizeof(struct rndzv_msg_entry));

    dbprintf("***   This is a RTR message:\n");
    dbprintf("***     addr=%p, rkey=%d, tag=%d, nbytes=%d, trans_id=%d\n", 
             rmsg.addr, rmsg.rkey, rmsg.tag, rmsg.nbytes, rmsg.trans_id);


    /* Post new rendezvous receive
     */
    ib_post_rndzv_recv(node);

    /* Send acknowledgement message
     */
    ib_rndzv_send_rtr_ack(node);
    
    /* Check rndzv_send_q for the corresponding messsage
     */
    pthread_mutex_lock(&rndzv_send_q_mutex);

    for(msg=rndzv_send_q[node]; msg != NULL; msg=msg->next)
      if(msg->buf != NULL && 
         msg->ib_flag == MP_RNDZV_INCOMPLETE &&
         msg->nbytes <= rmsg.nbytes &&
         (msg->tag == rmsg.tag || rmsg.tag == -1))
        break;

    if(msg == NULL) {

      /* We did not find message. Create a new message with info received in
       * RTR, and add it to queue.
       */
      dbprintf("***   Did not find corresponding msg in rndzv_send_q\n"
               "***   Creating a new message now\n");

      /* A msg with NULL buf element will indicate that this message was
       * posted in response to an RTR message, and the data is not
       * yet ready to send.
       */
      msg = create_msg_entry( NULL, rmsg.nbytes, myproc, node, rmsg.tag );
      msg->dest_ptr = rmsg.addr;
      msg->ib_index = rmsg.trans_id;
      msg->ib_key   = rmsg.rkey;
      msg->ib_flag  = MP_RNDZV_INCOMPLETE;

      post( msg, rndzv_send_q ); /* MUTEX no need to protect, post
                                    to rndzv_posted2ib_q is called in
                                    main thread and event handler, but
                                    already protected by mutex */

    } else {

      /* We found the corresponding message. Do the send and mark the
       * message as complete
       */
      dbprintf("***   Found the corresponding msg in rndzv_send_q\n"
               "***     buf=%p, lkey=%d, nbytes=%d, tag=%d\n", 
               msg->buf, msg->ib_key, msg->nbytes, msg->tag,
               "***   Doing send and marking msg complete\n");

      post( msg, rndzv_posted2ib_q ); /* MUTEX no need to protect, post
                                         to rndzv_posted2ib_q is called in
                                         main thread and event handler, but
                                         already protected by mutex */

      ib_rndzv_send_data(msg->buf, msg->nbytes, msg->ib_key, rmsg.trans_id,
                         node, rmsg.addr, rmsg.rkey, msg->tag);

      /* We need to keep the memory region handle active until the 
       * data is sent out through IB, so don't mark message as
       * complete yet.
       */

    }
      
    pthread_mutex_unlock(&rndzv_send_q_mutex);

  } else if(wc->imm_data >= MP_TRANS_MIN_ID || 
            wc->imm_data <  MP_TRANS_MAX_ID) {
    
    /* Process incoming data transfer
     */

    dbprintf("***   We received a data transfer, trans_id=%d.\n", 
             wc->imm_data);

    for(msg=rndzv_recv_q[node]; msg != NULL; msg=msg->next)
      if(msg->ib_index == wc->imm_data)
        break;

    if(msg == NULL) {

      fprintf(stderr, "Error, didn't find a corresponding message in\n"
              "rndzv_recv_q for the data transfer.\n");
      lprintf("Error, didn't find a corresponding message in\n"
              "rndzv_recv_q for the data transfer.\n");
      exit(-1);

    }

    /* Free transaction ID, mark message complete, and expect MP_Wait to 
     * clean it up
     */
    dbprintf("***   Found the corresponding msg in rndzv_recv_q,\n"
             "***     freeing transaction id and marking msg complete\n");

    ib_free_trans_id(node, msg->ib_index);

    msg->ib_flag = MP_RNDZV_COMPLETE;

  } else {

    fprintf(stderr, "*** Error, rndzv msg has invalid imm. data: %d\n",
            wc->imm_data);
    lprintf("*** Error, rndzv msg has invalid imm. data: %d\n",
            wc->imm_data);
    exit(-1);
  }
  
}

void ib_rndzv_handle_send(VAPI_wc_desc_t* wc, int node)
{
  struct MP_msg_entry* msg;

  if(wc->status != VAPI_SUCCESS) {
    fprintf(stderr, "*** Error sending rndzv data: status: %s\n",
            VAPI_wc_status_sym(wc->status));
    lprintf("*** Error sending rndzv data: status: %s\n",
            VAPI_wc_status_sym(wc->status));
    exit(-1);
  }
  
  dbprintf("*** Rendezvous data sent out successfully\n");

  msg = rndzv_posted2ib_q[node];

  if(msg == NULL) {
    fprintf(stderr, "*** Error, posted2ib queue is empty\n");
    lprintf("*** Error, posted2ib queue is empty\n");
    exit(-1);
  }

  dbprintf("*** Marking msg complete and removing from posted2ib queue\n");

  msg->ib_flag = MP_RNDZV_COMPLETE;

  rndzv_posted2ib_q[node] = msg->mnext;

}

/* XXX - remove this when we are done debugging */
void check_qp_state()
{
  VAPI_ret_t            ret;
  VAPI_qp_attr_t        attr;
  VAPI_qp_attr_mask_t   attr_mask;
  VAPI_qp_init_attr_t   init_attr;
  int                   i;

  for(i=0; i<nprocs; i++) if(i != myproc) {

    ret = VAPI_query_qp(hca_hndl, eager_qp_hndl[i], 
                        &attr, &attr_mask, &init_attr);

    if(ret != VAPI_SUCCESS) {
      fprintf(stderr, "Error retrieving QP info: %s\n", VAPI_strerror(ret));
      lprintf("Error retrieving QP info: %s\n", VAPI_strerror(ret));
      exit(-1);
    }

    dbprintf(" Eager QP %d status: %s\n", i, VAPI_qp_state_sym(attr.qp_state));

  }

}

/* XXX - need to think more about whether there is a problem with
 * event handler being interrupted by event handler.  Since each handler
 * will only go off once for each cq, seems like things should be ok...
 *
 * Famous last words.
 *
 * Update:  Now realize that Mellanox drivers use a seperate thread
 * for the event handler, so a thread isn't interrupted, per se, but
 * access to global variables is still a concern.  Use mutex if need
 * to control concurrent access.
 */
void ib_comp_event_handler(VAPI_hca_hndl_t hca, VAPI_cq_hndl_t cq, void* data)
{
  VAPI_ret_t        ret;
  VAPI_wc_desc_t    wc;
  int               node;
  int               i;
  int               new_completion;
  static            counter=1;

  /* Use this to make gprof report info for the event handling thread
   *
   * setitimer(ITIMER_PROF, &gprof_itimer, NULL);
   */

  dbprintf("\n\n*** Completion event handler called %d time(s)\n", counter++);

  /* Poll for a completion */

  ret = VAPI_CQ_EMPTY;
  ret = VAPI_poll_cq(hca, cq, &wc);

  if( ret == VAPI_CQ_EMPTY ) {

    dbprintf("*** No completion found upon entering handler\n");

    /* For whatever reason, we did not find a competion.  Request notification
     * of next completion, and if we happen to find a completion after that,
     * process it.
     */
    if( ib_req_comp(hca, cq, &wc) == 0 )
      return;

  } else if(ret != VAPI_OK) {

    fprintf(stderr, "*** Error polling for completion: %s\n", 
            VAPI_strerror(ret));
    lprintf("*** Error polling for completion: %s\n", VAPI_strerror(ret));

    fprintf(stderr, "  hca=%d, cq=%d\n", hca, cq);
    lprintf("  hca=%d, cq=%d\n", hca, cq);
   
    if(ret == VAPI_EINVAL_CQ_HNDL) {
      fprintf(stderr, "*** (%d) cq should be one of\n", myproc);
      fprintf(stderr, "*** (%d)   recv_cq_hndl[%d] = %d\n", myproc, 1-myproc,
              recv_cq_hndl[1-myproc]);
      fprintf(stderr, "*** (%d)   send_cq_hndl[%d] = %d\n", myproc, 1-myproc,
              send_cq_hndl[1-myproc]);
    }

    exit(-1);
  }

  /* Figure out which node we're dealing with */

  for(node=-1, i=0;  node==-1 && i<nprocs;  i++) if(i != myproc) {
    
    if(cq == send_cq_hndl[i] || cq == recv_cq_hndl[i])
      node=i;
    
  }

  if(node == -1) {
    fprintf(stderr, "*** Error finding matching CQ handle in event handler\n");
    lprintf("*** Error finding matching CQ handle in event handler\n");
    exit(-1);
  }

  new_completion=1;
  while(new_completion) {
      
    dbprintf("*** Got a completion of type %s, for node %d\n", 
             VAPI_cqe_opcode_sym(wc.opcode), node);

    /* Figure out what to do with completion */

    switch( wc.id ) 
      { 
      case MP_EAGER_RECEIVE_BUF:
        ib_eager_handle_recv(&wc, node);
        break;
        
      case MP_EAGER_SEND_BUF:
        ib_eager_handle_send(&wc, node);
        break;
        
      case MP_UPDATE_BUFFERS_FREED:
        dbprintf("*** Completion handler called for RDMA bufs freed update\n");
        if(wc.status != VAPI_SUCCESS) {
          fprintf(stderr, "*** Error sending bufs freed update - status: %s\n",
                  VAPI_wc_status_sym(wc.status));
          lprintf("*** Error sending bufs freed update - status: %s\n",
                  VAPI_wc_status_sym(wc.status));
          exit(-1);
        }
        break;

      case MP_RNDZV_SEND_RTR:
        if(wc.status != VAPI_SUCCESS) {
          fprintf(stderr, "*** Error sending rndzv rtr msg: status: %s\n",
                  VAPI_wc_status_sym(wc.status));
          lprintf("*** Error sending rndzv rtr msg: status: %s\n",
                  VAPI_wc_status_sym(wc.status));
          exit(-1);
        }
        dbprintf("*** Rendezvous rtr message sent out successfully\n");
        break;

      case MP_RNDZV_SEND_DATA:
        ib_rndzv_handle_send(&wc, node);
        break;

      case MP_RNDZV_SEND_RTR_ACK:
        if(wc.status != VAPI_SUCCESS) {
          fprintf(stderr, "*** Error sending rndzv rtr ack: status: %s\n",
                  VAPI_wc_status_sym(wc.status));
          lprintf("*** Error sending rndzv rtr ack: status: %s\n",
                  VAPI_wc_status_sym(wc.status));
          exit(-1);
        }
        dbprintf("*** Redezvous RTR ack sent out successfully\n");
        break;

      case MP_RNDZV_RECEIVE:
        ib_rndzv_handle_recv(&wc, node);
        break;
        
      default:
        fprintf(stderr, "*** Unrecognized message type: %d\n", wc.id);
        lprintf("*** Unrecognized message type: %d\n", wc.id);
        exit(-1);
      }

    new_completion = ib_req_comp(hca, cq, &wc);

  } /* While loop */

  dbprintf("*** Leaving completion handler\n\n");

}

/* Return 1 if we find a completion on the queue, else return 0 */

int ib_req_comp(VAPI_hca_hndl_t hca, VAPI_cq_hndl_t cq, VAPI_wc_desc_t* wc)
{
  VAPI_ret_t      ret;
  
  /* Check for new completions, part 1 */
  
  ret = VAPI_poll_cq(hca, cq, wc);
  if( ret == VAPI_CQ_EMPTY ) {  /* No new completions */
    
    dbprintf("***  No completion found when checking, before requesting "
             "notification\n");
    
  } else if(ret != VAPI_OK) {   /* Error */
    
    fprintf(stderr, "*** Error polling for new completions: %s\n", 
            VAPI_strerror(ret));
    lprintf("*** Error polling for new completions: %s\n", 
            VAPI_strerror(ret));
    exit(-1);
    
  } else {  /* New completion since we registered handler */
    
    dbprintf("***  Found another completion, skipping request for \n" 
             "      completion notification and processing it now\n");
    return 1;
    
  }

  /* Request completion */

  dbprintf("***  Requesting completion notification\n");

  ret = VAPI_req_comp_notif(hca_hndl, cq, VAPI_NEXT_COMP);
  if(ret != VAPI_OK) {
    fprintf(stderr, "*** Error requesting completion notification: %s\n",
            VAPI_strerror(ret));
    lprintf("*** Error requesting completion notification: %s\n",
            VAPI_strerror(ret));
    exit(-1);
  }
  
  /* Check for new completions, part 2 */

  ret = VAPI_poll_cq(hca, cq, wc);
  if( ret == VAPI_CQ_EMPTY ) {  /* No new completions */

    dbprintf("***  No completion found when checking, after requesting "
             "notification\n");

  } else if(ret != VAPI_OK) {   /* Error */

    fprintf(stderr, "*** Error polling for new completions: %s\n", 
            VAPI_strerror(ret));
    lprintf("*** Error polling for new completions: %s\n", 
            VAPI_strerror(ret));
    exit(-1);

  } else {  /* New completion since we registered handler */

    dbprintf("***  Found another completion, processing it now\n");

    return 1;

  }

  return 0;

}


/* XXX - This function needs to be thread-safe.  The use of eager_recv_
 * buffer_in_use is probably not thread-safe.  Currently, this function
 * is only called in two places, and those places are both protected
 * from each other through a mutex.  So everything should be ok for now.
 */
void ib_post_eager_recv(int src)
{
  VAPI_ret_t           ret;
  VAPI_rr_desc_t       rr;
  VAPI_sg_lst_entry_t  sg_lst;
  void*                addr;
  struct MP_msg_entry* msg;
  int                  free_buf;

  dbprintf("    Posting eager receive for node %d\n", src);

  /* Find next free buffer */
  
  for(free_buf=0; free_buf<EAGER_BUFFERS_PER_NODE; free_buf++) {
    
    if( eager_recv_buffer_in_use[src][free_buf] == 0 ) {

      /* Mark buffer in use, break from loop */

      eager_recv_buffer_in_use[src][free_buf] = 1;

      break;

    }

  }

  if( free_buf == EAGER_BUFFERS_PER_NODE ) {
    fprintf(stderr, "Error, no free eager receive buffers\n");
    lprintf("Error, no free eager receive buffers\n");
    exit(-1);
  }

  /* Calculate address of eager buffer */

  addr = eager_recv_buffers[ src ] + ( free_buf * EAGER_BUFFER_SIZE );

  msg = create_msg_entry( addr, EAGER_BUFFER_SIZE, src, -1, 0 );
 
  rr.opcode    = VAPI_RECEIVE;
  rr.comp_type = VAPI_SIGNALED;
  rr.id        = MP_EAGER_RECEIVE_BUF;

  sg_lst.addr = (VAPI_virt_addr_t)(u_int32_t) addr;
  sg_lst.lkey = eager_r_lkey[ src ];
  sg_lst.len  = EAGER_BUFFER_SIZE;
  
  rr.sg_lst_len = 1;
  rr.sg_lst_p = &sg_lst;

  /* Post message to VAPI layer */

  ret = VAPI_post_rr(hca_hndl, eager_qp_hndl[ src ], &rr);
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error posting eager recv for node %d: %s\n", 
            src, VAPI_strerror(ret));
    lprintf("Error posting eager recv %d for node %d: %s\n", 
            src, VAPI_strerror(ret));
    exit(-1);
  } 
  
  dbprintf("      posted eager receive to VAPI using buffer %d\n", free_buf);

  /* Post message to eager IB receive queue */

  msg->ib_flag = MP_WAITING_EAGER_RECV_COMPLETION;
  msg->ib_index = free_buf;

  post( msg, eager_ib_recv_q ); /* MUTEX no mutex in post needed since
                                   currently this is only location in which
                                   eager_ib_recv_q is posted to, and though
                                   this function is called in both threads,
                                   it is already protected by a mutex */

}

void ib_inc_eager_recv_freed(int node)
{
  VAPI_ret_t          ret;
  VAPI_sr_desc_t      sr;
  VAPI_sg_lst_entry_t sg_entry;

  dbprintf("    Incrementing count of eager receive buffers freed\n");

  /* Increment local count */

  /* XXX - potential problem if two threads process next instruction
   * at same time, but currently this function is protected by mutexes
   * in each location it is called.
   */

  INC_WITH_WRAP( eager_loc_recv_freed[ node ] );
  
  /* Update remote count using RDMA */
 
  sr.opcode      = VAPI_RDMA_WRITE;
  sr.imm_data    = 0;
  sr.set_se      = FALSE;

  /* XXX - Doing a signaled send every time adds approximately 2-3 us
   * to the latency of small messages, as measured with NetPIPE.
   */
  if( (eager_recvs[ node ] % (max_wr / 2)) == (max_wr/2)-1 )
    sr.comp_type   = VAPI_SIGNALED;
  else
    sr.comp_type   = VAPI_UNSIGNALED;
  sr.remote_addr = (VAPI_virt_addr_t)(u_int32_t) eager_rem_addrs[node];
  sr.r_key       = eager_rem_rkeys[node];
  sr.id          = MP_UPDATE_BUFFERS_FREED;

  sr.sg_lst_len = 1;
  sr.sg_lst_p = &sg_entry;
  
  sg_entry.lkey = eager_loc_mr_lkey;
  sg_entry.len  = sizeof( eager_loc_recv_freed[ node ] );
  sg_entry.addr = (VAPI_virt_addr_t)(u_int32_t) &eager_loc_recv_freed[ node ];

  ret = VAPI_post_sr(hca_hndl, eager_qp_hndl[ node ], &sr);
  if(ret != VAPI_OK) {
    fprintf(stderr, 
            "ib_inc_eager_recv_freed - Error posting RDMA write request: %s\n",
            VAPI_strerror(ret));
    lprintf("ib_inc_eager_recv_freed - Error posting RDMA write request: %s\n",
            VAPI_strerror(ret));
    exit(-1);
  } 

}

void bring_up_qps()
{
  int                 i; 
  IB_lid_t*           lid;
  VAPI_qp_num_t*      eager_qp_num;
  VAPI_qp_num_t*      rndzv_qp_num;
  VAPI_qp_num_t       d_sync_r_qp_num; /* QP num of node I receive from */
  VAPI_qp_num_t       d_sync_s_qp_num; /* QP num of node I send to      */
  VAPI_qp_attr_mask_t qp_attr_mask;    /* QP attribute mask             */
  VAPI_qp_attr_t      qp_attr;         /* QP attributes                 */
  VAPI_qp_cap_t       qp_cap;          /* QP capabilities               */
  VAPI_ret_t          ret;             /* Return value                  */

  dbprintf("  Bringing up Queue Pairs\n");

  /* Exchange lid's and qp_num's for eager and rndzv queue pairs */

  lid = malloc( sizeof(IB_lid_t) * nprocs );
  eager_qp_num = malloc( sizeof(VAPI_qp_num_t) * nprocs );
  rndzv_qp_num = malloc( sizeof(VAPI_qp_num_t) * nprocs );

  for(i=0; i<nprocs; i++) if(myproc != i) {
    shfs_send(&ib_lid, sizeof(IB_lid_t), i);
    shfs_send(&eager_qp_prop[i].qp_num, sizeof(VAPI_qp_num_t), i);
    shfs_send(&rndzv_qp_prop[i].qp_num, sizeof(VAPI_qp_num_t), i);
  }

  shfs_barrier();

  for(i=0; i<nprocs; i++) if(myproc != i) {
    shfs_recv(&lid[i], sizeof(IB_lid_t), i);
    shfs_recv(&eager_qp_num[i], sizeof(VAPI_qp_num_t), i);
    shfs_recv(&rndzv_qp_num[i], sizeof(VAPI_qp_num_t), i);
    dbprintf("    Read QP info from node %d: lid=%d, e_qp_num=%d, "
             "r_qp_num=%d\n", i, lid[i], eager_qp_num[i], rndzv_qp_num[i]);
  }

  /* Exchange qp_num's for synchronization queue pairs */

  sync_send_node = (myproc+1) % nprocs;
  sync_recv_node = (myproc==0) ? nprocs-1 : myproc-1;

  /* Sync with both nodes */
  shfs_sync(sync_send_node);
  shfs_sync(sync_recv_node);

  /* Send my info to adjacent nodes */
  shfs_send(&sync_s_qp_num, sizeof(VAPI_qp_num_t), sync_send_node);
  shfs_send(&sync_r_qp_num, sizeof(VAPI_qp_num_t), sync_recv_node);

  /* Sync with both nodes */
  shfs_sync(sync_send_node);
  shfs_sync(sync_recv_node);

  /* Read info from node that will be sending to me */
  shfs_recv(&d_sync_r_qp_num, sizeof(VAPI_qp_num_t), sync_recv_node);

  /* Read info from node that I will send to */
  shfs_recv(&d_sync_s_qp_num, sizeof(VAPI_qp_num_t), sync_send_node);

  dbprintf("    Read sync QP info from node %d: qp_num=%d\n",
           sync_send_node, d_sync_r_qp_num);
  dbprintf("    Read sync QP info from node %d: qp_num=%d\n",
           sync_recv_node, d_sync_s_qp_num);

  /***************************************/
  /* Bring up communications queue pairs */
  /***************************************/

  /* XXX - this is probably broken for > 2 nodes, due to the order
   * that nodes try to sync up with each other.  Fix this when we have time
   * to think about it (and we have more than 2 cards plus a switch to test 
   * it on)!
   *
   * Actually this probably does work, but it is not very efficient for a
   * large number of nodes.  We should use a reduceall-type pattern
   * or something.
   */
  for(i=0; i<nprocs; i++) if(myproc != i) {

    /******** state transition: RESET -> INIT *********/
  
    modifyQP_RESET_to_INIT( &eager_qp_hndl[i] );

    dbprintf("    Modified eager QP for node %d to INIT\n", i);

    modifyQP_RESET_to_INIT( &rndzv_qp_hndl[i] );

    dbprintf("    Modified rndzv QP for node %d to INIT\n", i);

    /******** state transition: INIT -> RTR *********/
  
    modifyQP_INIT_to_RTR( &eager_qp_hndl[i],
                          eager_qp_num[i],
                          lid[i] );

    dbprintf("    Modified eager QP for node %d to RTR\n", i);

    modifyQP_INIT_to_RTR( &rndzv_qp_hndl[i],
                          rndzv_qp_num[i],
                          lid[i] );

    dbprintf("    Modified rndzv QP for node %d to RTR\n", i);

    /* Sync before going to RTS */
    shfs_sync(i);

    /******** state transition: RTR -> RTS *********/

    modifyQP_RTR_to_RTS( &eager_qp_hndl[i] );
  
    dbprintf("    Modified eager QP for node %d to RTS\n", i);

    modifyQP_RTR_to_RTS( &rndzv_qp_hndl[i] );
  
    dbprintf("    Modified rndzv QP for node %d to RTS\n", i);

    dbprintf("    Doing send/receive test with node %d via eager channel\n",i);

    doSendRecvTest(i, eager_qp_hndl[i], send_cq_hndl[i], recv_cq_hndl[i]);

    dbprintf("      Test successful\n");

    dbprintf("    Doing send/receive test with node %d via rndzv channel\n",i);

    doSendRecvTest(i, rndzv_qp_hndl[i], send_cq_hndl[i], recv_cq_hndl[i]);

    dbprintf("      Test successful\n");


  }  
  
  /****************************************/
  /* Bring up synchronization queue pairs */
  /****************************************/

  /******** state transition: RESET -> INIT *********/

  modifyQP_RESET_to_INIT( &sync_s_qp_hndl );
  if(nprocs > 2)
    modifyQP_RESET_to_INIT( &sync_r_qp_hndl );

  dbprintf("    Modified QP for sync send and receive to INIT\n");
  
  /******** state transition: INIT -> RTR *********/
 
  modifyQP_INIT_to_RTR( &sync_s_qp_hndl, d_sync_s_qp_num,
                        lid[ sync_send_node ] );
  if(nprocs > 2)
    modifyQP_INIT_to_RTR( &sync_r_qp_hndl, d_sync_r_qp_num,
                          lid[ sync_recv_node ] );

  dbprintf("    Modified QP for sync send and receive to RTR\n");
  
  /* Sync before going to RTS */
  shfs_sync(sync_send_node);
  if(nprocs > 2)
    shfs_sync(sync_recv_node);

  /******** state transition: RTR -> RTS *********/
  
  modifyQP_RTR_to_RTS( &sync_s_qp_hndl );
  if(nprocs > 2)
    modifyQP_RTR_to_RTS( &sync_r_qp_hndl );

  dbprintf("    Modified QP for sync send and receive to RTS\n");
  
  if(myproc % 2 == 0) {

    dbprintf("    Doing send/receive test with node %d via sync channel\n", 
             sync_send_node);
    doSendRecvTest(sync_send_node, sync_s_qp_hndl, sync_cq_hndl, sync_cq_hndl);
    dbprintf("    Test successful\n");

  } else {

    dbprintf("    Doing send/receive test with node %d via sync channel\n", 
             sync_recv_node);
    doSendRecvTest(sync_recv_node, sync_r_qp_hndl, sync_cq_hndl, sync_cq_hndl);
    dbprintf("    Test successful\n");

  }

  if(nprocs > 2) {

    if(myproc % 2 != 0) {
      
      dbprintf("    Doing send/receive test with node %d via sync channel\n",
               sync_send_node);
      doSendRecvTest(sync_send_node, sync_s_qp_hndl, sync_cq_hndl, 
                     sync_cq_hndl);
      dbprintf("    Test successful\n");
      
    } else {
      
      dbprintf("    Doing send/receive test with node %d via sync channel\n",
               sync_recv_node);
      doSendRecvTest(sync_recv_node, sync_r_qp_hndl, sync_cq_hndl,
                     sync_cq_hndl);
      dbprintf("    Test successful\n");
      
    }


  }

  free(lid);
  free(eager_qp_num);
  free(rndzv_qp_num);

}

void modifyQP_RESET_to_INIT(const VAPI_qp_hndl_t* p_qp_hndl)
{
  VAPI_qp_attr_mask_t qp_attr_mask;
  VAPI_qp_attr_t      qp_attr;
  VAPI_qp_cap_t       qp_cap;
  VAPI_ret_t          ret;
  
  QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
  
  qp_attr.qp_state = VAPI_INIT;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);
  
  qp_attr.pkey_ix = 0;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PKEY_IX);
  
  qp_attr.port = ib_port_num;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PORT);
  
  qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE | VAPI_EN_REM_READ;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);
  
  ret = VAPI_modify_qp(hca_hndl, *p_qp_hndl, &qp_attr, &qp_attr_mask, 
                       &qp_cap);
  
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error modifying QP to INIT: %s\n", VAPI_strerror(ret));
    lprintf("Error modifying QP to INIT: %s\n", VAPI_strerror(ret));
    exit(-1);
  }
}

void modifyQP_INIT_to_RTR(const VAPI_qp_hndl_t* p_qp_hndl, 
                          VAPI_qp_num_t qp_num, int lid)
{
  VAPI_qp_attr_mask_t qp_attr_mask;
  VAPI_qp_attr_t      qp_attr;
  VAPI_qp_cap_t       qp_cap;
  VAPI_ret_t          ret;

  QP_ATTR_MASK_CLR_ALL(qp_attr_mask);

  qp_attr.qp_state = VAPI_RTR;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);

  qp_attr.qp_ous_rd_atom = 1;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_OUS_RD_ATOM);

  qp_attr.dest_qp_num = qp_num;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_DEST_QP_NUM);

  qp_attr.av.sl = 0;
  qp_attr.av.grh_flag = FALSE;
  qp_attr.av.dlid = lid;
  qp_attr.av.static_rate = 0;
  qp_attr.av.src_path_bits = 0;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_AV);

  qp_attr.path_mtu = ib_mtu;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PATH_MTU);

  qp_attr.rq_psn = 0;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RQ_PSN);

  qp_attr.pkey_ix = 0;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PKEY_IX);

  qp_attr.min_rnr_timer = 5;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_MIN_RNR_TIMER);
  
  ret = VAPI_modify_qp(hca_hndl, *p_qp_hndl, &qp_attr, &qp_attr_mask, 
                       &qp_cap);

  if(ret != VAPI_OK) {
    fprintf(stderr, "Error modifying QP to RTR: %s\n", VAPI_strerror(ret));
    lprintf("Error modifying QP to RTR: %s\n", VAPI_strerror(ret));
    exit(-1);
  }

}

void modifyQP_RTR_to_RTS(const VAPI_qp_hndl_t* p_qp_hndl)
{
  VAPI_qp_attr_mask_t qp_attr_mask;
  VAPI_qp_attr_t      qp_attr;
  VAPI_qp_cap_t       qp_cap;
  VAPI_ret_t          ret;

  QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
  
  qp_attr.qp_state = VAPI_RTS;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);
  
  qp_attr.sq_psn = 0;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_SQ_PSN);
  
  qp_attr.timeout = 0x20;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_TIMEOUT);
  
  qp_attr.retry_count = 1;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RETRY_COUNT);
  
  qp_attr.rnr_retry = 1;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RNR_RETRY);
  
  qp_attr.ous_dst_rd_atom = 1;
  QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_OUS_DST_RD_ATOM);
  
  ret = VAPI_modify_qp(hca_hndl, *p_qp_hndl, &qp_attr, &qp_attr_mask, 
                       &qp_cap);

  if(ret != VAPI_OK) {
    fprintf(stderr, "Error modifying QP to RTS: %s\n", VAPI_strerror(ret));
    lprintf("Error modifying QP to RTS: %s\n", VAPI_strerror(ret));
    exit(-1);
  }

}

/* Do a test run of posting a receive, sending a message, 
 * and polling for completion of both.
 */
void doSendRecvTest(int node, VAPI_qp_hndl_t qp_hndl, VAPI_cq_hndl_t s_cq_hndl,
                    VAPI_cq_hndl_t r_cq_hndl)
{
  int                 data_in;
  int                 data_out;
  VAPI_ret_t          ret;
  VAPI_mrw_t          send_mr_out;
  VAPI_mrw_t          recv_mr_out;
  VAPI_mr_hndl_t      send_mr_hndl;
  VAPI_mr_hndl_t      recv_mr_hndl;
  VAPI_rr_desc_t      rr;
  VAPI_sr_desc_t      sr;
  VAPI_sg_lst_entry_t sg_entry;
  VAPI_wc_desc_t      wc;

  register_memory_region(&data_in, sizeof(int), &recv_mr_hndl, &recv_mr_out,
                         VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE);

  register_memory_region(&data_out, sizeof(int), &send_mr_hndl, &send_mr_out,
                         VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE);

  /* Initialize buffers 
   */
  data_out = 1;
  data_in = 0;

  /* Post receive 
   */
  rr.opcode = VAPI_RECEIVE;
  rr.comp_type = VAPI_SIGNALED;
  rr.id = 0;

  rr.sg_lst_len = 1;
  rr.sg_lst_p = &sg_entry;

  sg_entry.lkey = recv_mr_out.l_key;
  sg_entry.len = sizeof(int);
  sg_entry.addr = (VAPI_virt_addr_t)(u_int32_t)&data_in;
  
  ret = VAPI_post_rr(hca_hndl, qp_hndl, &rr);
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error posting recv request: %s\n", VAPI_strerror(ret));
    lprintf("Error posting recv request: %s\n", VAPI_strerror(ret));
    exit(-1);
  } else {
    dbprintf("      Posted signaled receive request\n");
  }

  /* Sync with other node 
   */
  shfs_sync(node);

  sr.opcode = VAPI_SEND;
  sr.comp_type = VAPI_SIGNALED;
  sr.set_se = FALSE;  /* Set solicited event flag, not using events */
  sr.remote_qkey = 0; /* Remote Queue Pair key */
  sr.id = 0;          /* Request ID */

  sr.sg_lst_len = 1;
  sr.sg_lst_p = &sg_entry;

  sg_entry.lkey = send_mr_out.l_key; /* Local memory region key */
  sg_entry.len = sizeof(int);
  sg_entry.addr = (VAPI_virt_addr_t)(u_int32_t)&data_out;

  ret = VAPI_post_sr(hca_hndl, qp_hndl, &sr);
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error posting send request: %s\n", VAPI_strerror(ret));
    lprintf("Error posting send request: %s\n", VAPI_strerror(ret));
    exit(-1);
  } else {
    dbprintf("      Posted signaled send request\n");
  }

  /* Poll for completion on send queue 
   */
  ret = VAPI_CQ_EMPTY;
  while(ret == VAPI_CQ_EMPTY)
    ret = VAPI_poll_cq(hca_hndl, s_cq_hndl, &wc);
  
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error polling for completion on send queue: %s\n",
            VAPI_strerror(ret));
    lprintf("Error polling for completion on send queue: %s\n", 
            VAPI_strerror(ret));
    exit(-1);
  }
  
  dbprintf("      Got completion on send queue, status = %s\n",
           VAPI_wc_status_sym(wc.status));
  
  if(wc.status != VAPI_SUCCESS) {
    fprintf(stderr, "Error with send, status = %s\n", 
            VAPI_wc_status_sym(wc.status));
    lprintf("Error with send, status = %s\n", VAPI_wc_status_sym(wc.status));
    exit(-1);
  }

  /* Poll for completion on receive queue 
   */
  ret = VAPI_CQ_EMPTY;
  while(ret == VAPI_CQ_EMPTY)
    ret = VAPI_poll_cq(hca_hndl, r_cq_hndl, &wc);
  
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error polling for completion on recv queue: %s\n",
            VAPI_strerror(ret));
    lprintf("Error polling for completion on recv queue: %s\n", 
            VAPI_strerror(ret));
    exit(-1);
  }
  
  dbprintf("      Got completion on receive queue, status = %s\n",
           VAPI_wc_status_sym(wc.status));
  
  if(wc.status != VAPI_SUCCESS) {
    fprintf(stderr, "Error with receive, status = %s\n", 
            VAPI_wc_status_sym(wc.status));
    lprintf("Error with receive, status = %s\n", 
            VAPI_wc_status_sym(wc.status));
    exit(-1);
  }

  if(data_in != data_out) {
    fprintf(stderr, "Error, received data (%d) did not match sent data(%d)\n",
            data_in, data_out);
    lprintf("Error, received data (%d) did not match sent data(%d)\n",
            data_in, data_out);
    exit(-1);
  }
  
  dbprintf("      Received data matches sent data\n");

  /* deregister memory 
   */
  deregister_memory_region(send_mr_hndl);

  deregister_memory_region(recv_mr_hndl);

}

/* register_memory_region calls VAPI_register_mr to register a new
 * memory region with the access rights given in the acl argument.
 * All memory registration should occur through this function. If
 * the persistent memory region switch is turned on at the top of
 * this file, then we will add information about each memory region
 * to a data structure, and every call to this function will check the
 * data struct to determine if the memory has already been registered.
 */
void register_memory_region(void* start, int nbytes, VAPI_mr_hndl_t* hndl,
                            VAPI_mrw_t* mr_out, VAPI_mrw_acl_t acl)
{
  VAPI_ret_t ret;
  VAPI_mrw_t mr_in;
  
#if defined(RNDZV_PERSISTENT_MEM_REGS)
  VAPI_mr_change_t mr_change;

  if( persist_mr_get(start, hndl, mr_out) ) {
    dbprintf("      Found address %p in mr list (hndl=%d)\n", start, *hndl);
    
    if( (mr_out->acl & acl) != acl || mr_out->size < nbytes) {

      memcpy(&mr_in, mr_out, sizeof(VAPI_mrw_t));

      if( (mr_out->acl & acl) != acl) {
        dbprintf("        acls do not match\n");
        mr_in.acl |= acl;
        mr_change = VAPI_MR_CHANGE_ACL;
      }

      if( mr_out->size < nbytes ) {
        dbprintf("        requested size is larger than stored size\n");
        mr_in.size = nbytes;
        mr_change |= VAPI_MR_CHANGE_TRANS;
      }

      /* XXX apparently reregister is not yet supported in Mellanox's
       * VAPI layer. Once it is supported, it makes more sense to use this.
       * This section should be called relatively few times per memory
       * region, depending on usage, so not a big deal.

      ret = VAPI_reregister_mr(hca_hndl, *hndl, mr_change, &mr_in, hndl, 
                               mr_out);
      */

      ret = VAPI_deregister_mr(hca_hndl, *hndl);
      if(ret != VAPI_OK) {
        fprintf(stderr, "Error deregistering mr: %s\n", VAPI_strerror(ret));
        lprintf("Error deregistering mr: %s\n", VAPI_strerror(ret));
        exit(-1);
      }

      ret = VAPI_register_mr(hca_hndl, &mr_in, hndl, mr_out);

      if(ret != VAPI_OK) {
        fprintf(stderr, "Error reregistering memory region: %s\n",
                VAPI_strerror(ret));
        lprintf("Error reregistering memory region: %s\n", VAPI_strerror(ret));
        exit(-1);
      }

      dbprintf("         saving new memory region attributes\n");
      persist_mr_edit(start, *hndl, mr_out);

    } else {

      dbprintf("        requested attributes match stored attributes\n");
    
    }

    return;
  }
#endif

  dbprintf("      Registering new memory region for ptr=%p, nbytes=%d\n",
           start, nbytes);

  /* Set memory region properties */
  
  mr_in.acl = acl;
  mr_in.l_key = 0;
  mr_in.pd_hndl = pd_hndl;
  mr_in.r_key = 0;
  mr_in.size = nbytes;
  mr_in.start = (VAPI_virt_addr_t)(u_int32_t)start;
  mr_in.type = VAPI_MR;

  /* Register memory */

  ret = VAPI_register_mr(hca_hndl, &mr_in, hndl, mr_out);
  if(ret != VAPI_OK) {
    fprintf(stderr,"Error registering memory region: %s\n",VAPI_strerror(ret));
    lprintf("Error registering memory region: %s\n", VAPI_strerror(ret));
    exit(-1);
  } else {
    dbprintf("      Registered memory region (hndl=%d)\n", *hndl);
  }

#if defined(RNDZV_PERSISTENT_MEM_REGS)
  dbprintf("        Adding memory region to persistent mr list\n");
  persist_mr_add(start, *hndl, mr_out);
#endif

}

/* deregister_memory_region calls VAPI_deregister_mr to deregister
 * a memory region. All memory deregistration should occur through
 * this function. If the persistent memory region switch is turned on
 * at the top of this file, then we will not deregister memory in
 * this function, but rather wait until our data structure reaches
 * a certain size, and then begin evicting items from the data
 * structure and deregistering the corresponding memory regions as new
 * memory regions are registered.
 */
void deregister_memory_region(VAPI_mr_hndl_t hndl)
{
#if ! defined(RNDZV_PERSISTENT_MEM_REGS)
       
  VAPI_ret_t ret;

  ret = VAPI_deregister_mr(hca_hndl, hndl);
  if(ret != VAPI_OK) {
    fprintf(stderr, "Error deregistering memory region: %s\n", 
            VAPI_strerror(ret));
    lprintf("Error deregistering memory region: %s\n", VAPI_strerror(ret));
    exit(-1);
  }

  dbprintf("        Deregistered memory region (hndl=%d)\n", hndl);
#else
  dbprintf("        Bypassed deregistration of memory\n");
#endif
}          

/* Explanation of persistent memory region data structure:
 * 
 * Currently the data structure can be either a plain binary search tree (BST),
 * or a red-black binary search tree. #define USE_REDBLACK_TREE below will use
 * the red-black tree, otherwise a plain BST is used. The advantage of a
 * red-black tree is that the insertion and deletion methods take a little
 * extra time to ensure that the tree remains balanced. Otherwise, with a
 * regular binary search tree, if nodes are added with a trend of increasing
 * or decreasing keys, then we'll be building a tree that looks more like a 
 * linked-list, and the worst-case search time will be closer to N rather than
 * lg N (log N base 2), where N = number of nodes in the tree. This trend is 
 * especially likely since memory addresses are used as the data structure 
 * keys, and successive calls to malloc tend to return increasing addresses.
 *
 * The advantages of a balanced binary search tree over a linked-list or
 * fixed-size array are minimized when there are few nodes in the data
 * structure, but we do not know the user's usage patterns. This data
 * structure will be most effective when the program using this library
 * triggers many rendezvous transfers from different areas in memory.
 * 
 * The implementation of the red-black tree functionality was taken from
 * chapter 14, "Red-Black Trees", of:
 * 
 * Thomas H. Cormen, Charles E. Leiserson, Ronald L. Rivest. "Introduction to 
 * Algorithms." McGraw-Hill, 1990.
 */

#define USE_REDBLACK_TREE

/* persist_mr_add adds an element to our persistent memory region
 * data structure which includes address, the memory region handle,
 * and the memory region properties. It is always assumed that the
 * address passed in has not already been added to the data structure.
 */
void persist_mr_add(void* addr, VAPI_mr_hndl_t hndl, VAPI_mrw_t* prop)
{
   /* Allocate memory for new node
    */
   struct rndzv_mr_tree_node_t* newnode = 
      malloc( sizeof( struct rndzv_mr_tree_node_t ) );

   /* Populate fields of new node
    */
   newnode->addr = addr;
   memcpy(&(newnode->hndl), &hndl, sizeof(VAPI_mr_hndl_t));
   memcpy(&(newnode->prop), prop, sizeof(VAPI_mrw_t));
   newnode->p = &nilnode;
   newnode->left = &nilnode;
   newnode->right = &nilnode;

   /* Insert new node into tree
    */
#ifdef USE_REDBLACK_TREE
   rb_insert(&rndzv_mr_tree_root, newnode);
#else
   tree_insert(&rndzv_mr_tree_root, newnode);
#endif
}

/* persist_mr_get checks the persistent memory region data structure
 * for an existing entry with the address passed in. If it is found,
 * the function returns 1 and the corresponding mr handle and mrw_t 
 * struct are returned. If the address is not found in the data structure, 
 * the fuction returns 0 and hndl and mr_out are undefined.
 */
int persist_mr_get(void* addr, VAPI_mr_hndl_t* hndl, VAPI_mrw_t* prop)
{
   struct rndzv_mr_tree_node_t* node;
  
   /* Retrieve node pointer from tree
    */ 
   node = tree_search(rndzv_mr_tree_root, addr);

   if(node == &nilnode) return 0; 

   /* Copy data from node
    */
   memcpy(hndl, &(node->hndl), sizeof(VAPI_mr_hndl_t));
   memcpy(prop, &(node->prop), sizeof(VAPI_mrw_t));
   
   return 1;
}

/* persist_mr_edit retrieves the node from the data structure with address
 * addr, and modifies is hndl and prop structure elements. It is an error
 * to call this function with an address that is not in the data structure.
 */
void persist_mr_edit(void* addr, VAPI_mr_hndl_t hndl, VAPI_mrw_t* prop)
{
   struct rndzv_mr_tree_node_t* node;
  
   /* Retrieve node pointer from tree
    */ 
   node = tree_search(rndzv_mr_tree_root, addr);

   if(node == &nilnode) {
     fprintf(stderr, "persist_mr_edit: Error, addr %p not found in mr data "
                     "struct return 0\n", addr);
     exit(-1);
   }

   /* Set new data
    */
   memcpy(&(node->hndl), &hndl, sizeof(VAPI_mr_hndl_t));
   memcpy(&(node->prop), prop, sizeof(VAPI_mrw_t));
}

/* persist_mr_del removes the node with address addr from the data structure
 * and returns its hndl and prop structure elements in the corresponding
 * function arguments. It is an error to call this function with an address
 * that is not in the data structure.
 */
void persist_mr_del(void* addr, VAPI_mr_hndl_t* hndl, VAPI_mrw_t* prop)
{
   struct rndzv_mr_tree_node_t* node;

   /* Find the node we want to delete
    */
   node = tree_search(rndzv_mr_tree_root, addr);
  
   if(node == &nilnode) {
      fprintf(stderr, "persist_mr_del: Error, did not find addr %p in data "
                      "structure\n", addr);
      exit(-1);
   } 

   /* Return data of node to be deleted to user
    */
   memcpy(hndl, &(node->hndl), sizeof(VAPI_mr_hndl_t));
   memcpy(prop, &(node->prop), sizeof(VAPI_mrw_t));
   
   /* Delete node from tree
    */ 
#ifdef USE_REDBLACK_TREE
   node = rb_delete(&rndzv_mr_tree_root, node);
#else
   node = tree_delete(&rndzv_mr_tree_root, node);
#endif

   /* Free memory used by the pointer that was spliced
    * out of the tree (not necessarily the same as the
    * pointer passed to tree_delete
    */
   free(node);
}

/* persist_mr_delrand removes a random node from the data structure and
 * returns its addr, hndl, and prop structure elements through the function's 
 * parameters, if at least one node exists in the data structure. The function
 * returns the value 1 if a node was removed and 0 if the data structure is
 * empty.
 */
int persist_mr_delrand(void** addr, VAPI_mr_hndl_t* hndl, VAPI_mrw_t* prop)
{
   struct rndzv_mr_tree_node_t* node;

   if(rndzv_mr_tree_root == &nilnode) return 0;
      
   node = tree_minimum(rndzv_mr_tree_root);
   
   /* Return data of node to be deleted to user
    */
   *addr = node->addr;
   memcpy(hndl, &(node->hndl), sizeof(VAPI_mr_hndl_t));
   memcpy(prop, &(node->prop), sizeof(VAPI_mrw_t));
         
   /* Delete node from tree
    */ 
#ifdef USE_REDBLACK_TREE
   node = rb_delete(&rndzv_mr_tree_root, node);
#else
   node = tree_delete(&rndzv_mr_tree_root, node);
#endif

   /* Free memory used by the pointer that was spliced
    * out of the tree (not necessarily the same as the
    * pointer passed to tree_delete
    */
   free(node);

   return 1;
}

/* Implementation of the binary search tree
 */

/* BST Query functions
 */
void* tree_search(struct rndzv_mr_tree_node_t* node, void* addr)
{
   while(node != &nilnode && node->addr != addr) {
      if(addr < node->addr)
         node = node->left;
      else
         node = node->right;
   }

   return (void*)node;
}

void* tree_minimum(struct rndzv_mr_tree_node_t* node)
{
   while(node->left != &nilnode)
      node = node->left;

   return (void*)node;
}

void* tree_maximum(struct rndzv_mr_tree_node_t* node)
{
   while(node->right != &nilnode)
      node = node->right;

   return (void*)node;
}

void* tree_successor(struct rndzv_mr_tree_node_t* node)
{
   struct rndzv_mr_tree_node_t* par;
   
   if(node->right != &nilnode)
      return tree_minimum(node->right);

   par = node->p;

   while(par != &nilnode && node == par->right) {
      node = par;
      par = par->p;
   }

   return (void*)par;
}

void* tree_predecessor(struct rndzv_mr_tree_node_t* node)
{
   struct rndzv_mr_tree_node_t* par;
   
   if(node->left != &nilnode)
      return tree_maximum(node->left);

   par = node->p;

   while(par != &nilnode && node == par->left) {
      node = par;
      par = par->p;
   }

   return (void*)par;
}

/* BST Insert/Delete
 */
void tree_insert(struct rndzv_mr_tree_node_t** root, 
                 struct rndzv_mr_tree_node_t* newnode)
{
   struct rndzv_mr_tree_node_t* cur = *root;
   struct rndzv_mr_tree_node_t* par = &nilnode;

   /* Find insertion location
    */
   while( cur != &nilnode ) {

      par = cur;
      
      if(newnode->addr < cur->addr)
         cur = cur->left;
      else
         cur = cur->right;

   }

   /* Set parent pointer
    */
   newnode->p = par;

   /* Insert new node
    */
   if(par == &nilnode)
      *root = newnode;
   else if(newnode->addr < par->addr)
      par->left = newnode;
   else 
      par->right = newnode;
}

void* tree_delete(struct rndzv_mr_tree_node_t** root,
                  struct rndzv_mr_tree_node_t* delnode)
{
   struct rndzv_mr_tree_node_t* splicenode;
   struct rndzv_mr_tree_node_t* x;
   
   if(delnode->left == &nilnode || delnode->right == &nilnode)
      splicenode = delnode;
   else
      splicenode = tree_successor(delnode);

   if(splicenode->left != &nilnode)
      x = splicenode->left;
   else
      x = splicenode->right;

   x->p = splicenode->p;

   if(splicenode->p == &nilnode)
      *root = x;
   else if(splicenode == splicenode->p->left)
      splicenode->p->left = x;
   else
      splicenode->p->right = x;

   if(splicenode != delnode) {
      (*root)->addr = splicenode->addr;
      memcpy( &((*root)->hndl), &(splicenode->hndl), sizeof(VAPI_mr_hndl_t) );
      memcpy( &((*root)->prop), &(splicenode->hndl), sizeof(VAPI_mrw_t) );
   }

  return (void*)splicenode;
}

/* Miscellaneous
 */

tree_print()
{
   struct rndzv_mr_tree_node_t* node;
   int i;
   
   if(rndzv_mr_tree_root != &nilnode) {
      node = tree_minimum(rndzv_mr_tree_root);
      for(i=0; node != &nilnode; i++) {
         printf("%2d: addr=%03p, hndl=%03d, prop.size=%05d\n",
                i, node->addr, node->hndl, node->prop.size);
         node = tree_successor(node);
      }
   }

}

/* Implementation of Red-Black functionality
 */
void left_rotate(struct rndzv_mr_tree_node_t** root, 
                 struct rndzv_mr_tree_node_t* node)
{
   struct rndzv_mr_tree_node_t* rchild;

   rchild = node->right;
   node->right = rchild->left;

   if(rchild->left != &nilnode)
      rchild->left->p = node;

   rchild->p = node->p;

   if(node->p == &nilnode)
      *root = rchild;
   else if(node == node->p->left)
      node->p->left = rchild;
   else
      node->p->right = rchild;

   rchild->left = node;
   node->p = rchild;
}

void right_rotate(struct rndzv_mr_tree_node_t** root, 
                  struct rndzv_mr_tree_node_t* node)
{
   struct rndzv_mr_tree_node_t* lchild;

   lchild = node->left;
   node->left = lchild->right;

   if(lchild->right != &nilnode)
      lchild->right->p = node;

   lchild->p = node->p;

   if(node->p == &nilnode)
      *root = lchild;
   else if(node == node->p->right)
      node->p->right = lchild;
   else
      node->p->left = lchild;

   lchild->right = node;
   node->p = lchild;
}

void rb_insert(struct rndzv_mr_tree_node_t** root,
               struct rndzv_mr_tree_node_t* node)
{
   struct rndzv_mr_tree_node_t* uncle; /* Parent's sibling :) */

   /* Insert node using regular bst insert, and color it red
    */
   tree_insert(root, node);
   node->color = RED;

   /* Red-black trees require every red node to have black
    * children (nil is always black). Fix the tree if this 
    * property does not hold after inserting the new node
    * by moving the violating red node up towards the root
    * node until the condition is satisfied.
    */
   while(node != *root && node->p->color == RED) {
      if(node->p == node->p->p->left) {
         /* node's parent is left child of grandparent
          */
         uncle = node->p->p->right;
         if(uncle->color == RED) {
            /* Both parent and uncle are red. Change
             * them to black, change grandparent to red, and check loop 
             * condition on the grandparent.
             */
            node->p->color = BLACK;
            uncle->color = BLACK;
            node->p->p->color = RED;
            node = node->p->p;
         } else {
            /* Parent is red but uncle is black.
             */
            if(node == node->p->right) {
               /* Node is a right child. Set node to node's parent,
                * since after the left rotation the node's parent will
                * become the node's child and thus the violating node. 
                * Do left rotation.
                */
               node = node->p;
               left_rotate(root, node);
            }
            /* Set parent black, set grandparent red, and do right rotation
             * on grandparent. The tree will now satisfy all red-black
             * properties and the loop will exit.
             */
            node->p->color = BLACK;
            node->p->p->color = RED;
            right_rotate(root, node->p->p);
         }
      } else {
         /* node's parent is right child of grandparent. See comments
          * above for symmetrical situation.
          */
         uncle = node->p->p->left;
         if(uncle->color == RED) {
            node->p->color = BLACK;
            uncle->color = BLACK;
            node->p->p->color = RED;
            node = node->p->p;
         } else {
            if(node == node->p->left) {
               node = node->p;
               right_rotate(root, node);
            }
            node->p->color = BLACK;
            node->p->p->color = RED;
            left_rotate(root, node->p->p);
         }
      }
   }
   /* Always set root to black, since this is assumed in the algorithm
    * above.
    */
   (*root)->color = BLACK;
}

void* rb_delete(struct rndzv_mr_tree_node_t** root,
                struct rndzv_mr_tree_node_t*  delnode)
{
   struct rndzv_mr_tree_node_t* splicenode;
   struct rndzv_mr_tree_node_t* tmpnode;

   if(delnode->left == &nilnode || delnode->right == &nilnode)
      splicenode = delnode;
   else
      splicenode = tree_successor(delnode);

   if(splicenode->left != &nilnode)
      tmpnode = splicenode->left;
   else
      tmpnode = splicenode->right;

   tmpnode->p = splicenode->p;

   if( splicenode->p == &nilnode)
      *root = tmpnode;
   else if(splicenode == splicenode->p->left)
      splicenode->p->left = tmpnode;
   else
      splicenode->p->right = tmpnode;

   if(splicenode != delnode) {
      delnode->addr = splicenode->addr;
      memcpy(&(splicenode->hndl), &(delnode->hndl), 
             sizeof(VAPI_mr_hndl_t));
      memcpy(&(splicenode->prop), &(delnode->prop), 
             sizeof(VAPI_mrw_t));
   }

   if(splicenode->color == BLACK)
      rb_delete_fixup(root, tmpnode);

   return splicenode;
}

void rb_delete_fixup(struct rndzv_mr_tree_node_t** root,
                     struct rndzv_mr_tree_node_t*  node)
{
   struct rndzv_mr_tree_node_t* sibling;
   
   while(node != *root && node->color == BLACK) {
      if(node == node->p->left) {
         sibling = node->p->right;
         if(sibling->color == RED) {
            sibling->color = BLACK;
            node->p->color = RED;
            left_rotate(root, node->p);
            sibling = node->p->right;
         }
         if(sibling->left->color == BLACK && sibling->right->color == BLACK) {
            sibling->color = RED;
            node = node->p;
         } else {
            if(sibling->right->color == BLACK) {
               sibling->left->color = BLACK;
               sibling->color = RED;
               right_rotate(root, sibling);
               sibling = node->p->right;
            }
            sibling->color = node->p->color;
            node->p->color = BLACK;
            sibling->right->color = BLACK;
            left_rotate(root, node->p);
            node = *root;
         }
      } else {
         sibling = node->p->left;
         if(sibling->color == RED) {
            sibling->color = BLACK;
            node->p->color = RED;
            right_rotate(root, node->p);
            sibling = node->p->left;
         }
         if(sibling->right->color == BLACK && sibling->left->color == BLACK) {
            sibling->color = RED;
            node = node->p;
         } else {
            if(sibling->left->color == BLACK) {
               sibling->right->color = BLACK;
               sibling->color = RED;
               left_rotate(root, sibling);
               sibling = node->p->left;
            }
            sibling->color = node->p->color;
            node->p->color = BLACK;
            sibling->left->color = BLACK;
            right_rotate(root, node->p);
            node = *root;
         }
      }
   }
   node->color = BLACK;
}


/* Shared file-system communications functions.  Use these to bring
 * Infiniband up without using TCP/IP.
 */

void shfs_init()
{
  /* For now, just make sure the communication files do not already
   * exist in the directory, possibly from a previously crashed run,
   * as this can mess up the communications
   */

  dbprintf("Cleaning up leftover shfs files\n");

  if(myproc == 0) {
    system("rm -f .mplite.sync_file*");
    system("rm -f .mplite.send_file*");
  }
}

void shfs_sync(int dest)
{
  char sync_filename[1024];
  FILE* sync_file;

  /* If myproc is lower, create the sync file and wait for its removal */
  if( myproc < dest ) {

    sprintf(sync_filename, ".mplite.sync_file_%d-%d", myproc, dest);
    
    if( (sync_file = fopen(sync_filename, "w")) == NULL) {
      perror("shfs_sync: Couldn't open sync_file");
      exit(-1);
    }

    if( fclose(sync_file) != 0 ) {
      perror("shfs_sync: Couldn't close sync_file");
      exit(-1);
    }

     /* Loop until syncfile is removed by other node */
    while( (sync_file = fopen(sync_filename, "r")) != NULL ) {
      fclose(sync_file);
      usleep(1);
    }

  } else { 
    /* If myproc is higher, remove the sync file after it is created */
    
    sprintf(sync_filename, ".mplite.sync_file_%d-%d", dest, myproc);
    
    while( (sync_file = fopen(sync_filename, "r")) == NULL) {
      usleep(1);
    }

    if( fclose(sync_file) != 0 ) {
      perror("shfs_sync: Couldn't close sync_file");
      exit(-1);
    }

    remove(sync_filename);
    
  }
  

}

void shfs_barrier()
{
  int i;

  for(i=0; i<nprocs; i++) if(myproc != i)

    shfs_sync(i);

}

void shfs_send(void* buf, int nbytes, int dest)
{
  char send_filename[1024];
  FILE* send_file;

  sprintf(send_filename, ".mplite.send_file_%d-%d", myproc, dest);

  if( (send_file = fopen(send_filename, "a")) == NULL) {
    perror("shfs_send: Couldn't open send_file for writing");
    exit(-1);
  }

  if( fwrite(buf, nbytes, 1, send_file) != 1 ) {
    perror("shfs_send: Couldn't write to send_file");
    exit(-1);
  }

  if( fclose(send_file) != 0 ) {
    perror("shfs_send: Couldn't close send_file after writing");
    exit(-1);
  }
  
}

/* Make sure we do a sync with node that is sending to us before
 * we call shfs_recv, to make sure all the data has been written
 */
void shfs_recv(void* buf, int nbytes, int src)
{
  static long* filepos = NULL;
  int remove_file = 0;
  char recv_filename[1024];
  FILE* recv_file;
  
  /* Allocate space for filepos array if this is the first call */
  if(filepos == NULL) {
    int i;
    filepos = malloc(sizeof(long) * nprocs);
    for(i=0;i<nprocs;i++) filepos[i]=0;
  }

  sprintf(recv_filename, ".mplite.send_file_%d-%d", src, myproc);
  
  if( (recv_file = fopen(recv_filename, "r")) == NULL) {
    perror("shfs_recv: Couldn't open recv_file");
    exit(-1);
  }

  /* Go to last position */
  if( fseek(recv_file, filepos[src], SEEK_SET) != 0 ) {
    perror("shfs_recv: Couldn't seek\n");
    exit(-1);
  }

  if( fread(buf, nbytes, 1, recv_file) != 1) {
    perror("shfs_recv: Couldn't read from recv_file");
    exit(-1);
  }

  /* Save position */
  if( (filepos[src] = ftell(recv_file)) == -1 ) {
    perror("shfs_recv: Couldn't get file position");
    exit(-1);
  }

  /* Check if we've read in the last data element */
  if( fgetc(recv_file) == EOF) { 
    remove_file=1;
    filepos[src]=0;
  }

  if( fclose(recv_file) != 0 ) {
    perror("shfs_recv: Couldn't close recv_file");
    exit(-1);
  }
 
  if( remove_file ) remove(recv_filename);
  
}

void dump_and_quit()
{
  lprintf("\n\nReceived a SIGINT (cntl-c) or SIGHUP --> dump and exit\n");

  /* Cleanup */
  ib_cleanup();
  
  fclose(mylog);
  sleep(1);

  /* Exit in set_status */
  set_status(-1, "INTERRUPT", 
             "Received a Control-C or SEG fault, setting abort signal");
  
}

struct MP_msg_entry *create_msg_entry( void *buf, int nbytes,
                                       int src, int dest, int tag)
{
   struct MP_msg_entry *msg;
   int mid_start;
   static int mid = 0;

   /* We need to make sure mid is being modified by only one process
    * at a time, since create_msg_entry can be called from both
    * the main thread and the event handling thread
    */
   pthread_mutex_lock(&msg_id_mutex);

   mid_start = mid;
   while( msg_list[++mid%MAX_MSGS]->buf != NULL ) { 
      if( mid%MAX_MSGS == mid_start ) {
         lprintf("You have more than MAX_MSGS outstanding messages\n");
         lprintf("Edit globals.h to increase MAX_MSGS then recompile and relink\n");
         errorx(MAX_MSGS,"You have more than MAX_MSGS outstanding message headers");
      }
   }
   mid = mid%MAX_MSGS;

   msg = msg_list[mid];

   *msg = msg_null;  /* sets the default values from globals.h */

   msg->id = mid;

   /* Need to wait until we're done using mid to unlock, because it 
    * may change at any time that it's not protected 
    */
   pthread_mutex_unlock(&msg_id_mutex);

   msg->buf = msg->cur_ptr = msg->ptr[0] = buf;

   set_message( msg, nbytes, src, tag);

   if( dest < 0 ) {        /* This is a recv() */
      msg->dest = myproc;
      msg->recv_flag = 1;
   } else {                /* This is a send() */
      msg->dest = dest;
      msg->send_flag = 1;
   }

   if( nics > 1 ) msg->mtu = MTU_MIN;
   else msg->mtu = 1000000;
   
   return msg;
}

     /* Send to msg_q[], including mallocing a send buffer */

void send_to_q( struct MP_msg_entry *msg )
{
   char *membuf;

   dbprintf("  Sending the message (& %d %d %d) to my own msg_q[myproc]\n",
           msg->nbytes, msg->src, msg->tag); 

   membuf = malloc(msg->nbytes);
   if( ! membuf ) errorx(0,"ib.c send_to_self() - malloc() failed");

   MP_memcpy(membuf, msg->buf, msg->nbytes);

   msg->buf = membuf;
   msg->nleft = 0;
   msg->nhdr = 0;
   msg->send_flag = 3;

   post( msg, msg_q ); /* MUTEX no need to protect, posts to msg_q only
                          occur in main user thread */
}


   /* Post a msg to the end of any of the queues
    */

void post( struct MP_msg_entry *msg, void **q)
{
   struct MP_msg_entry *p;
   int dest;
   
   /* Use a mutex here because this function may be called by both the
    * main thread and the VAPI event handling thread. We need to serialize
    * modifications to the lists to prevent corruption
    */
   /* pthread_mutex_lock(&post_msg_mutex); */

   if( q == msg_q ) {

      dbprintf("      posting msg (& %d %d %d) to msg_q[%d]\n",
              msg->nbytes, msg->src, msg->tag, msg->src); 

      if( msg_q[msg->src] == NULL ) {
         msg_q[msg->src] = msg;
      } else {
         p = msg_q[msg->src];
         while (p->mnext != NULL ) p = p->mnext;
         p->mnext = msg;
      }

   } else if( q == eager_ib_recv_q ) {

      dbprintf("      posting msg (& %d %d %d) to eager_ib_recv_q[%d]\n",
              msg->nbytes, msg->src, msg->tag, msg->src); 

      if( eager_ib_recv_q[msg->src] == NULL ) {
         eager_ib_recv_q[msg->src] = msg;
      } else {
         p = eager_ib_recv_q[msg->src];
         while (p->next != NULL ) p = p->next;
         p->next = msg;
      }

   } else if( q == eager_user_recv_q ) {

      dbprintf("      posting msg (& %d %d %d) to eager_user_recv_q[%d]\n",
              msg->nbytes, msg->src, msg->tag, msg->src); 

      if( eager_user_recv_q[msg->src] == NULL ) {
         eager_user_recv_q[msg->src] = msg;
      } else {
         p = eager_user_recv_q[msg->src];
         while (p->next != NULL ) p = p->next;
         p->next = msg;
      }

   } else if( q == eager_send_q ) {

      dbprintf("      posting msg (& %d %d %d) to eager_send_q[%d]\n",
              msg->nbytes, msg->dest, msg->tag, msg->dest); 

      if( eager_send_q[msg->dest] == NULL ) {
         eager_send_q[msg->dest] = msg;
      } else {
         p = eager_send_q[msg->dest];
         while (p->next != NULL ) p = p->next;
         p->next = msg;
      }

   } else if( q == rndzv_recv_q ) {

      dbprintf("      posting msg (& %d %d %d) to rndzv_recv_q[%d]\n",
              msg->nbytes, msg->src, msg->tag, msg->src); 

      if( rndzv_recv_q[msg->src] == NULL ) {
         rndzv_recv_q[msg->src] = msg;
      } else {
         p = rndzv_recv_q[msg->src];
         while (p->next != NULL ) p = p->next;
         p->next = msg;
      }

   } else if( q == rndzv_send_q ) {

      dbprintf("      posting msg (& %d %d %d) to rndzv_send_q[%d]\n",
              msg->nbytes, msg->dest, msg->tag, msg->dest); 

      if( rndzv_send_q[msg->dest] == NULL ) {
         rndzv_send_q[msg->dest] = msg;
      } else {
         p = rndzv_send_q[msg->dest];
         while (p->next != NULL ) p = p->next;
         p->next = msg;
      }

   } else if( q == rndzv_posted2ib_q ) {

      dbprintf("      posting msg (& %d %d %d) to rndzv_posted2ib_q[%d]\n",
              msg->nbytes, msg->dest, msg->tag, msg->dest); 

      if( rndzv_posted2ib_q[msg->dest] == NULL ) {
         rndzv_posted2ib_q[msg->dest] = msg;
      } else {
         p = rndzv_posted2ib_q[msg->dest];
         while (p->mnext != NULL ) p = p->mnext;
         p->mnext = msg;
      }


   } else {

      fprintf(stderr, "post() - Error, invalid queue specified\n");
      exit(-1);

   }

   /* pthread_mutex_unlock(&post_msg_mutex); */
}


/* Retrieve a msg from msg_q[] if src and tag match.
 *   If the message is smaller than requested, all
 *   is copied and msg->nbytes is set to the lower value,
 *   which the user can access through MP_Get_Last_Size().
 */

void recv_from_q( struct MP_msg_entry *msg )
{
   struct MP_msg_entry *p, *last_p;
   int i, num_qs = 1, tsrc = 0, n, nic;
   char *ptr;

   if( msg->src >= 0 && msg_q[msg->src] == NULL ) return;

   dbprintf("    recv_from_q() trying to match (& %d %d %d) --> ", 
            msg->nbytes, msg->src, msg->tag);

   if( msg->src < 0 ) num_qs = nprocs;  
   else tsrc = msg->src;

     /* Check msg_q[msg->src] if the src is known,
        otherwise cycle thru all queues if src = -1 */

   for( i=0; i<num_qs; i++) {

      last_p = NULL;
      p = msg_q[tsrc];
      while( p != NULL ) {

            /* Check to see that the header has been read, and
             * nbytes and tag match.
             */

         if( p->nhdr == 0 && p->nbytes <= msg->nbytes &&
            ( msg->tag < 0 || p->tag == msg->tag ) ) { /* Found a match */

            if( p->nbytes < msg->nbytes ) {  /* shorter msg OK */

               dbprintf("\nWARNING: Message of %d Bytes is shorter "
                     "than the posted recv of %d Bytes\n",
                     p->nbytes, msg->nbytes);
            }

            set_message( msg, p->nbytes, tsrc, p->tag);

            dbprintf("found a completed match in msg_q[%d]\n", tsrc);
            
            MP_memcpy( msg->buf, p->buf, msg->nbytes);
            
            msg->nleft = 0;
            msg->nhdr  = 0;
            
            n_recv_buffered++;  nb_recv_buffered += msg->nbytes - msg->nleft;

                  /* Take out of the msg_q list */

            if( last_p == NULL ) msg_q[tsrc] = p->mnext;
            else last_p->mnext = p->mnext;

            free(p->buf);
            p->buf = NULL;                 /* Mark free for reuse */
            return;                        /* Then bail */
         }
         last_p = p;
         p = p->mnext;
      }
      tsrc++;
   }

   if( msg->src == myproc )
      errorx(msg->nbytes, "ib.c recv_from_q() - no message-to-self available");

   dbprintf("no match\n");
}

void check_malloc(void* ptr)
{
  if( ptr == NULL ) {

    fprintf(stderr, "Couldn't malloc memory, exiting...\n");
    lprintf("Couldn't malloc memory, exiting...\n");
    exit(-1);

  }
}
