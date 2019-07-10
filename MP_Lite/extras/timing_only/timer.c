/* Timing, operation counting, and memory tracking
 * Code by Dave Turner in 1996 (based on original work by David Deaven)
 *
 * Timing:  Bracket the section of interest with MP_Enter() and MP_Leave()
 *          functions.  At the end of the code, call MP_Time_Report()
 *          to generate a sorted list of the time spent in each section.
 *
 *          You must have bracket the entire program too or the precentages
 *          will be messed up.
 *
 * Memory:  Use mymalloc() and MP_Free() in place of malloc() and free()
 *          to track memory automatically.  Each time it is used, the
 *          total memory that has been dynamically allocated will be dumped
 *          to stdout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

int myproc = 0, nprocs = 1;
int debug_level = 0;     /* Set to 1 to print on each MP_Enter()/MP_Leave() */

/*#undef ELPRINT*/
#define MPRINTF if(myproc==0)printf

void MP_Enter(char *name);
void MP_Leave(double nflops);
void MP_Time_Report(char *filename);

void MEMORY(char *s, int nbytes);
void *MP_Malloc(size_t nbytes);
void *MP_Realloc(void *ptr, int nbytes);
void MP_Free(void *ptr);
void MEMSTRING(char *s);
void MP_memcpy(void *dest, const void *src, int n);

double MP_Time()
{
   struct timeval f;

   gettimeofday(&f, (struct timezone *)NULL);
   return ( (double)f.tv_sec + ((double)f.tv_usec)*0.000001);
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

void *MP_Malloc(size_t nbytes)
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
   b->size = nbytes;
   b->next = NULL;
   strcpy(b->name,mem_string);
   strcpy(mem_string,"\0");

   memdump(b);
   return b->ptr;
}

void MP_Free(void *ptr)
{
   struct memblock *b, *blast;

   for( b = membase, blast = NULL; b != NULL; b = b->next) {
      if( b->ptr == ptr) break;
      blast = b;
   }
   if( b == NULL ) {
      MPRINTF("-->MP_Free - Trying to free a non-existent block\n");
   } else {
      b->size = - b->size;
      memdump(b);
      if( blast == NULL ) membase = b->next;
      else blast->next = b->next;
      free(ptr);
   }
} 

void *MP_Realloc(void *ptr, int nbytes)
{
   struct memblock *b;
   void *ptr_new;

   for( b = membase; b != NULL; b = b->next) {
      if( b->ptr == ptr) break;
   }
   if( b == NULL || ptr == NULL) {
      ptr_new = MP_Malloc(nbytes);
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


char* Cleanup_String(char *string)
{
   char *p, *new_string;

/* Linux won't allow modification of the constant? string,
 *    so make a copy first */

   new_string = malloc(strlen(string)+1);
   strcpy(new_string, string);   

   if( strlen(new_string) > 39 ) new_string[39] = '\0';

/*   p = &string[strlen(string)-1];*/
/*   while( *p == ' ' && strlen(string) > 0 ) { *p = '\0'; p--; }*/

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

   if( debug_level == 1 ) {
      MPRINTF("-->Enter %s\n",block[id].name);
   }

   dt = t - block[id].t_enter;
   /* Add this time to the current time block and to the parent's child_time */
   block[id].total_time += dt;
   if(stack_ptr > 0) block[stack[stack_ptr-1]].child_time += dt;

   block[id].nflops += nflops;

   if( debug_level == 1 ) {
      MPRINTF("<--leave %s\n",block[id].name);
   }
}

/* f77 doesn't always properly terminate strings, so from f77 I use a
 * pipe '|' to terminate timefile for this function
 */

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

   if( myproc==0 ) {
      if( total_time < 1.0e-6 ) {
          fprintf(fp,"Total time is zero for some reason\n");
          return;
      }

      fprintf(fp,"    time in seconds       +children    calls   name\n");
      for(id=0; id<max_block; id++) {
         dt = block[id].total_time - block[id].child_time;
         frac = 100 * dt / total_time;
/*         if(frac < 0.001) break;*/
         fprintf(fp, " %10.3f (%5.1f%%) %13.3f %8d  %-17.17s", 
            dt, frac, block[id].total_time, 
            block[id].ncalls, block[id].name);
         if(block[id].nflops == 0) fprintf(fp,"\n");
         else if( block[id].nflops < 0 )
            fprintf(fp," %9.2f MB/sec\n",-block[id].nflops * 1.0e-6 / dt);
         else fprintf(fp," %9.2f MFlops\n",block[id].nflops * 1.0e-6 / dt);
      }
      if( total_nflops > 0 )
         fprintf(fp, 
          "\n %9.2f billion operations using %d processors --> %9.2f MFlops\n",
             total_nflops * 1.0e-9, nprocs, total_nflops *1.0e-6 / total_time);
      fclose(fp);
   }
   free( timefile );
}

/* Split off pre-aligned and odd bytes so the bulk of the
 * data is transferred at the highest rate.  Otherwise
 * memcpy() will spike for nbytes%8!=0 and un-aligned on PC Linux.
 */

void MP_memcpy(void *destination, const void *source, int nbytes)
{
  int nb_b4, nb_after;
  char *dest = destination, *src = (char *) source;

  nb_b4 = 8 - ((long int)src % 8);

  if( nb_b4 != 8 && nb_b4 <= nbytes) {    /* Copy up to an 8-byte boundary first 
                                            considering that nbytes can be less than nb_b4 */  

    memcpy( dest, src, nb_b4 );

    src += nb_b4;
    dest += nb_b4;
    nbytes -= nb_b4;

  }

  nb_after = nbytes % 8;
  nbytes -= nb_after;

  if( nbytes > 0 ) {    /* Copy the main data */

    memcpy( dest, src, nbytes );

  }  

  if( nb_after > 0 ) {    /* Copy the last few bytes */

    src += nbytes;
    dest += nbytes;

    memcpy( dest, src, nb_after );

  }  

}

void MP_ENTER_C  (char* string) { MP_Enter(string); }
void mp_enter_c  (char* string) { MP_Enter(string); }
void mp_enter_c_ (char* string) { MP_Enter(string); }
void mp_enter_c__(char* string) { MP_Enter(string); }

void MP_LEAVE  (double* flops) { MP_Leave( *flops); }
void mp_leave  (double* flops) { MP_Leave( *flops); }
void mp_leave_ (double* flops) { MP_Leave( *flops); }
void mp_leave__(double* flops) { MP_Leave( *flops); }

void MP_TIME_REPORT_C  (char* filename) { MP_Time_Report(filename); }
void mp_time_report_c  (char* filename) { MP_Time_Report(filename); }
void mp_time_report_c_ (char* filename) { MP_Time_Report(filename); }
void mp_time_report_c__(char* filename) { MP_Time_Report(filename); }


