	.file	"mmx_copy.c"
	.version	"01.01"
gcc2_compiled.:
.text
	.align 4
.globl mmx_copy 
	.type	 mmx_copy,@function
mmx_copy:
	pushl	%ebp
	movl	%esp, %ebp

        # save previous contents of registers to be used  
	pushl   %eax
	pushl   %ebx
	pushl   %ecx
	pushl   %edi
	pushl   %esi

        # move parameters of the function to registers   
	movl    0x8(%ebp),%edi   # destnation
        movl  	0xc(%ebp),%esi   # source 
        movl   	0x10(%ebp),%ecx  # size to copy in bytes
    
        # save the size to copy to stack 
        pushl   %ecx

        # handle the main part that has a size divisible by 8192 
        andl    $(-0x2000), %ecx        # %ecx = size - size % (0x2000) 
                                        #      = size - size % 8192 

        jz      less_8k          
        # save the size of the main part to stack    
        pushl   %ecx   

	leal    (%esi,%ecx,1),%esi      # %esi stores the end source position.
	leal    (%edi,%ecx,1),%edi      # %edi stores the end destination position.
	negl    %ecx             

	emms
	
mainloop:
	movl    $0x40,%eax              # loop index %eax = 64      
                                        # each iteration of prefetch loop prefetch 128 bytes 
                                        # 64 iterations will prefetch 64 * 128 = 8K bytes 

prefetchloop:                           # prefetch 8k data from memory
	movl    (%esi,%ecx,1),%ebx
	movl    0x40(%esi,%ecx,1),%ebx
	addl    $0x80,%ecx              # increase the address by 8 * 16 = 128, to prefetch  
                                        # next cache line
	decl    %eax
	jne     prefetchloop
	subl    $0x2000,%ecx            # decrease ecx by 2 * (16 ** 3) = 2 ** 13 = 8K bytes
	movl    $0x80,%eax              # eax = 8k/64 = 0x80

writeloop:                              # this loop will copy 64 bytes
	movq   (%esi,%ecx,1),%mm0
	movq   0x8(%esi,%ecx,1),%mm1
	movq   0x10(%esi,%ecx,1),%mm2
	movq   0x18(%esi,%ecx,1),%mm3
	movq   0x20(%esi,%ecx,1),%mm4
	movq   0x28(%esi,%ecx,1),%mm5
	movq   0x30(%esi,%ecx,1),%mm6
	movq   0x38(%esi,%ecx,1),%mm7
	movntq  %mm0,(%edi,%ecx,1)
	movntq  %mm1,0x8(%edi,%ecx,1)
	movntq  %mm2,0x10(%edi,%ecx,1)
	movntq  %mm3,0x18(%edi,%ecx,1)
	movntq  %mm4,0x20(%edi,%ecx,1)
	movntq  %mm5,0x28(%edi,%ecx,1)
	movntq  %mm6,0x30(%edi,%ecx,1)
	movntq  %mm7,0x38(%edi,%ecx,1)
	addl   $0x40,%ecx
	decl   %eax
	jne    writeloop
	orl    %ecx,%ecx                # whether ecx is zero (copy done)
	jne    mainloop


        # The above operations copied (size - size % 8192) bytes. 
        # The remaining part of (size % 8192) is copied by the following 
        # operation. Use the Rsize to denote (size % 8192)  
   
        # 1) Copy the (Rsize - Rsize % 128) bytes 
        popl  %ecx                      # get the size of the data copied again 
        addl  %esi, %ecx                # move the source pointer forward     
        addl  %edi, %ecx                # move the destination pointer forward 
     
less_8k:        
        popl  %ecx                      # get the original size 
        andl  $(0x2000 - 1), %ecx       # now ecx = Rsize, the remaining part
          jz  done
        pushl %ecx                      # save Rsize to the stack 

        movl  %ecx, %eax                   
        andl  $(-0x80), %eax            # %eax = Rsize - Rsize % 128. Let's call it size1.
        pushl %eax                      # save size1 to the stack 
        movl  %eax, %ecx                
        shrl  $7, %eax                  # loop index for fetch the remaining data = size1 / 128

        leal    (%esi,%ecx,1),%esi      # %esi stores the end src position to copy 
        leal    (%edi,%ecx,1),%edi      # %edi stores the end dst position to be copied to
        negl    %ecx                    # %ecx = - size1. 
                                        # After this (%esi, ecx, 1) will point to the starting src position 
                                        #            (%edi, ecx, 1) will point to the starting dst position 
        pushl   %ecx                    # Save it to stack 
   

prefetchloop1:                          # prefetch size1 bytes data from memory
        movl    (%esi,%ecx,1),%ebx   
        movl    0x40(%esi,%ecx,1),%ebx
        addl    $0x80,%ecx              # increase the address by 8 * 16 = 128, to prefetch
                                        # next 128 bytes of data 
        decl    %eax                     
        jne     prefetchloop1          
        popl    %ecx                    # Now prefetch has done, %ecx restored to -size1 so that  
                                        #            (%esi, ecx, 1) will point to the starting src position
                                        #            (%edi, ecx, 1) will point to the starting dst position
        popl    %eax                    # %eax restored to the size1 
        shrl    $6, %eax                # %eax = size1 % 64, the loop index for writeloop1.
    

writeloop1:                             # One iteration this loop will copy 64 bytes
        movq   (%esi,%ecx,1),%mm0
        movq   0x8(%esi,%ecx,1),%mm1
        movq   0x10(%esi,%ecx,1),%mm2
        movq   0x18(%esi,%ecx,1),%mm3
        movq   0x20(%esi,%ecx,1),%mm4
        movq   0x28(%esi,%ecx,1),%mm5
        movq   0x30(%esi,%ecx,1),%mm6
        movq   0x38(%esi,%ecx,1),%mm7
        movntq  %mm0,(%edi,%ecx,1)
        movntq  %mm1,0x8(%edi,%ecx,1)
        movntq  %mm2,0x10(%edi,%ecx,1)
        movntq  %mm3,0x18(%edi,%ecx,1)
        movntq  %mm4,0x20(%edi,%ecx,1)
        movntq  %mm5,0x28(%edi,%ecx,1)
        movntq  %mm6,0x30(%edi,%ecx,1)
        movntq  %mm7,0x38(%edi,%ecx,1)
        addl   $0x40,%ecx
        decl   %eax
        jne    writeloop1

        # Now (size1 = Rsize - Rsize % 128) bytes has been copied, only Rsize % 128 bytes are left  
        popl   %eax                       # %eax is restored to Rsize    
        andl   $(0x80 - 1), %eax          # %eax = Rsize % 128   

        #### to be done 
        #### copy Rsize % 128 bytes    
          
done:
	sfence
	emms

	popl   %esi
	popl   %edi
	popl   %ecx
	popl   %ebx
	popl   %eax

	leave
	ret

.Lfe1:
	.size	 mmx_copy,.Lfe1-mmx_copy
	.ident	"GCC: (GNU) 2.96 20000731 (Red Hat Linux 7.3 2.96-110)"
