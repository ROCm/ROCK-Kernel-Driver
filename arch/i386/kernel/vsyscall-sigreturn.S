/*
 * Common code for the sigreturn entry points on the vsyscall page.
 * So far this code is the same for both int80 and sysenter versions.
 * This file is #include'd by vsyscall-*.S to define them after the
 * vsyscall entry point.  The addresses we get for these entry points
 * by doing ".balign 32" must match in both versions of the page.
 */

#include <asm/unistd.h>


/* XXX
   Should these be named "_sigtramp" or something?
*/

	.text
	.balign 32
	.globl __kernel_sigreturn
	.type __kernel_sigreturn,@function
__kernel_sigreturn:
.LSTART_kernel_sigreturn:
	popl %eax		/* XXX does this mean it needs unwind info? */
	movl $__NR_sigreturn, %eax
	int $0x80
.LEND_sigreturn:
	.size __kernel_sigreturn,.-.LSTART_sigreturn

	.text
	.balign 32
	.globl __kernel_rt_sigreturn
	.type __kernel_rt_sigreturn,@function
__kernel_rt_sigreturn:
.LSTART_kernel_rt_sigreturn:
	movl $__NR_rt_sigreturn, %eax
	int $0x80
.LEND_rt_sigreturn:
	.size __kernel_rt_sigreturn,.-.LSTART_rt_sigreturn
	.previous
