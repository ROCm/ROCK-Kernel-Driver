/* $Id: entry.S,v 1.19 1999/12/08 22:05:10 harald Exp $
 *
 * Low level exception handling
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/sys.h>

#include <asm/asm.h>
#include <asm/current.h>
#include <asm/errno.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/stackframe.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/unistd.h>
#include <asm/isadep.h>

		.text
		.align 4
		.set	push
		.set	reorder
EXPORT(ret_from_fork)
		 move	a0, v0			# prev
		jal	schedule_tail
		j	ret_from_sys_call

EXPORT(handle_softirq)
		jal	do_softirq
		b	9f

reschedule:	jal	schedule 

EXPORT(ret_from_sys_call)
EXPORT(ret_from_irq)
		.type	ret_from_irq,@function
		lw	t0, irq_stat		# softirq_active
		lw	t1, irq_stat+4		# softirq_mask.  unused delay slot
		and	t0, t1
		bnez	t0, handle_softirq


9:		lw	t0,PT_STATUS(sp)	# returning to kernel mode?
		lw	t2, TASK_NEED_RESCHED($28)
		andi	t1, t0, KU_USER
		beqz	t1, return		# -> yes
		bnez	t2, reschedule
		lw	v0, TASK_SIGPENDING($28)
		 move	a0, zero
		beqz	v0, return
		 move	a1, sp
		jal	do_signal

EXPORT(return)	.set	noat
		RESTORE_ALL_AND_RET
		.set	at

/*
 * Common spurious interrupt handler.
 */
		.text
		.align  5
LEAF(spurious_interrupt)
		/*
		 * Someone tried to fool us by sending an interrupt but we
		 * couldn't find a cause for it.
		 */
		lui     t1,%hi(spurious_count)
		.set	reorder
		lw      t0,%lo(spurious_count)(t1)
		.set	noreorder
		addiu   t0,1
		j	ret_from_irq
		 sw      t0,%lo(spurious_count)(t1)
		END(spurious_interrupt)

/*
 * Build a default exception handler for the exceptions that don't need
 * special handlers.  If you didn't know yet - I *like* playing games with
 * the C preprocessor ...
 */
#define __BUILD_clear_none(exception)
#define __BUILD_clear_sti(exception)                                    \
		STI
#define __BUILD_clear_cli(exception)                                    \
		CLI
#define __BUILD_clear_fpe(exception)                                    \
		cfc1	a1,fcr31;                                       \
		li	a2,~(0x3f<<12);                                 \
		and	a2,a1;                                          \
		ctc1	a2,fcr31;                                       \
		STI
#define __BUILD_clear_ade(exception)                                    \
		.set	reorder;						\
		MFC0	t0,CP0_BADVADDR;                                \
		.set	noreorder;					\
		REG_S	t0,PT_BVADDR(sp);                               \
		KMODE
#define __BUILD_silent(exception)

#define fmt "Got %s at %08lx.\n"

#define __BUILD_verbose(exception)                                      \
		la	a1,8f;                                          \
		TEXT	(#exception);                                   \
		REG_L	a2,PT_EPC(sp);                                  \
		PRINT(fmt)
#define __BUILD_count(exception)                                        \
		.set	reorder;                                        \
		lw	t0,exception_count_##exception;                 \
		.set	noreorder;                                      \
		addiu	t0, 1;                                          \
		sw	t0,exception_count_##exception;                 \
		.data;                                                  \
EXPORT(exception_count_##exception);                                    \
		.word	0;                                              \
		.previous;
#define BUILD_HANDLER(exception,handler,clear,verbose)                  \
		.align	5;                                              \
		NESTED(handle_##exception, PT_SIZE, sp);                \
		.set	noat;                                           \
		SAVE_ALL;                                               \
		__BUILD_clear_##clear(exception);                       \
		.set	at;                                             \
		__BUILD_##verbose(exception);                           \
		jal	do_##handler;                                   \
		 move	a0,sp;                                          \
		j	ret_from_sys_call;                              \
		 nop;                                                   \
		END(handle_##exception)

		BUILD_HANDLER(adel,ade,ade,silent)		/* #4  */
		BUILD_HANDLER(ades,ade,ade,silent)		/* #5  */
		BUILD_HANDLER(ibe,ibe,cli,verbose)		/* #6  */
		BUILD_HANDLER(dbe,dbe,cli,silent)		/* #7  */
		BUILD_HANDLER(bp,bp,sti,silent)			/* #9  */
		BUILD_HANDLER(ri,ri,sti,silent)			/* #10 */
		BUILD_HANDLER(cpu,cpu,sti,silent)		/* #11 */
		BUILD_HANDLER(ov,ov,sti,silent)			/* #12 */
		BUILD_HANDLER(tr,tr,sti,silent)			/* #13 */
		BUILD_HANDLER(fpe,fpe,fpe,silent)		/* #15 */
		BUILD_HANDLER(watch,watch,sti,verbose)		/* #23 */
		BUILD_HANDLER(reserved,reserved,sti,verbose)	/* others */

		.set	pop

/*
 * Table of syscalls
 */
		.data
		.align	PTRLOG
EXPORT(sys_call_table)
#define SYS(call, narg) PTR call

		/* Reserved space for all SVR4 syscalls. */
		.space	(1000)*PTRSIZE

#ifdef CONFIG_BINFMT_IRIX
		/* 32bit IRIX5 system calls. */
#include "irix5sys.h"
#else
		.space	(1000)*PTRSIZE		/* No IRIX syscalls */
#endif

		/* Reserved space for all the BSD43 and POSIX syscalls. */
		.space	(2000)*PTRSIZE

		/* Linux flavoured syscalls. */
#include "syscalls.h"

/*
 * Number of arguments of each syscall
 */
EXPORT(sys_narg_table)
#undef SYS
#define SYS(call, narg) .byte narg

		/* Reserved space for all SVR4 flavoured syscalls. */
		.space	(1000)

#ifdef CONFIG_BINFMT_IRIX
		/* 32bit IRIX5 system calls. */
#include "irix5sys.h"
#else
		.space	(1000)			/* No IRIX syscalls */
#endif

		/* Reserved space for all the BSD43 and POSIX syscalls. */
		.space	(2000)

		/* Linux flavoured syscalls. */
#include "syscalls.h"
