/* 
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 * 
 * System call entry code Copyright (c) Matthew Wilcox 1999 <willy@bofh.ai>
 * Licensed under the GNU GPL.
 * thanks to Philipp Rumpf, Mike Shaver and various others
 * sorry about the wall, puffin..
 */

#include <asm/offsets.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/psw.h>
#include <asm/thread_info.h>

#include <asm/assembly.h>
#include <asm/processor.h>

#ifdef __LP64__
	.level          2.0w
#else
	.level		1.1
#endif
	.text

	.import syscall_exit,code
	.import syscall_exit_rfi,code
	.export linux_gateway_page

	/* Linux gateway page is aliased to virtual page 0 in the kernel
	 * address space. Since it is a gateway page it cannot be
	 * dereferenced, so null pointers will still fault. We start
	 * the actual entry point at 0x100. We put break instructions
	 * at the beginning of the page to trap null indirect function
	 * pointers.
	 */

	.align 4096
linux_gateway_page:

	.rept 56
	break   0,0
	.endr

set_thread_pointer:
	gate	.+8, %r0		/* increase privilege */
	depi	3, 31, 2, %r31		/* Ensure we return into user mode. */
	be	0(%sr7,%r31)		/* return to user space */
	mtctl	%r26, %cr27		/* move arg0 to the control register */

	.rept 4
	break   0,0
	.endr

/* This address must remain fixed, or user binaries go splat. */
	.align 256
linux_gateway_entry:
	gate	.+8, %r0			/* become privileged */
	mtsp	%r0,%sr4			/* get kernel space into sr4 */
	mtsp	%r0,%sr5			/* get kernel space into sr5 */
	mtsp	%r0,%sr6			/* get kernel space into sr6 */
	mfsp    %sr7,%r1                        /* save user sr7 */
	mtsp    %r1,%sr3                        /* and store it in sr3 */

#ifdef __LP64__
	/* for now we can *always* set the W bit on entry to the syscall
	 * since we don't support wide userland processes.  We could
	 * also save the current SM other than in r0 and restore it on
	 * exit from the syscall, and also use that value to know
	 * whether to do narrow or wide syscalls. -PB
	 */
	ssm	PSW_SM_W, %r1
	extrd,u	%r1,PSW_W_BIT,1,%r1
	/* sp must be aligned on 4, so deposit the W bit setting into
	 * the bottom of sp temporarily */
	or,ev	%r1,%r30,%r30
	b,n	1f
	/* The top halves of argument registers must be cleared on syscall
	 * entry from narrow executable.
	 */
	depdi	0, 31, 32, %r26
	depdi	0, 31, 32, %r25
	depdi	0, 31, 32, %r24
	depdi	0, 31, 32, %r23
	depdi	0, 31, 32, %r22
	depdi	0, 31, 32, %r21
1:	
#endif
	mfctl   %cr30,%r1
	xor     %r1,%r30,%r30                   /* ye olde xor trick */
	xor     %r1,%r30,%r1
	xor     %r1,%r30,%r30
	
	ldo     THREAD_SZ_ALGN+FRAME_SIZE(%r30),%r30  /* set up kernel stack */

	/* N.B.: It is critical that we don't set sr7 to 0 until r30
	 *       contains a valid kernel stack pointer. It is also
	 *       critical that we don't start using the kernel stack
	 *       until after sr7 has been set to 0.
	 */

	mtsp	%r0,%sr7			/* get kernel space into sr7 */
	STREGM	%r1,FRAME_SIZE(%r30)		/* save r1 (usp) here for now */
	mfctl	%cr30,%r1			/* get task ptr in %r1 */
	LDREG	TI_TASK(%r1),%r1

	/* Save some registers for sigcontext and potential task
	   switch (see entry.S for the details of which ones are
	   saved/restored).  TASK_PT_PSW is zeroed so we can see whether
	   a process is on a syscall or not.  For an interrupt the real
	   PSW value is stored.  This is needed for gdb and sys_ptrace. */
	STREG	%r0,  TASK_PT_PSW(%r1)
	STREG	%r2,  TASK_PT_GR2(%r1)		/* preserve rp */
	STREG	%r19, TASK_PT_GR19(%r1)

	LDREGM	-FRAME_SIZE(%r30), %r2		/* get users sp back */
#ifdef __LP64__
	extrd,u	%r2,63,1,%r19			/* W hidden in bottom bit */
#if 0
	xor	%r19,%r2,%r2			/* clear bottom bit */
	depd,z	%r19,1,1,%r19
	std	%r19,TASK_PT_PSW(%r1)
#endif
#endif
	STREG	%r2,  TASK_PT_GR30(%r1)		/* ... and save it */
	
	STREG	%r20, TASK_PT_GR20(%r1)
	STREG	%r21, TASK_PT_GR21(%r1)
	STREG	%r22, TASK_PT_GR22(%r1)
	STREG	%r23, TASK_PT_GR23(%r1)		/* 4th argument */
	STREG	%r24, TASK_PT_GR24(%r1)		/* 3rd argument */
	STREG	%r25, TASK_PT_GR25(%r1)		/* 2nd argument */
	STREG	%r26, TASK_PT_GR26(%r1)	 	/* 1st argument */
	STREG	%r27, TASK_PT_GR27(%r1)		/* user dp */
	STREG   %r28, TASK_PT_GR28(%r1)         /* return value 0 */
	STREG   %r28, TASK_PT_ORIG_R28(%r1)     /* return value 0 (saved for signals) */
	STREG	%r29, TASK_PT_GR29(%r1)		/* return value 1 */
	STREG	%r31, TASK_PT_GR31(%r1)		/* preserve syscall return ptr */
	
	ldo	TASK_PT_FR0(%r1), %r27		/* save fpregs from the kernel */
	save_fp	%r27				/* or potential task switch  */

	mfctl	%cr11, %r27			/* i.e. SAR */
	STREG	%r27, TASK_PT_SAR(%r1)

	loadgp

#ifdef __LP64__
	ldo	-16(%r30),%r29			/* Reference param save area */
	copy	%r19,%r2			/* W bit back to r2 */
#else
	/* no need to save these on stack in wide mode because the first 8
	 * args are passed in registers */
	stw     %r22, -52(%r30)                 /* 5th argument */
	stw     %r21, -56(%r30)                 /* 6th argument */
#endif

	mfctl	%cr30, %r1
	LDREG	TI_FLAGS(%r1), %r19
	bb,<,n	%r19,31-TIF_SYSCALL_TRACE,.Ltracesys
	
	/* Note!  We cannot use the syscall table that is mapped
	nearby since the gateway page is mapped execute-only. */

#ifdef __LP64__
	ldil	L%sys_call_table, %r1
	or,=	%r2,%r2,%r2
	addil	L%(sys_call_table64-sys_call_table), %r1
	ldo	R%sys_call_table(%r1), %r19
	or,=	%r2,%r2,%r2
	ldo	R%sys_call_table64(%r1), %r19
#else
	ldil	L%sys_call_table, %r1
	ldo     R%sys_call_table(%r1), %r19
#endif	
	comiclr,>>=	__NR_Linux_syscalls, %r20, %r0
	b,n	.Lsyscall_nosys
	
#ifdef __LP64__
	ldd,s   %r20(%r19), %r19
#else
	ldwx,s  %r20(%r19), %r19
#endif
	/* If this is a sys_rt_sigreturn call, and the signal was received
	 * when not in_syscall, then we want to return via syscall_exit_rfi,
	 * not syscall_exit.  Signal no. in r20, in_syscall in r25 (see
	 * trampoline code in signal.c).
	 */
	ldi	__NR_rt_sigreturn,%r2
	comb,=	%r2,%r20,.Lrt_sigreturn
.Lin_syscall:
	ldil	L%syscall_exit,%r2
	be      0(%sr7,%r19)
	ldo	R%syscall_exit(%r2),%r2
.Lrt_sigreturn:
	comib,<> 0,%r25,.Lin_syscall
	ldil	L%syscall_exit_rfi,%r2
	be      0(%sr7,%r19)
	ldo	R%syscall_exit_rfi(%r2),%r2

	/* Note!  Because we are not running where we were linked, any
	calls to functions external to this file must be indirect.  To
	be safe, we apply the opposite rule to functions within this
	file, with local labels given to them to ensure correctness. */
	
.Lsyscall_nosys:
syscall_nosys:
	ldil	L%syscall_exit,%r1
	be	R%syscall_exit(%sr7,%r1)
	ldo	-ENOSYS(%r0),%r28		   /* set errno */


/* Warning! This trace code is a virtual duplicate of the code above so be
 * sure to maintain both! */
.Ltracesys:
tracesys:
	/* Need to save more registers so the debugger can see where we
	 * are.  This saves only the lower 8 bits of PSW, so that the C
	 * bit is still clear on syscalls, and the D bit is set if this
	 * full register save path has been executed.  We check the D
	 * bit on syscall_return_rfi to determine which registers to
	 * restore.  An interrupt results in a full PSW saved with the
	 * C bit set, a non-straced syscall entry results in C and D clear
	 * in the saved PSW.
	 */
	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	TI_TASK(%r1), %r1
	ssm	0,%r2
	STREG	%r2,TASK_PT_PSW(%r1)		/* Lower 8 bits only!! */
	mfsp	%sr0,%r2
	STREG	%r2,TASK_PT_SR0(%r1)
	mfsp	%sr1,%r2
	STREG	%r2,TASK_PT_SR1(%r1)
	mfsp	%sr2,%r2
	STREG	%r2,TASK_PT_SR2(%r1)
	mfsp	%sr3,%r2
	STREG	%r2,TASK_PT_SR3(%r1)
	STREG	%r2,TASK_PT_SR4(%r1)
	STREG	%r2,TASK_PT_SR5(%r1)
	STREG	%r2,TASK_PT_SR6(%r1)
	STREG	%r2,TASK_PT_SR7(%r1)
	STREG	%r2,TASK_PT_IASQ0(%r1)
	STREG	%r2,TASK_PT_IASQ1(%r1)
	LDREG	TASK_PT_GR31(%r1),%r2
	STREG	%r2,TASK_PT_IAOQ0(%r1)
	ldo	4(%r2),%r2
	STREG	%r2,TASK_PT_IAOQ1(%r1)
	ldo	TASK_REGS(%r1),%r2
	/* reg_save %r2 */
	STREG	%r3,PT_GR3(%r2)
	STREG	%r4,PT_GR4(%r2)
	STREG	%r5,PT_GR5(%r2)
	STREG	%r6,PT_GR6(%r2)
	STREG	%r7,PT_GR7(%r2)
	STREG	%r8,PT_GR8(%r2)
	STREG	%r9,PT_GR9(%r2)
	STREG	%r10,PT_GR10(%r2)
	STREG	%r11,PT_GR11(%r2)
	STREG	%r12,PT_GR12(%r2)
	STREG	%r13,PT_GR13(%r2)
	STREG	%r14,PT_GR14(%r2)
	STREG	%r15,PT_GR15(%r2)
	STREG	%r16,PT_GR16(%r2)
	STREG	%r17,PT_GR17(%r2)
	STREG	%r18,PT_GR18(%r2)
	/* Finished saving things for the debugger */

	ldil	L%syscall_trace,%r1
	ldil	L%tracesys_next,%r2
	be	R%syscall_trace(%sr7,%r1)
	ldo	R%tracesys_next(%r2),%r2
	
tracesys_next:	
	ldil	L%sys_call_table,%r1
	ldo     R%sys_call_table(%r1), %r19

	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	TI_TASK(%r1), %r1
	LDREG   TASK_PT_GR20(%r1), %r20
	LDREG   TASK_PT_GR26(%r1), %r26		/* Restore the users args */
	LDREG   TASK_PT_GR25(%r1), %r25
	LDREG   TASK_PT_GR24(%r1), %r24
	LDREG   TASK_PT_GR23(%r1), %r23
#ifdef __LP64__
	LDREG   TASK_PT_GR22(%r1), %r22
	LDREG   TASK_PT_GR21(%r1), %r21
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif

	comiclr,>>=	__NR_Linux_syscalls, %r20, %r0
	b,n	.Lsyscall_nosys

#ifdef __LP64__
	ldd,s   %r20(%r19), %r19
#else
	ldwx,s  %r20(%r19), %r19
#endif
	/* If this is a sys_rt_sigreturn call, and the signal was received
	 * when not in_syscall, then we want to return via syscall_exit_rfi,
	 * not syscall_exit.  Signal no. in r20, in_syscall in r25 (see
	 * trampoline code in signal.c).
	 */
	ldi	__NR_rt_sigreturn,%r2
	comb,=	%r2,%r20,.Ltrace_rt_sigreturn
.Ltrace_in_syscall:
	ldil	L%tracesys_exit,%r2
	be      0(%sr7,%r19)
	ldo	R%tracesys_exit(%r2),%r2

	/* Do *not* call this function on the gateway page, because it
	makes a direct call to syscall_trace. */
	
tracesys_exit:
	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	TI_TASK(%r1), %r1
#ifdef __LP64__
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif
	bl	syscall_trace, %r2
	STREG   %r28,TASK_PT_GR28(%r1)          /* save return value now */
	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	TI_TASK(%r1), %r1
	LDREG   TASK_PT_GR28(%r1), %r28		/* Restore return val. */

	ldil	L%syscall_exit,%r1
	be,n	R%syscall_exit(%sr7,%r1)

.Ltrace_rt_sigreturn:
	comib,<> 0,%r25,.Ltrace_in_syscall
	ldil	L%tracesys_sigexit,%r2
	be      0(%sr7,%r19)
	ldo	R%tracesys_sigexit(%r2),%r2

tracesys_sigexit:
	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	0(%r1), %r1
#ifdef __LP64__
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif
	bl	syscall_trace, %r2
	nop

	ldil	L%syscall_exit_rfi,%r1
	be,n	R%syscall_exit_rfi(%sr7,%r1)

	.align 4096
	.export sys_call_table
.Lsys_call_table:
sys_call_table:
#include "syscall_table.S"
.end
#ifdef __LP64__
	.align 4096
	.export sys_call_table64
.Lsys_call_table64:
sys_call_table64:
#define SYSCALL_TABLE_64BIT
#include "syscall_table.S"
#endif


	/* Make sure nothing else is placed on this page */

	.align 4096
	.export end_linux_gateway_page
end_linux_gateway_page:

