       /* setjmp / longjmp for the kernel.
        * Inspired by the glibc version.
	* From Joe Korty
        */

	 .text
        .globl 	kdba_setjmp 
        .p2align 4
kdba_setjmp:
        movq    %rbx,0x0(%rdi)
        movq    %rbp,0x8(%rdi)
        movq    %r12,0x10(%rdi)
        movq    %r13,0x18(%rdi)
        movq    %r14,0x20(%rdi)
        movq    %r15,0x28(%rdi)
        leaq    0x8(%rsp),%rdx
        movq    %rdx,0x30(%rdi)
        movq    (%rsp),%rax
        movq    %rax,0x38(%rdi)
        xorq    %rax,%rax
        ret

        .globl  kdba_longjmp
kdba_longjmp:
        movq    0x0(%rdi),%rbx
        movq    0x8(%rdi),%rbp
        movq    0x10(%rdi),%r12
        movq    0x18(%rdi),%r13
        movq    0x20(%rdi),%r14
        movq    0x28(%rdi),%r15
        test    %esi,%esi
        mov     $1,%eax
        cmove   %eax,%esi
        mov     %esi,%eax
        movq    0x38(%rdi),%rdx
        movq    0x30(%rdi),%rsp
        jmpq    *%rdx

