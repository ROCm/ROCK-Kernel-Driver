/* ------------------------------------------------------------------------------
 *
 * Linux/PARISC Project (http://www.thepuffingroup.com/parisc)
 *
 * System call entry code Copyright (c) Matthew Wilcox 1999 <willy@bofh.ai>
 * Licensed under the GNU GPL.
 * thanks to Philipp Rumpf, Mike Shaver and various others
 * sorry about the wall, puffin..
 */

#define __ASSEMBLY__
#include <asm/assembly.h>
#include <asm/offset.h>
#include <asm/unistd.h>
#include <asm/errno.h>

	.text

	.import hpux_call_table
	.import hpux_syscall_exit,code
	.export hpux_gateway_page

	.align 4096
hpux_gateway_page:
	nop
	mfsp	%sr7,%r1			;! we must set sr3 to the space
	mtsp	%r1,%sr3			;! of the user before the gate
#ifdef __LP64__
#warning NEEDS WORK for 64-bit
#endif
	ldw	-64(%r30), %r28			;! 8th argument
	ldw	-60(%r30), %r19			;! 7th argument
	ldw	-56(%r30), %r20			;! 6th argument
	ldw	-52(%r30), %r21			;! 5th argument
	gate	.+8, %r0			;! become privileged
	mtsp	%r0,%sr4			;! get kernel space into sr4
	mtsp	%r0,%sr5			;! get kernel space into sr5
	mtsp	%r0,%sr6			;! get kernel space into sr6
	mtsp	%r0,%sr7			;! get kernel space into sr7
	mfctl	%cr30,%r1			;! get the kernel task ptr
	mtctl	%r0,%cr30			;! zero it (flag)
	STREG	%r30,TASK_PT_GR30(%r1)		;! preserve userspace sp
	STREG	%r2,TASK_PT_GR2(%r1)		;! preserve rp
	STREG	%r27,TASK_PT_GR27(%r1)		;! user dp
	STREG	%r31,TASK_PT_GR31(%r1)		;! preserve syscall return ptr

	loadgp					;! setup kernel dp

	ldo	TASK_SZ_ALGN+64(%r1),%r30	;! set up kernel stack

	stw	%r21, -52(%r30)			;! 5th argument
	stw	%r20, -56(%r30)			;! 6th argument
	stw	%r19, -60(%r30)			;! 7th argument
	stw	%r28, -64(%r30)			;! 8th argument

	ldil	L%hpux_call_table, %r21
	ldo	R%hpux_call_table(%r21), %r21
	comiclr,>>=	__NR_HPUX_syscalls, %r22, %r0
	b,n	syscall_nosys
	ldwx,s	%r22(%r21), %r21
	ldil	L%hpux_syscall_exit,%r2
	be	0(%sr7,%r21)
	ldo	R%hpux_syscall_exit(%r2),%r2

syscall_nosys:
	ldil	L%hpux_syscall_exit,%r1
	be	R%hpux_syscall_exit(%sr7,%r1)
	ldo	-ENOSYS(%r0),%r28

	.align 4096
	.export end_hpux_gateway_page
end_hpux_gateway_page:
