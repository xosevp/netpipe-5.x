/* MP_Lite message-passing library
 * Copyright (C) 1997-2004 Dave Turner
 * (Some code based on original work by David Deaven)
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

/* Timing, operation counting, and memory tracking
 */

#include "globals.h"   /* Globals myproc, nprocs, mylog, hostname, logfile */
#include "mplite.h"

double MP_Time();

/*#undef ELPRINT*/
#define MPRINTF if(myproc==0)printf

/*
  void MP_Enter(char *name);
  void MP_Leave(double nflops);
  void MP_Time_Report(char *filename);
  void MD_Time_Report(char *filename, int nsteps);
  void MP_Reset_Time();
  void MEMORY(char *s, int nbytes);
  void *malloc_block(size_t nbytes);
  void *realloc_block(void *ptr, int nbytes);
  void *free_block(void *ptr);
  void MEMSTRING(char *s);
*/


/********************************************************************************/
/* Added by Adam Oline (copied from MP_memcpy, replaced memcpy() with memmove() */
/********************************************************************************/
void MP_memmove(void* destination, const void* source, int nbytes)
{
  int nb_b4, nb_after;
  char *dest = destination, *src = (char *) source;
  
  nb_b4 = 8 - ((long int)src % 8);

  if( nb_b4 != 8 && nb_b4 <= nbytes) {    /* Copy up to an 8-byte boundary first 
                                            considering that nbytes can be less than nb_b4 */  
    memmove( dest, src, nb_b4 );

    src += nb_b4;
    dest += nb_b4;
    nbytes -= nb_b4;

  }

  nb_after = nbytes % 8;
  nbytes -= nb_after;

  if( nbytes > 0 ) {    /* Copy the main data */

    memmove( dest, src, nbytes );

  }  

  if( nb_after > 0 ) {    /* Copy the last few bytes */

    src += nbytes;
    dest += nbytes;

    memmove( dest, src, nb_after );

  }  

}

/* Report significant changes in allocated memory  */

void MEMORY(char *s, int nbytes)
{
   static double total_mb = 0.0;
   double mb;

   mb = (double) nbytes / (1024.0 * 1024.0);
   total_mb += mb;
   MPRINTF("MEMORY  %-10s  %8.3f MB  --> %8.3f MBytes\n",s,mb,total_mb);
}

struct memblock {
   void *ptr;           /* Pointer to the memory */
   size_t size;           /* Number of bytes */
   char name[20];       /* Name of array */
   void *next;          /* Pointer to the next memblock in the linked list */
};

static struct memblock *membase = NULL;
static char mem_string[20] = "\0";
static double total_mb = 0.0, max_mb = 0.0;

void MEMSTRING(char *s)
{ strcpy(mem_string,s); }

void memdump(struct memblock *b)
{
   double mb;

   mb = ((double) ((int) b->size) ) / (1024.0 * 1024.0);
   total_mb += mb;
   if( total_mb > max_mb) {
      max_mb = total_mb;
      if( fabs(mb) >= 0.1 )
         MPRINTF("-->MEMORY  %-10s  %8.3f MB  --> %8.3f MBytes\n",
                  b->name, mb, total_mb);
   }
}  

void *malloc_block(size_t nbytes)
{
   struct memblock *b;

   if( membase == NULL ) {
      b = membase = malloc(sizeof(struct memblock));
   } else {
      b = membase;
      while( b->next != NULL ) b = b->next;
      b->next = malloc(sizeof(struct memblock));
      b = b->next;
   }
   b->ptr = malloc(nbytes);
   if( b->ptr == NULL ) {
      fprintf(stderr,"MP_Lite: WARNING - malloc_block of %d bytes failed\n",
              (int) nbytes);
   }  
   b->size = nbytes;
   b->next = NULL;
   strcpy(b->name,mem_string);
   strcpy(mem_string,"\0");

   memdump(b);
   return b->ptr;
}

void free_block(void *ptr)
{
   struct memblock *b, *blast;

   for( b = membase, blast = NULL; b != NULL; b = b->next) {
      if( b->ptr == ptr) break;
      blast = b;
   }
   if( b == NULL ) {
      MPRINTF("-->free_block - Trying to free a non-existent block\n");
   } else {
      b->size = - b->size;
      memdump(b);
      if( blast == NULL ) membase = b->next;
      else blast->next = b->next;
      free(ptr);
   }
} 

void *realloc_block(void *ptr, size_t nbytes)
{
   struct memblock *b;
   void *ptr_new;

   for( b = membase; b != NULL; b = b->next) {
      if( b->ptr == ptr) break;
   }
   if( b == NULL || ptr == NULL) {
      ptr_new = malloc_block(nbytes);
   } else {
      b->size = nbytes - b->size;
      memdump(b);
      ptr_new = b->ptr = realloc(ptr, nbytes);
      b->size = nbytes;
   }
   return ptr_new;
}


/* Timing routines for the entire code */

struct timed_block {
   char name[40];          /* name from input parameters */
   double t_enter;         /* when did entry occur */
   double total_time;      /* total time spent in routine and children */
   double child_time;      /* total time spent in lower routines */
   int ncalls;            /* number of entries into this block of code */
   double nflops;          /* total number of floating-point operations */
};

#define NUM_TIMED_BLOCKS 500
static struct timed_block block[NUM_TIMED_BLOCKS];
static int stack[NUM_TIMED_BLOCKS], max_block = 0, stack_ptr = 0, id;

/* Reset the timed blocks after step 0 */
void MP_Reset_Time(void)
{
   double t = MP_Time();
   int i;

   for(i=1;i<NUM_TIMED_BLOCKS;i++) {         /* dont reset block 0 (all) (init block) */
      block[i].t_enter = t;     /* reset t_enter on main-loop */
      block[i].total_time = 0;  /* zero out the total times */
      block[i].child_time = 0;  /* zero out the child times */
      block[i].ncalls = 0;      /* zero out the number of calls */
      block[i].nflops = 0;      /* zero out the nflops */
   }
}



char* Cleanup_String(char *string)
{
   char *p, *new_string;

/* Linux won't allow modification of the constant? string,
 *    so make a copy first */

   new_string = malloc(strlen(string)+1);
   strcpy(new_string, string);   

   if( strlen(new_string) > 39 ) new_string[39] = '\0';

// Get rid of trailing white spaces

   p = &new_string[strlen(new_string)-1];
   while( *p == ' ' && strlen(new_string) > 0 ) { *p = '\0'; p--; }

   /* Treat a pipe '|' as a string termination */

   p = strstr(new_string, "|");
   if( p != NULL ) *p = '\0' ;

   return new_string;
}


/* Enter a timed block
 *   - f77 doesn't always terminate strings properly, so it's possible
 *     that some garbage will be tacked onto the end of the 'name' string.
 *     If you encounter problems, use a pipe '|' at the end of the string
 *     to manually terminate it.  If you use the timer_f.f Fortran wrappers,
 *     this will be done automatically.
 */

void MP_Enter(char *name)
{
   double t = MP_Time();
   int i;

   name = Cleanup_String(name);

   if( debug_level == 1 ) {
      MP_Sync();
      MPRINTF("-->ENTER %s\n",name);
   }

   /* Find the block id or add a new one if necessary */

   for(i=0, id = -1; i<max_block; i++) {

     if(strcmp(name, block[i].name) == 0) id = i;

   }

   if(id == -1) {
      id = max_block++;
      if( id > NUM_TIMED_BLOCKS ) {
         MPRINTF("ERROR - Increase the number of timed blocks in timer.c\n");
         exit(1);
      }
      strcpy(block[id].name, name);
      block[id].total_time = 0;
      block[id].child_time = 0;
      block[id].ncalls = 0;
      block[id].nflops = 0;
   }

      /* Add it to the stack */

   stack[stack_ptr++] = id;
   block[id].t_enter = t;
   block[id].ncalls++;

   free( name );
}

void check_type(double *nflops)
{
  double integral, fractional;

  fractional = modf( *nflops, &integral );
  if ( fractional ) { 
     fprintf(stderr, 
        "Warning: Bad argument type for MP_Leave(), setting nflops to 0.0\n");
     *nflops = 0.0;
  } else if ( *nflops > 1e18 || *nflops < -1e18 ) {
     fprintf(stderr, 
       "Warning: Value out of range for MP_Leave(), setting nflops to 0.0\n");
     *nflops = 0.0;
   }
}

/* Leave a timed block, reverting to the previous context.  */

void MP_Leave(double nflops)
{
   double dt, t = MP_Time();

#ifdef USER_FRIENDLY
   check_type(&nflops);
#endif

   if( stack_ptr <= 0 ) {
      fprintf(stderr,"WARNING - MP_Leave() has no matching MP_Enter()\n");
      return;
   }
   id = stack[--stack_ptr];

   dt = t - block[id].t_enter;
   /* Add this time to the current time block and to the parent's child_time */
   block[id].total_time += dt;
   if(stack_ptr > 0) block[stack[stack_ptr-1]].child_time += dt;

   block[id].nflops += nflops;

   if( debug_level == 1 ) {
      MP_Sync();
      MPRINTF("<--leave %s\n",block[id].name);
   }
}

/* f77 doesn't always properly terminate strings, so from f77 I use a
 * pipe '|' to terminate for this function
 */

double MP_Get_Time(char *name)
{
   int id;
   double time = 0.0;

   name = Cleanup_String(name);

   for(id=0; id<max_block; id++) {
      if( !strcmp(block[id].name,name) ) {
         time = block[id].total_time;
      }
   }

   free( name );
   return time;
}

double MP_Get_Flops(char *name)
{
   int id;
   double flops = 0.0;

   name = Cleanup_String(name);

   for(id=0; id<max_block; id++) {
      if( !strcmp(block[id].name,name) ) {
         flops = block[id].nflops;
      }
   }

   free( name );
   return flops;
}

double MP_Myflops()
{
   int id;
   double myflops = 0.0;

   for(id=0; id<max_block; id++) {
      if( block[id].nflops > 0.0 ) {
         myflops += block[id].nflops;
      }
   }
   return myflops;
}

void MP_Time_Report(char *timefile)
{
   double dt, frac, total_time = 0, total_nflops = 0;
   int jd;
   FILE *fp = NULL;

   timefile = Cleanup_String(timefile);
   total_time = block[0].total_time;

      /* Sort by descending time spent in the block */

   for(id=0; id<max_block-1; id++) {

      dt = block[id].total_time - block[id].child_time;

      for(jd=id+1; jd<max_block; jd++) {

         if( block[jd].total_time - block[jd].child_time > dt ) {

            dt = block[jd].total_time - block[jd].child_time;
            block[max_block] = block[id];
            block[id] = block[jd];
            block[jd] = block[max_block];

   }   }   }

   if( myproc==0 ) {

      if( stack_ptr != 0 ) {

        fprintf(stderr,"MP_Time_Report() called before "
                       "timed blocks completed\n");
        free( timefile );
        return;

      }

      fp = fopen(timefile,"w");

   }

   for(id=0; id<max_block; id++) {
      if( block[id].nflops > 0 ) total_nflops += block[id].nflops;
   }
   MP_dSum(&total_nflops,1);

   if( myproc==0 ) {
      if( total_time < 1.0e-6 ) {
          fprintf(fp,"Total time is zero for some reason\n");
          return;
      }

      fprintf(fp,"    time in seconds       +children    calls   name\n");
      for(id=0; id<max_block; id++) {
         dt = block[id].total_time - block[id].child_time;
         frac = 100 * dt / total_time;
         if(frac < 0.001) break;
         fprintf(fp, " %10.3f (%5.1f%%) %13.3f %8d  %-17.17s", 
            dt, frac, block[id].total_time, 
            block[id].ncalls, block[id].name);
         if(block[id].nflops == 0) fprintf(fp,"\n");
         else if( block[id].nflops < 0 )
            fprintf(fp," %9.2f MB/sec\n",-block[id].nflops * 1.0e-6 / dt);
         else fprintf(fp," %9.3f GFlops\n",block[id].nflops * 1.0e-9 / dt);
      }
      if( total_nflops > 0 )
         fprintf(fp, 
          "\n %9.2f billion operations using %d processors --> %9.3f GFlops\n",
             total_nflops * 1.0e-9, nprocs, total_nflops *1.0e-9 / total_time);
      fclose(fp);
   }
   free( timefile );
}

/* This is a special version of MP_Time_Report() for Molecular Dynamics codes */

void MD_Time_Report(char *timefile, int nsteps)
{
   double dt, frac, total_time = 0, total_nflops = 0;
   int jd;
   FILE *fp = NULL;

   timefile = Cleanup_String(timefile);

      /* Sort by descending time spent in the block (except for init block 0 */

   for(id=1;id<max_block-1;id++) {

      dt = block[id].total_time - block[id].child_time;

      for(jd=id+1;jd<max_block;jd++) {

         if(block[jd].total_time - block[jd].child_time > dt) {

            dt = block[jd].total_time - block[jd].child_time;
            block[max_block] = block[id];
            block[id] = block[jd];
            block[jd] = block[max_block];

   }   }   }

   if( myproc == 0 ) {
      if(stack_ptr != 0) {
         fprintf(stderr,"MD_Time_Report() called before "
                        "timed blocks completed\n");
         free( timefile );
         return;
      }
      fp = fopen(timefile,"w");
   }

   for(id=1; id<max_block; id++) {
      total_time += block[id].total_time - block[id].child_time;
      if( strstr(block[id].name,"Comm") < block[id].name)
         total_nflops += block[id].nflops;
   }
   MP_dSum(&total_nflops,1);

   if( myproc == 0 ) {
      fprintf(fp,"    time in seconds     +children      calls   name\n");
      for(id=1; id<max_block; id++) {
         dt = block[id].total_time - block[id].child_time;
         frac = 100 * dt / total_time;
         if(frac < 0.001) break;
         fprintf(fp, " %10.3f(%4.1f%%) %13.3f %8d  %-15.15s",
            dt, frac, block[id].total_time,
            block[id].ncalls, block[id].name);
         if(block[id].nflops == 0) fprintf(fp,"\n");
         else if( strstr(block[id].name,"Comm") >= block[id].name)
            fprintf(fp," %9.2f MB/sec\n",block[id].nflops * 1.0e-6 / dt);
         else fprintf(fp," %9.3f GFlops\n",block[id].nflops * 1.0e-9 / dt);
      }
      fprintf(fp, " %10.3f main-loop time for %d MD steps\n", total_time, nsteps);
      fprintf(fp, "\n %10.3f seconds initialization time\n",
                  block[0].total_time - total_time);
      fprintf(fp, "\n   %10.3f MD steps/hour\n",nsteps*3600/total_time);
      fprintf(fp, "\n %9.2f billion operations using %d processors "
            "--> %9.3f GFlops\n",
            total_nflops * 1.0e-9, nprocs, total_nflops *1.0e-9 / total_time);
      fclose(fp);
   }
   free( timefile );
}

void MP_Zero_Time( char *name )
{
   int i;

   name = Cleanup_String(name);

   /* Find the block id and zero the total_time */

   for(i=0; i<max_block; i++) {

     if(strcmp(name, block[i].name) == 0) block[i].total_time = 0;

   }

   free( name );
}



