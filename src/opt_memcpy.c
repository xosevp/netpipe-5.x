/*#define AMDCOPY*/
/*#define NTCOPY*/

/* Requires gcc 3.x to be installed (needs xmmintrin.h) */
#if defined( NTCOPY )
#include <xmmintrin.h>
#endif

#include <stdio.h>
#include <string.h>


   /*construct a function that can do well on most bufferalignment */

#define LONGMSGSIZE (2.5*131072)    /* Long message size */    
#define BLOCKSIZE  131072     /* Needs to be divisible by 16 */ 
#define PAGESIZE   4096
#define NUMPERPAGE 512        /* Number of elements fit in a page */
#define ALIGNMENT  16


#define small_memcpy(dest,src,n) \
    { register unsigned long int dummy; \
    asm volatile ( \
      "rep; movsb\n\t" \
      :"=&D"(dest), "=&S"(src), "=&c"(dummy) \
      :"0" (dest), "1" (src),"2" (n) \
      : "memory");  }

extern int myproc; 
void ntcopy(void *dest, const void *src, int size); 
void opt_memcpy(void *dest, const void *src, int nbytes);


#if defined(NTCOPY)
void ntcopy(void *dest, const void *src, int size)
{
  int ii, jj, kk, N, delta, LEFT, blocksize, size1;

  double *a, *b;
  double temp;

     /* copy the first few bytes to make dest divisible by 8 */

  if (size <= ALIGNMENT) {
    memcpy(dest, (void *)src, size);  
    return;
  }

  delta = ((long)dest) & (ALIGNMENT - 1);
  if (delta != 0) {
    delta = ALIGNMENT - delta;
    size -= delta;
    memcpy(dest, (void *)src, delta);
  } 
  a = (double *)((unsigned int)src + delta);
  b = (double *)((unsigned int)dest + delta);
  N  = 2 * (size / 16);   /* number of doubles  */      
  LEFT = size % 16;  
  blocksize = N; 

  if (blocksize > BLOCKSIZE / 8) blocksize = BLOCKSIZE / 8;

  for(N=2*(size/16); N>0; N -= blocksize)
  {
    if (N < blocksize) blocksize = N; 
    _mm_prefetch((char*)&a[0], _MM_HINT_NTA);
    /* prefetch a block of size blocksize */
    for (jj = 0; jj < blocksize; jj += NUMPERPAGE)  
    {
      /* prefetch one page of memory */  
      if (jj + NUMPERPAGE < blocksize ) 
      { 
        temp = a[jj + NUMPERPAGE]; /* TLB priming */
      }

      for (kk = jj + 16; kk < jj + NUMPERPAGE && kk < blocksize; kk += 16) {
        _mm_prefetch((char*)&a[kk], _MM_HINT_NTA);
      } 
    }

    if ( ((long) a) & (ALIGNMENT - 1) )
    {
      size1 = blocksize - blocksize % 16; 
      for (kk = 0; kk < size1; kk += 16) 
      {
        /* copy one cacheline (128 bytes) */  
        _mm_stream_ps((float*)&b[kk],
          _mm_loadu_ps((float*)&a[kk]));
        _mm_stream_ps((float*)&b[kk+2],
          _mm_loadu_ps((float*)&a[kk+2]));
        _mm_stream_ps((float*)&b[kk+4],
          _mm_loadu_ps((float*)&a[kk+4]));
        _mm_stream_ps((float*)&b[kk+6],
          _mm_loadu_ps((float*)&a[kk+6]));
        _mm_stream_ps((float*)&b[kk+8],
          _mm_loadu_ps((float*)&a[kk+8]));
        _mm_stream_ps((float*)&b[kk+10],
          _mm_loadu_ps((float*)&a[kk+10]));
        _mm_stream_ps((float*)&b[kk+12],
          _mm_loadu_ps((float*)&a[kk+12]));
        _mm_stream_ps((float*)&b[kk+14],
          _mm_loadu_ps((float*)&a[kk+14]));
      }

      for (kk = size1; kk <  blocksize; kk += 2)   
      {
        _mm_stream_ps((float*)&b[kk],
          _mm_loadu_ps((float*)&a[kk]));
      }
    }

    else 
    {
      size1 = blocksize - blocksize % 16;
      for (kk = 0; kk < size1; kk+=16) 
      {
        _mm_stream_ps((float*)&b[kk],
          _mm_load_ps((float*)&a[kk]));
        _mm_stream_ps((float*)&b[kk+2],
          _mm_load_ps((float*)&a[kk+2]));
        _mm_stream_ps((float*)&b[kk+4],
          _mm_load_ps((float*)&a[kk+4]));
        _mm_stream_ps((float*)&b[kk+6],
          _mm_load_ps((float*)&a[kk+6]));
        _mm_stream_ps((float*)&b[kk+8],
          _mm_load_ps((float*)&a[kk+8]));
        _mm_stream_ps((float*)&b[kk+10],
          _mm_load_ps((float*)&a[kk+10]));
        _mm_stream_ps((float*)&b[kk+12],
          _mm_load_ps((float*)&a[kk+12]));
        _mm_stream_ps((float*)&b[kk+14],
          _mm_load_ps((float*)&a[kk+14]));
      }
      for (kk = size1; kk < blocksize; kk += 2)
      {
        _mm_stream_ps((float*)&b[kk],
          _mm_load_ps((float*)&a[kk]));
      }
    } 
    /* finished copying one block  */
    a = a + blocksize;
    b = b + blocksize;
  } 
  _mm_sfence();

  
  if (LEFT > 0)
  {
    memcpy((char*)b, (char *)a, LEFT);  
    
  }
} 

#elif defined(AMDCOPY)

/* This implementation is based on the code in
 * Pages from 73 to 74 of AMD AThlonTM Processor 
 * x86 Optimization Guide. The mmx_copy called by this function is in
 * mmx_copy.s, which basically converts the assembly code in the Guide  
 * from Intel's syntax to AT&T's syntax */

void *amdcopy(void *to, const void *from, size_t len)
{
   void *dest, *src;
   int nhead, nblock, nleft;

      /* Copy serveral bytes in the beginning to  
       * make the destination pointer divisible by 16.  */

   if (len < 16)  {

      memcpy(to, from, len);

   } else {

      dest = to;
      src  = (void *)from;

      if ( (int) dest % 16 ) {

         nhead = 16 - ((int) dest % 16);
         memcpy(dest, src, nhead); 
         dest = (void *)((int) dest + nhead);
         src  = (void *)((int) src + nhead);
         len -= nhead;

      }

         /* Copy the main part, which is devisible by 8192  */

      nleft = len % 128;
      nblock = len - nleft;

      if (nblock > 0) {    /* lock_copy(); */

         mmx_copy(dest, src, nblock);   /* In mmx_copy.s */

             /* unlock_copy(); */

         dest = (void *)((int) dest + nblock);
         src  = (void *)((int) src + nblock);
      }

         /* Copy the remaining part  */

      memcpy(dest, src, nleft);\
   }
   return to;
}
#elif defined(MMXCOPY) | defined(PAGECOPY)

/* mmxcopy and pagecopy are modified from the Linux kernel - Dave Turner */
static char fpu_state[512];


#define save_fpu_state() \
        __asm__ __volatile__ ( \
                "   fnsave %0\n" \
                "   fwait\n" \
		: "=m" (fpu_state) ) \

#define restore_fpu_state() \
        __asm__ __volatile__ ( \
                "   frstor %0\n" \
		: "=m" (fpu_state) ); \


#if defined(MMXCOPY)       /* The performance beats memcpy by just 5-10% */

void *_mmx_memcpy(void *to, const void *from, size_t len)
{
	void *p;
	int i;

	p = to;
	i = len >> 6; /* len/64 */

        if( len > 63 ) {
           
           save_fpu_state();      /* Store the FPU state */

              /* Prefetch 5 64-byte cache lines??? */

	   __asm__ __volatile__ (
		"1: prefetcht0 (%0)\n"
		"   prefetcht0 64(%0)\n"
		"   prefetcht0 128(%0)\n"
		"   prefetcht0 192(%0)\n"
		"   prefetcht0 256(%0)\n"
		"2:  \n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x1AEB, 1b\n"
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from) );
		
              /* Prefetch cache line then copy 64 bytes */

	   for(; i>5; i--) {
		__asm__ __volatile__ (
		"1:  prefetcht0 320(%0)\n"
		"2:  movq (%0), %%mm0\n"
		"  movq 8(%0), %%mm1\n"
		"  movq 16(%0), %%mm2\n"
		"  movq 24(%0), %%mm3\n"
		"  movq %%mm0, (%1)\n"
		"  movq %%mm1, 8(%1)\n"
		"  movq %%mm2, 16(%1)\n"
		"  movq %%mm3, 24(%1)\n"
		"  movq 32(%0), %%mm0\n"
		"  movq 40(%0), %%mm1\n"
		"  movq 48(%0), %%mm2\n"
		"  movq 56(%0), %%mm3\n"
		"  movq %%mm0, 32(%1)\n"
		"  movq %%mm1, 40(%1)\n"
		"  movq %%mm2, 48(%1)\n"
		"  movq %%mm3, 56(%1)\n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x05EB, 1b\n"	/* jmp on 5 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	   }

              /* Move the last 5 blocks (5*64=320 Bytes) without prefetch */

	   for(; i>0; i--) {
		__asm__ __volatile__ (
		"  movq (%0), %%mm0\n"
		"  movq 8(%0), %%mm1\n"
		"  movq 16(%0), %%mm2\n"
		"  movq 24(%0), %%mm3\n"
		"  movq %%mm0, (%1)\n"
		"  movq %%mm1, 8(%1)\n"
		"  movq %%mm2, 16(%1)\n"
		"  movq %%mm3, 24(%1)\n"
		"  movq 32(%0), %%mm0\n"
		"  movq 40(%0), %%mm1\n"
		"  movq 48(%0), %%mm2\n"
		"  movq 56(%0), %%mm3\n"
		"  movq %%mm0, 32(%1)\n"
		"  movq %%mm1, 40(%1)\n"
		"  movq %%mm2, 48(%1)\n"
		"  movq %%mm3, 56(%1)\n"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	   }

           restore_fpu_state(); /* Restore the FPU state */

        }
	   /* Now do the tail of the block */

	memcpy(to, from, len&63);

	return p;
}

#else

int fast_page_copy( void *to, void *from, int nbytes)
{
   char *dest = (char *) to, *src = (char *) from;

   if( nbytes >= 4096 ) {

      save_fpu_state();

      while( nbytes >= 4096 ) {
         fast_copy_page(dest, src); 
         src += 4096;
         dest += 4096;
         nbytes -= 4096;
      }
      restore_fpu_state();
   }

   if( nbytes > 0 ) memcpy( dest, src, nbytes);
}

#ifdef CONFIG_MK7
int fast_copy_page(void *to, void *from)
{
	int i;

/*	kernel_fpu_begin();*/

	/* maybe the prefetcht0 stuff can go before the expensive fnsave...
	 * but that is for later. -AV */

	__asm__ __volatile__ (
		"1: prefetcht0 (%0)\n"
		"   prefetcht0 64(%0)\n"
		"   prefetcht0 128(%0)\n"
		"   prefetcht0 192(%0)\n"
		"   prefetcht0 256(%0)\n"
		"2:  \n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x1AEB, 1b\n"	/* jmp on 26 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from) );

	for(i=0; i<(4096-320)/64; i++)
	{
		__asm__ __volatile__ (
		"1: prefetcht0 320(%0)\n"
		"2: movq (%0), %%mm0\n"
		"   movntq %%mm0, (%1)\n"
		"   movq 8(%0), %%mm1\n"
		"   movntq %%mm1, 8(%1)\n"
		"   movq 16(%0), %%mm2\n"
		"   movntq %%mm2, 16(%1)\n"
		"   movq 24(%0), %%mm3\n"
		"   movntq %%mm3, 24(%1)\n"
		"   movq 32(%0), %%mm4\n"
		"   movntq %%mm4, 32(%1)\n"
		"   movq 40(%0), %%mm5\n"
		"   movntq %%mm5, 40(%1)\n"
		"   movq 48(%0), %%mm6\n"
		"   movntq %%mm6, 48(%1)\n"
		"   movq 56(%0), %%mm7\n"
		"   movntq %%mm7, 56(%1)\n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x05EB, 1b\n"	/* jmp on 5 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	}
	for(i=(4096-320)/64; i<4096/64; i++)
	{
		__asm__ __volatile__ (
		"2: movq (%0), %%mm0\n"
		"   movntq %%mm0, (%1)\n"
		"   movq 8(%0), %%mm1\n"
		"   movntq %%mm1, 8(%1)\n"
		"   movq 16(%0), %%mm2\n"
		"   movntq %%mm2, 16(%1)\n"
		"   movq 24(%0), %%mm3\n"
		"   movntq %%mm3, 24(%1)\n"
		"   movq 32(%0), %%mm4\n"
		"   movntq %%mm4, 32(%1)\n"
		"   movq 40(%0), %%mm5\n"
		"   movntq %%mm5, 40(%1)\n"
		"   movq 48(%0), %%mm6\n"
		"   movntq %%mm6, 48(%1)\n"
		"   movq 56(%0), %%mm7\n"
		"   movntq %%mm7, 56(%1)\n"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	}
	/* since movntq is weakly-ordered, a "sfence" is needed to become
	 * ordered again.  */

	__asm__ __volatile__ (
		"  sfence \n" : :
	);
}

#else

/*	Generic MMX implementation without K7 specific streaming */
 
int fast_copy_page(void *to, void *from)
{
	int i;
	
	__asm__ __volatile__ (
		"1: prefetcht0 (%0)\n"
		"   prefetcht0 64(%0)\n"
		"   prefetcht0 128(%0)\n"
		"   prefetcht0 192(%0)\n"
		"   prefetcht0 256(%0)\n"
		"2:  \n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x1AEB, 1b\n"	/* jmp on 26 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from) );

	for(i=0; i<4096/64; i++)
	{
		__asm__ __volatile__ (
		"1: prefetcht0 320(%0)\n"
		"2: movq (%0), %%mm0\n"
		"   movq 8(%0), %%mm1\n"
		"   movq 16(%0), %%mm2\n"
		"   movq 24(%0), %%mm3\n"
		"   movq %%mm0, (%1)\n"
		"   movq %%mm1, 8(%1)\n"
		"   movq %%mm2, 16(%1)\n"
		"   movq %%mm3, 24(%1)\n"
		"   movq 32(%0), %%mm0\n"
		"   movq 40(%0), %%mm1\n"
		"   movq 48(%0), %%mm2\n"
		"   movq 56(%0), %%mm3\n"
		"   movq %%mm0, 32(%1)\n"
		"   movq %%mm1, 40(%1)\n"
		"   movq %%mm2, 48(%1)\n"
		"   movq %%mm3, 56(%1)\n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x05EB, 1b\n"	/* jmp on 5 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	}
}

#endif    /* k7 or Intel */
#endif    /* PAGECOPY */
#endif    /* optimized copy type */

void  opt_memcpy(void *to, const void *from, int nbytes) 
{
   char *dest = (char *) to, *src = (char *) from;

#if defined( NTCOPY )

   ntcopy(dest, src, nbytes); 

#elif defined( AMDCOPY )

   amdcopy(dest, src, nbytes); 

#elif defined( PAGECOPY )

   fast_page_copy(dest, src, nbytes); 

#elif defined( MMXCOPY )

   _mmx_memcpy(dest, src, nbytes); 

#endif
}

 
