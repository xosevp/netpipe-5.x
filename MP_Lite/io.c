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

/* This file contains some C-like I/O commands
 *   - These are callable from C and F77
 *
 * MP_gfscanf() - Node 0 does the fscanf() and broadcasts to all other nodes.
 * MP_nofscanf() - Node 0 does nprocs fscanf() and sends each to a different
 *                 node in node-order.
 * MP_Get_Token() and MP_Pass_Token() provide node-order I/O
 *     by passing a token that contains the file offset (shared file pointer).
 *     All nodes must participate, beginning by all opening the same file.
 *   - If the file descriptor is NULL, then the token is passed but nothing
 *     is done to any file.  This is useful if all nodes are reading from a
 *     common file, but you don't want them all to yell at once.  
 *
 - SunOS has a different format for va_start() that conflicts with the 
 -   current setup.  ifdefs will need to be inserted to fix it.
 */

#include "globals.h"     /* Globals myproc, nprocs, mylog, logfile */
#include <stdarg.h>

#include "mplite.h"
/*
void MP_Send(), MP_ASend(), MP_Recv(), MP_ARecv(), MP_Wait(), MP_Sync();
void errorx(), errorxs(), errorw();
*/

static FILE *fdlist[100], *echo[100];


/* MP_gfscanf() and MP_nofscanf() functions */


int mp_vfscanf(FILE *fd, char *fmt, char *x)
{
   char lfmt[100], type_char;
   char *p1, *p2;
   int xinc = 0;
   int i, err;

   p1 = fmt;
   while ( *p1 != '\0' ) {

      p2 = lfmt;

again:
      while( *p1 != '%' ) *p2++ = *p1++;             /* copy upto the next % */
      *p2++ = *p1++;                                 /* copy the % sign */
      if( *p1 == '%' || *p1 == '*' ) { *p2++ = *p1++; goto again; }

      while( strchr("0123456789", *p1) != NULL ) *p2++ = *p1++;

      type_char = *p1;
      *p2++ = *p1++;                                 /* copy the type char */
      if( type_char == 'l' ) *p2++ = *p1++;
      if( strchr(p1, '%') == NULL ) {   /* finish it off */
         while( *p1 != '\0' ) *p2++ = *p1++;
      }
      *p2 = '\0';

/*printf("  type char %c  format string |%s| %d\n", type_char, lfmt, xinc);*/

      switch( type_char ) {
         case 'd': case 'i': case 'u' :
            i = xinc%sizeof(int); if( i != 0 ) xinc += sizeof(int) - i;
            err = fscanf(fd, lfmt, (int *) &x[xinc]);
            xinc += sizeof(int);
            break;
         case 'f': case 'e': case 'g' :
            i = xinc%sizeof(float); if( i != 0 ) xinc += sizeof(float) - i;
            err = fscanf(fd, lfmt, (float *) &x[xinc]);
            xinc += sizeof(float);
            break;
         case 'c':
            err = fscanf(fd, lfmt, &x[xinc]);
            xinc += 1;
            break;
         case 'l':
            i = xinc%sizeof(double); if( i != 0 ) xinc += sizeof(double) - i;
            err = fscanf(fd, lfmt, (double *) &x[xinc]);
            xinc += sizeof(double);
            break;
         case 's':
            err = fscanf(fd, lfmt, &x[xinc]);
            break;
         default:
            errorx(0,"io.c mp_vfscanf() - Unknown input format\n");
            break;
      }
   }
   return xinc;
}


void mp_vunpack(char *x, char *fmt, va_list args)
{
   char lfmt[100], type_char;
   char *p1, *p2, *cptr;
   int xinc = 0, *iptr, i;
   double *dptr;
   float *fptr;

   p1 = fmt;
   while ( *p1 != '\0' ) {

      p2 = lfmt;

again:
      while( *p1 != '%' ) *p2++ = *p1++;             /* copy upto the next % */
      *p2++ = *p1++;                                 /* copy the % sign */
      if( *p1 == '%' || *p1 == '*' ) { *p2++ = *p1++; goto again; }

      while( strchr("0123456789", *p1) != NULL ) *p2++ = *p1++;

      type_char = *p1;
      *p2++ = *p1++;                                 /* copy the type char */
      if( type_char == 'l' ) *p2++ = *p1++;
      if( strchr(p1, '%') == NULL ) {   /* finish it off */
         while( *p1 != '\0' ) *p2++ = *p1++;
      }
      *p2 = '\0';

/*printf(" unpack  type char %c  format string |%s| %d\n", type_char, lfmt, xinc);*/

      switch( type_char ) {
         case 'd': case 'i': case 'u' :
            i = xinc%sizeof(int); if( i != 0 ) xinc += sizeof(int) - i;
            iptr = va_arg( args, int * );
            *iptr = *(int *) &x[xinc];
            xinc += sizeof(int);
            break;
         case 'f': case 'e': case 'g' :
            i = xinc%sizeof(float); if( i != 0 ) xinc += sizeof(float) - i;
            fptr = va_arg( args, float * );
            *fptr = *(float *) &x[xinc];
            xinc += sizeof(float);
            break;
         case 'c':
            cptr = va_arg( args, char * );
            *cptr = x[xinc];
            xinc += 1;
            break;
         case 'l':
            i = xinc%sizeof(double); if( i != 0 ) xinc += sizeof(double) - i;
            dptr = va_arg( args, double * );
            *dptr = *(double *) &x[xinc];
            xinc += sizeof(double);
            break;
         case 's':
            cptr = va_arg( args, char * );
            strcpy( cptr, &x[xinc]); xinc += strlen(cptr)+1;
            break;
         default:
            errorx(0,"io.c mp_vunpack() - Unknown input format\n");
            break;
      }
   }
}

void MP_gfscanf(FILE *fd, char *fmt, ...)
{
   char x[1000];
   int nbytes;
   va_list args;

   if( myproc == 0 ) nbytes = mp_vfscanf(fd, fmt, x);

   MP_iBroadcast(&nbytes, 1);
   MP_Broadcast(x, nbytes);

   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}

void mp_gfscanf(int *unit, char *fmt, ...)
{
   char x[1000]; int nbytes; va_list args;
   if( myproc == 0 ) nbytes = mp_vfscanf(fdlist[*unit], fmt, x);
   MP_iBroadcast(&nbytes, 1);
   MP_Broadcast(x, nbytes);
   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}

void mp_gfscanf_(int *unit, char *fmt, ...)
{
   char x[1000]; int nbytes; va_list args;
   if( myproc == 0 ) nbytes = mp_vfscanf(fdlist[*unit], fmt, x);
   MP_iBroadcast(&nbytes, 1);
   MP_Broadcast(x, nbytes);
   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}

void mp_gfscanf__(int *unit, char *fmt, ...)
{
   char x[1000]; int nbytes; va_list args;
   if( myproc == 0 ) nbytes = mp_vfscanf(fdlist[*unit], fmt, x);
   MP_iBroadcast(&nbytes, 1);
   MP_Broadcast(x, nbytes);
   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}

void MP_GFSCANF(int *unit, char *fmt, ...)
{
   char x[1000]; int nbytes; va_list args;
   if( myproc == 0 ) nbytes = mp_vfscanf(fdlist[*unit], fmt, x);
   MP_iBroadcast(&nbytes, 1);
   MP_Broadcast(x, nbytes);
   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}



void MP_nofscanf(FILE *fd, char *fmt, ...)
{
   char x[1000];
   int n, nbytes;
   va_list args;

   if( myproc == 0 ) {
      for( n=0; n<nprocs; n++) {
         nbytes = mp_vfscanf(fd, fmt, x);
         MP_iSend( &nbytes, 1, n, 0);
         MP_Send( x, nbytes, n, 0);
   }  }   
   MP_iRecv(&nbytes, 1, 0, 0);
   MP_Recv(x, nbytes, 0, 0);
   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}

void mp_nofscanf(int *unit, char *fmt, ...)
{
   char x[1000]; int n, nbytes; va_list args;
   if( myproc == 0 ) {
      for( n=0; n<nprocs; n++) {
         nbytes = mp_vfscanf(fdlist[*unit], fmt, x);
         MP_iSend( &nbytes, 1, n, 0);
         MP_Send( x, nbytes, n, 0);
   }  }   
   MP_iRecv(&nbytes, 1, 0, 0); MP_Recv(x, nbytes, 0, 0);
   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}

void mp_nofscanf_(int *unit, char *fmt, ...)
{
   char x[1000]; int n, nbytes; va_list args;
   if( myproc == 0 ) {
      for( n=0; n<nprocs; n++) {
         nbytes = mp_vfscanf(fdlist[*unit], fmt, x);
         MP_iSend( &nbytes, 1, n, 0);
         MP_Send( x, nbytes, n, 0);
   }  }   
   MP_iRecv(&nbytes, 1, 0, 0); MP_Recv(x, nbytes, 0, 0);
   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}

void mp_nofscanf__(int *unit, char *fmt, ...)
{
   char x[1000]; int n, nbytes; va_list args;
   if( myproc == 0 ) {
      for( n=0; n<nprocs; n++) {
         nbytes = mp_vfscanf(fdlist[*unit], fmt, x);
         MP_iSend( &nbytes, 1, n, 0);
         MP_Send( x, nbytes, n, 0);
   }  }   
   MP_iRecv(&nbytes, 1, 0, 0); MP_Recv(x, nbytes, 0, 0);
   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}

void MP_NOFSCANF(int *unit, char *fmt, ...)
{
   char x[1000]; int n, nbytes; va_list args;
   if( myproc == 0 ) {
      for( n=0; n<nprocs; n++) {
         nbytes = mp_vfscanf(fdlist[*unit], fmt, x);
         MP_iSend( &nbytes, 1, n, 0);
         MP_Send( x, nbytes, n, 0);
   }  }   
   MP_iRecv(&nbytes, 1, 0, 0); MP_Recv(x, nbytes, 0, 0);
   va_start(args, fmt);
      mp_vunpack(x, fmt, args);
   va_end(args);
}



static long token = 0;   /* token contains the file offset in bytes */

void MP_Get_Token(FILE *fd)
{
   int prev, err;

   if( myproc > 0 ) {
      prev = (nprocs + myproc-1)%nprocs;

      MP_Recv(&token, sizeof(long), prev, 0);

      if( fd != NULL ) {
         err = fseek(fd, token, SEEK_SET);
         if( err != 0 ) errorw(err, "io.c#MP_Get_Token() - fseek error");
      }
   }
}


void MP_Pass_Token(FILE *fd)
{
   int next, prev, err;

   next = (myproc+1)%nprocs;
   prev = (nprocs + myproc-1)%nprocs;

   if( fd != NULL ) {
      fflush(fd);
      token = ftell(fd);
   }

   MP_Send(&token, sizeof(long), next, 0);

   if( myproc == 0 ) {

      MP_Recv(&token, sizeof(long), prev, 0);

      if( fd != NULL ) {
         err = fseek(fd, token, SEEK_SET);
         if( err != 0 ) errorw(err, "io.c#MP_Pass_Token() - fseek error");
      }
   }
}


/* Config I/O routines  ( var = ###    comments )
 *   - read in lines as necessary
 *   - ignore commented out lines starting with one of !@#%
 *   - discard until after an '=' if present (var = )
 *   - fill the array or variable
 *   - discard the rest of the line as garbage (comments)
 *  Strings are treated as arrays of characters, unterminated
 *   - 'w' means read space separated words, with '\0' termination
 */

void MP_Read_Type(int unit, FILE *fd, void *vec, int n, char op_type, int wlen)
{
   int max_length = 8192, count = 0, nbytes;
   char line_in[8192], *line, *p;

   double* dvec = (double *) vec;
   float* fvec = (float *) vec;
   int* ivec = (int *) vec;
   char* cvec =  vec;
   char* fgs;


   if (myproc == 0) {          /* only processor 0 reads the file */
      while( count < n ) {           /* read lines until 'n' elements found */
         if (0) {
            printf("ERROR - Premature End-of-File reached in MP_Read_Type()\n");
            exit(1);
         } else {
           fgs = fgets(line_in, max_length, fd);
           line = line_in;
           line[strlen(line)-1] = '\0';   /* chop off the newline at the end */
         }

               /* Check for an excessively long line */

         if (strlen(line) >= max_length-1) {
            printf("WARNING - Max line length reached in MP_Read_Type()\n");
         }

               /* Ignore comment lines having leading characters {!@#%/} */

         if ( (strchr("!@#%/", line[0]) == NULL) &&    /* not a comment line */
               strlen(line) > 1 ) {                   /* not a blank line */

/*printf("  |%s|\n", line);*/
               /* skip everything up to an '=' sign if present */

            if ( (p=strchr(line, '=')) != NULL) line = ++p;
            while( *line == ' ' ) line++;
/*printf("  ||%s||\n", line);*/

               /* for strings of characters */

           if( op_type == 'c' ) {
              while( line != NULL && count < n ) cvec[count++] = *line++;
/*              cvec[count] = '\0';*/
              if (echo[unit]) fprintf(echo[unit]," %s",cvec);
              break;
           }

               /* for reading one word */
/*
           if( op_type == 'w' ) {
              sscanf(line, "%s", cvec);
              if (echo[unit]) fprintf(echo[unit]," %s",cvec);
              break;
           }
*/

               /* for reading arrays of numbers */

            p = strtok(line, " ");             /* tokenize the line */
            while( p != NULL && count < n) {
               if( op_type == 'd' ) {       /* array of doubles */
                  dvec[count++] = atof(p);
                  if (echo[unit]) fprintf(echo[unit]," %f",dvec[count-1]);
               } else if( op_type == 'i' ) {       /* array of ints */
                  ivec[count++] = atoi(p);
                  if (echo[unit]) fprintf(echo[unit]," %d",ivec[count-1]);
               } else if( op_type == 'f' ) {       /* array of floats */
                  fvec[count++] = atof(p);
                  if (echo[unit]) fprintf(echo[unit]," %f",fvec[count-1]);
               } else if( op_type == 'w' ) {       /* array of floats */
                  if( strlen(p) > wlen )
                     errorw(wlen, "io.c MP_wRead() - word length too long");
                  strcpy( &cvec[count*wlen], p);
                  if (echo[unit]) fprintf(echo[unit]," %s", &cvec[count*wlen]);
                  count++;
               }
               p = strtok(NULL, " ");          /* get the next token */
            }                               /* done with this line */
         }
      }                               /* found 'n' values */
      if (echo[unit]) fprintf(echo[unit],"\n");
   }
   if( op_type == 'd' ) nbytes = n*sizeof(double);
   else if( op_type == 'i' ) nbytes = n*sizeof(int);
   else if( op_type == 'f' ) nbytes = n*sizeof(float);
   else if( op_type == 'w' ) nbytes = strlen(cvec)+1;
   else nbytes = n;
   if( op_type == 'w' ) MP_iBroadcast(&nbytes, 1);
   MP_Broadcast(vec, nbytes);
/*
   if( op_type == 'i') {
      if( myproc==0 ) printf("Read %d ints - %d ...\n", n, ivec[0]);
   } else if( op_type == 'f' || op_type == 'd' ){
      if( myproc==0 ) printf("Read %d reals - %f ...\n", n, dvec[0]);
   } else if( op_type == 'w' ){
      if( myproc==0 ) printf("Read %d word - %s ...\n", n, cvec);
   } else{
      if( myproc==0 ) printf("Read %d chars - %c ...\n", n, cvec[0]);
   }
*/
}


void MP_dRead(FILE *fd, double *vec, int n)
     { MP_Read_Type(0, fd, vec, n, 'd', 0); }

void MP_iRead(FILE *fd, int *vec, int n)
     { MP_Read_Type(0, fd, vec, n, 'i', 0); }

void MP_fRead(FILE *fd, float *vec, int n)
     { MP_Read_Type(0, fd, vec, n, 'f', 0); }

void MP_cRead(FILE *fd, char *vec, int n)
     { MP_Read_Type(0, fd, vec, n, 'c', 0); }

void MP_wRead(FILE *fd, char *vec, int n, int wlen)
     { MP_Read_Type(0, fd, vec, n, 'w', wlen); }

FILE *MP_fopen(char *filename, char *filetype)
{
   FILE *fd;

   if( myproc == 0 ) {
      fd = fopen( filename, filetype);
      if( fd == NULL ) {
         fprintf(stderr,"io.c MP_fopen(%s,%s) - failed to open a file\n",
                 filename, filetype);
         exit(-1);
      }
   } else fd = NULL;
   return fd;
}

void MP_fclose( FILE *fd)
{
   if( myproc == 0 ) fclose(fd);
}


/* f77 versions mapping unit numbers to file descriptors
 *   - f77 must use MP_fopen() to create a file descriptor
 */

void mp_fopen(int *unit, char *filename, char *filetype)
{
   if( *unit <= 0 || *unit > 99 )
     errorx(*unit, "io.c MP_fopen() - unit out of range 1-99\n");

   if( fdlist[*unit] != NULL ) {
     errorw(*unit, "io.c MP_fopen() - Re-opening a file that was not closed\n");
     fclose(fdlist[*unit]);
   }

   if( strchr("rwa", *filetype) == NULL ) 
      errorx(*unit, "io.c MP_fopen() - the file type must be r, w, or a\n");

   fdlist[*unit] = fopen( filename, filetype);
   if( fdlist[*unit] == NULL ) {
      fprintf(stderr,"io.c MP_fopen(%s,%s) - failed to open a file\n",
              filename, filetype);
      exit(-1);
   }
}
void mp_fopen_ (int *unit, char *filename, char *filetype)
     { mp_fopen(unit, filename, filetype); }
void mp_fopen__(int *unit, char *filename, char *filetype)
     { mp_fopen(unit, filename, filetype); }
void MP_FOPEN  (int *unit, char *filename, char *filetype)
     { mp_fopen(unit, filename, filetype); }

void mp_fclose(int *unit)
{
   fclose(fdlist[*unit]);
   fdlist[*unit] = NULL;
}
void mp_fclose_ (int *unit) { mp_fclose(unit); }
void mp_fclose__(int *unit) { mp_fclose(unit); }
void MP_FCLOSE  (int *unit) { mp_fclose(unit); }

void mp_echo  ( int *unit, int *echo_unit) { echo[*unit] = fdlist[*echo_unit]; }
void mp_echo_ ( int *unit, int *echo_unit) { echo[*unit] = fdlist[*echo_unit]; }
void mp_echo__( int *unit, int *echo_unit) { echo[*unit] = fdlist[*echo_unit]; }
void MP_ECHO  ( int *unit, int *echo_unit) { echo[*unit] = fdlist[*echo_unit]; }


void mp_dread    (int *unit, double *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'd', 0); }
void mp_dread_  (int *unit, double *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'd', 0); }
void mp_dread__(int *unit, double *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'd', 0); }
void MP_DREAD  (int *unit, double *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'd', 0); }

void mp_iread  (int *unit, int *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'i', 0); }
void mp_iread_ (int *unit, int *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'i', 0); }
void mp_iread__(int *unit, int *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'i', 0); }
void MP_IREAD  (int *unit, int *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'i', 0); }

void mp_fread  (int *unit, float *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'f', 0); }
void mp_fread_ (int *unit, float *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'f', 0); }
void mp_fread__(int *unit, float *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'f', 0); }
void MP_FREAD  (int *unit, float *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'f', 0); }

void mp_cread  (int *unit, char *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'c', 0); }
void mp_cread_ (int *unit, char *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'c', 0); }
void mp_cread__(int *unit, char *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'c', 0); }
void MP_CREAD  (int *unit, char *vec, int *n)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'c', 0); }

void mp_wread  (int *unit, char *vec, int *n, int *wlen)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'w', *wlen); }
void mp_wread_ (int *unit, char *vec, int *n, int *wlen)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'w', *wlen); }
void mp_wread__(int *unit, char *vec, int *n, int *wlen)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'w', *wlen); }
void MP_WREAD  (int *unit, char *vec, int *n, int *wlen)
     { MP_Read_Type(*unit, fdlist[*unit], vec, *n, 'w', *wlen); }

/*
void mp_gfscanf(int *unit, char *fmt, ...)

void mp_nofscanf(int *unit, char *fmt, ...)
*/



/* Output routines 
 *  - MP_gfprintf() - only node 0 (the 'master') does the fprintf()
 */

void MP_gfprintf(FILE *fd, char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
     if( myproc == 0 ) vfprintf(fd, fmt, args);
   va_end(args);
}

void mp_gfprintf(int *unit, char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
     if( myproc == 0 ) vfprintf(fdlist[*unit], fmt, args);
   va_end(args);
}

void mp_gfprintf_(int *unit, char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
     if( myproc == 0 ) vfprintf(fdlist[*unit], fmt, args);
   va_end(args);
}

void mp_gfprintf__(int *unit, char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
     if( myproc == 0 ) vfprintf(fdlist[*unit], fmt, args);
   va_end(args);
}

void MP_GFPRINTF(int *unit, char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
     if( myproc == 0 ) vfprintf(fdlist[*unit], fmt, args);
   va_end(args);
}

void nodump( FILE *fd, char *line)
{
   int n, nbytes;

   if( myproc == 0 ) {
      for( n=0; n<nprocs; n++) {
         if( n > 0 ) {
            MP_iRecv( &nbytes, 1, n, 0);
            MP_Recv(line, nbytes, n, 0);
         }
         fprintf(fd, "%s", line);
      }
   } else {
      nbytes = strlen(line);
      MP_iSend( &nbytes, 1, 0, 0);
      MP_Send(line, nbytes, 0, 0);
   }
}

void MP_nofprintf(FILE *fd, char *fmt, ...)
{
   char line[8192];
   va_list args;

   va_start(args, fmt);
     vsprintf(&line[0], fmt, args);
   va_end(args);

   nodump( fd, line);
}

void mp_nofprintf(int *unit, char *fmt, ...)
{
   char line[8192]; va_list args;
   va_start(args, fmt);
     vsprintf(line, fmt, args);
   va_end(args);
   nodump( fdlist[*unit], line);
}

void mp_nofprintf_(int *unit, char *fmt, ...)
{
   char line[8192]; va_list args;
   va_start(args, fmt);
     vsprintf(line, fmt, args);
   va_end(args);
   nodump( fdlist[*unit], line);
}

void mp_nofprintf__(int *unit, char *fmt, ...)
{
   char line[8192]; va_list args;
   va_start(args, fmt);
     vsprintf(line, fmt, args);
   va_end(args);
   nodump( fdlist[*unit], line);
}

void MP_NOFPRINTF(int *unit, char *fmt, ...)
{
   char line[8192]; va_list args;
   va_start(args, fmt);
     vsprintf(line, fmt, args);
   va_end(args);
   nodump( fdlist[*unit], line);
}


/* Printing log and debug information to the .node# log files
 */

void va_fprintf(FILE *fp, char *fmt, va_list ap)
{
   char *p, *sval;
   int ival;
   double dval;
   void* pval;

   for( p=fmt; *p; p++){
      if( *p != '%' ) {
         fprintf(fp, "%c", *p);
      } else {
         switch( *(++p) ) {
            case 'd':
               ival = va_arg(ap, int);
               fprintf(fp, "%d", ival);
               break;
            case 'f':
               dval = va_arg(ap, double);
               fprintf(fp, "%f", dval);
               break;
            case 's':
               for( sval = va_arg(ap, char *); *sval; sval++) {
                  fprintf(fp,"%c", *sval );
               }
               break;
            case 'p':
               pval = va_arg(ap, void*);
               fprintf(fp, "%p", pval);
               break;
            default:
               fprintf(fp, "%c", *p);
               break;
         }
      }
   }

   fflush(fp);
}

void lprintf(char *fmt, ...)       /* Log printf() function */
{
   va_list ap;

#if defined(TCP) || defined(INFINIBAND)
      Block_Signals();
#endif

   va_start(ap, fmt);

   va_fprintf(mylog, fmt, ap);

   va_end(ap);

#if defined(TCP) || defined(INFINIBAND)
      Unblock_Signals();
#endif

}

/* We are blocking signals during debug output because at least some
 * systems (linux) use mutexes to access files, and we can get into a
 * deadlock situation if a mutex is acquired, we receive a SIGIO, causing 
 * a second call to an output function, which again tries to acquire a mutex,
 * and hangs waiting for the mutex to be released.
 */

void dbprintf(char *fmt, ...)       /* Debug printf() to log file */
{
   va_list ap;

   if( debug_level >= 2 ) {
#if defined(TCP) || defined(INFINIBAND)
      Block_Signals();
#endif
      va_start(ap, fmt);
      if( mylog ) va_fprintf(mylog, fmt, ap);    /* Print to the log file */
      else va_fprintf(stderr, fmt, ap);
      va_end(ap);
#if defined(TCP) || defined(INFINIBAND)
      Unblock_Signals();
#endif
   }
}

void ddbprintf(char *fmt, ...)       /* Debug printf() to log file */
{
   va_list ap;

   if( debug_level >= 3 ) {
#if defined(TCP) || defined(INFINIBAND)
      Block_Signals();
#endif
      va_start(ap, fmt);
      if( mylog ) va_fprintf(mylog, fmt, ap);    /* Print to the log file */
      else va_fprintf(stderr, fmt, ap);
      va_end(ap);
#if defined(TCP) || defined(INFINIBAND)
      Unblock_Signals();
#endif
   }
}

