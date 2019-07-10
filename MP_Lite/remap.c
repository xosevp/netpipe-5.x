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

/* This file contains mapping functions to map a 2D mesh onto various
 *   underlying network topologies.
 */

#include "mplite.h"
#include "globals.h"
#include "stdio.h"

int nrows, ncols;
int mycol, myrow;

#undef TRANSPOSE
#undef SUBBLOCK


void MP_Remap_Setup(int n, int m, int *mp)
{
   nrows = n;
   ncols = m;

   if( nprocs == 1 ) return;

   if( nrows*ncols != nprocs ) {
      fprintf(stderr,"ERROR - MP_Remap_Setup(), nrows=%d x ncols=%d != nprocs=%d\n",
              nrows, ncols, nprocs);
      exit(0);
   } 

#if defined(TRANSPOSE)
   if( myproc == 0 )
      printf("Remapping the 2D mesh by transposing rows and columns\n");
#elif defined(SUBBLOCK)
   if( myproc == 0 )
      printf("Remapping the 2D mesh using a subblock mapping\n");
#endif
   *mp = MP_Remap( *mp );
}


#if defined (_AIX)         /* Guessing this is an IBM SP */

   /* Rows will normally be mapped to SMP nodes.
    * TRANSPOSE will map columns to each SMP node.
    * SUBBLOCK will map subblocks to each SMP node to mix on and off node comm.
    */

#if defined(TRANSPOSE)

     /* Map columns onto the SMP nodes instead */

  int MP_Remap(int node)
  {
     int new_node;

     myrow = node / ncols;
     mycol = node % ncols;

        /* Now transpose the myrow and mycol to get the new node number */

     new_node = mycol * ncols + myrow;

     return new_node;
  }

#elif defined(SUBBLOCK)

     /* Map subblocks to SMP nodes instead (assume 16-way SMP nodes) */

  int MP_Remap(int node)
  {
     int new_node, nbrows, nbcols;
     int mybrow, mybcol, mysmp;

     nbrows = nbcols = sqrt( nprocs/16 );

     if( nprocs%16 != 0 || nbrows*nbcols != nprocs) 
        return node;                                  /* Do not remap */

        /* Which SMP node am I on? */

     myrow = node / ncols;
     mycol = node % ncols;

     mybrow = myrow / 4;
     mybcol = mycol / 4;

     mysmp = mybcol + mybrow * nbcols;

        /* Now, which proc within that SMP node */

     new_node = mysmp * 16 + (myrow % 4) * 4 + (mycol % 4);

     return new_node;
  }


#else

  int MP_Remap(int node) { return node; }  /* No change */

#endif

#elif defined (_CRAYT3E)   /* 3D torroid of the Cray T3E */

int MP_Remap(int node)
{
   int new_node = -1;

/*
   if( nprocs == 8 ) {
      switch(node) {
         case 0: new_node = 1; break;
         case 1: new_node = 3; break;
         case 2: new_node = 5; break;
         case 3: new_node = 7; break;
         case 4: new_node = 0; break;
         case 5: new_node = 2; break;
         case 6: new_node = 4; break;
         case 7: new_node = 6; break;
         default: printf("Problem in MP_Remap\n");
      }
   } else if( nprocs == 16 ) {
*/

   if( nprocs == 16 ) {
      switch(node) {
         case  0: new_node =  0; break;
         case  1: new_node =  1; break;
         case  2: new_node =  8; break;
         case  3: new_node =  9; break;
         case  4: new_node =  2; break;
         case  5: new_node =  3; break;
         case  6: new_node = 10; break;
         case  7: new_node = 11; break;
         case  8: new_node =  4; break;
         case  9: new_node =  5; break;
         case 10: new_node = 12; break;
         case 11: new_node = 13; break;
         case 12: new_node =  6; break;
         case 13: new_node =  7; break;
         case 14: new_node = 14; break;
         case 15: new_node = 15; break;
         default: printf("Problem in MP_Remap\n");
      }
   } else {
      new_node = node;
   }
   
   return new_node;
}

#include <intrinsics.h>
#include <unistd.h>
void MP_Print_Remap()
{
  int i;
  long npes, me_virtual, me_physical, me_logical, x, y, z;
  long byte = 0xff;
  npes = sysconf(_SC_CRAY_NPES);
  me_virtual = sysconf(_SC_CRAY_VPE);
  me_physical = sysconf(_SC_CRAY_PPE);
  me_logical = sysconf(_SC_CRAY_LPE);
  x = me_physical & byte;
  y = (me_physical >> 8) & byte;
  z = (me_physical >> 16) & byte;

  barrier();
  if (me_virtual == 0) printf("\n  V    L        P   x   y   z\n");
  for (i = 0; i < npes; i++) {
    barrier();
    if (me_virtual == i) {
      printf("%3d %4d %8x  %2d  %2d  %2d\n",
             me_virtual, me_logical, me_physical, x, y, z);
    }
  }

  if (me_virtual == 0) printf("\nReMap  L        P   x   y   z\n");
  for (i = 0; i < npes; i++) {
    barrier();
    if (i == MP_Remap(me_virtual)) {
      printf("%3d %4d %8x  %2d  %2d  %2d\n",
             me_virtual, me_logical, me_physical, x, y, z);
    }
  }
}

#elif defined (__ncube__)   /* Hypercube of the nCube 2 */
#else    /* Already a 2D mesh (Paragon), or topology doesn't matter */

int MP_Remap(int node)
{ return node; }

#endif


/* Fortran wrappers for the remap functions */

void MP_REMAP_SETUP  (int *nrows, int *ncols, int *myproc)
   { MP_Remap_Setup( *nrows, *ncols, myproc ); }
void mp_remap_setup  (int *nrows, int *ncols, int *myproc)
   { MP_Remap_Setup( *nrows, *ncols, myproc ); }
void mp_remap_setup_ (int *nrows, int *ncols, int *myproc)
   { MP_Remap_Setup( *nrows, *ncols, myproc ); }
void mp_remap_setup__(int *nrows, int *ncols, int *myproc)
   { MP_Remap_Setup( *nrows, *ncols, myproc ); }

int MP_REMAP  ( int *node ) { return MP_Remap( *node ); }
int mp_remap  ( int *node ) { return MP_Remap( *node ); }
int mp_remap_ ( int *node ) { return MP_Remap( *node ); }
int mp_remap__( int *node ) { return MP_Remap( *node ); }
