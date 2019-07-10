/* MP_Lite message-passing library
 * Copyright (C) 1997-2004 Dave Turner and Weiyi Chen
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

/* 
 * VIA module for MP_Lite - Weiyi Chen and Dave Turner
 */

#include "globals.h"
#include "mplite.h"
#include "via.h"

extern struct via_conn   *cd[MAX_PROCS][MAX_NICS];
extern struct via_conn   *syncPrev, *syncNext;

/* Shared with MP_Lite other modules */
struct MP_msg_entry      *msg_list[MAX_MSGS];
void                     **recv_q, **send_q, **msg_q;
int                      n_sends = 0, n_recvs = 0;
int                      n_send_packets = 0, n_recv_packets = 0;
int                      n_buffering = 0, n_rdma_packets = 0;
int                      mypid, nics;

void MP_Init(int *argc, char ***argv)
{
    FILE    *fd;
    int     iproc, i, j;
    // int     node;
    char    *ptr, garb[10], command[100];
    struct  sigaction sigact;

    errno = -1;

    gethostname(myhostname,100);
    ptr = strstr(myhostname,".");
    if (ptr != NULL) strcpy(ptr,"\0");
    mypid = getpid();


        // Open a .node.myhostname.mypid log file for each node.  After node 
        // numbers are assigned, this will be moved to .node# .

    sprintf(filename, ".node.%s.%d", myhostname, mypid);
    mylog = fopen( filename, "w" );
    if (!mylog) errorxs(0,"Couldn't open the log file %s",filename);

        // Open .mplite.config and read in nprocs and hostnames.

    fd = fopen(".mplite.config","r");
    if (!fd) errorx(0,"Couldn't open the .mplite.config file");

    fscanf(fd, "%d", &nprocs);
    fgets(garb, 10, fd);
    fscanf(fd, "%d", &nics);
    fgets(garb, 10, fd);
    fgets(prog_name, 100, fd);

    if (nics > 4) errorx(nics, "4 NICs maximum supported at this time");

    for(i = 0; i < nprocs; i++) {

        fscanf(fd, "%d", &iproc);

        for(j = 0; j < nics; j++) {
            interface[i][j] = malloc(100 * sizeof(char));
            fscanf(fd, "%s", interface[i][j]);
        }

        fgets(garb, 10, fd);

            // Cut the hostname down

        strcpy(hostname[i], interface[i][0]);
        ptr = strstr(hostname[i],".");
        if (ptr != NULL) strcpy(ptr,"\0"); 
    }

    fclose(fd);
    
    get_myproc();

    if (myproc < 0) {
        errorxs(myproc,"%s matched no entry in .mplite.config", myhostname);
    }

    lprintf("myproc: %d\n", myproc);

    initialize_status_file();

        // All proc determined

    sprintf(command, "mv .node.%s.%d .node%d", myhostname, mypid, myproc);
    system(command);

        // Determine VIA device name

    get_devices();

        // Setup VIA

    via_init();

        // Setup communication

    via_conn_setup();

    trace_set0();

        // Set up the message queues each source node.
        // Posted receives go to the end of the recv_q[src]  queue.
        // Posted sends    go to the end of the send_q[dest] queue.
        // Buffered receives go to the msg_q[src] queue.

    for (i = 0; i < MAX_MSGS; i++) {
        msg_list[i] = malloc(sizeof(struct MP_msg_entry));
        assert(msg_list[i]);
        *msg_list[i] = msg_null;
    } 
    
    recv_q = (void **) malloc( (nprocs+1)*sizeof(void *) ) + 1;
    send_q = (void **) malloc( (nprocs+1)*sizeof(void *) ) + 1;
    msg_q  = (void **) malloc( (nprocs+1)*sizeof(void *) ) + 1;

    for (i = -1; i < nprocs; i++) recv_q[i] = send_q[i] = msg_q[i] = NULL;

        // Control-C or kill will dump the messages

    sigact.sa_handler = dump_and_quit;
    sigaction(SIGINT, &sigact, NULL);
   
    /*
        // Handshake with all, just to be polite
    for (i = 0; i < nprocs; i++) if (i != myproc) {
        MP_Send(&myproc, sizeof(int), i, 0);
    }

    for (i = 0; i < nprocs; i++) if (i != myproc) {
        MP_Recv(&node, sizeof(int), i, 0);
        if (node != i) lprintf("WARNING - Handshake(%d %d)\n",i, node);
    }
    */

        // Set up a default mask for the global operations 
        // that include all procs

    MP_all_mask = malloc(nprocs * sizeof(int));
    if (!MP_all_mask) errorx(0,"mvia.c - malloc(MP_all_mask) failed");
    for (i = 0; i < nprocs; i++) MP_all_mask[i] = 1;

    MP_Sync(); 
    
    if (myproc == 0) {
        printf("------------ Done with handshaking ------------\n");
        fflush(stdout);
    }

    lprintf("Done with all the handshaking, exitting MP_Init()\n");
    lprintf("------------------------------------------------\n");
    set_status(0, "running", "");
}

void MP_Finalize(void)
{
    int i, j;

    MP_Sync();

    via_clean();

    set_status(0, "completed", "");
    lprintf("------------------------------------------------\n");
    dump_msg_state();
    lprintf("************************************************\n");

    lprintf("%d sends used %d packets\n",n_sends,n_send_packets);
    lprintf("%d receives used %d packets\n",n_recvs,n_recv_packets);
    lprintf("%d rdma packets\n", n_rdma_packets);
    lprintf("%d buffering\n", n_buffering);
    lprintf("************************************************\n");
    fclose(mylog);
    trace_dump();       // Only active if compiled with -DTRACE

        // Release resources

    for (i = 0; i < MAX_MSGS; i++) free(msg_list[i]);
    free(recv_q - 1);
    free(send_q - 1);
    free(msg_q - 1);

    free(MP_all_mask);

    for (i = 0; i < nprocs; i++)
        for (j = 0; j < nics; j++)
            free(interface[i][j]);
}


void MP_Send(void *buf, int nbytes, int dest, int tag)
{
    int msg_id;

    MP_ASend(buf, nbytes, dest, tag, &msg_id);
    MP_Wait(&msg_id);
}

void MP_ASend(void *buf, int nbytes, int dest, int tag, int *msg_id)
{
    struct MP_msg_entry *msg;

    dbprintf("\n>MP_ASend(& %d %d %d)\n", nbytes, dest, tag);

    range_check(nbytes, dest);

    msg = create_msg_entry(buf, nbytes, myproc, dest, tag);
    *msg_id = msg->id;
    msg->trace_id = trace_start(nbytes, dest);

    if (dest == myproc) send_to_q(msg);
    else post(msg, send_q);

    n_sends++;
    dbprintf("<MP_ASend()\n");
}

void MP_Recv(void *buf, int nbytes, int src, int tag)
{
    int msg_id;

    MP_ARecv(buf, nbytes, src, tag, &msg_id);
    MP_Wait(&msg_id);
}

void MP_ARecv(void *buf, int nbytes, int src, int tag, int *msg_id)
{
    struct MP_msg_entry *msg;

    dbprintf("\n>MP_ARecv(& %d %d %d)\n", nbytes, src, tag);

    if (src < 0) src = -1;
    range_check(nbytes, src);

    msg = create_msg_entry(buf, nbytes, src, -1, tag);
    *msg_id = msg->id; 
    msg->trace_id = trace_start(-nbytes, src);

    post(msg, recv_q);

    n_recvs++;
    dbprintf("<MP_ARecv()\n");
}

void MP_Wait(int *msg_id)
{
    struct MP_msg_entry *msg; 
    struct MP_msg_entry *smsg;
    
    dbprintf(">MP_Wait(%d)\n", *msg_id);

    if (*msg_id < 0) errorx(-1, "Invalid message id");
    msg = msg_list[*msg_id];

        // If receive, receive from msg_q first

    if (msg->send_flag != 1) recv_from_q(msg);

    while (msg->nleft > 0) {

        if (msg->send_flag == 1) {

            // Take a message out of send_q. 
            // Eventually smsg will equal to msg

        smsg = send_q[msg->dest];
        if (smsg == NULL) errorx(msg->id, "Message for send not posted");
        send_q[msg->dest] = smsg->next;

            // Send smsg. This is to keep the sending order

        via_do_send(smsg);

        } else {

            if (msg->src == -1) via_do_recv_from_any();
            else via_do_recv(cd[msg->src][0]);
        }
    }

    last_src = msg->src;
    last_tag = msg->tag;
    last_size = msg->nbytes;

    if (msg->send_flag != 3) msg->buf = NULL;
    
    trace_end(msg->trace_id, msg->stats, msg->send_flag);
    dbprintf("<MP_Wait()\n");
}

void MP_Block(int *msg_id) 
{
    MP_Wait(msg_id);
}

void MP_Sync(void)
{
    int r1, r2, r3, r4, token = 777, szoi = sizeof(int), tid;

    if (nprocs == 1) return;

    dbprintf(">MP_Sync() started -");
    tid = trace_start( 0, 0);
    
    r1 = r2 = r3 = r4 = -1; 
    
    if (myproc == 0) r1 = via_send(syncNext, (char *)&token, szoi);
    r2 = via_recv(syncPrev, (char *)&token, szoi);
    r3 = via_send(syncNext, (char *)&token, szoi);
    r4 = via_recv(syncPrev, (char *)&token, szoi);
    if (myproc != 0) r1 = via_send(syncNext, (char *)&token, szoi);
    
    if (r1 <= 0 || r2 <= 0 || r3 <= 0 || r4 <= 0)
        errorx(0,"problem in MP_Sync()");
    
    trace_end(tid, 0, 0);
    dbprintf(" done\n");
}

void dump_and_quit()
{
   lprintf("\n\nReceived a SIGINT (cntl-c) --> dump and exit\n");
   dump_msg_state();
   fclose(mylog);
   sleep(1);
   set_status(-1, "INTERUPT", "Received a Control-C or SEG fault, setting abort signal");
}

/* Dump the current state of all sends, recvs, and recvs buffered in msg_q[].
 */

void dump_msg_state(void)
{
   int node;
   struct MP_msg_entry *msg, *p;

   lprintf("Dumping the current state of the message queues:\n");

   for( node=0; node<nprocs; node++) if( node != myproc ) {
      msg = send_q[node];
      while( msg != NULL ) {
         lprintf("  --> Sent %d of %d bytes to node %d with tag %d %s\n", 
             msg->nbytes - msg->nleft, msg->nbytes, node, msg->tag,
             msg->send_flag == 3 ? "- the rest is buffered" : "");
         msg = msg->next;
   }  }

   for( node=0; node<nprocs; node++) if( node != myproc ) {
      msg = recv_q[node];
      while( msg != NULL ) {
         lprintf("  <-- Received %d of %d bytes from node %d with tag %d\n", 
             msg->nbytes - msg->nleft, msg->nbytes, node, msg->tag);
         msg = msg->next;
   }  }

   for( node=0; node<nprocs; node++) {
      p = msg_q[node];
      while( p != NULL ) {
         lprintf("  <-> Buffered a %d byte message from node %d with tag %d\n", 
              p->nbytes, p->src, p->tag);
         p = p->next;
   }  }
}

struct MP_msg_entry *
create_msg_entry(void *buf, int nbytes, int src, int dest, int tag)
{
    struct MP_msg_entry    *msg;
    int                    mid_start;
    static int             mid = 0; 
    
    mid_start = mid; 
    
    while (msg_list[++mid % MAX_MSGS]->buf != NULL) { 
        if (mid % MAX_MSGS == mid_start) {
            lprintf("Too mang outstanding messages\n");
            lprintf("Edit mvia.c to increase MAX_MSGS\n");
            errorx(MAX_MSGS,"Too many outstanding messages!");
        }
    } 
    
    mid = mid % MAX_MSGS;
    msg = msg_list[mid];
    *msg = msg_null;        // sets the default values from mplite.h 
    
    msg->id = mid;
    msg->buf = msg->cur_ptr = buf;
    msg->nbytes = msg->nleft = nbytes; 
    
    msg->tag = tag;
    msg->src = src; 
    
    if( dest < 0 ) {        // This is a recv()
        msg->dest = myproc;
    } else {                // This is a send()
        msg->dest = dest;
        msg->send_flag = 1;
    } 

        // Channel-bounding split and register memory
        // Only valid for large messages

    if (msg->nbytes >= RDMAW_THRESHOLD &&
        ((msg->send_flag == 1 && msg->dest != myproc) || 
        (msg->send_flag != 1 && msg->src != myproc))) {

        via_msg_split(msg, msg->nbytes);
        via_register_mem(msg);
    }

    return msg;
}


void send_to_q(struct MP_msg_entry *msg)
{
    struct MP_msg_entry *new_msg;
    char *membuf; 
    
    dbprintf("  Sending the message (%d %d %d) to my own msg_q[myproc]\n",
            msg->nbytes, msg->src, msg->tag); 
    
    membuf = malloc(msg->nbytes);
    if (!membuf) errorx(0,"send_to_self() - malloc() failed"); 
    MP_memcpy(membuf, msg->buf, msg->nbytes); 

    new_msg = create_msg_entry(membuf, msg->nbytes, myproc, -1, msg->tag);
    post(new_msg, msg_q);

    new_msg->nleft = 0;
    msg->nleft = 0; 
}


   /* Post a msg to the end of the msg_q[], recv_q[], or send_q[] queues
    */

void post( struct MP_msg_entry *msg, void **q)
{
   struct MP_msg_entry *p;
   int node = -911;

   if( q == msg_q ) {
      node = msg->src;
      dbprintf("   posting msg (& %d %d %d) to msg_q[%d]\n",
              msg->nbytes, msg->src, msg->tag, node); 
   } else if( q == recv_q ) {
      node = msg->src;
      dbprintf("   posting msg (& %d %d %d) to recv_q[%d]\n",
              msg->nbytes, msg->src, msg->tag, node); 
   } else {
      node = msg->dest;
      dbprintf("   posting msg (& %d %d %d) to send_q[%d]\n",
              msg->nbytes, msg->dest, msg->tag, node); 
   }

   if( q[node] == NULL ) {
      q[node] = msg;
   } else {
      p = q[node];
      while (p->next != NULL ) p = p->next;
      p->next = msg;
   }
}


/* Retrieve a completed msg from msg_q[] if there is a match.
 */

void recv_from_q(struct MP_msg_entry *msg)
{
    struct MP_msg_entry *p, *last_p;
    int    i, num_qs = 1, tsrc = 0;

    dbprintf("   recv_from_q() trying to match (& %d %d %d) --> ", 
        msg->nbytes, msg->src, msg->tag);
    
    if (msg->src < 0) num_qs = nprocs;  
    else tsrc = msg->src;

        // Check msg_q[msg->src] if the src is known,
        // otherwise cycle thru all queues if src = -1

    for (i = 0; i < num_qs; i++) { 
        last_p = NULL;
        p = msg_q[tsrc];

        while (p != NULL) {

                // Not match
            if ((msg->tag > 0 && p->tag != msg->tag) || 
                         (p->nbytes > msg->nbytes)) {
                last_p = p;
                p = p->next;
                continue;
            }

                // Match
            if (msg->nbytes < p->nbytes)
                errorx(0, "Message Truncated");

                // Wait until complete

            while (p->nleft > 0) via_do_recv(cd[p->src][0]);
            dbprintf("found a match in msg_q[%d]\n", tsrc);

        if (msg->nbytes >= RDMAW_THRESHOLD && msg->src != myproc) {
                via_deregister_mem(msg);
            }

            MP_memcpy(msg->buf, p->buf, p->nbytes); 
            msg->nleft -= p->nbytes;

                // Take out of the msg_q list

            if (last_p == NULL) msg_q[tsrc] = p->next;
            else last_p->next = p->next; 

                // Mark free for reuse

            free(p->buf);
            p->buf = NULL;
            
            if (msg->nleft > 0) {
                    // Continue search
                msg->buf += p->nbytes;
                msg->nbytes -= p->nbytes;
                p = p->next;

                if (msg->nbytes >= RDMAW_THRESHOLD && msg->src != myproc) {

                    via_msg_split(msg, msg->nbytes);
                    via_register_mem(msg);

                }

            } else {

                    // Take msg out of the recv_q list

                p = recv_q[msg->src];
                if (p == msg) recv_q[msg->src] = msg->next;
                else {
                    while (p->next != msg) p = p->next; 
                    p->next = msg->next;
                }
                msg->src = tsrc;
                break;
            }
        }

        if (msg->nleft == 0) break; 
        tsrc++; 
    }

    if (msg->nleft != 0) dbprintf("no match\n");
}

    // Find a posted receive

struct MP_msg_entry *find_a_posted_recv(int src, int tag, int size)
{
        // p: current pointer
        // q: points to a node just before p

    struct MP_msg_entry *p, *q;

        // search recv_q[src]
    p = recv_q[src];
    q = NULL;

    while (p!= NULL) {
        if ((p->tag == -1 || p->tag == tag) && (p->nbytes >= size)) {
            if (q == NULL) recv_q[src] = p->next;
            else q->next = p->next;
            return p;
        }
        q = p;
        p = p->next;
    }

        // search recv_q[-1]

    p = recv_q[-1];
    q = NULL;

    while (p!= NULL) {
        if ((p->tag == -1 || p->tag == tag) && (p->nbytes >= size)) {
            if (q == NULL) recv_q[src] = p->next;
            else q->next = p->next;
            return p;
        }
        q = p;
        p = p->next;
    }

    return NULL;
}

void MP_Bedcheck() { }


    /* Descriptors for RDMAW */

VIP_DESCRIPTOR           *rdmaDesc[MAX_NICS];
VIP_DESCRIPTOR           *rdmaDescHead[MAX_NICS];
VIP_MEM_HANDLE           rdmaDescHand[MAX_NICS];

    // Request an RDMA descriptor

VIP_DESCRIPTOR *via_rdma_desc_request(int nic)
{
    VIP_DESCRIPTOR *p;

    p = rdmaDescHead[nic];
    if (p != NULL) rdmaDescHead[nic] = (VIP_DESCRIPTOR *)p->CS.Next.Address; 
    return p;
}

    // Release an RDMA descriptor

void via_rdma_desc_release(VIP_DESCRIPTOR *p, int nic)
{
    p->CS.Control = VIP_CONTROL_OP_RDMAWRITE;
    p->CS.Status = 0;
    p->CS.Reserved = 0;
    p->DS[0].Remote.Reserved = 0;

    p->CS.Next.Address = rdmaDescHead[nic];
    rdmaDescHead[nic] = p;
}

extern VIP_CQ_HANDLE rcq[MAX_NICS];
extern struct via_conn *cd[MAX_PROCS][MAX_NICS];
extern VIP_MEM_HANDLE mbufHand[MAX_NICS];


    // Blocking send msg

void via_do_send(struct MP_msg_entry *msg) 
{
    struct via_conn    *c;
    via_hdr_t          hdr;
    via_hdr_rts_t      hdr_rts;

    c = cd[msg->dest][0];

        // Eager protocol

    if (msg->nbytes < RDMAW_THRESHOLD) {

            // Send header and data together

        hdr.op = OP_SEND;
        hdr.nbytes = msg->nbytes;
        hdr.tag = msg->tag;

        via_send_with_header(c, &hdr, msg->buf, msg->nbytes);

        msg->nleft = 0;

        return;
    }

        // Rendevous protocol
        // Request to send

    hdr_rts.op = OP_RDMAW_RTS;
    hdr_rts.nbytes = msg->nbytes;
    hdr_rts.tag = msg->tag;
    hdr_rts.srcId = msg->id;

    via_send_with_header(c, (via_hdr_t *)&hdr_rts, NULL, 0);

    // Wait replay and start sending

    while (msg->nleft > 0) via_do_recv(c);
}


    // Receive from a specific source (blocking)

void via_do_recv(struct via_conn *c)
{
    VIP_DESCRIPTOR    *p;

        // Receive header

    p = via_recv_one(c);

        // Do different work

    via_op(c, p);

    via_desc_release(p, c->nic);
}


    // Receive from any source

void via_do_recv_from_any()
{
    VIP_DESCRIPTOR    *p;
    VIP_VI_HANDLE     vi;
    VIP_BOOLEAN       is_recv_q;
    VIP_RETURN        rc;
    struct via_conn   *c;
    int               src;

        // Non-blocking check every sources

    for (src = 0; src < nprocs; src++) {
        if (src == myproc)  continue;

            // Try to receive the header

        c = cd[src][0];

        rc = VipCQDone(rcq[0], &vi, &is_recv_q);

        if (rc == VIP_SUCCESS) {    // Have received something
    
            assert(is_recv_q == VIP_TRUE);

            do {
                rc = VipRecvDone(vi, &p);
            } while (rc == VIP_NOT_DONE);

            via_check_error(rc, "VipRecvDone()");

                // Do different work

            via_op(c, p);

                // Re-post a descriptor because we have consumed one

            via_desc_release(p, c->nic);
            p = via_desc_request(c->nic);
            VipPostRecv(c->viHand, p, mbufHand[c->nic]);
            
        } else if (rc != VIP_NOT_DONE) {
            errorx(rc, "VipRecvDone()");
        }
    }
}

extern VIP_NIC_HANDLE nicHand[MAX_NICS];
extern VIP_MEM_ATTRIBUTES memAttrs[MAX_NICS];
extern VIP_MEM_HANDLE mHandle[MAX_MSGS][MAX_NICS];
extern int nics;

/* Dynamic memory registration cache */

struct dreg_entry dreg_cache[MAX_DREG_ENTRY];

/* Initialize cache */

void via_dreg_init()
{
    int i;

    for (i = 0; i < MAX_DREG_ENTRY; i++) {
        dreg_cache[i].status = DREG_ENTRY_INVALID;
    }
}

/* Deregister all memory */

void via_dreg_clean()
{
    int i, j;
    VIP_RETURN rc;

    for (i = 0; i < MAX_DREG_ENTRY; i++) {
        if (dreg_cache[i].status == DREG_ENTRY_CACHED) {
            for (j = 0; j < nics; j++) { 
                rc = VipDeregisterMem(nicHand[j], dreg_cache[i].buffer[j], 
                    dreg_cache[i].mHandle[j]);
                via_check_error(rc, "via_dreg_clean: VipDeregisterMem()");
            }
            dreg_cache[i].status = DREG_ENTRY_INVALID;
        }
    }
}

/* Search cache for a hit */

void via_register_mem(struct MP_msg_entry *msg)
{
    int         length = 0;
    int         first_empty = -1, least_used = -1, new_place;
    long        earliest_time = 0;
    int         i, j;
    VIP_RETURN  rc;

    /* Calculate the memory size needs to be registerd. 
     * We can't use msg->nbytes, because it may be larger than the 
     * size needs to be registered */

    if (msg->send_flag == 1) length = msg->nbytes;
    else for (i = 0; i < nics; i++) length += msg->nbl[i];

    for (i = 0; i < MAX_DREG_ENTRY; i++) {
        if (dreg_cache[i].status != DREG_ENTRY_INVALID &&
            dreg_cache[i].buffer[0] == msg->buf && 
            dreg_cache[i].nbytes == length) {

            /* cache hit */

            for (j = 0; j < nics; j++) mHandle[msg->id][j] = 
                dreg_cache[i].mHandle[j];
            dreg_cache[i].status = msg->id;
            dreg_cache[i].timestamp = MP_Time();
            return;

        } else if (dreg_cache[i].status == DREG_ENTRY_INVALID) {

            if (first_empty == -1) first_empty = i;     

        } else if (dreg_cache[i].status == DREG_ENTRY_CACHED) {

            if (least_used == -1 || earliest_time > dreg_cache[i].timestamp) {
                earliest_time = dreg_cache[i].timestamp;
                least_used = i;
            }
        }
    }

        /* cache miss */

    if (first_empty != -1) new_place = first_empty;
    else if (least_used != -1) new_place = least_used;
    else new_place = -1;

        /* deregister expired memory */

    if (new_place != -1 && new_place == least_used) {
        for (i = 0; i < nics; i++) { 
        rc = VipDeregisterMem(nicHand[i], dreg_cache[least_used].buffer[i], 
                dreg_cache[least_used].mHandle[i]);
        via_check_error(rc, "via_register_memory: VipDeregisterMem()");
        }
    }

        /* register new memory */

    for (i = 0; i < nics; i++) {
        rc = VipRegisterMem(nicHand[i], msg->buffer[i],
            msg->nbl[i], &memAttrs[i], &mHandle[msg->id][i]);
        via_check_error(rc, "via_register_mem: VipRegisterMem()");

        if (new_place != -1) {
            dreg_cache[new_place].mHandle[i] = mHandle[msg->id][i];
            dreg_cache[new_place].buffer[i] = msg->buffer[i];
        }
    }

    if (new_place != -1) {
        dreg_cache[new_place].nbytes = length;
        dreg_cache[new_place].status = msg->id;
        dreg_cache[new_place].timestamp = MP_Time();
    }
}

void via_deregister_mem(struct MP_msg_entry *msg)
{
    int        i, length = 0;
    VIP_RETURN rc;

        /* Compute the message size to be deregistered */

    if (msg->send_flag == 1) length = msg->nbytes;
    else for (i = 0; i < nics; i++) length += msg->nbl[i];

        /* If found in the cache, set the appropriate status */

    for (i = 0; i < MAX_DREG_ENTRY; i++) {
        if (dreg_cache[i].buffer[0] == msg->buf && 
            dreg_cache[i].nbytes == length) {

            if (dreg_cache[i].status == msg->id) 
                dreg_cache[i].status = DREG_ENTRY_CACHED;

            return;
        }
    }

        /* If not found, deregister the memory */

    for (i = 0; i < nics; i++) { 
        rc = VipDeregisterMem(nicHand[i], msg->buffer[i], mHandle[msg->id][i]);
        via_check_error(rc, "via_deregister_mem(): VipDeregisterMem()");
    }
}

VIP_NIC_HANDLE           nicHand[MAX_NICS];
VIP_NIC_ATTRIBUTES       nicAttrs[MAX_NICS];
VIP_VI_ATTRIBUTES        viAttrs[MAX_NICS];
VIP_VI_ATTRIBUTES        remoteViAttrs;
VIP_MEM_ATTRIBUTES       memAttrs[MAX_NICS];
VIP_PROTECTION_HANDLE    ptag[MAX_NICS];
VIP_CQ_HANDLE            scq[MAX_NICS];
VIP_CQ_HANDLE            rcq[MAX_NICS];

/* VIA device name */
char                     viaDevice[MAX_NICS][20];

/* Registered user buffer handle */
VIP_MEM_HANDLE           mHandle[MAX_MSGS][MAX_NICS];

/* Connection descriptors */
struct via_conn          *cd[MAX_PROCS][MAX_NICS];

/* Synchronization channel */
struct via_conn          *syncPrev, *syncNext;

/* mbuf */
extern struct mbuf       *mbufMem[MAX_NICS];
extern VIP_DESCRIPTOR    *descHead[MAX_NICS];
extern VIP_MEM_HANDLE    mbufHand[MAX_NICS];

/* dbuf */
extern VIP_DESCRIPTOR    *rdmaDesc[MAX_NICS];
extern VIP_DESCRIPTOR    *rdmaDescHead[MAX_NICS];
extern VIP_MEM_HANDLE    rdmaDescHand[MAX_NICS];

extern int nics;

#define    RDMA_DESC_SIZE    (sizeof(VIP_DESCRIPTOR) * RDMA_DESC_NUM)
#define    DESC_TOTAL_NUM    (DESC_POST_NUM * (nprocs - 1) + DESC_SHARE_NUM)
#define    MBUF_TOTAL_SIZE   (DESC_TOTAL_NUM * MBUF_SIZE)


    // VI Initialization

void via_init(void)
{
    int           nic, node;
    VIP_RETURN    rc;

        // Initialize NICs

    for (nic = 0; nic < nics; nic++) {

        rc = VipOpenNic(viaDevice[nic], &nicHand[nic]);
        via_check_error(rc, "via_init: VipOpenNic()");

        rc = VipNSInit(nicHand[nic], NULL);
        via_check_error(rc, "via_init: VipNSInit()");

        rc = VipQueryNic(nicHand[nic], &nicAttrs[nic]);
        via_check_error(rc, "via_init: VipQueryNic()");

        lprintf("device: %s\n", viaDevice[nic]);

        via_alloc_memory(nic);

        via_dreg_init();

        // Install error handler
        // VipErrorCallback(nicHand[nic], NULL, via_error_callback);

            // Create a connection descriptor for each node 

        for (node = 0; node < nprocs; node++) {
            if (node != myproc) cd[node][nic] = via_create_vi(node, nic, 0);
        }
    }

        // Sychronization channel

    syncNext = via_create_vi((myproc + 1) % nprocs, 0, 1);
    syncPrev = via_create_vi((myproc - 1 + nprocs) % nprocs, 0, 1);

    for (nic = 0; nic < nics; nic++) VipNSShutdown(nicHand[nic]);
}

void via_clean()
{
    int               i, j;

    via_disconnect();

        // Release vi resources

    via_dreg_clean();

    via_destroy_vi(syncNext);
    via_destroy_vi(syncPrev);

    for (i = 0; i < nics; i++) {

        for (j = 0; j < nprocs; j++) { 
            if (j != myproc) via_destroy_vi(cd[j][i]);
        }

        VipDestroyCQ(scq[i]);
        VipDestroyCQ(rcq[i]);

        VipDeregisterMem(nicHand[i], mbufMem[i], mbufHand[i]);
        VipDeregisterMem(nicHand[i], rdmaDesc[i], rdmaDescHand[i]);
        VipDestroyPtag(nicHand[i], ptag[i]);

        free(mbufMem[i]);
        free(rdmaDesc[i]);
        VipCloseNic(nicHand[i]);
    }
}


    // Create a VI for connection

struct via_conn *via_create_vi(int node, int nic, int sync)
{
    struct via_conn    *c;
    VIP_RETURN         rc;

        // Allocate descriptor

    c = (struct via_conn *)calloc(1, sizeof(struct via_conn));
    assert(c);

        // Set NIC handle

    c->peer = node;
    c->nic = nic;
    c->sync = sync;

    c->seq = MIN_SEQ;
    c->seqExp = MIN_SEQ;

        // Set address

    via_set_local_address(c);
    via_set_remote_address(c);

        // Create VI

    rc = VipCreateVi(nicHand[nic],&viAttrs[nic], 
    scq[nic], rcq[nic], &c->viHand);
    via_check_error(rc, "via_create_vi: VipCreateVi()");

        // Put receive descriptors before connect!

    via_prepare_receive(c);

    return c;
}


    // Destroy VI

void via_destroy_vi(struct via_conn *c)
{
    VipDisconnect(c->viHand);
    VipDestroyVi(c->viHand);

    free(c->localAddr);
    free(c->remoteAddr);

    free(c);
}


    // Set VI local address
    // Descriminator: (myproc, peer, nic)

void via_set_local_address(struct via_conn *c)
{
    int    addrLen;
    int    discLen, disc[3];    // Discriminator

    addrLen = nicAttrs[c->nic].NicAddressLen;
    discLen = sizeof(disc);

    c->localAddr = malloc(sizeof(VIP_NET_ADDRESS) + addrLen + discLen);
    assert(c->localAddr);

    c->localAddr->HostAddressLen = addrLen;
    c->localAddr->DiscriminatorLen = discLen;

    MP_memcpy(c->localAddr->HostAddress, 
    nicAttrs[c->nic].LocalNicAddress, addrLen);

    disc[0] = myproc;
    disc[1] = c->peer;
    if (c->sync) disc[2] = nics;
    else disc[2] = c->nic;

    MP_memcpy((c->localAddr->HostAddress) + addrLen, &disc, discLen);
}


    // Set VI remote address
    // Descriminator: (peer, myproc, nic)

void via_set_remote_address(struct via_conn *c)
{
    int           addrLen;
    int           discLen, disc[3];    // Discriminator
    VIP_RETURN    rc;

    addrLen = nicAttrs[c->nic].NicAddressLen;
    discLen = sizeof(disc);

    c->remoteAddr = malloc(sizeof(VIP_NET_ADDRESS) + addrLen + discLen);
    assert(c->remoteAddr);

    c->remoteAddr->HostAddressLen = addrLen;

    rc = VipNSGetHostByName(nicHand[c->nic], interface[c->peer][c->nic],
            c->remoteAddr, 0);
    via_check_error(rc, "via_set_remote_address: VipNSGetHostByName()");

    c->remoteAddr->DiscriminatorLen = discLen;

    disc[0] = c->peer;
    disc[1] = myproc;
    if (c->sync) disc[2] = nics;
    else disc[2] = c->nic;

    MP_memcpy((c->remoteAddr->HostAddress) + addrLen, &disc, discLen);
}


    // Allocate descriptor and data buffer

void via_alloc_memory(int nic)
{
    VIP_RETURN      rc;
    VIP_DESCRIPTOR  *p = NULL;
    int             i;

        // Memory Protection tag

    rc = VipCreatePtag(nicHand[nic], &ptag[nic]);
    via_check_error(rc, "via_alloc_memory: VipCreatePtag()");

        // Memory Attributes

    memAttrs[nic].Ptag = ptag[nic];
    memAttrs[nic].EnableRdmaWrite = VIP_TRUE;
    memAttrs[nic].EnableRdmaRead = VIP_FALSE;

        // Allocate buffer and descriptor memory

    mbufMem[nic] = (struct mbuf *)memalign(PAGE_SIZE, MBUF_TOTAL_SIZE);
    assert(mbufMem[nic]);

        // Register memory 

    rc = VipRegisterMem(nicHand[nic], mbufMem[nic], MBUF_TOTAL_SIZE,
            &memAttrs[nic], &mbufHand[nic]);
    via_check_error(rc, "via_alloc_memory: VipRegisterMem()");

        // Setup mbuf list

    for (i = 0; i < DESC_TOTAL_NUM; i++) {

        p = &mbufMem[nic][i].desc;

        p->CS.Control = VIP_CONTROL_OP_SENDRECV|VIP_CONTROL_IMMEDIATE;
        p->CS.SegCount = 1;
        p->CS.Length = BUF_SIZE;
        p->CS.Status = 0;
        p->CS.Reserved = 0;
        p->CS.Next.Address = &mbufMem[nic][i + 1].desc;
        p->DS[0].Local.Data.Address = (char *)p + sizeof(VIP_DESCRIPTOR);
        p->DS[0].Local.Handle = mbufHand[nic];
        p->DS[0].Local.Length = BUF_SIZE;

    }

    p->CS.Next.Address = NULL;
    descHead[nic] = &mbufMem[nic][0].desc;

        // Setup descriptor list for RDMAW

    rdmaDesc[nic] = (VIP_DESCRIPTOR *)memalign(PAGE_SIZE, RDMA_DESC_SIZE);
    assert(rdmaDesc);

    rc = VipRegisterMem(nicHand[nic], rdmaDesc[nic], RDMA_DESC_SIZE,
        &memAttrs[nic], &rdmaDescHand[nic]);
    via_check_error(rc, "via_alloc_memory: VipRegisterMem()");

    for (i = 0; i < RDMA_DESC_NUM; i++) {
        p = &rdmaDesc[nic][i];

        p->CS.Control = VIP_CONTROL_OP_RDMAWRITE;
        p->CS.SegCount = 2;
        p->CS.Reserved = 0;
        p->CS.Status = 0;
        p->CS.Next.Address = &rdmaDesc[nic][i + 1];
        p->DS[0].Remote.Reserved = 0;
    }

    p->CS.Next.Address = NULL;
    rdmaDescHead[nic] = &rdmaDesc[nic][0];

        // Completion queue

    rc = VipCreateCQ(nicHand[nic], 1024, &scq[nic]);
    via_check_error(rc, "VipCreateCQ(scq)");

    rc = VipCreateCQ(nicHand[nic], 1024, &rcq[nic]);
    via_check_error(rc, "VipCreateCQ(rcq)");

        // VI attributes

    if (nicAttrs[nic].ReliabilityLevelSupport >= 
            VIP_SERVICE_RELIABLE_RECEPTION) {
        viAttrs[nic].ReliabilityLevel = VIP_SERVICE_RELIABLE_RECEPTION;
    } else if (nicAttrs[nic].ReliabilityLevelSupport >=
            VIP_SERVICE_RELIABLE_DELIVERY) {
        viAttrs[nic].ReliabilityLevel = VIP_SERVICE_RELIABLE_DELIVERY;
    } else {
        viAttrs[nic].ReliabilityLevel = VIP_SERVICE_UNRELIABLE;
    }

    lprintf("NIC %d Reliability Level: %d\n", 
        nic, viAttrs[nic].ReliabilityLevel);

    viAttrs[nic].Ptag = ptag[nic];
    viAttrs[nic].EnableRdmaWrite = VIP_TRUE;
    viAttrs[nic].EnableRdmaRead = VIP_FALSE;
    viAttrs[nic].QoS = 0;
    viAttrs[nic].MaxTransferSize = nicAttrs[nic].MaxTransferSize;
}

    // Pre-post receive descriptors for each connection

void via_prepare_receive(struct via_conn *c)
{
    int                i, n;
    VIP_DESCRIPTOR    *p;

    if (c->sync) n = 2;
    else n = DESC_POST_NUM;

        // Pre-post descriptors

    for (i = 0; i < n; i++) {
        p = via_desc_request(c->nic);
        if (p == NULL) errorx(0, "via_prepare_receive()");
        VipPostRecv(c->viHand, p, mbufHand[c->nic]);
    }
}

    // Setup connection

void via_conn_setup(void)
{
    int    node, nic;
    int    j = 999;

        // Each node accepts connections from lesser nodes

    for (node = 0; node < myproc; node ++) {
        for (nic = 0; nic < nics; nic++) {
            via_accept_connection(cd[node][nic]);
            lprintf("accept connection from %d(%d)\n", node, nic);
        }
    }

        // Each node initiates connections to greater nodes

    for (node = myproc + 1; node < nprocs; node ++) {
        for (nic = 0; nic < nics; nic++) {
            via_connect_to(cd[node][nic]);
            lprintf("connect to %d(%d)\n", node, nic);
        }
    }

        // Go signal

    if (myproc < nprocs - 1) via_send(cd[myproc + 1][0], &j, sizeof(int));
    if (myproc > 0) via_recv(cd[myproc - 1][0], &j, sizeof(int));

        // Setup channel for MP_Sync()

    if (myproc == 0) {
        via_connect_to(syncNext);
        via_accept_connection(syncPrev);
    } else {
        via_accept_connection(syncPrev);
        via_connect_to(syncNext);
    }
}

    // Accept connection request

void via_accept_connection(struct via_conn *c)
{
    VIP_RETURN rc;

    rc = VipConnectWait(nicHand[c->nic], c->localAddr, TIMEOUT,
            c->remoteAddr, &remoteViAttrs, &c->connHand);
    via_check_error(rc, "via_accept_connection: VipConnectWait()");

    rc = VipConnectAccept(c->connHand, c->viHand);
    via_check_error(rc, "via_accept_connection: VipConnectAccept()");
}

    // Make connection request

void via_connect_to(struct via_conn *c)
{
    VIP_RETURN rc = VIP_NO_MATCH;

    while (rc == VIP_NO_MATCH) {
        rc = VipConnectRequest(c->viHand, c->localAddr, c->remoteAddr,
                TIMEOUT, &remoteViAttrs);
    }

    via_check_error(rc, "via_connect_to: VipConnectRequest()");
}

void via_disconnect()
{
    int i, j;

    for (i = 0; i < nics ; i++)
        for (j = 0; j < nprocs ; j++)
        if (j != myproc) VipDisconnect(cd[j][i]->viHand);

    VipDisconnect(syncNext->viHand);
    VipDisconnect(syncPrev->viHand);
}


/* Pre-registered mbuf */
struct mbuf              *mbufMem[MAX_NICS];

/* Descriptor link list header */
VIP_DESCRIPTOR        *descHead[MAX_NICS];

/* Buffer handle */
VIP_MEM_HANDLE           mbufHand[MAX_NICS];

extern VIP_DESCRIPTOR *descHead[MAX_NICS];

    // Request a descriptor

VIP_DESCRIPTOR *via_desc_request(int nic)
{
    VIP_DESCRIPTOR *p;

    p = descHead[nic];
    if (p != NULL) descHead[nic] = (VIP_DESCRIPTOR *)p->CS.Next.Address; 
    return p;
}

    // Release a descriptor

void via_desc_release(VIP_DESCRIPTOR *p, int nic)
{
    p->CS.Length = BUF_SIZE;
    p->CS.Status = 0;
    p->CS.Reserved = 0;
    p->DS[0].Local.Length = BUF_SIZE;

    p->CS.Next.Address = descHead[nic];
    descHead[nic] = p;
}

void via_post_send(struct via_conn *c, VIP_DESCRIPTOR *p)
{
    VIP_RETURN    rc;

    p->CS.ImmediateData = c->seq;
    rc = VipPostSend(c->viHand, p, p->DS[0].Local.Handle);
    via_check_error(rc, "via_post_send: VipPostSend()");

    if (c->seq == MAX_SEQ) c->seq = MIN_SEQ;
    else c->seq ++;
}

void via_post_recv(struct via_conn *c, VIP_DESCRIPTOR *p)
{
    VIP_RETURN     rc;

    rc = VipPostRecv(c->viHand, p, p->DS[0].Local.Handle);
    via_check_error(rc, "via_post_recv: VipPostRecv()");
}

extern VIP_NIC_ATTRIBUTES nicAttrs[MAX_NICS];
extern VIP_MEM_HANDLE     mHandle[MAX_MSGS][MAX_NICS];
extern struct via_conn    *cd[MAX_PROCS][MAX_NICS];
extern VIP_MEM_HANDLE     rdmaDescHand[MAX_NICS];
extern int                n_rdma_packets;
extern int                nics;

#ifndef FAST_RDMAW


    // RDMA Write, for Fast Ethernet

int via_rdmaw(struct via_conn *c, struct MP_msg_entry *msg, 
        via_hdr_cts_t *hdr_cts)
{
    VIP_DESCRIPTOR    *p[MAX_NICS];
    VIP_RETURN        rc;
    int               i, j, packetSize, mtu;

    mtu = nicAttrs[c->nic].MaxTransferSize;

        // If you are using more than 2 nics, bi-directional rdmaw will
        // have problem if using too large mtu.
        // Do not know why
        // if (nics > 2) mtu = 4096;

        // Get a descriptor for each nic

    for (i = 0; i < nics; i++) {
        p[i] = via_rdma_desc_request(i);
        assert(p[i]);

        p[i]->DS[0].Remote.Handle = hdr_cts->mHand[i];
        p[i]->DS[1].Local.Handle = mHandle[msg->id][i];
    }

        // Write data in turn

    while (msg->nbl[0] > 0) {
        for (i = 0; i < nics; i++) {

            if (msg->nbl[i] <= 0) break;
            c = cd[c->peer][i];

            packetSize = (msg->nbl[i] > mtu) ? mtu : msg->nbl[i];
        if (packetSize == msg->nbl[i]) {
        p[i]->CS.Control = 
        VIP_CONTROL_OP_RDMAWRITE|VIP_CONTROL_IMMEDIATE;
                p[i]->CS.ImmediateData = - hdr_cts->dstId;
            }

            p[i]->CS.Reserved = 0;
            p[i]->CS.Length = packetSize;
            p[i]->CS.Status = 0;
            p[i]->DS[0].Remote.Data.Address = 
                hdr_cts->addr + (msg->ptr[i] - msg->buf);
            p[i]->DS[0].Remote.Reserved = 0;
            p[i]->DS[1].Local.Data.Address = msg->ptr[i];
            p[i]->DS[1].Local.Length = packetSize;

            rc = VipPostSend(c->viHand, p[i], rdmaDescHand[i]);
            via_check_error(rc, "via_rdmaw: VipPostWait()");

            msg->ptr[i] += packetSize;
            msg->nbl[i] -= packetSize;
        }

            // Wait send to complete

        for (j = 0; j < i; j++) {
        p[j] = via_send_wait(j);
            n_rdma_packets++;
        }
    }

    for (i = 0; i < nics; i++) {
        via_rdma_desc_release(p[i], i);
    }

    return msg->nbytes;
}

#else

    // Fast RDMA Write

int via_rdmaw(struct via_conn *c, struct MP_msg_entry *msg,
        via_hdr_cts_t *hdr_cts)
{
    VIP_DESCRIPTOR    *p;
    VIP_RETURN        rc;
    int               packetSize, nDescPost[MAX_NICS];
    int               i, mtu;

    mtu = nicAttrs[0].MaxTransferSize;

    for (i = 0; i < nics; i++) nDescPost[i] = 0;

    i = 0;

    while (msg->nbl[i] > 0) {

            // Send as much data as possible

        c = cd[c->peer][i];

        p = via_rdma_desc_request(i);
        if (p == NULL) break;

        packetSize = (msg->nbl[i] > mtu)?mtu:msg->nbl[i];

        if (packetSize == msg->nbl[i]) { 
            p->CS.Control = VIP_CONTROL_OP_RDMAWRITE|VIP_CONTROL_IMMEDIATE;
            p->CS.ImmediateData = - hdr_cts->dstId;
        }

        p->CS.Length = packetSize;
        p->DS[0].Remote.Data.Address = hdr_cts->addr + 
            (msg->ptr[i] - msg->buf);
        p->DS[0].Remote.Handle = hdr_cts->mHand[i];
        p->DS[1].Local.Data.Address = msg->ptr[i];
        p->DS[1].Local.Handle = mHandle[msg->id][i];
        p->DS[1].Local.Length = packetSize;

        rc = VipPostSend(c->viHand, p, rdmaDescHand[i]);
        via_check_error(rc, "via_rdmaw: VipPostSend()");

        nDescPost[i]++;
        msg->ptr[i] += packetSize;
        msg->nbl[i] -= packetSize;
        i = (i + 1) % nics;
    }

    i = 0;

    while (nDescPost[i] > 0) {

            // Wait send to complete

        c = cd[c->peer][i];

        p = via_send_wait(c);
        n_rdma_packets++;

        if (msg->nbl[i] > 0) {
            packetSize = (msg->nbl[i] > mtu)?mtu:msg->nbl[i];

            if (packetSize == msg->nbl[i]) { 
        p->CS.Control = VIP_CONTROL_OP_RDMAWRITE|
        VIP_CONTROL_IMMEDIATE;
        p->CS.ImmediateData = hdr_cts->dstId;
        }

            p->CS.Length = packetSize;
            p->CS.Reserved = 0;
            p->CS.Status = 0;
            p->DS[0].Remote.Data.Address = hdr_cts->addr + 
                    (msg->ptr[i] - msg->buf);
            p->DS[0].Remote.Reserved = 0;
            p->DS[1].Local.Data.Address = msg->ptr[i];
            p->DS[1].Local.Length = packetSize;

            rc = VipPostSend(c->viHand, p, rdmaDescHand[i]);
            via_check_error(rc, "via_rdmaw: VipPostSend()");

            nDescPost[i]++;
            msg->ptr[i] += packetSize;
            msg->nbl[i] -= packetSize;
        } else {

                // Restore and release

            via_rdma_desc_release(p, i);
            nDescPost[i]--;
        }

        i = (i + 1) % nics;
    }

    return msg->nbytes;
}

#endif

/*
    // RDMA Write for single NIC
int via_rdmaw_one_nic(struct via_conn *c, struct MP_msg_entry *msg,
        via_hdr_cts_t *hdr_cts)
{
    VIP_DESCRIPTOR    *p;
    VIP_RETURN        rc;
    int               packetSize, nDescPost;
    int               mtu;

    mtu = nicAttrs[0].MaxTransferSize;
    nDescPost = 0;

        // Send as much data as possible
    do {
        p = via_rdma_desc_request(0);
        if (p == NULL) break;

        packetSize = (msg->nleft > mtu)?mtu:msg->nleft;

        if (packetSize == msg->nleft) { 
            p->CS.Control = VIP_CONTROL_OP_RDMAWRITE|VIP_CONTROL_IMMEDIATE;
            p->CS.ImmediateData = - hdr_cts->dstId;
        }

        p->CS.Length = packetSize;
        p->DS[0].Remote.Data.Address = hdr_cts->addr;
        p->DS[0].Remote.Handle = hdr_cts->mHand[0];
        p->DS[1].Local.Data.Address = msg->buf;
        p->DS[1].Local.Handle = mHandle[msg->id][0];
        p->DS[1].Local.Length = packetSize;

        rc = VipPostSend(c->viHand, p, rdmaDescHand[0]);
        via_check_error(rc, "via_rdmaw: VipPostSend()");

        nDescPost++;
        msg->buf += packetSize;
        msg->nleft -= packetSize;
        hdr_cts->addr += packetSize;

    } while (msg->nleft > 0);

    while (nDescPost > 0) {

            // Wait send to complete
        p = via_send_wait(0);
        n_rdma_packets++;

        if (msg->nleft > 0) {
            packetSize = (msg->nleft > mtu)?mtu:msg->nleft;

            if (packetSize == msg->nleft) { 
                p->CS.Control = VIP_CONTROL_OP_RDMAWRITE|VIP_CONTROL_IMMEDIATE;
                p->CS.ImmediateData = hdr_cts->dstId;
        }

            p->CS.Length = packetSize;
            p->CS.Reserved = 0;
            p->CS.Status = 0;
            p->DS[0].Remote.Data.Address = hdr_cts->addr;
            p->DS[0].Remote.Reserved = 0;
            p->DS[1].Local.Data.Address = msg->buf;
            p->DS[1].Local.Length = packetSize;

            rc = VipPostSend(c->viHand, p, rdmaDescHand[0]);
            via_check_error(rc, "via_rdmaw: VipPostSend()");

        msg->buf += packetSize;
            msg->nleft -= packetSize;
        hdr_cts->addr += packetSize;
        } else {
                // Restore and release
            via_rdma_desc_release(p, 0);
            nDescPost --;
        }
    }

    return msg->nbytes;
}
*/

extern struct via_conn *cd[MAX_PROCS][MAX_NICS];
extern int nics;
extern int n_send_packets, n_recv_packets;

    // Blocking send

int via_send(struct via_conn *c, void *buf, size_t len)
{
    VIP_DESCRIPTOR    *p;
    int               nLeftx[MAX_NICS], nDescPost[MAX_NICS];
    int               packetSize;
    char              *ptr;
    int               i, j;

    ptr = buf;

    for (i = 0; i < nics; i++) {
        nLeftx[i] = len / nics;
        if (i < len % nics) nLeftx[i]++;
        nDescPost[i] = 0;
    }

    i = j = 0;

    while (nLeftx[i] > 0) {

            // Use all nics

        do {
            c = cd[c->peer][i];

            p = via_desc_request(i);
            if (p == NULL) break;

                // Determine packet size

            if (nLeftx[i] < BUF_SIZE) packetSize = nLeftx[i];
            else packetSize = BUF_SIZE;

            p->CS.Length = packetSize;
            p->DS[0].Local.Length = packetSize;

                // Copy data to send buffer

            MP_memcpy(p->DS[0].Local.Data.Address, ptr, packetSize);

                // Post send 

        via_post_send(c, p);

                // Mark progress

            ptr += packetSize;
            nLeftx[i] -= packetSize;
            nDescPost[i]++;
            i = (i + 1) % nics;

        } while (nLeftx[i] > 0);

        while (nDescPost[j] > 0) {

            c = cd[c->peer][j];
            p = via_send_wait(c);
            n_send_packets++;

            via_desc_release(p, j);
            nDescPost[j]--;
            j = (j + 1) % nics;
        }
    }

    return len;
}

    // Blocking receive

int via_recv(struct via_conn *c, void *buf, size_t len)
{
    VIP_DESCRIPTOR    *p;
    int               nLeftx[MAX_NICS], nDescPost[MAX_NICS]; 
    char              *ptr;
    int               i;

    ptr = buf;

    for (i = 0; i < nics; i++) {
        nLeftx[i] = len / nics;
        if (i < len % nics) nLeftx[i]++;
        nDescPost[i] = 0;
    }

        // Post enough descriptors

    i = 0;
    while (nLeftx[i] > 0) {
        p = via_desc_request(i);
        if (p == NULL) break;

        via_post_recv(cd[c->peer][i], p);

        nLeftx[i] -= BUF_SIZE;
        nDescPost[i]++;
        i = (i + 1) % nics;
    }

    i = 0;
    while (nDescPost[i] > 0) {

        c = cd[c->peer][i];

        p = via_recv_wait(c);
        n_recv_packets++;

            // Copy data

        MP_memcpy(ptr, p->DS[0].Local.Data.Address, p->CS.Length);
        ptr += p->CS.Length;
        via_desc_release(p, i);

        if (nLeftx[i] > 0) {
            p = via_desc_request(i);

        via_post_recv(c, p);

            nLeftx[i] -= BUF_SIZE;
            i = (i + 1) % nics;
        } else {
            nDescPost[i]--;
        }

        i = (i + 1) % nics;
    }

    return len;
}


extern void **msg_q;
extern int n_send_packets, n_recv_packets;
extern int n_buffering;
extern int nics;
extern char viaDevice[MAX_NICS][20];

    // Consume one receive descriptor

VIP_DESCRIPTOR *via_recv_one(struct via_conn *c)
{
    VIP_DESCRIPTOR   *p;

        // Post one receive descriptor because we will use one later

    p = via_desc_request(c->nic);

    via_post_recv(c, p);
    p = via_recv_wait(c);

    n_recv_packets++;

    return p;
}

void via_send_one(struct via_conn *c, VIP_DESCRIPTOR *p)
{
    via_post_send(c, p);
    p = via_send_wait(c);
    via_desc_release(p, c->nic);

    n_send_packets++;
}

    // Send header followed by data if available

void via_send_with_header(struct via_conn *c, via_hdr_t *hdr, char *buf, int len)
{
    VIP_DESCRIPTOR    *p;
    char              *ptr;
    int               hdrSize = 0;
    int               dataSize;

        // Request a descriptor

    p = via_desc_request(c->nic);
    assert(p);

        // Determine header size

    switch (hdr->op) {
        case OP_SEND:
            hdrSize = sizeof(via_hdr_t);
        break;
        case OP_RDMAW_RTS:
            hdrSize = sizeof(via_hdr_rts_t);
        break;
        case OP_RDMAW_CTS:
            hdrSize = sizeof(via_hdr_cts_t);
        break;
        default:
            errorx(-1, "Unknow operator, via_send_with_header()");
    }

        // Determine the data size in the first packet

    dataSize = BUF_SIZE - hdrSize;
    if (dataSize > len) dataSize = len;
    if (nics > 1 && len >= MIN_SPLIT) dataSize = 0;

    p->CS.Length = hdrSize + dataSize;
    p->DS[0].Local.Length = p->CS.Length;
    ptr = (char *)p->DS[0].Local.Data.Address;

    MP_memcpy(ptr, hdr, hdrSize);
    if (dataSize > 0) MP_memcpy(ptr + hdrSize, buf, dataSize);

        // Send header

    via_send_one(c, p);

        // Send other data

    if (dataSize < len) via_send(c, buf + dataSize, len - dataSize);
}

void via_msg_split(struct MP_msg_entry *msg, int length)
{
    int i, avg_len;

    avg_len = ((length / 8) / nics) * 8;
    msg->buffer[0] = msg->ptr[0] = msg->buf;
    msg->nbl[0] = avg_len + (length - avg_len * nics);

    for (i = 1; i < nics; i++) {
        msg->nbl[i] = avg_len;
        msg->buffer[i] = msg->ptr[i] = msg->buffer[i - 1] + msg->nbl[i - 1];
    }
}

    // Buffer the received data

struct MP_msg_entry *via_buffering(struct via_conn *c, VIP_DESCRIPTOR *p)
{
    via_hdr_t              *hdr;
    struct MP_msg_entry    *msg;
    void                   *buf;

    hdr = (via_hdr_t *) p->DS[0].Local.Data.Address;

    buf = memalign(PAGE_SIZE, hdr->nbytes);
    assert(buf);

    msg = create_msg_entry(buf, hdr->nbytes, c->peer, -1, hdr->tag);
    post(msg, msg_q);

    n_buffering++;

    return msg;
}

    // VIA error callback function

void via_error_callback(VIP_PVOID c, VIP_ERROR_DESCRIPTOR *p)
{
    if (p->DescriptorPtr != NULL) {
        lprintf("Descriptor Error\n");
        lprintf("Descriptor Status = %d\n", p->DescriptorPtr->CS.Status);
    }

    lprintf("OpCode = %d\n", p->OpCode);
    lprintf("ResourceCode = %d\n", p->ResourceCode);
    lprintf("ErrorCode = %d\n", p->ErrorCode);

    via_clean();

    errorx(p->ErrorCode, "VIA error");
}

void get_devices()
{
    int            i, j;
    int            sockfd;
    struct         hostent        *hptr;
    struct         in_addr        *iaddr;
    struct         sockaddr_in    *saddr;
    struct         ifreq        ifi;

#ifdef GIGANET
    strcpy(viaDevice[0], "/dev/clanvi0");
    return;
#endif

    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) errorx(-1, "socket()");

    for (i = 0; i < nics; i++) {

        strcpy(viaDevice[i], "NONE");

        if (strcmp(interface[myproc][i], "localhost") == 0) {
            sprintf(viaDevice[i], "/etc/via_lo");
        continue;
        }

            // Get IP address

        hptr = gethostbyname(interface[myproc][i]);
        if (hptr == NULL) errorx(-1, "gethostbyname()");

        iaddr = (struct in_addr *)hptr->h_addr_list[0];

            // Check each interface for a match (maximum 8)

        for (j = 0; j < 8; j++) {

                // Get interface address

            sprintf(ifi.ifr_name, "eth%d", j);
            if (ioctl(sockfd, SIOCGIFADDR, &ifi) < 0) {
                continue;
            }

            saddr = (struct sockaddr_in *)&ifi.ifr_addr;

            if (saddr->sin_addr.s_addr == iaddr->s_addr) {
                sprintf(viaDevice[i], "/dev/via_eth%d", j);
                break;
            }
        }

        if (j == 8) errorx(0, "Too many network interfaces\n");

        if (strcmp(viaDevice[i], "NONE") == 0) {
        printf("%s\n", interface[myproc][i]);
        errorx(-1, "Interface not found");
        }
    }

    close(sockfd);
}

void get_myproc()
{
    int      i, shmid = -1, semid = -1;
    key_t    key;
    int      *ptr = NULL;
    int      nsmp = 0;
    char     *pathname = ".mplite.config";

    myproc = -1;

    for (i = 0; i < nprocs; i++) {

        if (strcmp(myhostname, hostname[i])) continue;
        nsmp++;

        if (nsmp == 1) {
            myproc = i;
            continue;
        }

        if  (nsmp == 2) {

                // Create key

            key = ftok(pathname, 0);
            if (key == -1) errorx(errno, "ftok()");

                // Attach shmem

            shmid = shmget(key, sizeof(int), IPC_CREAT|0600);
            if (shmid == -1) errorx(errno, "shmget()");

            ptr = shmat(shmid, NULL, 0);

                // Create or open semaphore

            semid = sem_create(key, 1);

            sem_wait(semid);

            if (*ptr <= myproc) {
                *ptr = myproc + 1;
                sem_signal(semid);
                break;
            }

            sem_signal(semid);
        }

        sem_wait(semid);

        if (*ptr <= i) {
            *ptr = i + 1;
            myproc = i;
            sem_signal(semid);
            break;
        }

        sem_signal(semid);
    }

        // Last one delete shmem and semaphor

    if (nsmp > 1) {

        shmdt(ptr);
        //sem_close(semid);
        
        for (i = nprocs - 1; i >= 0; i--) {
            if (!strcmp(myhostname, hostname[i])) break;
        }

        if (myproc == i) {
            shmctl(shmid, IPC_RMID, NULL);
            sem_rm(semid);
        }
    }
}

extern VIP_CQ_HANDLE scq[MAX_NICS];
extern VIP_CQ_HANDLE rcq[MAX_NICS];

    // Wait send or receive descriptor complete

VIP_DESCRIPTOR *via_send_wait(struct via_conn *c)
{
    VIP_DESCRIPTOR   *p;
    VIP_VI_HANDLE    vi;
    VIP_BOOLEAN      is_recv_q;
    VIP_RETURN       rc;
    int              i;

    for (i = 0; i < POLLING_COUNT; i++) {
        rc = VipCQDone(scq[c->nic], &vi, &is_recv_q);
        if (rc == VIP_SUCCESS) break;
    }

    if (rc == VIP_NOT_DONE) {
        rc = VipCQWait(scq[c->nic], TIMEOUT, &vi, &is_recv_q);
        via_check_error(rc, "via_send_wait: VipCQWait()");
    }

    do {
        rc = VipSendDone(vi, &p);
    } while (rc == VIP_NOT_DONE);

    via_check_error(rc, "via_send_wait: VipSendDone()");

    return p;
}

VIP_DESCRIPTOR *via_recv_wait(struct via_conn *c)
{
    VIP_DESCRIPTOR   *p;
    VIP_VI_HANDLE    vi;
    VIP_BOOLEAN      is_recv_q;
    VIP_RETURN       rc;
    int              i;

    for (i = 0; i < POLLING_COUNT; i++) {
        rc = VipCQDone(rcq[c->nic], &vi, &is_recv_q);
        if (rc == VIP_SUCCESS) break;
    }

    if (rc == VIP_NOT_DONE) {
        rc = VipCQWait(rcq[c->nic], TIMEOUT, &vi, &is_recv_q);
        via_check_error(rc, "via_recv_wait: VipCQWait()");
    }

    do {
        rc = VipRecvDone(vi, &p);
    } while (rc == VIP_NOT_DONE);

    via_check_error(rc, "via_recv_wait: VipRecvDone()");

    if ((int)(p->CS.ImmediateData) > 0) {
        if (p->CS.ImmediateData != c->seqExp) {
            printf("p->CS.ImmediateData = %d\n", p->CS.ImmediateData);
        printf("seqExp = %d\n", c->seqExp);
            errorx(-1, "Sequence number mismatch, increase DESC_POST_NUM");
        }

        if (c->seqExp == MAX_SEQ) c->seqExp = MIN_SEQ;
        else c->seqExp ++;
    }

    return p;
}

extern struct MP_msg_entry *msg_list[MAX_MSGS];
extern void **recv_q;
extern struct via_conn *cd[MAX_PROCS][MAX_NICS];
extern VIP_MEM_HANDLE mHandle[MAX_MSGS][MAX_NICS];
extern int nics;

// Do different work according to the header
void via_op(struct via_conn *c, VIP_DESCRIPTOR *p)
{
    via_hdr_t      *hdr;
    VIP_DESCRIPTOR *pt;
    int            nic;
    int            imd;

    imd = (int) p->CS.ImmediateData; 

    // Nagative immediatedata can only be in the last packet of RDMAW
    if (imd <= 0) {

        // Wait until all nics complete
        for (nic = 1; nic < nics; nic++) {

            pt = via_recv_one(cd[c->peer][nic]);

	    assert(pt->CS.ImmediateData == p->CS.ImmediateData);

	    via_desc_release(pt, nic);
        }

        via_get_op_rdmaw_done(- imd);
        return;
    }

    hdr = (via_hdr_t *) p->DS[0].Local.Data.Address;

    switch (hdr->op) {
        case OP_SEND:
            via_get_op_send(c, p);
            break;
        case OP_RDMAW_RTS:
            via_get_op_rdmaw_rts(c, p);
            break;
        case OP_RDMAW_CTS:
            via_get_op_rdmaw_cts(c, p);
            break;
        default:
            errorx(hdr->op, "Unknonw operation");
    }
}

// If the header is OP_SEND
// Receive header and data. Buffer data if necessary
void via_get_op_send(struct via_conn *c, VIP_DESCRIPTOR *p)
{
    struct MP_msg_entry    *msg;
    via_hdr_t              *hdr;
    int                    size;
    char                   *ptr;
    int                    hdrSize;

    ptr = (char *) p->DS[0].Local.Data.Address;
    hdr = (via_hdr_t *) ptr;
    hdrSize = sizeof(via_hdr_t);

    // Search recv_q
    // If receive is not posted, buffer the data
    msg = find_a_posted_recv(c->peer, hdr->tag, hdr->nbytes);
    if (msg == NULL) msg = via_buffering(c, p);

    if (msg->nbytes >= hdr->nbytes) {

        // We have a large enough buffer
        size = p->CS.Length - hdrSize;

        if (size > 0) MP_memcpy(msg->buf, ptr + hdrSize, size);
        if (size < hdr->nbytes) {
            via_recv(c, msg->buf + size, hdr->nbytes - size);
        }

        // Mark progress
        msg->nleft = msg->nbytes - hdr->nbytes;

        if (msg->nleft > 0) {
            // Message not complete, post to recv_q again
            msg->nbytes -= hdr->nbytes;
            msg->buf += hdr->nbytes;
            post(msg, recv_q);
        } 
    } else {
        // Buffer is not large enough
        errorx(-1, "Message Truncated");
    }
}

// If the header is RDMAW RTS, send CTS 
void via_get_op_rdmaw_rts(struct via_conn *c, VIP_DESCRIPTOR *p)
{
    struct MP_msg_entry    *msg;
    via_hdr_rts_t          *hdr_rts;
    via_hdr_cts_t          hdr_cts;

    hdr_rts = (via_hdr_rts_t *) p->DS[0].Local.Data.Address;

    // Search recv_q, buffer data if necessary
    msg = find_a_posted_recv(c->peer, hdr_rts->tag, hdr_rts->nbytes);
    if (msg == NULL) msg = via_buffering(c, p);

    // If buffer is too small, error and quit
    // If buffer is too large, re-split the message
    if (msg->nbytes < hdr_rts->nbytes) errorx(-1, "Message Truncated");
    else if (msg->nbytes > hdr_rts->nbytes) {

        via_deregister_mem(msg);
        via_msg_split(msg, hdr_rts->nbytes);
	via_register_mem(msg);
    }

    // Send "CTS"
    hdr_cts.op = OP_RDMAW_CTS;
    hdr_cts.nbytes = msg->nbytes;
    hdr_cts.tag = msg->tag;
    hdr_cts.srcId = hdr_rts->srcId;
    hdr_cts.dstId = msg->id;
    hdr_cts.addr = msg->buf;
    MP_memcpy(hdr_cts.mHand, mHandle[msg->id], sizeof(VIP_MEM_HANDLE) * nics);

    via_send_with_header(c,(via_hdr_t *)&hdr_cts, NULL, 0);

    // If destination buffer is larger than source
    // We must wait until the transfer completed
    // Then post the remain

    if (msg->nbytes > hdr_rts->nbytes) {
        do {
	    via_do_recv(c);
	} while (msg->nleft > 0);

	msg->nbytes -= hdr_rts->nbytes;
	msg->nleft = msg->nbytes;
	msg->buf += hdr_rts->nbytes;

	if (msg->nbytes >= RDMAW_THRESHOLD) {
            via_msg_split(msg, msg->nbytes);
	    via_register_mem(msg);
        }

        post(msg, recv_q);
	recv_from_q(msg);
    }
}

// If the header is RDMAW CTS , beging RDMA Write
void via_get_op_rdmaw_cts(struct via_conn *c, VIP_DESCRIPTOR *p)
{
    struct MP_msg_entry    *msg;
    via_hdr_cts_t          *hdr_cts;

    hdr_cts = (via_hdr_cts_t *) p->DS[0].Local.Data.Address;
    msg = msg_list[hdr_cts->srcId];

    assert(hdr_cts->nbytes >= msg->nbytes);

    // RDMA Write
    via_rdmaw(c, msg, hdr_cts);

    // Deregister memory
    via_deregister_mem(msg);

    // Mark progress
    msg->nleft = 0;
}

// If the header is OP_RDMAW_DONE, adjust message progress
void via_get_op_rdmaw_done(int dstId)
{
    struct MP_msg_entry    *msg;

    msg = msg_list[dstId];

    via_deregister_mem(msg);

    msg->nleft = 0;
}

/*
 * Provide a simpler and easier to understand interface to the System V
 * semaphore system calls.  There are 7 routines available to the user:
 *
 * id = sem_create(key, initval);   # create with initial value or open
 * id = sem_open(key);  # open (must already exist)
 * sem_wait(id);   # wait = P = down by 1
 * sem_signal(id);   # signal = V = up by 1
 * sem_op(id, amount);  # wait   if (amount < 0)
 *					# signal if (amount > 0)
 *	sem_close(id);			# close
 *	sem_rm(id);			# remove (delete)
 *
 * We create and use a 3-member set for the requested semaphore.
 * The first member, [0], is the actual semaphore value, and the second
 * member, [1], is a counter used to know when all processes have finished
 * with the semaphore.  The counter is initialized to a large number,
 * decremented on every create or open and incremented on every close.
 * This way we can use the "adjust" feature provided by System V so that
 * any process that exit's without calling sem_close() is accounted
 * for.  It doesn't help us if the last process does this (as we have
 * no way of getting control to remove the semaphore) but it will
 * work if any process other than the last does an exit (intentional
 * or unintentional).
 * The third member, [2], of the semaphore set is used as a lock variable
 * to avoid any race conditions in the sem_create() and sem_close()
 * functions.
 */


extern int errno;

#define BIGCOUNT 10000		/* initial value of process counter */

/*
 * Define the semaphore operation arrays for the semop() calls.
 */

static struct sembuf op_lock[2] = {
	{2, 0, 0},		/* wait for [2] (lock) to equal 0 */
	{2, 1, SEM_UNDO}	/* then increment [2] to 1 - this locks it */
				/* UNDO to release the lock if processes exits
			   	   before explicitly unlocking */
};

static struct sembuf	op_endcreate[2] = {
	{1, -1, SEM_UNDO},	/* decrement [1] (proc counter) with 
				   undo on exit */
				/* UNDO to adjust proc counter if process exits
			   	   before explicitly calling sem_close() */
	{2, -1, SEM_UNDO}	/* then decrement [2] (lock) back to 0 */
};

static struct sembuf	op_open[1] = {
	{1, -1, SEM_UNDO}	/* decrement [1] (proc counter) with 
				   undo on exit */
};

static struct sembuf	op_close[3] = {
	{2, 0, 0},		/* wait for [2] (lock) to equal 0 */
	{2, 1, SEM_UNDO},	/* then increment [2] to 1 - this locks it */
	{1, 1, SEM_UNDO}	/* then increment [1] (proc counter) */
};

static struct sembuf	op_unlock[1] = {
	{2, -1, SEM_UNDO}	/* decrement [2] (lock) back to 0 */
};

static struct sembuf	op_op[1] = {
	{0, 99, SEM_UNDO}	/* decrement or increment [0] with undo on exit */
			/* the 99 is set to the actual amount to add
			   or subtract (positive or negative) */
};

/****************************************************************************
 * Create a semaphore with a specified initial value.
 * If the semaphore already exists, we don't initialize it (of course).
 * We return the semaphore ID if all OK, else -1.
 */

int
sem_create(key, initval)
key_t	key;
int	initval;	/* used if we create the semaphore */
{
	register int		id, semval;
	union sem_union {
		int		val;
		struct semid_ds	*buf;
		ushort		*array;
	} semctl_arg;

	if (key == IPC_PRIVATE)
		return(-1);	/* not intended for private semaphores */

	else if (key == (key_t) -1)
		return(-1);	/* probably an ftok() error by caller */

again:
	if ( (id = semget(key, 3, 0666 | IPC_CREAT)) < 0)
		return(-1);	/* permission problem or tables full */

	/*
	 * When the semaphore is created, we know that the value of all
	 * 3 members is 0.
	 * Get a lock on the semaphore by waiting for [2] to equal 0,
	 * then increment it.
	 *
	 * There is a race condition here.  There is a possibility that
	 * between the semget() above and the semop() below, another
	 * process can call our sem_close() function which can remove
	 * the semaphore if that process is the last one using it.
	 * Therefore, we handle the error condition of an invalid
	 * semaphore ID specially below, and if it does happen, we just
	 * go back and create it again.
	 */

	if (semop(id, &op_lock[0], 2) < 0) {
		if (errno == EINVAL)
			goto again;
		err_sys("can't lock");
	}

	/*
	 * Get the value of the process counter.  If it equals 0,
	 * then no one has initialized the semaphore yet.
	 */

	if ( (semval = semctl(id, 1, GETVAL, 0)) < 0)
		err_sys("can't GETVAL");

	if (semval == 0) {
		/*
		 * We could initialize by doing a SETALL, but that
		 * would clear the adjust value that we set when we
		 * locked the semaphore above.  Instead, we'll do 2
		 * system calls to initialize [0] and [1].
		 */

		semctl_arg.val = initval;
		if (semctl(id, 0, SETVAL, semctl_arg) < 0)
			err_sys("can SETVAL[0]");

		semctl_arg.val = BIGCOUNT;
		if (semctl(id, 1, SETVAL, semctl_arg) < 0)
			err_sys("can SETVAL[1]");
	}

	/*
	 * Decrement the process counter and then release the lock.
	 */

	if (semop(id, &op_endcreate[0], 2) < 0)
		err_sys("can't end create");

	return(id);
}

/****************************************************************************
 * Open a semaphore that must already exist.
 * This function should be used, instead of sem_create(), if the caller
 * knows that the semaphore must already exist.  For example a client
 * from a client-server pair would use this, if its the server's
 * responsibility to create the semaphore.
 * We return the semaphore ID if all OK, else -1.
 */

int
sem_open(key)
key_t	key;
{
	register int	id;

	if (key == IPC_PRIVATE)
		return(-1);	/* not intended for private semaphores */

	else if (key == (key_t) -1)
		return(-1);	/* probably an ftok() error by caller */

	if ( (id = semget(key, 3, 0)) < 0)
		return(-1);	/* doesn't exist, or tables full */

	/*
	 * Decrement the process counter.  We don't need a lock
	 * to do this.
	 */

	if (semop(id, &op_open[0], 1) < 0)
		err_sys("can't open");

	return(id);
}

/****************************************************************************
 * Remove a semaphore.
 * This call is intended to be called by a server, for example,
 * when it is being shut down, as we do an IPC_RMID on the semaphore,
 * regardless whether other processes may be using it or not.
 * Most other processes should use sem_close() below.
 */

void
sem_rm(id)
int	id;
{
	if (semctl(id, 0, IPC_RMID, 0) < 0)
		err_sys("can't IPC_RMID");
}

/****************************************************************************
 * Close a semaphore.
 * Unlike the remove function above, this function is for a process
 * to call before it exits, when it is done with the semaphore.
 * We "decrement" the counter of processes using the semaphore, and
 * if this was the last one, we can remove the semaphore.
 */

void
sem_close(id)
int	id;
{
	register int	semval;

	/*
	 * The following semop() first gets a lock on the semaphore,
	 * then increments [1] - the process counter.
	 */

	if (semop(id, &op_close[0], 3) < 0)
		err_sys("can't semop");

	/*
	 * Now that we have a lock, read the value of the process
	 * counter to see if this is the last reference to the
	 * semaphore.
	 * There is a race condition here - see the comments in
	 * sem_create().
	 */

	if ( (semval = semctl(id, 1, GETVAL, 0)) < 0)
		err_sys("can't GETVAL");

	if (semval > BIGCOUNT)
		err_dump("sem[1] > BIGCOUNT");
	else if (semval == BIGCOUNT)
		sem_rm(id);
	else
		if (semop(id, &op_unlock[0], 1) < 0)
			err_sys("can't unlock");	/* unlock */
}

/****************************************************************************
 * Wait until a semaphore's value is greater than 0, then decrement
 * it by 1 and return.
 * Dijkstra's P operation.  Tanenbaum's DOWN operation.
 */

void
sem_wait(id)
int	id;
{
	sem_op(id, -1);
}

/****************************************************************************
 * Increment a semaphore by 1.
 * Dijkstra's V operation.  Tanenbaum's UP operation.
 */

void
sem_signal(id)
int	id;
{
	sem_op(id, 1);
}

/****************************************************************************
 * General semaphore operation.  Increment or decrement by a user-specified
 * amount (positive or negative; amount can't be zero).
 */

void
sem_op(id, value)
int	id;
int	value;
{
	if ( (op_op[0].sem_op = value) == 0)
		err_sys("can't have value == 0");

	if (semop(id, &op_op[0], 1) < 0)
		err_sys("sem_op error");
}

void
err_sys(msg)
char	*msg;
{
	perror(msg);
	exit(-1);
}

void
err_dump(msg)
char	*msg;
{
	err_sys(msg);
}

