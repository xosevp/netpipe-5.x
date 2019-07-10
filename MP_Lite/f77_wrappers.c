/* MP_Lite message-passing library
 * Copyright (C) 1997-2004 Dave Turner
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

/*    T3E         - F77 calls are all caps
 *    Linux g77   - F77 calls are lower case with double underscores
 *    Linux pgf77 - F77 calls are lower case with a single underscore
 *    SGI IRIX6.2 - F77 calls are lower case with a single underscore
 *    IBM AIX     - F77 calls are lower case with no underscores
 */


#include "globals.h"
#include "mplite.h"

#if defined (MPI)
  void MP_INIT_F2C()   { MP_Init(NULL,NULL); }
  void mp_init_f2c()   { MP_Init(NULL,NULL); }
  void mp_init_f2c_()  { MP_Init(NULL,NULL); }
  void mp_init_f2c__() { MP_Init(NULL,NULL); }
#else
  void MP_INIT  (int *argc, char ***argv) { MP_Init(NULL,NULL); }
  void mp_init  (int *argc, char ***argv) { MP_Init(NULL,NULL); }
  void mp_init_ (int *argc, char ***argv) { MP_Init(NULL,NULL); }
  void mp_init__(int *argc, char ***argv) { MP_Init(NULL,NULL); }
#endif

void MP_NPROCS(int* n) { MP_Nprocs(n);} 
void mp_nprocs(int* n) { MP_Nprocs(n);} 
void mp_nprocs_(int* n) { MP_Nprocs(n);} 
void mp_nprocs__(int* n) { MP_Nprocs(n);} 

void MP_MYPROC(int* m) { MP_Myproc(m); } 
void mp_myproc(int* m) { MP_Myproc(m); } 
void mp_myproc_(int* m) { MP_Myproc(m); } 
void mp_myproc__(int* m) { MP_Myproc(m); } 

void MP_FINALIZE() { MP_Finalize();}
void mp_finalize() { MP_Finalize();}
void mp_finalize_() { MP_Finalize();}
void mp_finalize__() { MP_Finalize();}


void MP_WAIT(int *msg_id) { MP_Wait(msg_id); }
void mp_wait(int *msg_id) { MP_Wait(msg_id); }
void mp_wait_(int *msg_id) { MP_Wait(msg_id); }
void mp_wait__(int *msg_id) { MP_Wait(msg_id); }

/* MP_Send() */

void MP_SEND(void *buf, int *nbytes, int *dest, int *tag)
     { MP_Send(buf, *nbytes, *dest, *tag); }
void mp_send(void *buf, int *nbytes, int *dest, int *tag)
     { MP_Send(buf, *nbytes, *dest, *tag); }
void mp_send_(void *buf, int *nbytes, int *dest, int *tag)
     { MP_Send(buf, *nbytes, *dest, *tag); }
void mp_send__(void *buf, int *nbytes, int *dest, int *tag)
     { MP_Send(buf, *nbytes, *dest, *tag); }

void MP_DSEND(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(double), *dest, *tag); }
void mp_dsend(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(double), *dest, *tag); }
void mp_dsend_(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(double), *dest, *tag); }
void mp_dsend__(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(double), *dest, *tag); }

void MP_ISEND(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(int), *dest, *tag); }
void mp_isend(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(int), *dest, *tag); }
void mp_isend_(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(int), *dest, *tag); }
void mp_isend__(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(int), *dest, *tag); }

void MP_FSEND(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(float), *dest, *tag); }
void mp_fsend(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(float), *dest, *tag); }
void mp_fsend_(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(float), *dest, *tag); }
void mp_fsend__(void *buf, int *count, int *dest, int *tag)
     { MP_Send(buf,*count*sizeof(float), *dest, *tag); }

/* MP_ASend() */

void MP_ASEND(void *buf, int *nbytes, int *dest, int *tag, int *msg_id)
     { MP_ASend(buf, *nbytes, *dest, *tag, msg_id); }
void mp_asend(void *buf, int *nbytes, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *nbytes, *dest, *tag, msg_id); }
void mp_asend_(void *buf, int *nbytes, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *nbytes, *dest, *tag, msg_id); }
void mp_asend__(void *buf, int *nbytes, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *nbytes, *dest, *tag, msg_id); }

void MP_DASEND(void *buf, int *count, int *dest, int *tag, int *msg_id)
     { MP_ASend(buf, *count*sizeof(double), *dest, *tag, msg_id); }
void mp_dasend(void *buf, int *count, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *count*sizeof(double), *dest, *tag, msg_id); }
void mp_dasend_(void *buf, int *count, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *count*sizeof(double), *dest, *tag, msg_id); }
void mp_dasend__(void *buf, int *count, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *count*sizeof(double), *dest, *tag, msg_id); }

void MP_IASEND(void *buf, int *count, int *dest, int *tag, int *msg_id)
     { MP_ASend(buf, *count*sizeof(int), *dest, *tag, msg_id); }
void mp_iasend(void *buf, int *count, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *count*sizeof(int), *dest, *tag, msg_id); }
void mp_iasend_(void *buf, int *count, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *count*sizeof(int), *dest, *tag, msg_id); }
void mp_iasend__(void *buf, int *count, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *count*sizeof(int), *dest, *tag, msg_id); }

void MP_FASEND(void *buf, int *count, int *dest, int *tag, int *msg_id)
     { MP_ASend(buf, *count*sizeof(float), *dest, *tag, msg_id); }
void mp_fasend(void *buf, int *count, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *count*sizeof(float), *dest, *tag, msg_id); }
void mp_fasend_(void *buf, int *count, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *count*sizeof(float), *dest, *tag, msg_id); }
void mp_fasend__(void *buf, int *count, int *dest, int *tag,int *msg_id)
     { MP_ASend(buf, *count*sizeof(float), *dest, *tag, msg_id); }


/* MP_Recv() */

void MP_RECV(void *buf, int *nbytes, int *src, int *tag)
     { MP_Recv(buf, *nbytes, *src, *tag); }
void mp_recv(void *buf, int *nbytes, int *src, int *tag)
     { MP_Recv(buf, *nbytes, *src, *tag); }
void mp_recv_(void *buf, int *nbytes, int *src, int *tag)
     { MP_Recv(buf, *nbytes, *src, *tag); }
void mp_recv__(void *buf, int *nbytes, int *src, int *tag)
     { MP_Recv(buf, *nbytes, *src, *tag); }

void MP_DRECV(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(double), *src, *tag); }
void mp_drecv(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(double), *src, *tag); }
void mp_drecv_(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(double), *src, *tag); }
void mp_drecv__(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(double), *src, *tag); }

void MP_IRECV(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(int), *src, *tag); }
void mp_irecv(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(int), *src, *tag); }
void mp_irecv_(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(int), *src, *tag); }
void mp_irecv__(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(int), *src, *tag); }

void MP_FRECV(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(float), *src, *tag); }
void mp_frecv(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(float), *src, *tag); }
void mp_frecv_(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(float), *src, *tag); }
void mp_frecv__(void *buf, int *count, int *src, int *tag)
     { MP_Recv(buf, *count*sizeof(float), *src, *tag); }

/* MP_ARecv() */

void MP_ARECV(void *buf, int *nbytes, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *nbytes, *src, *tag, msg_id); }
void mp_arecv(void *buf, int *nbytes, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *nbytes, *src, *tag, msg_id); }
void mp_arecv_(void *buf, int *nbytes, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *nbytes, *src, *tag, msg_id); }
void mp_arecv__(void *buf, int *nbytes, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *nbytes, *src, *tag, msg_id); }

void MP_DARECV(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(double), *src, *tag, msg_id); }
void mp_darecv(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(double), *src, *tag, msg_id); }
void mp_darecv_(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(double), *src, *tag, msg_id); }
void mp_darecv__(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(double), *src, *tag, msg_id); }

void MP_IARECV(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(int), *src, *tag, msg_id); }
void mp_iarecv(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(int), *src, *tag, msg_id); }
void mp_iarecv_(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(int), *src, *tag, msg_id); }
void mp_iarecv__(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(int), *src, *tag, msg_id); }

void MP_FARECV(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(float), *src, *tag, msg_id); }
void mp_farecv(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(float), *src, *tag, msg_id); }
void mp_farecv_(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(float), *src, *tag, msg_id); }
void mp_farecv__(void *buf, int *count, int *src, int *tag, int *msg_id)
     { MP_ARecv(buf, *count*sizeof(float), *src, *tag, msg_id); }



void MP_SSEND(void *buf, int *nbytes, int *dest, int *tag)
     { MP_SSend(buf, *nbytes, *dest, *tag); }
void mp_ssend(void *buf, int *nbytes, int *dest, int *tag)
     { MP_SSend(buf, *nbytes, *dest, *tag); }
void mp_ssend_(void *buf, int *nbytes, int *dest, int *tag)
     { MP_SSend(buf, *nbytes, *dest, *tag); }
void mp_ssend__(void *buf, int *nbytes, int *dest, int *tag)
     { MP_SSend(buf, *nbytes, *dest, *tag); }

void MP_SRECV(void *buf, int *nbytes, int *src, int *tag)
     { MP_SRecv(buf, *nbytes, *src, *tag); }
void mp_srecv(void *buf, int *nbytes, int *src, int *tag)
     { MP_SRecv(buf, *nbytes, *src, *tag); }
void mp_srecv_(void *buf, int *nbytes, int *src, int *tag)
     { MP_SRecv(buf, *nbytes, *src, *tag); }
void mp_srecv__(void *buf, int *nbytes, int *src, int *tag)
     { MP_SRecv(buf, *nbytes, *src, *tag); }

void MP_SSHIFT(void *buf_out, int *dest, void *buf_in, int *src, int *nbytes)
     { MP_SShift( buf_out, *dest, buf_in, *src, *nbytes); }
void mp_sshift(void *buf_out,int *dest, void *buf_in,int *src,int *nbytes)
     { MP_SShift( buf_out, *dest, buf_in, *src, *nbytes); }
void mp_sshift_(void *buf_out,int *dest, void *buf_in,int *src,int *nbytes)
     { MP_SShift( buf_out, *dest, buf_in, *src, *nbytes); }
void mp_sshift__(void *buf_out,int *dest, void *buf_in,int *src,int *nbytes)
     { MP_SShift( buf_out, *dest, buf_in, *src, *nbytes); }

/* shm-based "in place" communication functions */

/*
void MP_IPSEND  (char *buf, int *offset, int *nbytes, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *nbytes, *dest, *tag); }
void mp_ipsend  (char *buf, int *offset, int *nbytes, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *nbytes, *dest, *tag); }
void mp_ipsend_ (char *buf, int *offset, int *nbytes, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *nbytes, *dest, *tag); }
void mp_ipsend__(char *buf, int *offset, int *nbytes, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *nbytes, *dest, *tag); }

void MP_DIPSEND  (double *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(double), *dest, *tag); }
void mp_dipsend  (double *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(double), *dest, *tag); }
void mp_dipsend_ (double *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(double), *dest, *tag); }
void mp_dipsend__(double *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(double), *dest, *tag); }

void MP_IIPSEND  (int *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(int), *dest, *tag); }
void mp_iipsend  (int *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(int), *dest, *tag); }
void mp_iipsend_ (int *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(int), *dest, *tag); }
void mp_iipsend__(int *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(int), *dest, *tag); }

void MP_FIPSEND  (float *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(float), *dest, *tag); }
void mp_fipsend  (float *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(float), *dest, *tag); }
void mp_fipsend_ (float *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(float), *dest, *tag); }
void mp_fipsend__(float *buf, int *offset, int *count, int *dest, int *tag)
     { MP_IPSend(&buf[*offset], *count*sizeof(float), *dest, *tag); }

void MP_IPRECV  (char *buf, int *offset, int *nbytes, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *nbytes, *src, *tag);
       *offset = (int) ((char *)ptr - buf); }
void mp_iprecv  (char *buf, int *offset, int *nbytes, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *nbytes, *src, *tag);
       *offset = (int) ((char *)ptr - buf); }
void mp_iprecv_ (char *buf, int *offset, int *nbytes, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *nbytes, *src, *tag);
       *offset = (int) ((char *)ptr - buf); }
void mp_iprecv__(char *buf, int *offset, int *nbytes, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *nbytes, *src, *tag);
       *offset = (int) ((char *)ptr - buf); }

void MP_DIPRECV  (double *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(double), *src, *tag);
       *offset = (int) ((double *)ptr - buf); }
void mp_diprecv  (double *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(double), *src, *tag);
       *offset = (int) ((double *)ptr - buf); }
void mp_diprecv_ (double *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(double), *src, *tag);
       *offset = (int) ((double *)ptr - buf); }
void mp_diprecv__(double *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(double), *src, *tag);
       *offset = (int) ((double *)ptr - buf); }

void MP_IIPRECV  (int *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(int), *src, *tag);
       *offset = (int) ((int *)ptr - buf); }
void mp_iiprecv  (int *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(int), *src, *tag);
       *offset = (int) ((int *)ptr - buf); }
void mp_iiprecv_ (int *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(int), *src, *tag);
       *offset = (int) ((int *)ptr - buf); }
void mp_iiprecv__(int *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(int), *src, *tag);
       *offset = (int) ((int *)ptr - buf); }

void MP_FIPRECV  (float *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(float), *src, *tag);
       *offset = (int) ((float *)ptr - buf); }
void mp_fiprecv  (float *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(float), *src, *tag);
       *offset = (int) ((float *)ptr - buf); }
void mp_fiprecv_ (float *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(float), *src, *tag);
       *offset = (int) ((float *)ptr - buf); }
void mp_fiprecv__(float *buf, int *offset, int *count, int *src, int *tag)
     { void *ptr = NULL;
       MP_IPRecv(&ptr, *count*sizeof(float), *src, *tag);
       *offset = (int) ((float *)ptr - buf); }

void MP_SHMMALLOC  ( char *buf, int *offset, int *nbytes)
     { char *ptr; void *MP_shmMalloc();
       ptr = (char *) MP_shmMalloc( *nbytes);
       *offset = (int) (ptr - buf); }
void mp_shmmalloc  ( char *buf, int *offset, int *nbytes)
     { char *ptr; void *MP_shmMalloc();
       ptr = (char *) MP_shmMalloc( *nbytes);
       *offset = (int) (ptr - buf); }
void mp_shmmalloc_ ( char *buf, int *offset, int *nbytes)
     { char *ptr; void *MP_shmMalloc();
       ptr = (char *) MP_shmMalloc( *nbytes);
       *offset = (int) (ptr - buf); }
void mp_shmmalloc__( char *buf, int *offset, int *nbytes)
     { char *ptr; void *MP_shmMalloc();
       ptr = (char *) MP_shmMalloc( *nbytes);
       *offset = (int) (ptr - buf); }

void MP_DSHMMALLOC  ( double *buf, int *offset, int *count)
     { double *ptr; void *MP_shmMalloc();
       ptr = (double *) MP_shmMalloc( *count*sizeof(double));
       *offset = (int) (ptr - buf); }
void mp_dshmmalloc  ( double *buf, int *offset, int *count)
     { double *ptr; void *MP_shmMalloc();
       ptr = (double *) MP_shmMalloc( *count*sizeof(double));
       *offset = (int) (ptr - buf); }
void mp_dshmmalloc_ ( double *buf, int *offset, int *count)
     { double *ptr; void *MP_shmMalloc();
       ptr = (double *) MP_shmMalloc( *count*sizeof(double));
       *offset = (int) (ptr - buf); }
void mp_dshmmalloc__( double *buf, int *offset, int *count)
     { double *ptr; void *MP_shmMalloc();
       ptr = (double *) MP_shmMalloc( *count*sizeof(double));
       *offset = (int) (ptr - buf); }

void MP_ISHMMALLOC  ( int *buf, int *offset, int *count)
     { int *ptr; void *MP_shmMalloc();
       ptr = (int *) MP_shmMalloc( *count*sizeof(int));
       *offset = (int) (ptr - buf); }
void mp_ishmmalloc  ( int *buf, int *offset, int *count)
     { int *ptr; void *MP_shmMalloc();
       ptr = (int *) MP_shmMalloc( *count*sizeof(int));
       *offset = (int) (ptr - buf); }
void mp_ishmmalloc_ ( int *buf, int *offset, int *count)
     { int *ptr; void *MP_shmMalloc();
       ptr = (int *) MP_shmMalloc( *count*sizeof(int));
       *offset = (int) (ptr - buf); }
void mp_ishmmalloc__( int *buf, int *offset, int *count)
     { int *ptr; void *MP_shmMalloc();
       ptr = (int *) MP_shmMalloc( *count*sizeof(int));
       *offset = (int) (ptr - buf); }

void MP_FSHMMALLOC  ( float *buf, int *offset, int *count)
     { float *ptr; void *MP_shmMalloc();
       ptr = (float *) MP_shmMalloc( *count*sizeof(float));
       *offset = (int) (ptr - buf); }
void mp_fshmmalloc  ( float *buf, int *offset, int *count)
     { float *ptr; void *MP_shmMalloc();
       ptr = (float *) MP_shmMalloc( *count*sizeof(float));
       *offset = (int) (ptr - buf); }
void mp_fshmmalloc_ ( float *buf, int *offset, int *count)
     { float *ptr; void *MP_shmMalloc();
       ptr = (float *) MP_shmMalloc( *count*sizeof(float));
       *offset = (int) (ptr - buf); }
void mp_fshmmalloc__( float *buf, int *offset, int *count)
     { float *ptr; void *MP_shmMalloc();
       ptr = (float *) MP_shmMalloc( *count*sizeof(float));
       *offset = (int) (ptr - buf); }

void MP_SHMFREE  ( char *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_shmfree  ( char *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_shmfree_ ( char *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_shmfree__( char *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }

void MP_DSHMFREE  ( double *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_dshmfree  ( double *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_dshmfree_ ( double *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_dshmfree__( double *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }

void MP_ISHMFREE  ( int *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_ishmfree  ( int *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_ishmfree_ ( int *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_ishmfree__( int *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }

void MP_FSHMFREE  ( float *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_fshmfree  ( float *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_fshmfree_ ( float *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }
void mp_fshmfree__( float *buf, int *offset)
     { MP_shmFree( &buf[*offset] ); }

*/

/* malloc() and free() functions for f77 */

void MP_MALLOC  ( char *buf, int *offset, int *count)
     { char *ptr;
       ptr = (char *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_malloc  ( char *buf, int *offset, int *count)
     { char *ptr;
       ptr = (char *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_malloc_ ( char *buf, int *offset, int *count)
     { char *ptr;
       ptr = (char *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_malloc__( char *buf, int *offset, int *count)
     { char *ptr;
       ptr = (char *) malloc( *count );
       *offset = (int) (ptr - buf); }

void MP_DMALLOC  ( double *buf, int *offset, int *count)
     { double *ptr;
       ptr = (double *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_dmalloc  ( double *buf, int *offset, int *count)
     { double *ptr;
       ptr = (double *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_dmalloc_ ( double *buf, int *offset, int *count)
     { double *ptr;
       ptr = (double *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_dmalloc__( double *buf, int *offset, int *count)
     { double *ptr;
       ptr = (double *) malloc( *count );
       *offset = (int) (ptr - buf); }

void MP_IMALLOC  ( int *buf, int *offset, int *count)
     { int *ptr;
       ptr = (int *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_imalloc  ( int *buf, int *offset, int *count)
     { int *ptr;
       ptr = (int *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_imalloc_ ( int *buf, int *offset, int *count)
     { int *ptr;
       ptr = (int *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_imalloc__( int *buf, int *offset, int *count)
     { int *ptr;
       ptr = (int *) malloc( *count );
       *offset = (int) (ptr - buf); }

void MP_FMALLOC  ( float *buf, int *offset, int *count)
     { float *ptr;
       ptr = (float *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_fmalloc  ( float *buf, int *offset, int *count)
     { float *ptr;
       ptr = (float *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_fmalloc_ ( float *buf, int *offset, int *count)
     { float *ptr;
       ptr = (float *) malloc( *count );
       *offset = (int) (ptr - buf); }
void mp_fmalloc__( float *buf, int *offset, int *count)
     { float *ptr;
       ptr = (float *) malloc( *count );
       *offset = (int) (ptr - buf); }

void MP_FREE  ( char *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_free  ( char *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_free_ ( char *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_free__( char *buf, int *offset)
     { free( &buf[*offset] ); }

void MP_DFREE  ( double *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_dfree  ( double *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_dfree_ ( double *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_dfree__( double *buf, int *offset)
     { free( &buf[*offset] ); }

void MP_IFREE  ( int *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_ifree  ( int *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_ifree_ ( int *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_ifree__( int *buf, int *offset)
     { free( &buf[*offset] ); }

void MP_FFREE  ( float *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_ffree  ( float *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_ffree_ ( float *buf, int *offset)
     { free( &buf[*offset] ); }
void mp_ffree__( float *buf, int *offset)
     { free( &buf[*offset] ); }


    /* Global operations from mplite.c */

void MP_SYNC() { MP_Sync(); }
void mp_sync() { MP_Sync(); }
void mp_sync_() { MP_Sync(); }
void mp_sync__() { MP_Sync(); }

void MP_BROADCAST(void *ptr, int *nbytes) { MP_Broadcast(ptr, *nbytes); }
void mp_broadcast(void *ptr, int *nbytes) { MP_Broadcast(ptr, *nbytes); }
void mp_broadcast_(void *ptr, int *nbytes) { MP_Broadcast(ptr, *nbytes); }
void mp_broadcast__(void *ptr, int *nbytes) { MP_Broadcast(ptr, *nbytes); }

void MP_DBROADCAST(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(double)); }
void mp_dbroadcast(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(double)); }
void mp_dbroadcast_(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(double)); }
void mp_dbroadcast__(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(double)); }

void MP_IBROADCAST(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(int)); }
void mp_ibroadcast(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(int)); }
void mp_ibroadcast_(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(int)); }
void mp_ibroadcast__(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(int)); }

void MP_FBROADCAST(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(float)); }
void mp_fbroadcast(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(float)); }
void mp_fbroadcast_(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(float)); }
void mp_fbroadcast__(void *ptr, int *count)
     { MP_Broadcast(ptr, *count * sizeof(float)); }


void MP_BCAST  (void *ptr, int *nbytes, int *root)
     { MP_Bcast(ptr, *nbytes, *root); }
void mp_bcast  (void *ptr, int *nbytes, int *root)
     { MP_Bcast(ptr, *nbytes, *root); }
void mp_bcast_ (void *ptr, int *nbytes, int *root)
     { MP_Bcast(ptr, *nbytes, *root); }
void mp_bcast__(void *ptr, int *nbytes, int *root)
     { MP_Bcast(ptr, *nbytes, *root); }

void MP_DBCAST  (void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(double), *root); }
void mp_dbcast  (void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(double), *root); }
void mp_dbcast_ (void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(double), *root); }
void mp_dbcast__(void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(double), *root); }

void MP_IBCAST  (void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(int), *root); }
void mp_ibcast  (void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(int), *root); }
void mp_ibcast_ (void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(int), *root); }
void mp_ibcast__(void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(int), *root); }

void MP_FBCAST  (void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(float), *root); }
void mp_fbcast  (void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(float), *root); }
void mp_fbcast_ (void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(float), *root); }
void mp_fbcast__(void *ptr, int *count, int *root)
     { MP_Bcast(ptr, *count * sizeof(float), *root); }


void MP_DSUM(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 0, MP_all_mask); }
void mp_dsum(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 0, MP_all_mask); }
void mp_dsum_(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 0, MP_all_mask); }
void mp_dsum__(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 0, MP_all_mask); }

void MP_ISUM(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 1, MP_all_mask); }
void mp_isum(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 1, MP_all_mask); }
void mp_isum_(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 1, MP_all_mask); }
void mp_isum__(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 1, MP_all_mask); }

void MP_FSUM(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 2, MP_all_mask); }
void mp_fsum(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 2, MP_all_mask); }
void mp_fsum_(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 2, MP_all_mask); }
void mp_fsum__(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 2, MP_all_mask); }

void MP_DMAX(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 3, MP_all_mask); }
void mp_dmax(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 3, MP_all_mask); }
void mp_dmax_(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 3, MP_all_mask); }
void mp_dmax__(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 3, MP_all_mask); }

void MP_IMAX(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 4, MP_all_mask); }
void mp_imax(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 4, MP_all_mask); }
void mp_imax_(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 4, MP_all_mask); }
void mp_imax__(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 4, MP_all_mask); }

void MP_FMAX(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 5, MP_all_mask); }
void mp_fmax(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 5, MP_all_mask); }
void mp_fmax_(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 5, MP_all_mask); }
void mp_fmax__(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 5, MP_all_mask); }

void MP_DMIN(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 6, MP_all_mask); }
void mp_dmin(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 6, MP_all_mask); }
void mp_dmin_(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 6, MP_all_mask); }
void mp_dmin__(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 6, MP_all_mask); }

void MP_IMIN(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 7, MP_all_mask); }
void mp_imin(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 7, MP_all_mask); }
void mp_imin_(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 7, MP_all_mask); }
void mp_imin__(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 7, MP_all_mask); }

void MP_FMIN(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 8, MP_all_mask); }
void mp_fmin(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 8, MP_all_mask); }
void mp_fmin_(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 8, MP_all_mask); }
void mp_fmin__(void *ptr, int *nelements) 
     { MP_Global(ptr, *nelements, 8, MP_all_mask); }


  /* Masked versions of the global operations */

void MP_DSUM_MASKED(void *ptr, int *nelements, int *mask) 
     { MP_Global(ptr, *nelements, 0, mask); }
void mp_dsum_masked(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 0, mask); }
void mp_dsum_masked_(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 0, mask); }
void mp_dsum_masked__(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 0, mask); }

void MP_ISUM_MASKED(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 1, mask); }
void mp_isum_masked(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 1, mask); }
void mp_isum_masked_(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 1, mask); }
void mp_isum_masked__(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 1, mask); }

void MP_FSUM_MASKED(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 2, mask); }
void mp_fsum_masked(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 2, mask); }
void mp_fsum_masked_(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 2, mask); }
void mp_fsum_masked__(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 2, mask); }

void MP_DMAX_MASKED(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 3, mask); }
void mp_dmax_masked(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 3, mask); }
void mp_dmax_masked_(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 3, mask); }
void mp_dmax_masked__(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 3, mask); }

void MP_IMAX_MASKED(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 4, mask); }
void mp_imax_masked(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 4, mask); }
void mp_imax_masked_(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 4, mask); }
void mp_imax_masked__(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 4, mask); }

void MP_FMAX_MASKED(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 5, mask); }
void mp_fmax_masked(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 5, mask); }
void mp_fmax_masked_(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 5, mask); }
void mp_fmax_masked__(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 5, mask); }

void MP_DMIN_MASKED(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 6, mask); }
void mp_dmin_masked(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 6, mask); }
void mp_dmin_masked_(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 6, mask); }
void mp_dmin_masked__(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 6, mask); }

void MP_IMIN_MASKED(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 7, mask); }
void mp_imin_masked(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 7, mask); }
void mp_imin_masked_(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 7, mask); }
void mp_imin_masked__(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 7, mask); }

void MP_FMIN_MASKED(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 8, mask); }
void mp_fmin_masked(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 8, mask); }
void mp_fmin_masked_(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 8, mask); }
void mp_fmin_masked__(void *ptr, int *nelements, int *mask)
     { MP_Global(ptr, *nelements, 8, mask); }

double MP_TIME() { double MP_Time(); return MP_Time(); }
double mp_time() { double MP_Time(); return MP_Time(); }
double mp_time_() { double MP_Time(); return MP_Time(); }
double mp_time__() { double MP_Time(); return MP_Time(); }


/* Functions from timer.c */

void MP_ENTER_C(char* string) { MP_Enter(string); }
void mp_enter_c(char* string) { MP_Enter(string); }
void mp_enter_c_(char* string) { MP_Enter(string); }
void mp_enter_c__(char* string) { MP_Enter(string); }

void MP_LEAVE(double* flops)
{
#ifdef USER_FRIENDLY
  char buf[15];
  int      exp;
  char      *p;
  double flops_out = 0.0;

  bzero (buf, 15);
  sprintf (buf,"%.2e",*flops);
  p = strchr (buf, (int)'e');
  p += 1;
  exp = atoi(p);
  if ( exp > 18 || exp < -18 )
  {
    fprintf (stderr, "Bad argument to MP_Leave(), converting it to 0.0\n");
  } else flops_out = *flops;
#endif
  MP_Leave(flops_out);
}

void mp_leave(double* flops)
{
#ifdef USER_FRIENDLY
  char buf[15];
  int      exp;
  char      *p;
  double flops_out = 0.0;

  bzero (buf, 15);
  sprintf (buf,"%.2e",*flops);
  p = strchr (buf, (int)'e');
  p += 1;
  exp = atoi(p);
  if ( exp > 18 || exp < -18 )
  {
    fprintf (stderr, "Bad argument to MP_Leave(), converting it to 0.0\n");
  } else flops_out = *flops;
#endif
  MP_Leave(flops_out);
}

void mp_leave_(double* flops)
{
#ifdef USER_FRIENDLY
  char buf[15];
  int      exp;
  char      *p;
  double flops_out = 0.0;

  bzero (buf, 15);
  sprintf (buf,"%.2e",*flops);
  p = strchr (buf, (int)'e');
  p += 1;
  exp = atoi(p);
  if ( exp > 18 || exp < -18 )
  {
    fprintf (stderr, "Bad argument to MP_Leave(), converting it to 0.0\n");
  } else flops_out = *flops;
#endif
  MP_Leave(flops_out);
}

void mp_leave__(double* flops)
{
#ifdef USER_FRIENDLY
  char buf[15];
  int      exp;
  char      *p;
  double flops_out = 0.0;

  bzero (buf, 15);
  sprintf (buf,"%.2e",*flops);
  p = strchr (buf, (int)'e');
  p += 1;
  exp = atoi(p);
  if ( exp > 18 || exp < -18 )
  {
    fprintf (stderr, "Bad argument to MP_Leave(), converting it to 0.0\n");
  } else flops_out = *flops;
#endif
  MP_Leave(flops_out);
}

void MP_TIME_REPORT_C(char* filename) { MP_Time_Report(filename); }
void mp_time_report_c(char* filename) { MP_Time_Report(filename); }
void mp_time_report_c_(char* filename) { MP_Time_Report(filename); }
void mp_time_report_c__(char* filename) { MP_Time_Report(filename); }

void MP_RESET_TIME() { MP_Reset_Time(); }
void mp_reset_time() { MP_Reset_Time(); }
void mp_reset_time_() { MP_Reset_Time(); }
void mp_reset_time__() { MP_Reset_Time(); }

void MP_ZERO_TIME  (char* name) { MP_Zero_Time( name ); }
void mp_zero_time  (char* name) { MP_Zero_Time( name ); }
void mp_zero_time_ (char* name) { MP_Zero_Time( name ); }
void mp_zero_time__(char* name) { MP_Zero_Time( name ); }

double MP_GET_TIME_C(char *name)
    { double MP_Get_Time(); return MP_Get_Time(name); }
double mp_get_time_c(char *name)
    { double MP_Get_Time(); return MP_Get_Time(name); }
double mp_get_time_c_(char *name)
    { double MP_Get_Time(); return MP_Get_Time(name); }
double mp_get_time_c__(char *name)
    { double MP_Get_Time(); return MP_Get_Time(name); }

double MP_GET_FLOPS_C(char *name)
    { double MP_Get_Flops(); return MP_Get_Flops(name); }
double mp_get_flops_c(char *name)
    { double MP_Get_Flops(); return MP_Get_Flops(name); }
double mp_get_flops_c_(char *name)
    { double MP_Get_Flops(); return MP_Get_Flops(name); }
double mp_get_flops_c__(char *name)
    { double MP_Get_Flops(); return MP_Get_Flops(name); }

double MP_MYFLOPS() { double MP_Myflops(); return MP_Myflops(); }
double mp_myflops() { double MP_Myflops(); return MP_Myflops(); }
double mp_myflops_() { double MP_Myflops(); return MP_Myflops(); }
double mp_myflops__() { double MP_Myflops(); return MP_Myflops(); }

void MP_BEDCHECK  () { MP_Bedcheck(); }
void mp_bedcheck  () { MP_Bedcheck(); }
void mp_bedcheck_ () { MP_Bedcheck(); }
void mp_bedcheck__() { MP_Bedcheck(); }

void MP_SET_DEBUG  (int *value){ MP_Set_Debug( *value); }
void mp_set_debug  (int *value){ MP_Set_Debug( *value); }
void mp_set_debug_ (int *value){ MP_Set_Debug( *value); }
void mp_set_debug__(int *value){ MP_Set_Debug( *value); }

/* Last source and tag functions for f77 */

void MP_GET_LAST_SOURCE  ( int *source ){ MP_Get_Last_Source( source ); }
void mp_get_last_source  ( int *source ){ MP_Get_Last_Source( source ); }
void mp_get_last_source_ ( int *source ){ MP_Get_Last_Source( source ); }
void mp_get_last_source__( int *source ){ MP_Get_Last_Source( source ); }

void MP_GET_LAST_TAG  ( int *tag ){ MP_Get_Last_Tag( tag ); }
void mp_get_last_tag  ( int *tag ){ MP_Get_Last_Tag( tag ); }
void mp_get_last_tag_ ( int *tag ){ MP_Get_Last_Tag( tag ); }
void mp_get_last_tag__( int *tag ){ MP_Get_Last_Tag( tag ); }

void MP_GET_LAST_SIZE  ( int *size ){ MP_Get_Last_Size( size ); }
void mp_get_last_size  ( int *size ){ MP_Get_Last_Size( size ); }
void mp_get_last_size_ ( int *size ){ MP_Get_Last_Size( size ); }
void mp_get_last_size__( int *size ){ MP_Get_Last_Size( size ); }

void MP_CART_CREATE  (int *ndims, int *dim){
   MP_Cart_Create( *ndims, dim); }
void mp_cart_create  (int *ndims, int *dim){
   MP_Cart_Create( *ndims, dim); }
void mp_cart_create_ (int *ndims, int *dim){
   MP_Cart_Create( *ndims, dim); }
void mp_cart_create__(int *ndims, int *dim){
   MP_Cart_Create( *ndims, dim); }

void MP_CART_COORDS  ( int *iproc, int *coord){
   MP_Cart_Coords( *iproc, coord); }
void mp_cart_coords  ( int *iproc, int *coord){
   MP_Cart_Coords( *iproc, coord); }
void mp_cart_coords_ ( int *iproc, int *coord){
   MP_Cart_Coords( *iproc, coord); }
void mp_cart_coords__( int *iproc, int *coord){
   MP_Cart_Coords( *iproc, coord); }

void MP_CART_GET  ( int *dim, int *coord) {
   MP_Cart_Get( dim, coord ); }
void mp_cart_get  ( int *dim, int *coord) {
   MP_Cart_Get( dim, coord ); }
void mp_cart_get_ ( int *dim, int *coord) {
   MP_Cart_Get( dim, coord ); }
void mp_cart_get__( int *dim, int *coord) {
   MP_Cart_Get( dim, coord ); }

void MP_CART_SHIFT  ( int *dir, int *dist, int *source, int *dest){
   MP_Cart_Shift( *dir, *dist, source, dest); }
void mp_cart_shift  ( int *dir, int *dist, int *source, int *dest){
   MP_Cart_Shift( *dir, *dist, source, dest); }
void mp_cart_shift_ ( int *dir, int *dist, int *source, int *dest){
   MP_Cart_Shift( *dir, *dist, source, dest); }
void mp_cart_shift__( int *dir, int *dist, int *source, int *dest){
   MP_Cart_Shift( *dir, *dist, source, dest); }

void MP_CART_RANK  ( int *coord, int *rank ) {
   MP_Cart_Rank( coord, rank); }
void mp_cart_rank  ( int *coord, int *rank ) {
   MP_Cart_Rank( coord, rank); }
void mp_cart_rank_ ( int *coord, int *rank ) {
   MP_Cart_Rank( coord, rank); }
void mp_cart_rank__( int *coord, int *rank ) {
   MP_Cart_Rank( coord, rank); }


