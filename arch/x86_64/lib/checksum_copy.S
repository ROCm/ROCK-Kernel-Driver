/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Tom May, <ftom@netcom.com>
 *              Pentium Pro/II routines:
 *              Alexander Kjeldaas <astor@guardian.no>
 *              Finn Arne Gangstad <finnag@guardian.no>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 * Changes:     Ingo Molnar, converted csum_partial_copy() to 2.1 exception
 *			     handling.
 *		Andi Kleen,  add zeroing on error
 *                   converted to pure assembler
 *              Andi Kleen   initial raw port to x86-64
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/errno.h>


/* Version for PentiumII/PPro ported to x86-64. Still very raw and
   does not exploit 64bit.  */ 	

#define SRC(y...)			\
	9999: y;			\
	.section __ex_table, "a";	\
	.quad 9999b, 6001f	;	\
	.previous

#define DST(y...)			\
	9999: y;			\
	.section __ex_table, "a";	\
	.quad 9999b, 6002f	;	\
	.previous
	
#define ROUND1(x) \
	SRC(movl x(%rsi), %ebx	)	;	\
	addl %ebx, %eax			;	\
	DST(movl %ebx, x(%rdi)	)	;

#define ROUND(x) \
	SRC(movl x(%rsi), %ebx	)	;	\
	adcl %ebx, %eax			;	\
	DST(movl %ebx, x(%rdi)	)	;

#define ARGBASE 0

/*
	asmlinkage unsigned int csum_partial_copy_generic( const char *src, char *dst, int len, int sum,
	                                                   int *src_err_ptr, int *dst_err_ptr);
	rdi .. src
	rsi .. dst	(copy in r12)
	rdx .. len	(copy in r10)
	rcx .. sum
	r8 ..  src_err_ptr
	r9 ..  dst_err_ptr

	OPTIMIZEME: this routine should take advantage of checksumming 64bits at a time
*/

	.globl csum_partial_copy_generic		
csum_partial_copy_generic:
	pushq %r10
	pushq %r12
	pushq %rbx
	pushq %rbp
	xchgq %rsi, %rdi

	movq %rdx, %r10
	movq %rsi, %r12
	
	movq %rcx, %rax
	movq %rdx, %rcx	# And now it looks like PII case
	movl %ecx, %ebx
	movl %esi, %edx
	shrl $6, %ecx
	andl $0x3c, %ebx
	negq %rbx
	subq %rbx, %rsi
	subq %rbx, %rdi
	lea 3f(%rbx,%rbx), %rbx
	testq %rsi, %rsi
	jmp *%rbx
1:	addq $64,%rsi
	addq $64,%rdi
	ROUND1(-64) ROUND(-60) ROUND(-56) ROUND(-52)	
	ROUND (-48) ROUND(-44) ROUND(-40) ROUND(-36)	
	ROUND (-32) ROUND(-28) ROUND(-24) ROUND(-20)	
	ROUND (-16) ROUND(-12) ROUND(-8)  ROUND(-4)	
3:	adcl $0,%eax
	addl $64,%edx
	dec %ecx
	jge 1b
4:      movq %r10,%rdx
	andl $3, %edx
	jz 7f
	cmpl $2, %edx
	jb 5f
SRC(	movw (%rsi), %dx         )
	leaq 2(%rsi), %rsi
DST(	movw %dx, (%rdi)         )
	leaq 2(%rdi), %rdi
	je 6f
	shll $16,%edx
5:
SRC(	movb (%rsi), %dl         )
DST(	movb %dl, (%rdi)         )
6:	addl %edx, %eax
	adcl $0, %eax
7:
.section .fixup, "ax"
6001:	
	movl $-EFAULT, (%r8)
	# zero the complete destination (computing the rest is too much work)
	movq %r12,%rdi	# dst
	movq %r10,%rcx	# len
	xorl %eax,%eax
	rep; stosb
	jmp 7b
6002:	movl $-EFAULT,(%r9)
	jmp  7b			
.previous
	popq %rbp
	popq %rbx
	popq %r12
	popq %r10
	ret
				
#undef ROUND
#undef ROUND1
