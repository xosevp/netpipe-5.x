// vim:expandtab:autoindent:tabstop=4:shiftwidth=4:filetype=c:
/*****************************************************************************/
/* "NetPIPE" -- Network Protocol Independent Performance Evaluator.          */
/* Copyright 1997, 1998 Iowa State University Research Foundation, Inc.      */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation.  You should have received a copy of the     */
/* GNU General Public License along with this program; if not, write to the  */
/* Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   */
/*                                                                           */
/*       udapl.c           ---- uDAPL module for NetPIPE                     */
/*****************************************************************************/

/*
 * :NOTE: This code uses UDAPL_DEVICE as the name of the Interface Adapter to
 *        pass to dat_ia_open. UDAPL_DEVICE by default is defined to be "ib0"
 *        for an InfiniBand transport. The default device name can be changed
 *        by redefining UDAPL_DEVICE below.
 *
 * :NOTE: For InfiniBand, this code requires that IPoIB be running in order to
 *        resolve the host name.
 *
 * :NOTE: For best latency & throughput numbers, use local_poll.
 *        To demonstrate CPU efficiency, use evd_wait or cno_wait.
 *
 *****************************************************************************
 *
 * EXAMPLE USAGE OF THe UDAPL MODULE:
 *
 * Server side (host name ibdemo):  ./NPudapl -t send_recv -c local_poll
 * Client side (host name ibcool):  ./NPudapl -t send_recv -c local_poll -h ibdemo
 *
 * Server side (host name ibdemo):  ./NPudapl -t rdma_write -c local_poll
 * Client side (host name ibcool):  ./NPudapl -t rdma_write -c local_poll -h ibdemo
 *
 * Server side (host name ibdemo):  ./NPudapl -t rdma_write -c evd_wait
 * Client side (host name ibcool):  ./NPudapl -t rdma_write -c evd_wait -h ibdemo
 *
 * Server side (host name ibdemo):  ./NPudapl -t send_recv -c evd_wait
 * Client side (host name ibcool):  ./NPudapl -t send_recv -c evd_wait -h ibdemo
 *
 * Server side (host name ibdemo):  ./NPudapl -t send_recv -c dq_poll
 * Client side (host name ibcool):  ./NPudapl -t send_recv -c dq_poll -h ibdemo
 *
 * Server side (host name ibdemo):  ./NPudapl -t send_recv -c cno_wait
 * Client side (host name ibcool):  ./NPudapl -t send_recv -c cno_wait -h ibdemo
 *
 */

#include    "netpipe.h"
#include    <stdio.h>
#include    <getopt.h>

/* Debugging output macro */

FILE* logfile;

#if 0
#define LOGPRINTF(_format, _aa...) fprintf(logfile, _format, ##_aa); fflush(logfile)
#else
#define LOGPRINTF(_format, _aa...)
#endif

/* Header files needed for Infiniband */
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dat/udat.h>

/* Local definitions */
#define NP_FAILURE     -1
#define NP_SUCCESS      0
#define EVD_QLEN     1024
#define CONN_QUAL    1040
#define NP_CONNECTED    1
#define UDAPL_DEVICE "ib0"

/* Global vars */
static DAT_IA_HANDLE          gIA            = DAT_HANDLE_NULL;
static DAT_PZ_HANDLE          gPZ            = DAT_HANDLE_NULL;
static DAT_EP_HANDLE          gEP            = DAT_HANDLE_NULL;
static DAT_PSP_HANDLE         gPSP           = DAT_HANDLE_NULL;
static DAT_CNO_HANDLE         gCNO           = DAT_HANDLE_NULL;
static DAT_EVD_HANDLE         gConnReqEvd    = DAT_HANDLE_NULL;
static DAT_EVD_HANDLE         gConnEvd       = DAT_HANDLE_NULL;
static DAT_EVD_HANDLE         gAsyncEvd      = DAT_HANDLE_NULL;
static DAT_EVD_HANDLE         gRecvDtoEvd    = DAT_HANDLE_NULL;
static DAT_EVD_HANDLE         gSendDtoEvd    = DAT_HANDLE_NULL;
static uint8_t                gConnected     = 0; // disconnected
static DAT_LMR_HANDLE         gSndLmrHandle  = DAT_HANDLE_NULL;
static DAT_LMR_CONTEXT        gSndLmrContext = 0;
static DAT_LMR_HANDLE         gRcvLmrHandle  = DAT_HANDLE_NULL;
static DAT_RMR_HANDLE         gRcvRmrHandle  = DAT_HANDLE_NULL;
static DAT_LMR_CONTEXT        gRcvLmrContext = 0;
static DAT_RMR_CONTEXT        gRcvRmrContext = 0;
static void                  *gRemoteAddress = NULL;
static DAT_RMR_CONTEXT        gRemoteKey     = 0;
static DAT_REGION_DESCRIPTION gRegion;

/* Local prototypes */

/* Function definitions */

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
   /* Set defaults */
   p->prot.commtype = NP_COMM_SENDRECV;  /* Use Send/Receive communications  */
   p->prot.comptype = NP_COMP_LOCALPOLL; /* Use local polling for completion */
   p->tr  = 0;                           /* I am not the transmitter         */
   p->rcv = 1;                           /* I am the receiver                */      
}

void Setup(ArgStruct *p)
{

 struct sockaddr_in *lsin1;      /* ptr to sockaddr_in in ArgStruct */
 struct hostent     *addr;
 struct protoent    *proto;
 char               *host;
 char                logfilename[80];
 int                 sockfd;

 /* Sanity check */
 if( p->prot.commtype == NP_COMM_RDMAWRITE && 
     p->prot.comptype != NP_COMP_LOCALPOLL &&
     p->prot.comptype != NP_COMP_EVD ) {
   fprintf(stderr, "Error, RDMA Write only supported with local polling & evd wait by this module.\n");
   exit(-1);
 }
 
 /* Open log file */
 sprintf(logfilename, ".udapllog%d", 1 - p->tr);
 logfile = fopen(logfilename, "w");

 host = p->host;                           /* copy ptr to hostname */ 

 lsin1 = &(p->prot.sin1);

 bzero((char *) lsin1, sizeof(*lsin1));

 if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
   printf("NetPIPE: can't open stream socket! errno=%d\n", errno);
   exit(-4);
 }

 if(!(proto = getprotobyname("tcp"))){
   printf("NetPIPE: protocol 'tcp' unknown!\n");
   exit(555);
 }

 if (p->tr){                                  /* if client i.e., Sender */

   if (atoi(host) > 0) {                   /* Numerical IP address */
     lsin1->sin_family = AF_INET;
     lsin1->sin_addr.s_addr = inet_addr(host);

   } else {
      
     if ((addr = gethostbyname(host)) == NULL){
       printf("NetPIPE: invalid hostname '%s'\n", host);
       exit(-5);
     }

     lsin1->sin_family = addr->h_addrtype;
     bcopy(addr->h_addr, (char*) &(lsin1->sin_addr.s_addr), addr->h_length);
   }

   lsin1->sin_port = htons(p->port);

 } else {                                 /* we are the receiver (server) */
   
   bzero((char *) lsin1, sizeof(*lsin1));
   lsin1->sin_family      = AF_INET;
   lsin1->sin_addr.s_addr = htonl(INADDR_ANY);
   lsin1->sin_port        = htons(p->port);
   
   if (bind(sockfd, (struct sockaddr *) lsin1, sizeof(*lsin1)) < 0){
     printf("NetPIPE: server: bind on local address failed! errno=%d", errno);
     exit(-6);
   }

 } 
 if(p->tr) {
   p->commfd = sockfd;
 } else {
   p->servicefd = sockfd;
 }

 /* Establish tcp connections */
 establish( p );

 /* Initialize uDAPL */
 if ( NP_FAILURE == init_uDAPL(p) ) {
   CleanUp( p );
   exit( NP_FAILURE );
 }

}   

int lookup_addr(DAT_IA_ADDRESS_PTR netAddr, char *serverName)
{
    struct addrinfo *target;
    int              status;
    unsigned int     addr;

    status = getaddrinfo( serverName, NULL, NULL, &target );

    if ( status ) {
        fprintf(stderr, "getaddrinfo failed: %d\n", status);
        return NP_FAILURE;
    }

    addr = ((struct sockaddr_in *) target->ai_addr)->sin_addr.s_addr;

    *netAddr = *((DAT_IA_ADDRESS_PTR) target->ai_addr);

    freeaddrinfo(target);

    return 0;
}


int init_uDAPL(ArgStruct *p)
{
  int                status = 0;
  DAT_RETURN         dat_status;
  DAT_CR_HANDLE      cr;
  DAT_EVD_HANDLE     evd_handle;
  DAT_CONTEXT        context;
  DAT_EP_ATTR        ep_attr;
  DAT_EVENT          event;
  DAT_COUNT          count;
  DAT_IA_ADDRESS_PTR remote_net_addr;

  // If p->host is NULL, this is the server. Need to create the PSP and listen
  // for a connection.
  if ( NULL == p->host ) {
    /* ** SERVER / REMOTE ** */
    dat_status = dat_ia_open( UDAPL_DEVICE, EVD_QLEN, &gAsyncEvd, &gIA );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_ia_open failed: status=0x%x\n", dat_status);
        fprintf(stderr, "Can not open IA %s\n", UDAPL_DEVICE);
        return NP_FAILURE;
    }

    dat_status = dat_pz_create( gIA, &gPZ );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_pz_create failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }

    dat_status = dat_cno_create( gIA, DAT_OS_WAIT_PROXY_AGENT_NULL, &gCNO );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_cno_create failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }

    dat_status = dat_evd_create( gIA, EVD_QLEN, DAT_HANDLE_NULL, DAT_EVD_CR_FLAG, &gConnReqEvd );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_create for gConnReqEvd failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }
    
    dat_status = dat_psp_create( gIA, CONN_QUAL, gConnReqEvd, DAT_PSP_CONSUMER_FLAG, &gPSP );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_psp_create failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }

    dat_status = dat_evd_create( gIA, EVD_QLEN, DAT_HANDLE_NULL, DAT_EVD_DTO_FLAG, &gSendDtoEvd );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_create for gSendDtoEvd failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }

    dat_status = dat_evd_create( gIA, EVD_QLEN, gCNO, DAT_EVD_DTO_FLAG, &gRecvDtoEvd );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_create for gRecvDtoEvd failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }

    dat_status = dat_evd_create( gIA, EVD_QLEN, DAT_HANDLE_NULL, DAT_EVD_CONNECTION_FLAG, &gConnEvd );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_create for gConnEvd failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }

    dat_status = dat_evd_wait( gConnReqEvd, DAT_TIMEOUT_INFINITE, 1, &event, &count );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_wait failed: status=0x%x", dat_status);
        return NP_FAILURE; 
    }
    
    if ( DAT_CONNECTION_REQUEST_EVENT != event.event_number ) {
        fprintf(stderr, "Rejected the event 0x%x", event.event_number);
        return NP_FAILURE;
    }

    cr = event.event_data.cr_arrival_event_data.cr_handle;

    // :NOTE: these values may have to be tuned for your uDAPL device.
    memset(&ep_attr, 0, sizeof(ep_attr));
    ep_attr.max_mtu_size     = 8388608;
    ep_attr.max_rdma_size    = 8388608;
    ep_attr.qos              = DAT_QOS_BEST_EFFORT;
    ep_attr.service_type     = DAT_SERVICE_TYPE_RC;
    ep_attr.max_recv_dtos    = 20000;
    ep_attr.max_request_dtos = 20000;
    ep_attr.max_recv_iov     = 4;
    ep_attr.max_request_iov  = 4;
    ep_attr.max_rdma_read_in = 4;
    ep_attr.max_rdma_read_out= 4;

    ep_attr.request_completion_flags = DAT_COMPLETION_SUPPRESS_FLAG;
    ep_attr.recv_completion_flags    = DAT_COMPLETION_DEFAULT_FLAG;

    dat_status = dat_ep_create( gIA, gPZ, gRecvDtoEvd, gSendDtoEvd, gConnEvd, &ep_attr, &gEP );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_ep_create failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }

    dat_status = dat_cr_accept( cr, gEP, 0, NULL );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_cr_accept failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }

    dat_status = dat_evd_wait( gConnEvd, DAT_TIMEOUT_INFINITE, 1, &event, &count );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_wait failed: status=0x%x", dat_status);
        return NP_FAILURE; 
    }

    if ( DAT_CONNECTION_EVENT_ESTABLISHED != event.event_number ) {
        fprintf(stderr, "Rejected the event 0x%x", event.event_number);
        return NP_FAILURE;
    }

    // CONNECTION ESTABLISHED

    gConnected = NP_CONNECTED;
    
    status = NP_SUCCESS;

  } else {
    /* ** CLIENT / LOCAL ** */

    // p->host is NOT NULL, so this is the client. Need to attempt to establish
    // a connection with the server.
    
    remote_net_addr = (DAT_IA_ADDRESS_PTR)malloc(sizeof(*remote_net_addr));

    if ( NP_SUCCESS != lookup_addr(remote_net_addr, p->host) ) {
        fprintf(stderr, "unable to look up server address\n");
        return NP_FAILURE;
    }

    dat_status = dat_ia_open( UDAPL_DEVICE, EVD_QLEN, &gAsyncEvd, &gIA );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_ia_open failed: status=0x%x", dat_status);
        gIA = DAT_HANDLE_NULL;
        gAsyncEvd = DAT_HANDLE_NULL;
        return NP_FAILURE;
    }

    dat_status = dat_cno_create( gIA, DAT_OS_WAIT_PROXY_AGENT_NULL, &gCNO );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_cno_create failed: status=0x%x", dat_status);
        gCNO = DAT_HANDLE_NULL;
        return NP_FAILURE;
    }

    dat_status = dat_evd_create( gIA, EVD_QLEN, DAT_HANDLE_NULL, DAT_EVD_DTO_FLAG, &gSendDtoEvd );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_create for gSendDtoEvd failed: status=0x%x", dat_status);
        gSendDtoEvd = DAT_HANDLE_NULL;
        return NP_FAILURE;
    }

    dat_status = dat_evd_create( gIA, EVD_QLEN, gCNO, DAT_EVD_DTO_FLAG, &gRecvDtoEvd );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_create for gRecvDtoEvd failed: status=0x%x", dat_status);
        gRecvDtoEvd = DAT_HANDLE_NULL;
        return NP_FAILURE;
    }

    dat_status = dat_evd_create( gIA, EVD_QLEN, DAT_HANDLE_NULL, DAT_EVD_CONNECTION_FLAG, &gConnEvd );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_create for connect_evd failed: status=0x%x", dat_status);
        gConnEvd = DAT_HANDLE_NULL;
        return NP_FAILURE;
    }

    dat_status = dat_pz_create( gIA, &gPZ );

    if (DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_pz_create failed: status=0x%x", dat_status);
        gPZ = DAT_HANDLE_NULL;
        return NP_FAILURE;
    }

    // :NOTE: these values may have to be tuned for your uDAPL device.
    memset(&ep_attr, 0, sizeof(ep_attr));
    ep_attr.max_mtu_size     = 8388608;
    ep_attr.max_rdma_size    = 8388608;
    ep_attr.qos              = DAT_QOS_BEST_EFFORT;
    ep_attr.service_type     = DAT_SERVICE_TYPE_RC;
    ep_attr.max_recv_dtos    = 20000;
    ep_attr.max_request_dtos = 20000;
    ep_attr.max_recv_iov     = 4;
    ep_attr.max_request_iov  = 4;
    ep_attr.max_rdma_read_in = 4;
    ep_attr.max_rdma_read_out= 4;

    ep_attr.request_completion_flags = DAT_COMPLETION_SUPPRESS_FLAG;
    ep_attr.recv_completion_flags    = DAT_COMPLETION_DEFAULT_FLAG;

    dat_status = dat_ep_create( gIA, gPZ, gRecvDtoEvd, gSendDtoEvd, gConnEvd, &ep_attr, &gEP );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_ep_create failed: status=0x%x", dat_status);
        gEP = DAT_HANDLE_NULL;
        return NP_FAILURE;
    }

    dat_status = dat_ep_connect( gEP,
                                 remote_net_addr,
                                 CONN_QUAL,
                                 DAT_TIMEOUT_INFINITE,
                                 0,
                                 NULL,
                                 DAT_QOS_BEST_EFFORT,
                                 DAT_CONNECT_DEFAULT_FLAG );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_ep_connect failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }

    dat_status = dat_evd_wait( gConnEvd, DAT_TIMEOUT_INFINITE, 1, &event, &count );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "dat_evd_wait failed: status=0x%x", dat_status);
        return NP_FAILURE;
    }
    
    if ( DAT_CONNECTION_EVENT_ESTABLISHED != event.event_number ) {
            fprintf(stderr, "Received an unexpected event: %x\n", event.event_number);
            return NP_FAILURE;
    }

    // CONNECTION ESTABLISHED
    gConnected = NP_CONNECTED;

    status = NP_SUCCESS;

  } // end of CLIENT/SERVER if..else..

  return status;
}

// Disconnect if connected and clean-up all resources.
int finalize_uDAPL(ArgStruct *p)
{
  int        status = NP_SUCCESS;
  DAT_RETURN dat_status;
  DAT_EVENT  event;
  DAT_COUNT  count;

  // if connected was established, need to disconnect cleanly
  if ( NP_CONNECTED == gConnected ) {

    if ( NULL == p->host ) {
        /* ** SERVER ** */
        dat_status = dat_evd_dequeue( gConnEvd, &event );

        while ( dat_status != DAT_SUCCESS ) {
            dat_status = dat_evd_dequeue( gConnEvd, &event );
        }

        // :NOTE: checking the event number for debug purposes
        if ( DAT_CONNECTION_EVENT_DISCONNECTED == event.event_number ) {
            fprintf(stderr, "Disconnected.\n");
        } else {
            fprintf(stderr, "Unexpected event: 0x%x\n", event.event_number);
        }

    } else {
        /* ** CLIENT ** */
        dat_status = dat_ep_disconnect( gEP, DAT_CLOSE_ABRUPT_FLAG );

        if ( DAT_SUCCESS != dat_status ) {
            fprintf(stderr, "dat_ep_disconnect failed. status=0x%x\n", dat_status);
            return NP_FAILURE;
        }
            
        dat_status = dat_evd_wait( gConnEvd, DAT_TIMEOUT_INFINITE, 1, &event, &count );

        if ( DAT_SUCCESS != dat_status ) {
            fprintf(stderr, "dat_evd_wait failed: status=0x%x", dat_status);
            return NP_FAILURE;
        }
    
        if ( DAT_CONNECTION_EVENT_DISCONNECTED != event.event_number ) {
                fprintf(stderr, "Received an unexpected event: %x\n", event.event_number);
                return NP_FAILURE;
        } else {
            fprintf(stderr, "Disconnected.\n");
        }
    }

  }

  // free resources

  if ( DAT_HANDLE_NULL != gEP ) {
    dat_status = dat_ep_free( gEP );
    gEP = DAT_HANDLE_NULL;
  }

  if ( DAT_HANDLE_NULL != gPZ ) {
    dat_status = dat_pz_free(gPZ);
    gPZ = DAT_HANDLE_NULL;
  }
    
  if ( DAT_HANDLE_NULL != gSendDtoEvd ) {
    dat_status = dat_evd_free(gSendDtoEvd);
    gSendDtoEvd = DAT_HANDLE_NULL;
  }
    
  if ( DAT_HANDLE_NULL != gRecvDtoEvd ) {
    dat_status = dat_evd_free(gRecvDtoEvd);
    gRecvDtoEvd = DAT_HANDLE_NULL;
  }
    
  if ( DAT_HANDLE_NULL != gConnEvd ) {
    dat_status = dat_evd_free(gConnEvd);
    gConnEvd = DAT_HANDLE_NULL;
  }

  if ( DAT_HANDLE_NULL != gConnReqEvd ) {
    dat_status = dat_evd_free(gConnReqEvd);
    gConnReqEvd = DAT_HANDLE_NULL;
  }

  if ( DAT_HANDLE_NULL != gAsyncEvd ) {
    dat_status = dat_evd_free(gAsyncEvd);
    gAsyncEvd = DAT_HANDLE_NULL;
  }

  if ( DAT_HANDLE_NULL != gCNO ) {
    dat_status = dat_cno_free(gCNO);
    gCNO = DAT_HANDLE_NULL;
  }

  if ( DAT_HANDLE_NULL != gPSP ) {
    dat_status = dat_psp_free(gPSP);
    gPSP = DAT_HANDLE_NULL;
  }

  if ( DAT_HANDLE_NULL != gIA ) {
    dat_status = dat_ia_close(gIA, DAT_CLOSE_ABRUPT_FLAG);
    gIA = DAT_HANDLE_NULL;
  }

  return status;
}

static int
readFully(int fd, void *obuf, int len)
{
  int bytesLeft = len;
  char *buf = (char *) obuf;
  int bytesRead = 0;

  while (bytesLeft > 0 &&
        (bytesRead = read(fd, (void *) buf, bytesLeft)) > 0) {
      bytesLeft -= bytesRead;
      buf += bytesRead;
  }

  if (bytesRead <= 0) {
    return bytesRead;
  }

  return len;
}

void Sync(ArgStruct *p)
{
    char s[] = "SyncMe";
    char response[7];

    if (write(p->commfd, s, strlen(s)) < 0 ||
        readFully(p->commfd, response, strlen(s)) < 0) {
        perror("NetPIPE: error writing or reading synchronization string");
        exit(3); 
    }
    if (strncmp(s, response, strlen(s))) {
        fprintf(stderr, "NetPIPE: Synchronization string incorrect!\n");
        exit(3);
    }
}

void PrepareToReceive(ArgStruct *p)
{
    DAT_RETURN           dat_status;
    DAT_LMR_TRIPLET      lmr_triplet;
    DAT_COMPLETION_FLAGS comp_flags;

    // no need to post a receive if doing RDMA Write with local polling
    if ( ( NP_COMM_RDMAWRITE == p->prot.commtype ) && 
         ( NP_COMP_LOCALPOLL == p->prot.comptype ) ) {
        return;
    }
     
    // if using RDMA_WRITE with an EVD completion, post the
    // zero length receive request for the handshake
    if ( ( NP_COMM_RDMAWRITE == p->prot.commtype ) && 
         ( NP_COMP_EVD == p->prot.comptype ) ) {

        comp_flags = DAT_COMPLETION_DEFAULT_FLAG;

        // post zero length receive for handshake
        dat_status = dat_ep_post_recv( gEP,
                                       0,
                                       NULL,
                                       (DAT_DTO_COOKIE)NULL,
                                       comp_flags );
    } else {
        comp_flags = DAT_COMPLETION_DEFAULT_FLAG;

        lmr_triplet.lmr_context     = gRcvLmrContext;
        lmr_triplet.virtual_address = (uintptr_t)p->r_ptr;
        lmr_triplet.segment_length  = p->bufflen;

        dat_status = dat_ep_post_recv( gEP,
                                       1, // one entry in iov
                                       &lmr_triplet,
                                       (DAT_DTO_COOKIE)NULL,
                                       comp_flags );
    }

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "Error posting receive request: 0x%x\n", dat_status);
        CleanUp(p);
        exit(NP_FAILURE);
    } else {
        LOGPRINTF("Posted receive request\n");
    }
}

void SendData(ArgStruct *p)
{
    DAT_RETURN           dat_status;
    DAT_LMR_TRIPLET      lmr_triplet;
    DAT_RMR_TRIPLET      rmr_triplet;
    DAT_COMPLETION_FLAGS comp_flags;
    DAT_RETURN           dq_status;
    DAT_EVENT            event;

    comp_flags = DAT_COMPLETION_SUPPRESS_FLAG;

    lmr_triplet.lmr_context     = gSndLmrContext;
    lmr_triplet.virtual_address = (uintptr_t)p->s_ptr;
    lmr_triplet.segment_length  = p->bufflen;

    if ( NP_COMM_SENDRECV == p->prot.commtype ) {

        LOGPRINTF("Doing regular send\n");
        dat_status = dat_ep_post_send( gEP,
                                       1, // 1 entry in iov
                                       &lmr_triplet,
                                       (DAT_DTO_COOKIE)NULL,
                                       comp_flags );
    } 
    
    if ( NP_COMM_RDMAWRITE == p->prot.commtype ) {

        rmr_triplet.rmr_context    = gRemoteKey;
        rmr_triplet.target_address = (uintptr_t)(gRemoteAddress + (p->s_ptr - p->s_buff));
        rmr_triplet.segment_length = p->bufflen;

        LOGPRINTF("Doing RDMA write (raddr=%p)\n", rmr_triplet.target_address);

        dat_status = dat_ep_post_rdma_write( gEP,
                                             (DAT_COUNT)1,
                                             &lmr_triplet,
                                             (DAT_DTO_COOKIE)NULL,
                                             &rmr_triplet,
                                             comp_flags );

        // if the caller wants to use EVD completion notification instead
        // of local polling (to illustrate CPU efficiency), send a 0 length
        // packet as the handshake to indicate rdma_write completion.
        if ( NP_COMP_EVD == p->prot.comptype ) {

            dat_status = dat_ep_post_send( gEP,
                                           0,
                                           NULL,
                                           (DAT_DTO_COOKIE)NULL,
                                           comp_flags );
        }
    }

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "Error posting send request: 0x%x\n", dat_status);
    } else {
        LOGPRINTF("Posted send request\n");
    }
}

void RecvData(ArgStruct *p)
{
    DAT_RETURN     dat_status = DAT_SUCCESS;
    DAT_EVD_HANDLE evd_handle = DAT_HANDLE_NULL;
    DAT_COUNT      count      = 0;
    DAT_EVENT      event;

    LOGPRINTF("Receiving at buffer address %p\n", p->r_ptr);

    if( NP_COMP_LOCALPOLL == p->prot.comptype ) {
       
        /* Poll for receive completion locally on the receive data */
        LOGPRINTF("Waiting for last byte of data to arrive\n");
     
        while(p->r_ptr[p->bufflen-1] != 'a' + (p->cache ? 1 - p->tr : 1) ) {
             // BUSY WAIT -- this should be fine since we 
             // declared r_ptr with volatile qualifier
        }

        /* Reset last byte */
        p->r_ptr[p->bufflen-1] = 'a' + (p->cache ? p->tr : 0);

        if (NP_COMM_RDMAWRITE != p->prot.commtype) {
            dat_status = dat_evd_dequeue( gRecvDtoEvd, &event );
        }

        LOGPRINTF("Received all of data\n");

    } else if( p->prot.comptype == NP_COMP_DQPOLL ) {

        /* Poll for receive completion using uDAPL Dequeue function */

        LOGPRINTF("Polling EVD via Dequeue for work completion\n");

        dat_status = DAT_QUEUE_EMPTY;

        while ( dat_status != DAT_SUCCESS ) {
            dat_status = dat_evd_dequeue( gRecvDtoEvd, &event );
        }

        if ( DAT_DTO_COMPLETION_EVENT != event.event_number ) {
            fprintf(stderr, "Unexpected completion event: %d\n", event.event_number);
            exit(NP_FAILURE);
        }

        LOGPRINTF("Retrieved successful completion\n");
     
    } else if( NP_COMP_EVD == p->prot.comptype ) {
        /*
         * Instead of polling directly on data or event dispatcher queue,
         * wait on the EVD to get the completion event.
         */

        LOGPRINTF("Waiting on the EVD\n");

        dat_status = dat_evd_wait( gRecvDtoEvd,
                                   DAT_TIMEOUT_INFINITE,
                                   1,
                                   &event,
                                   &count );
        
        if ( DAT_SUCCESS != dat_status ) {
            fprintf(stderr, "Error in RecvData, waiting on EVD for completion: %d\n", dat_status);
            exit(NP_FAILURE);
        }

        if ( DAT_DTO_COMPLETION_EVENT != event.event_number ) {
            fprintf(stderr, "Unexpected completion event: %d\n", event.event_number);
            exit(NP_FAILURE);
        }

        LOGPRINTF("Receive completed\n");

    } else if ( NP_COMP_CNO == p->prot.comptype ) {

        LOGPRINTF("Waiting on the CNO\n");

        dat_status = dat_cno_wait( gCNO, DAT_TIMEOUT_INFINITE, &evd_handle );

        if ( DAT_SUCCESS != dat_status ) {
            fprintf(stderr, "Error in RecvData, waiting on CNO for completion: %d\n", dat_status);
            exit(NP_FAILURE);
        }

        dat_status = dat_evd_dequeue( gRecvDtoEvd, &event );

        while ( DAT_SUCCESS != dat_status ) {
            dat_status = dat_evd_dequeue( gRecvDtoEvd, &event );
        }

        if ( DAT_DTO_COMPLETION_EVENT != event.event_number ) {
            fprintf(stderr, "Unexpected completion event: %d\n", event.event_number);
            exit(NP_FAILURE);
        }
    }
}

/* Reset is used after a trial to empty the work request queues so we
   have enough room for the next trial to run */
void Reset(ArgStruct *p)
{
    DAT_RETURN dat_status;
    DAT_EVENT  event;

    LOGPRINTF("Posting recv request in Reset\n");

    /* Post Receive */
    dat_status = dat_ep_post_recv( gEP, 
                                   0,
                                   NULL,
                                   (DAT_DTO_COOKIE)NULL,
                                   DAT_COMPLETION_DEFAULT_FLAG );

    if ( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "  Error posting recv request: %d\n", dat_status);
        CleanUp(p);
        exit(NP_FAILURE);
    }

    /* Make sure both nodes have preposted receives */
    Sync(p);

    LOGPRINTF("Posting send request \n");

    /* Post Send */
    dat_status = dat_ep_post_send( gEP, 
                                   0,
                                   NULL,
                                   (DAT_DTO_COOKIE)NULL,
                                   DAT_COMPLETION_DEFAULT_FLAG );

    if( DAT_SUCCESS != dat_status ) {
        fprintf(stderr, "  Error posting send request in Reset: %d\n", dat_status);
        exit(NP_FAILURE);
    }

    LOGPRINTF("Polling for completion of send request\n");

    dat_status = DAT_QUEUE_EMPTY;

    while ( DAT_SUCCESS != dat_status ) {
        dat_status = dat_evd_dequeue( gSendDtoEvd, &event );
    }

    if( DAT_DTO_COMPLETION_EVENT != event.event_number ) {
        fprintf(stderr, "Unexpected event when polling EVD: %d\n", event.event_number);
        exit(NP_FAILURE);
    }          

    LOGPRINTF("Status of send completion: %d\n", dat_status);

    LOGPRINTF("Polling for completion of receive request\n");

    dat_status = DAT_QUEUE_EMPTY;
    while ( DAT_SUCCESS != dat_status ) {
        dat_status = dat_evd_dequeue( gRecvDtoEvd, &event );
    }

    if( DAT_DTO_COMPLETION_EVENT != event.event_number ) {
        fprintf(stderr, "Unexpected event when polling EVD: %d\n", event.event_number);
        exit(NP_FAILURE);
    }          

    LOGPRINTF("Status of recv completion: %d\n", dat_status);

    LOGPRINTF("Done with reset\n");
}

void SendTime(ArgStruct *p, double *t)
{
    uint32_t ltime, ntime;

    /*
      Multiply the number of seconds by 1e6 to get time in microseconds
      and convert value to an unsigned 32-bit integer.
      */
    ltime = (uint32_t)(*t * 1.e6);

    /* Send time in network order */
    ntime = htonl(ltime);
    if (write(p->commfd, (char *)&ntime, sizeof(uint32_t)) < 0) {
        printf("NetPIPE: write failed in SendTime: errno=%d\n", errno);
        exit(301);
    }
}

void RecvTime(ArgStruct *p, double *t)
{
    uint32_t ltime, ntime;
    int bytesRead;

    bytesRead = readFully(p->commfd, (void *)&ntime, sizeof(uint32_t));
    if (bytesRead < 0) {
        printf("NetPIPE: read failed in RecvTime: errno=%d\n", errno);
        exit(302);
    } else if (bytesRead != sizeof(uint32_t)) {
        fprintf(stderr, "NetPIPE: partial read in RecvTime of %d bytes\n", bytesRead);
        exit(303);
    }
    ltime = ntohl(ntime);

    /* Result is ltime (in microseconds) divided by 1.0e6 to get seconds */
    *t = (double)ltime / 1.0e6;
}

void SendRepeat(ArgStruct *p, int rpt)
{
    uint32_t lrpt, nrpt;

    lrpt = rpt;
    /* Send repeat count as a long in network order */
    nrpt = htonl(lrpt);
    if (write(p->commfd, (void *) &nrpt, sizeof(uint32_t)) < 0) {
        printf("NetPIPE: write failed in SendRepeat: errno=%d\n", errno);
        exit(304);
    }
}

void RecvRepeat(ArgStruct *p, int *rpt)
{
    uint32_t lrpt, nrpt;
    int bytesRead;

    bytesRead = readFully(p->commfd, (void *)&nrpt, sizeof(uint32_t));

    if (bytesRead < 0) {
      printf("NetPIPE: read failed in RecvRepeat: errno=%d\n", errno);
      exit(305);
    } else if (bytesRead != sizeof(uint32_t)) {
      fprintf(stderr, "NetPIPE: partial read in RecvRepeat of %d bytes\n", bytesRead);
      exit(306);
    }

    lrpt = ntohl(nrpt);

    *rpt = lrpt;
}

void establish(ArgStruct *p)
{
 int clen;
 struct protoent;

 clen = sizeof(p->prot.sin2);
 if(p->tr){
   if(connect(p->commfd, (struct sockaddr *) &(p->prot.sin1),
              sizeof(p->prot.sin1)) < 0){
     printf("Client: Cannot Connect! errno=%d\n",errno);
     exit(-10);
   }
  }
  else {
    /* SERVER */
    listen(p->servicefd, 5);
    p->commfd = accept(p->servicefd, (struct sockaddr *) &(p->prot.sin2),
                       &clen);

    if(p->commfd < 0){
      printf("Server: Accept Failed! errno=%d\n",errno);
      exit(-12);
    }
  }
}

void CleanUp(ArgStruct *p)
{
   char *quit="QUIT";
   if (p->tr)
   {
      write(p->commfd,quit, 5);
      read(p->commfd, quit, 5);
      close(p->commfd);
   }
   else
   {
      read(p->commfd,quit, 5);
      write(p->commfd,quit,5);
      close(p->commfd);
      close(p->servicefd);
   }

   finalize_uDAPL(p);
}


void AfterAlignmentInit(ArgStruct *p)
{
  int bytesRead;

  /* Exchange buffer pointers and remote infiniband keys if doing rdma. Do
   * the exchange in this function because this will happen after any
   * memory alignment is done, which is important for getting the 
   * correct remote address.
  */
  if( p->prot.commtype == NP_COMM_RDMAWRITE ) {
     
     /* Send my receive buffer address
      */
     if(write(p->commfd, (void *)&p->r_buff, sizeof(void*)) < 0) {
        perror("NetPIPE: write of buffer address failed in AfterAlignmentInit");
        exit(-1);
     }
     
     LOGPRINTF("Sent buffer address: %p\n", p->r_buff);
     
     /* Send my remote key for accessing
      * my remote buffer via IB RDMA
      */
     if(write(p->commfd, (void *)&gRcvRmrContext, sizeof(DAT_RMR_CONTEXT)) < 0) {
        perror("NetPIPE: write of remote key failed in AfterAlignmentInit");
        exit(-1);
     }
  
     LOGPRINTF("Sent remote key: %d\n", gRcvRmrContext);
     
     /* Read the sent data
      */
     bytesRead = readFully(p->commfd, (void *)&gRemoteAddress, sizeof(void*));
     if (bytesRead < 0) {
        perror("NetPIPE: read of buffer address failed in AfterAlignmentInit");
        exit(-1);
     } else if (bytesRead != sizeof(void*)) {
        perror("NetPIPE: partial read of buffer address in AfterAlignmentInit");
        exit(-1);
     }
     
     LOGPRINTF("Received remote address from other node: %p\n", gRemoteAddress);
     
     bytesRead = readFully(p->commfd, (void *)&gRemoteKey, sizeof(DAT_RMR_CONTEXT));
     if (bytesRead < 0) {
        perror("NetPIPE: read of remote key failed in AfterAlignmentInit");
        exit(-1);
     } else if (bytesRead != sizeof(DAT_RMR_CONTEXT)) {
        perror("NetPIPE: partial read of remote key in AfterAlignmentInit");
        exit(-1);
     }
     
     LOGPRINTF("Received remote key from other node: %d\n", gRemoteKey);
  }
}


void MyMalloc(ArgStruct *p, int bufflen, int soffset, int roffset)
{
  DAT_RETURN dat_status;

  /* Allocate buffers */

  p->r_buff = malloc(bufflen+MAX(soffset,roffset));
  if(p->r_buff == NULL) {
    fprintf(stderr, "Error malloc'ing buffer\n");
    exit(NP_FAILURE);
  }

  if(p->cache) {

    /* Infiniband spec says we can register same memory region
     * more than once, so just copy buffer address. We will register
     * the same buffer twice with Infiniband.
     */
    p->s_buff = p->r_buff;

  } else {

    p->s_buff = malloc(bufflen+soffset);
    if(p->s_buff == NULL) {
      fprintf(stderr, "Error malloc'ing buffer\n");
      exit(NP_FAILURE);
    }
  }

  /* Register buffers with uDAPL */

  // register receive buffer
  gRegion.for_va = p->r_buff;

  dat_status = dat_lmr_create( gIA,
                               DAT_MEM_TYPE_VIRTUAL,
                               gRegion,
                               bufflen+MAX(soffset,roffset),
                               gPZ,
                               DAT_MEM_PRIV_READ_FLAG  |
                               DAT_MEM_PRIV_WRITE_FLAG | 
                               DAT_MEM_PRIV_REMOTE_WRITE_FLAG,
                               &gRcvLmrHandle,
                               &gRcvLmrContext,
                               &gRcvRmrContext,
                               NULL,
                               NULL );

  if ( DAT_SUCCESS != dat_status ) {
    fprintf(stderr, "Error registering receive buffer: 0x%x\n", dat_status);
    exit(NP_FAILURE);
  } else {
    LOGPRINTF("Registered Receive Buffer\n");
  }

  // register send buffer
  gRegion.for_va = p->s_buff;

  dat_status = dat_lmr_create( gIA,
                               DAT_MEM_TYPE_VIRTUAL,
                               gRegion,
                               bufflen+soffset,
                               gPZ,
                               DAT_MEM_PRIV_READ_FLAG | DAT_MEM_PRIV_WRITE_FLAG,
                               &gSndLmrHandle,
                               &gSndLmrContext,
                               NULL,
                               NULL,
                               NULL );

  if ( DAT_SUCCESS != dat_status ) {
    fprintf(stderr, "Error registering send buffer: 0x%x\n", dat_status);
    exit(NP_FAILURE);
  } else {
    LOGPRINTF("Registered Send Buffer\n");
  }
}

void FreeBuff(char *buff1, char *buff2)
{
  DAT_RETURN dat_status;

  if ( DAT_HANDLE_NULL != gSndLmrHandle ) {
    LOGPRINTF("Deregistering send buffer\n");

    dat_status = dat_lmr_free( gSndLmrHandle );

    if ( DAT_SUCCESS != dat_status ) {
      fprintf(stderr, "Error deregistering Send Memory Region: 0x%x\n", dat_status);
    } else {
      gSndLmrHandle = DAT_HANDLE_NULL;
    }
  }

  if ( DAT_HANDLE_NULL != gRcvLmrHandle ) {
    LOGPRINTF("Deregistering receive buffer\n");

    dat_status = dat_lmr_free( gRcvLmrHandle );

    if ( DAT_SUCCESS != dat_status ) {
      fprintf(stderr, "Error deregistering Receive Memory Region: 0x%x\n", dat_status);
    } else {
      gRcvLmrHandle = DAT_HANDLE_NULL;
    }
  }

  if( NULL != buff1 ) {
    free(buff1);
  }

  if( NULL != buff2 ) {
    free(buff2);
  }
}

