/* MP_Lite message-passing library
 * Copyright (C) 1998-2004 Dave Turner and Xuehua Chen
 * The amd_copy routine is based on the code in pages 73 to 74 of 
 * AMD AThlonTM Processor x86 Optimization Guide. The mmx_copy called 
 * by this function is in mmx_copy.s, which basically converts the 
 * assembly code in the Guide from Intel's syntax to AT&T's syntax.
 * Other mmx_copy routines are based on those in the Linux kernel.
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
 *    Define one of these in the makefile, or MEMCPY will be used
 *
 * MEMCPY            The default is to use a normal memcpy()
 * AMDCOPY           This is recommend for x86 systems.
 *                   This requires the mmx_copy.s file.
 * NTCOPY            non-temporal memcopy for main memory copies
 * WACOPY            write allocate copy
 * CBCOPY            write allocate for short, non-temporal for long
 * KMEMCPY           based on the Linux kernel mmx_copy
 * KNTCOPY           based on the Linux kernel fast_copy_page
 */


#if defined(NTCOPY)
#include <xmmintrin.h>
#endif

#include <stdio.h>
#include <string.h>

#define CACHESIZE 262144
#define LONGMSGSIZE (0.5*CACHESIZE)    /* Long message size,  */    
/*#define LONGMSGSIZE 8192 */   /* Long message size */    
/*#define BLOCKSIZE  (2*131072) */
#define BLOCKSIZE  (0.5*CACHESIZE)     /* Needs to be divisible by 16 */ 
/* #define BLOCKSIZE  8192 */    /* Needs to be divisible by 16 */ 
/*#define PAGESIZE   4096*/
#define NUMPERPAGE 512        /* Number of elements fit in a page */
#define ALIGNMENT  16
#define PENTIUM4 

#if defined(PENTIUM4)
#define CACHELINE     128     /* on Pentimum 4 */
#else
#define CACHELINE     32      /* on Pentimum 3 */
#endif

#define small_memcpy(dst,src,n) \
    { register unsigned long int dummy; \
    asm volatile ( \
      "rep; movsb\n\t" \
      :"=&D"(dst), "=&S"(src), "=&c"(dummy) \
      :"0" (dst), "1" (src),"2" (n) \
      : "memory");  }


void ntcopy(void *dst, const void *src, int size); 
void memcpy_8(void *destination, const void *source, int nbytes);
void memcpy_16(void *destination, const void *source, int nbytes);
void MP_memcpy(void *dst, const void *src, int nbytes);
void mmx_copy(void *dest, void *src, int nblock); 

/* 
 * This function optimize the memory copy if number of bytes
 * to transfer is not equal to 8   
 */
void memcpy_8(void *destination, const void *source, int nbytes)
{
  int nb_b4, nb_after;
  char *dest = (char *)destination, *src = (char *) source;

  nb_b4 = 8 - ((long int)src % 8);

  if( nb_b4 != 8 && nb_b4 <= nbytes) {  /* 
					 * Copy up to an 8-byte boundary first
                                         * considering that nbytes can be less
                                         * than nb_b4  
					 */
    memcpy( dest, src, nb_b4 );

    src += nb_b4;
    dest += nb_b4;
    nbytes -= nb_b4;

  }

  nb_after = nbytes % 8;
  nbytes -= nb_after;

  if( nbytes > 0 ) {      /* Copy the main data */

    memcpy( dest, src, nbytes );
  }

  if( nb_after > 0 ) {    /* Copy the last few bytes */

    src += nbytes;
    dest += nbytes;

    memcpy( dest, src, nb_after );

  }
}

/*
 * This function also optimize the memory copy if number of bytes
 * to transfer is not equal to 8, and its performance is not   
 */

void memcpy_16(void *destination, const void *source, int nbytes)
{
  int nb_b4, nb_after; 
  char *dest = (char *)destination, *src = (char *)source; 
 
  nb_b4 = 16 - ((long) dest % 16); 
  if (nb_b4 != 16 && nb_b4 <= nbytes) 
  { 
    memcpy(dest, src, nb_b4);
    src += nb_b4;
    dest += nb_b4;
    nbytes -= nb_b4; 
  } 

  /*memcpy(dest, src, nbytes);  */
  nb_after = nbytes % 16;
  nbytes -= nb_after;

  if ( nbytes > 0) {
    memcpy(dest, src, nbytes);
  } 

  if( nb_after > 0 ) {    
    src += nbytes;
    dest += nbytes;
    memcpy( dest, src, nb_after );
  }  
}

#if defined(NTCOPY)
/*
 * This implementation is based on the code in 
 * Page 6-45 to 6-47 in IA-32 Intel@ Architectur Optimization.
 * We generlized it to make it work for all sizes 
 */
void ntcopy(void *dst, const void *src, int size)
{
  int ii, jj, kk, N, delta, LEFT, blocksize, size1;

  double *a, *b;
  double temp;

  /* copy the first few bytes to make dest divisible by 8 */
  if (size <= ALIGNMENT)
  {
    memcpy(dst, (void *)src, size);  
    return;
  }

  delta = ((int)dst) & (ALIGNMENT - 1);
  if (delta != 0)
  {
    delta = ALIGNMENT - delta;
    size -= delta;
    memcpy(dst, (void *)src, delta);
  } 
  a = (double *)((int)src + (int)delta);
  b = (double *)((int)dst + (int)delta);
  N  = 2 * (size / 16);   /* number of doubles  */      
  LEFT = size % 16;  
  blocksize = N; 

  if (blocksize > BLOCKSIZE / 8)
    blocksize = BLOCKSIZE / 8;

  for(; N>0; N -= blocksize) 
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

    if ( ((int) a) & (ALIGNMENT - 1) )
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

#endif


#if defined(AMDCOPY) || defined(CBCOPY)
/*
 * This implementation is based on the code in
 * Pages from 73 to 74 of AMD AThlonTM Processor 
 * x86 Optimization Guide. The mmx_copy called by this function is in
 * mmx_copy.s, which basically converts the assembly code in the Guide  
 * from Intel's syntax to AT&T's syntax
 */
void *amd_copy(void *to, const void *from, size_t len)
{
   void *dest, *src;
   int nhead, nblock, nleft;

      /* Copy serveral bytes in the beginning to  
       * make the destination pointer divisible by 16.  */

   if (len < 16)  {

      memcpy_8(to, from, len);

   } else {

      dest = to;
      src  = (void *)from;

      if ( (int) dest % 16 ) {

         nhead = 16 - ((int) dest % 16);
         memcpy_8(dest, src, nhead); 
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

      memcpy_8(dest, src, nleft);\
   }
   return to;
}
#endif 


#if defined(KNTCOPY)
/* 
 * Modified from the function of fast_copy_page in the kernel source 
 * file src/arch/i386/lib/mmx.c for linux version 2.4.20. kntcopy will
 * call this function to copy data of any sizes. 
 */
void mmx_copy2(void *to, void *from, size_t len)
/* static void fast_copy_page(void *to, void *from) */
{
      int i;
      int k;   /* added by cxh */

     /* commented by Xuehua Chen */ 
     /* kernel_fpu_begin(); */
     /* commented by Xuehua Chen */ 

     /* 
      * maybe the prefetch stuff can go before the expensive fnsave...
      * but that is for later. -AV
      */
     /* added by Xuehua Chen */
     for(k=0; k<len/4096; k++)
     {
        /* added by Xuehua Chen */
	__asm__ __volatile__ (
		"   emms \n");
        /* added by Xuehua Chen */
	__asm__ __volatile__ (
		"1: prefetcht1 (%0)\n"
		"   prefetcht1 64(%0)\n"
		"   prefetcht1 128(%0)\n"
		"   prefetcht1 192(%0)\n"
		"   prefetcht1 256(%0)\n"
		"2:  \n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x1AEB, 1b\n" /* jmp on 26 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"       .align 4\n"
		"       .long 1b, 3b\n"
		".previous"
		: : "r" (from) );
	
	for(i=0; i<(4096-320)/64; i++)
	{
		__asm__ __volatile__ (
			"1: prefetcht1 320(%0)\n"
			"2: movq (%0), %%mm0\n"
			"   movq 8(%0), %%mm1\n"
			"   movq 16(%0), %%mm2\n"
			"   movq 24(%0), %%mm3\n"
			"   movq 32(%0), %%mm4\n"
			"   movq 40(%0), %%mm5\n"
			"   movq 48(%0), %%mm6\n"
			"   movq 56(%0), %%mm7\n"
			"   movntq %%mm1, 8(%1)\n"
			"   movntq %%mm0, (%1)\n"
			"   movntq %%mm2, 16(%1)\n"
			"   movntq %%mm3, 24(%1)\n"
			"   movntq %%mm4, 32(%1)\n"
			"   movntq %%mm5, 40(%1)\n"
			"   movntq %%mm6, 48(%1)\n"
			"   movntq %%mm7, 56(%1)\n"
			".section .fixup, \"ax\"\n"
			"3: movw $0x05EB, 1b\n" /* jmp on 5 bytes */
			"   jmp 2b\n"
			".previous\n"
			".section __ex_table,\"a\"\n"
			"       .align 4\n"
			"       .long 1b, 3b\n"
			".previous"
			: : "r" (from), "r" (to) : "memory");
	        from = (void *)((int) from + 64);
	        to   = (void *)((int) to   + 64);
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
	        from = (void *)((int) from + 64);
	        to   = (void *)((int) to   + 64);
        }

	/* since movntq is weakly-ordered, a "sfence" is needed to become
	 * ordered again.
	 */
	__asm__ __volatile__ (

	/* added by Xuehua Chen */
        "  emms \n"
	/* added by Xuehua Chen */

	"  sfence \n" : :
        );
     /* added by Xuehua Chen */
     }
     /* added by Xuehua Chen */

     /* commented by Xuehua Chen */
     /* kernel_fpu_end(); */
     /* commented by Xuehua Chen */
}


void *kntcopy(void *dest, const void *src, size_t len)
{
	int nblock, nleft;
        void *to, *from;   	
	nleft = len % 4096;
	nblock = len - nleft;
      		
	to = dest;
	from = (void *)src;
	if (nblock > 0)
  	  mmx_copy2(to, from, nblock); 

        memcpy((void *) ((int) to   + nblock),
               (void *) ((int) from + nblock), nleft);
	return to; 
} 
#endif


#if defined(KMEMCPY)
/*
 * This implememtation is based on the function of _mmx_memcpy   
 * in the kernel source src/linux/arch/i386/lib/mmx.c for version 2.4.20 
 */
void *kmemcpy(void *to, const void *from, size_t len)
{
        void *p;
        int i;

        __asm__ __volatile__ (
                "    emms\n"
              );

        p = to;
        i = len >> 6; /* len/64 */

        __asm__ __volatile__ (
                "1: prefetcht1 (%0)\n"            /* This set is 28 bytes */
                "   prefetcht1 64(%0)\n"
                "   prefetcht1 128(%0)\n"
                "   prefetcht1 192(%0)\n"
                "   prefetcht1 256(%0)\n"
                "2:  \n"
                ".section .fixup, \"ax\"\n"
                "3: movw $0x1AEB, 1b\n" /* jmp on 26 bytes */
                "   jmp 2b\n"
                ".previous\n"
                ".section __ex_table,\"a\"\n"
                "       .align 4\n"
                "       .long 1b, 3b\n"
                ".previous"
                : : "r" (from) );


        for(i=(4096-320)/64; i<4096/64; i++)
        {
                __asm__ __volatile__ (
                "1:  prefetcht1 320(%0)\n"
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
                "3: movw $0x05EB, 1b\n" /* jmp on 5 bytes */
                "   jmp 2b\n"
                ".previous\n"
                ".section __ex_table,\"a\"\n"
                "       .align 4\n"
                "       .long 1b, 3b\n"
                ".previous"
                : : "r" (from), "r" (to) : "memory");
	        from = (void *)((int) from + 64);
	        to   = (void *)((int) to   + 64);
        }

        for(i=(4096-320)/64; i<4096/64; i++)
        {
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
	        from = (void *)((int) from + 64);
	        to   = (void *)((int) to   + 64);
       }
       /*
        *      Now do the tail of the block
        */
       memcpy(to, (void *) from, len&63);
        __asm__ __volatile__ (
               "    emms\n"
               /*
		 "    pop %edx\n"
                 "    pop %ecx\n"
                 "    pop %ebx\n"
                 "    pop %eax\n" 
		*/
               );
       return p;
}
#endif

void  MP_memcpy(void *dst, const void *src, int nbytes) 
{
#if defined(WACOPY)
  memcpy_8(dst, (void *)src, nbytes);
#elif defined(NTCOPY)
  ntcopy(dst, src, nbytes); 
#elif defined(AMDCOPY)
  amd_copy(dst, src, nbytes);
#elif defined(KNTCOPY)
  kntcopy(dst, src, nbytes);
#elif defined(KMEMCPY)
  kmemcpy(dst, src, nbytes);
#elif defined(CBCOPY)
  if (nbytes > LONGMSGSIZE)
    amd_copy(dst, src, nbytes);
  else
    memcpy_8(dst, src, nbytes);
#else
  memcpy( dst, (void *)src, nbytes);
#endif
}
