//#define ATOLL_MAX_MSG_SIZE 65536
//#define ATOLL_MAX_MSG_SIZE 32768
//#define ATOLL_MAX_MSG_SIZE 131072
//#define ATOLL_MAX_MSG_SIZE 16384
#define ATOLL_MAX_MSG_SIZE 8192
#define SEQ (1<<31)


int global_nbytes;
int err;

int Atoll_send_sliced(atoll_handle handle, unsigned int tag, void* buf, size_t buflen, void* head, size_t headlen, int flags)
{
  int i;
  int nr_msgs;
  int msg_remainder;
  void* buf_slice;
  int new_tag;
  int buf_slice_size;

  //ignore header size for the moment
  nr_msgs = (buflen)/ ATOLL_MAX_MSG_SIZE;
  msg_remainder = buflen%ATOLL_MAX_MSG_SIZE;

  if (tag & SEQ) printf ("ERROR tag\n");
  //send header and first part of data in first msg
     buf_slice=buf;
     new_tag=tag;
     
     if (buflen < ATOLL_MAX_MSG_SIZE)
       {
	 buf_slice_size = buflen;
	 msg_remainder=0;
       }
     else if ( buflen<ATOLL_MAX_MSG_SIZE*1.5)
       {
	 buf_slice_size=buflen;
	 nr_msgs=0;
	 msg_remainder = 0;
       }
     else 
       {
	 buf_slice_size=ATOLL_MAX_MSG_SIZE;
	 nr_msgs--;
       }
     
     if(nr_msgs || msg_remainder) new_tag = tag | SEQ;

     while(1)
       {
        err = Atoll_send(handle,new_tag, buf_slice, buf_slice_size, head, headlen, flags);
	 
         if (err == ATOLL_SUCCESS)  break;
	 if (err==ATOLL_ERR_NO_DESC_MEM || err==ATOLL_ERR_NO_DMA_MEM) {
	   continue;
	 }  else {
	   printf("Atoll_send returned error %d\n",err);
	   return err;
	 }
       }

 
  //send rest
 

  for(i=0; i<nr_msgs; i++)
    {
      new_tag=tag | SEQ;
      buf_slice+= buf_slice_size;
      buf_slice_size = ATOLL_MAX_MSG_SIZE;

      if(i==nr_msgs-1 && msg_remainder<ATOLL_MAX_MSG_SIZE/2) 
	{
	  new_tag= tag;
	  buf_slice_size = ATOLL_MAX_MSG_SIZE+msg_remainder;
	  msg_remainder=0;
	}
      while(1)
	{
	  err = Atoll_send(handle,new_tag, buf_slice, buf_slice_size, head, 0, flags);
	 
	  if (err == ATOLL_SUCCESS)  break;

	  if (err==ATOLL_ERR_NO_DESC_MEM || err==ATOLL_ERR_NO_DMA_MEM) {
	    continue;
	  }  else {
	    printf("Atoll_send returned error %d\n",err);
	    return err;
	  }
	}

    }

  //still something to send?
  if (msg_remainder)
    {
      buf_slice+=buf_slice_size;
      new_tag = tag;
      while(1)
 	{
	  err = Atoll_send(handle,new_tag, buf_slice, msg_remainder, head, 0, flags);
	  
	  if (err == ATOLL_SUCCESS)  break;
	  
	  if (err==ATOLL_ERR_NO_DESC_MEM || err==ATOLL_ERR_NO_DMA_MEM) {
	    continue;
	  }  else {
	    printf("Atoll_send returned error %d\n",err);
	    return err;
	  }
	}
    }

  return err;
}

int Atoll_recv_sliced(port_id self, void* buf, size_t buflen, void* head, size_t headlen, unsigned int* tag, unsigned flags, size_t* blen, size_t* hlen)
{
  //int slice=0;
  int tag_in; 
  void* buf_sliced;
  int slice_blen;
  int dummy_hlen;


  global_nbytes = buflen;

  while(1)
    {
      err = Atoll_recv(self, buf, buflen, head, headlen, &tag_in, 
                       flags, &slice_blen, hlen);

      if (err == ATOLL_SUCCESS) {
      	break;
      } else if (err == ATOLL_ERR_NO_MSG) {
	continue;
      } else {
	Atoll_perror("--Atoll_recv_message returned :",err);
	return err;
      }
    }
  
  *blen = slice_blen;
  buf_sliced = buf + slice_blen;

  while (tag_in & SEQ)
    {
     
      while(1)
	{
	  err = Atoll_recv(self, buf_sliced, buflen-*blen, NULL, 0, &tag_in, 
                       ATOLL_RECV_DMA, &slice_blen, &dummy_hlen);
	  
	  if (err == ATOLL_SUCCESS) {
	    *blen = *blen + slice_blen;
	    buf_sliced = buf_sliced + slice_blen;	    
	    break;
	  } else if (err == ATOLL_ERR_NO_MSG) {
	    continue;
	  } else {
	    Atoll_perror("---Atoll_recv_message returned :",err);
	    printf("buflen-*blen:%d\n", buflen-*blen);
	    return err;
	  }
	}
     

    }
  return 0;
}
