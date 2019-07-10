int MPID_ControlMsgAvail(void)
{

}

void MPID_RecvAnyControl(MPID_PKT_T *pkt, int size, int *from)
{
   MP_Recv(pkt, size, -1, 0);
}

void MPID_SendControl(MPID_PKT_T *pkt, int size, int dest)
{
   int msgid;

   MP_Send(pkt, size, dest, 0, &msgid);
}

void MPID_SendControlBlock(MPID_PKT_T *pkt, int size, int dest)
{
   MP_Send(pkt, size, dest, 0);
}

void MPID_RecvFromChannel(void *buf, int maxsize, int from)
{
   MP_Recv(buf, maxsize, from, from+1);
}

void MPID_SendChannel(void *buf, int size, int dest)
{
   MP_Send(buf, size, dest, dest+1);
}

/* Non Blocking Operations */

void MPID_IRecvFromChannel(void *buf, int size, int dest, MPI_Request id)
{
	MP_ARecv(buf, size, dest, dest+1, &id);
}

void MPID_WRecvFromChannel(void *buf, int size, int dest, MPI_Request id)
{
	MP_Wait(&id);
}

void MPID_RecvStatus(MPI_Request id)
{

}

void MPID_ISendChannel(void *buf, int size, int dest, MPI_Request id)
{
	MP_ASend(buf, size, dest, myproc+1, &id);
}

void MPID_WSendChannel(void *buf, int size, int dest, MPI_Request id)
{
	MP_Wait(&id);
}

void MPID_TSendChannel(MPI_Request id)
{
	
}

/* Out-of-Band Operations */

static int CurTag = 1024;
static int TagsInUse = 0;

MPID_CreateSendTransfer(void *buf, int size, int partner, MPI_Request id)
{
	*(id) = 0;
}

MPID_CreateRecvTransfer(void *buf, int size, int partner, MPI_Request id)
{
	*(id) = CurTag++;
	TagsInUse++;
}

MPID_StartRecvTransfer(void *buf, int size, int partner, int id, MPI_Request rid)
{
	MP_ARecv(buf, size, partner, id, &rid);
}

MPID_EndRecvTransfer(void *buf, int size, int partner, int id, MPI_Request rid)
{
	MP_Wait(&rid);

	if ( --TagsInUse == 0 ) CurTag = 1024;
	else if ( id == CurTag-1 ) CurTag--;
}

MPID_TestRecvTransfer(MPI_Request rid)
{

}

MPID_StartSendTransfer(void *buf, int size, int partner, int id, MPI_Request sid)
{
	MP_ASend(buf, size, partner, 1+id, &sid);
}

MPID_EndSendTransfer(void *buf, int size, int partner, int id, MPI_Request sid)
{
	MP_Wait(&sid);
}

MPID_TestSendTransfer(MPI_Request sid)
{

}
