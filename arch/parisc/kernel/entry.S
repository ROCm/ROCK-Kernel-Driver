/*------------------------------------------------------------------------------
 * Native PARISC/Linux Project (http://www.puffingroup.com/parisc)
 *
 * kernel entry points (interruptions, system call wrappers)
 *  Copyright (C) 1999,2000 Philipp Rumpf 
 *  Copyright (C) 1999 SuSE GmbH Nuernberg 
 *  Copyright (C) 2000 Hewlett-Packard (John Marvin)
 *  Copyright (C) 1999 Hewlett-Packard (Frank Rowand)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <asm/offset.h>

/* the following is the setup i think we should follow:
 * whenever the CPU is interruptible, the following has to be true:
 *  CR30 is the kernel sp or 0 if we currently use the kernel stack
 *  CR31 is the kernel gp */ 

/* we have the following possibilities to act on an interruption:
 *  - handle in assembly and use shadowed registers only
 *  - save registers to kernel stack and handle in assembly or C */

	.text

#ifdef __LP64__
	.level 2.0w
#endif

#define __ASSEMBLY__
#include <asm/assembly.h>	/* for LDREG/STREG defines */
#include <asm/pgtable.h>
#include <asm/psw.h>
#include <asm/signal.h>

#ifdef __LP64__
#define FRAME_SIZE	64
#else
#define FRAME_SIZE	64
#endif

	/* Switch to virtual mapping, trashing only %r1 */
	.macro	virt_map rfi_type
	mtsm	%r0
	tovirt	%r29
	tovirt	%r30
	mfsp	%sr7, %r1
	mtsp	%r1, %sr3
	mtsp	%r0, %sr4
	mtsp	%r0, %sr5
	mtsp	%r0, %sr6
	mtsp	%r0, %sr7
	ldil	L%KERNEL_PSW, %r1
	ldo	R%KERNEL_PSW(%r1), %r1
	LDIL_FIXUP(%r1)
	mtctl	%r1, %cr22
	mtctl	%r0, %cr17
	mtctl	%r0, %cr17
	ldil	L%.+28, %r1
	ldo	R%.+24(%r1), %r1
	LDIL_FIXUP(%r1)
	mtctl	%r1, %cr18
	ldo	4(%r1), %r1
	mtctl	%r1, %cr18
	\rfi_type
	nop
	.endm

	.macro	get_stack
	mfctl	%cr30, %r1 
	comib,=,n 0, %r1, 0f   /* forward so predicted not taken */

	/* we save the registers in the task struct */
	ldo	TASK_REGS(%r1), %r29
	tophys	%r29
	STREG	%r30, PT_GR30(%r29)
	STREG	%r1,  PT_CR30(%r29)
	ldo	TASK_SZ_ALGN(%r1), %r30
	b	1f		    /* unconditional so predicted taken */	
	mtctl	%r0,%cr30
0:
	/* we put a struct pt_regs on the stack and save the registers there */
	copy	%r30,%r29
	ldo	PT_SZ_ALGN(%r30),%r30
	tophys	%r29
	STREG	%r30,PT_GR30(%r29)
	STREG	%r0,PT_CR30(%r29)
1:
	.endm

	.macro	rest_stack regs
	LDREG	PT_CR30(\regs), %r1
	comib,=,n 0, %r1, 2f/* forward so predicted not taken */

	/* we restore the registers out of the task struct */
	mtctl	%r1, %cr30
	LDREG	PT_GR1(\regs), %r1
	LDREG	PT_GR30(\regs),%r30
	b	3f
	LDREG	PT_GR29(\regs),%r29
2:
	/* we take a struct pt_regs off the stack */
	LDREG	PT_GR1(\regs),  %r1
	LDREG	PT_GR29(\regs), %r29
	ldo	-PT_SZ_ALGN(%r30), %r30
3:
	.endm

#ifdef OLD
	/* fixme interruption handler */
	.macro	def code
	/* WARNING!!! THIS IS DEBUG CODE ONLY!!! */
	b	unimplemented_64bitirq
	ldi	\code, %r1
	.align	32
	.endm

	/* Use def to enable break - KWDB wants em
	 * (calls traps.c:handle_interruption) */
	.macro	pass_break code

#else
	/* default interruption handler
	 * (calls traps.c:handle_interruption) */
	.macro	def code
#endif
	mtctl	%r29, %cr31
	mtctl	%r1,  %cr28
	ldi	\code, %r1
	b	intr_save
	mtctl   %r1, %cr29
	.align	32
	.endm

	/* Interrupt interruption handler
	 * (calls irq.c:do_irq_mask) */
	.macro	extint code
	mtctl	%r29, %cr31
	mtctl	%r1,  %cr28
	mfctl	%cr23, %r1
	mtctl	%r1, %cr23
	b	intr_extint
	mtctl	%r1, %cr29
	.align	32
	.endm	

	.import	os_hpmc, code

	/* HPMC handler */
	.macro	hpmc code
	nop			/* must be a NOP, will be patched later */
	ldil	L%PA(os_hpmc), %r3
	ldo	R%PA(os_hpmc)(%r3), %r3
	bv,n	0(%r3)
	nop
	.word	0		/* checksum (will be patched) */
	.word	PA(os_hpmc)	/* address of handler */
	.word	0		/* length of handler */
	.endm

	/*
	 * Performance Note: Instructions will be moved up into
	 * this part of the code later on, once we are sure
	 * that the tlb miss handlers are close to final form.
	 */

	/* Register definitions for tlb miss handler macros */

	va  = r8	/* virtual address for which the trap occured */
	spc = r24	/* space for which the trap occured */

#ifndef __LP64__

	/*
	 * itlb miss interruption handler (parisc 1.1 - 32 bit)
	 */

	.macro	itlb_11 code

	mfctl	%pcsq, spc
	b	itlb_miss_11
	mfctl	%pcoq, va

	.align		32
	.endm
#endif
	
	/*
	 * itlb miss interruption handler (parisc 2.0)
	 */

	.macro	itlb_20 code

	mfctl	%pcsq, spc
#ifdef __LP64__
	b       itlb_miss_20w
#else
	b	itlb_miss_20
#endif
	mfctl	%pcoq, va

	.align		32
	.endm
	
#ifndef __LP64__
	/*
	 * naitlb miss interruption handler (parisc 1.1 - 32 bit)
	 *
	 * Note: naitlb misses will be treated
	 * as an ordinary itlb miss for now.
	 * However, note that naitlb misses
	 * have the faulting address in the
	 * IOR/ISR.
	 */

	.macro	naitlb_11 code

	mfctl	%isr,spc
	b	itlb_miss_11
	mfctl 	%ior,va
	/* FIXME: If user causes a naitlb miss, the priv level may not be in
	 * lower bits of va, where the itlb miss handler is expecting them
	 */

	.align		32
	.endm
#endif
	
	/*
	 * naitlb miss interruption handler (parisc 2.0)
	 *
	 * Note: naitlb misses will be treated
	 * as an ordinary itlb miss for now.
	 * However, note that naitlb misses
	 * have the faulting address in the
	 * IOR/ISR.
	 */

	.macro	naitlb_20 code

	mfctl	%isr,spc
#ifdef __LP64__
	b       itlb_miss_20w
#else
	b	itlb_miss_20
#endif
	mfctl 	%ior,va
	/* FIXME: If user causes a naitlb miss, the priv level may not be in
	 * lower bits of va, where the itlb miss handler is expecting them
	 */

	.align		32
	.endm
	
#ifndef __LP64__
	/*
	 * dtlb miss interruption handler (parisc 1.1 - 32 bit)
	 */

	.macro	dtlb_11 code

	mfctl	%isr, spc
	b	dtlb_miss_11
	mfctl	%ior, va

	.align		32
	.endm
#endif

	/*
	 * dtlb miss interruption handler (parisc 2.0)
	 */

	.macro	dtlb_20 code

	mfctl	%isr, spc
#ifdef __LP64__
	b       dtlb_miss_20w
#else
	b	dtlb_miss_20
#endif
	mfctl	%ior, va

	.align		32
	.endm
	
#ifndef __LP64__
	/* nadtlb miss interruption handler (parisc 1.1 - 32 bit)
	 *
	 * Note: nadtlb misses will be treated
	 * as an ordinary dtlb miss for now.
	 *
	 */

	.macro	nadtlb_11 code

	mfctl	%isr,spc
	b	dtlb_miss_11
	mfctl	%ior,va

	.align		32
	.endm
#endif
	
	/* nadtlb miss interruption handler (parisc 2.0)
	 *
	 * Note: nadtlb misses will be treated
	 * as an ordinary dtlb miss for now.
	 *
	 */

	.macro	nadtlb_20 code

	mfctl	%isr,spc
#ifdef __LP64__
	b       dtlb_miss_20w
#else
	b	dtlb_miss_20
#endif
	mfctl	%ior,va

	.align		32
	.endm
	
#ifndef __LP64__
	/*
	 * dirty bit trap interruption handler (parisc 1.1 - 32 bit)
	 */

	.macro	dbit_11 code

	mfctl	%isr,spc
	b	dbit_trap_11
	mfctl	%ior,va

	.align		32
	.endm
#endif

	/*
	 * dirty bit trap interruption handler (parisc 2.0)
	 */

	.macro	dbit_20 code

	mfctl	%isr,spc
#ifdef __LP64__
	b       dbit_trap_20w
#else
	b	dbit_trap_20
#endif
	mfctl	%ior,va

	.align		32
	.endm

	/*
	 * Align fault_vector_20 on 4K boundary so that both
	 * fault_vector_11 and fault_vector_20 are on the
	 * same page. This is only necessary as long as we
	 * write protect the kernel text, which we may stop
	 * doing once we use large parge translations to cover
	 * the static part of the kernel address space.
	 */


	.export fault_vector_20

	.align 4096

fault_vector_20:
	/* First vector is invalid (0) */
	.ascii	"cows can fly"
	.byte 0
	.align 32

	hpmc		 1
	def		 2
	def		 3
	extint		 4
	def		 5
	itlb_20		 6
	def		 7
	def		 8
	def              9
	def		10
	def		11
	def		12
	def		13
	def		14
	dtlb_20		15
	naitlb_20	16
	nadtlb_20	17
	def		18
	def		19
	dbit_20		20
	def		21
	def		22
	def		23
	def		24
	def		25
	def		26
	def		27
	def		28
	def		29
	def		30
	def		31

#ifndef __LP64__

	.export fault_vector_11
	
	.align 2048

fault_vector_11:
	/* First vector is invalid (0) */
	.ascii	"cows can fly"
	.byte 0
	.align 32

	hpmc		 1
	def		 2
	def		 3
	extint		 4
	def		 5
	itlb_11		 6
	def		 7
	def		 8
	def              9
	def		10
	def		11
	def		12
	def		13
	def		14
	dtlb_11		15
	naitlb_11	16
	nadtlb_11	17
	def		18
	def		19
	dbit_11		20
	def		21
	def		22
	def		23
	def		24
	def		25
	def		26
	def		27
	def		28
	def		29
	def		30
	def		31

#endif

	.import		handle_interruption,code
	.import		handle_real_interruption,code
	.import		do_irq_mask,code
	.import		parisc_stopkernel,code
	.import		cpu_irq_region,data

	/*
	 * r26 = function to be called
	 * r25 = argument to pass in
	 * r24 = flags for do_fork()
	 *
	 * Kernel threads don't ever return, so they don't need
	 * a true register context. We just save away the arguments
	 * for copy_thread/ret_ to properly set up the child.
	 */

#define CLONE_VM 0x100	/* Must agree with <linux/sched.h> */

	.export __kernel_thread, code
	.import do_fork
__kernel_thread:
	STREG	%r2, -RP_OFFSET(%r30)

	copy	%r30, %r1
	ldo	PT_SZ_ALGN(%r30),%r30
#ifdef __LP64__
	/* Yo, function pointers in wide mode are little structs... -PB */
	/* XXX FIXME do we need to honor the fptr's %dp value too? */
	ldd	16(%r26), %r26
#endif
	STREG	%r26, PT_GR26(%r1)  /* Store function & argument for child */
	STREG	%r25, PT_GR25(%r1)
	ldo	CLONE_VM(%r0), %r26   /* Force CLONE_VM since only init_mm */
	or	%r26, %r24, %r26      /* will have kernel mappings.	 */
	copy	%r0, %r25
	bl	do_fork, %r2
	copy	%r1, %r24

	/* Parent Returns here */

	ldo	-PT_SZ_ALGN(%r30), %r30
	LDREG	-RP_OFFSET(%r30), %r2
	bv	%r0(%r2)
	nop

	/*
	 * Child Returns here
	 *
	 * copy_thread moved args from temp save area set up above
	 * into task save area.
	 */

	.export	ret_from_kernel_thread
ret_from_kernel_thread:

	LDREG	TASK_PT_GR26-TASK_SZ_ALGN(%r30), %r1
	LDREG	TASK_PT_GR25-TASK_SZ_ALGN(%r30), %r26
	ble	0(%sr7, %r1)
	copy	%r31, %r2

	b	sys_exit
	ldi	0, %r26

	.import	sys_execve, code
	.export	__execve, code
__execve:
	copy	%r2, %r15
	copy	%r23, %r17
	copy	%r30, %r16
	ldo	PT_SZ_ALGN(%r30), %r30
	STREG	%r26, PT_GR26(%r16)
	STREG	%r25, PT_GR25(%r16)
	STREG	%r24, PT_GR24(%r16)
	bl	sys_execve, %r2
	copy	%r16, %r26

	comib,<>,n 0,%r28,__execve_failed

	b	intr_return
	STREG	%r17, PT_CR30(%r16)

__execve_failed:
	/* yes, this will trap and die. */
	copy	%r15, %r2
	bv	%r0(%r2)
	nop

	.align 4

	/*
	 * struct task_struct *_switch_to(struct task_struct *prev,
	 *	struct task_struct *next)
	 *
	 * switch kernel stacks and return prev */
	.export	_switch_to, code
_switch_to:
	STREG	 %r2, -RP_OFFSET(%r30)

	callee_save

	ldil	L%_switch_to_ret, %r2
	ldo	R%_switch_to_ret(%r2), %r2
	LDIL_FIXUP(%r2)

	STREG	%r2, TASK_PT_KPC(%r26)
	LDREG	TASK_PT_KPC(%r25), %r2

	STREG	%r30, TASK_PT_KSP(%r26)
	LDREG	TASK_PT_KSP(%r25), %r30

	bv	%r0(%r2)
	nop

_switch_to_ret:
	mtctl	%r0, %cr0		/* Needed for single stepping */
	callee_rest

	LDREG	-RP_OFFSET(%r30), %r2
	bv	%r0(%r2)
	copy	%r26, %r28

	/*
	 * Common rfi return path for interruptions, kernel execve, and some
	 * syscalls.  The sys_rt_sigreturn syscall will return via this path
	 * if the signal was received when the process was running; if the
	 * process was blocked on a syscall then the normal syscall_exit
	 * path is used.  All syscalls for traced proceses exit via
	 * intr_restore.
	 * Note that the following code uses a "relied upon translation". See
	 * the parisc ACD for details. The ssm is necessary due to a PCXT bug.
	 */

	.align 4096

	.export	syscall_exit_rfi
syscall_exit_rfi:
	copy    %r30,%r16
	/* FIXME! depi below has hardcoded dependency on kernel stack size */
	depi    0,31,14,%r16 /* get task pointer */
	ldo	TASK_REGS(%r16),%r16
	/* Force iaoq to userspace, as the user has had access to our current
	 * context via sigcontext.
	 * XXX do we need any other protection here?
	 */
	LDREG	PT_IAOQ0(%r16),%r19
	depi	3,31,2,%r19
	STREG	%r19,PT_IAOQ0(%r16)
	LDREG	PT_IAOQ1(%r16),%r19
	depi	3,31,2,%r19
	STREG	%r19,PT_IAOQ1(%r16)
	
intr_return:

	/* Check for software interrupts */

	.import irq_stat,data

	ldil    L%irq_stat,%r19
	ldo     R%irq_stat(%r19),%r19
	LDIL_FIXUP(%r19)

#ifdef CONFIG_SMP
	copy    %r30,%r1
	/* FIXME! depi below has hardcoded dependency on kernel stack size */
	depi    0,31,14,%r1 /* get task pointer */
	ldw     TASK_PROCESSOR(%r1),%r20 /* get cpu # - int */
#if (IRQSTAT_SZ == 32)
	dep     %r20,26,27,%r20 /* shift left 5 bits */
#else
#error IRQSTAT_SZ changed, fix dep
#endif /* IRQSTAT_SZ */
	add     %r19,%r20,%r19
#endif /* CONFIG_SMP */

	ldw     IRQSTAT_SI_ACTIVE(%r19),%r20	/* hardirq.h: unsigned int */
	ldw     IRQSTAT_SI_MASK(%r19),%r19	/* hardirq.h: unsigned int */
	and     %r19,%r20,%r20
	comib,<>,n 0,%r20,intr_do_softirq /* forward */

intr_check_resched:

	/* check for reschedule */
	copy    %r30,%r1
	/* FIXME! depi below has hardcoded dependency on kernel stack size */
	depi    0,31,14,%r1 /* get task pointer */
	LDREG     TASK_NEED_RESCHED(%r1),%r19	/* sched.h: long need_resched */
	comib,<>,n 0,%r19,intr_do_resched /* forward */

intr_check_sig:
	/* As above */
	copy    %r30,%r1
	depi    0,31,14,%r1 /* get task pointer */
	ldw	TASK_SIGPENDING(%r1),%r19	/* sched.h: int sigpending */
	comib,<>,n 0,%r19,intr_do_signal /* forward */

intr_restore:
	copy	    	%r16, %r29
	ldo		PT_FR31(%r29), %r29
	rest_fp		%r29
	copy    	%r16, %r29
	rest_general 	%r29
	ssm		0,%r0
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	tophys		%r29
	mtsm		%r0
	rest_specials	%r29
	rest_stack	%r29
	rfi
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	.import do_softirq,code
intr_do_softirq:
	bl      do_softirq,%r2
	nop
	b       intr_check_resched
	nop

	.import schedule,code
intr_do_resched:
	/* Only do reschedule if we are returning to user space */
	LDREG     PT_SR7(%r16), %r20
	comib,= 0,%r20,intr_restore /* backward */
	nop

	bl      schedule,%r2
	ssm     PSW_SM_I, %r0

	/* It's OK to leave I bit on */
	b       intr_return /* start over if we got a resched */
	nop

	.import do_signal,code
intr_do_signal:
	/* Only do signals if we are returning to user space */
	LDREG   PT_SR7(%r16), %r20
	comib,= 0,%r20,intr_restore /* backward */
	nop

	copy	%r0, %r24			/* unsigned long in_syscall */
	copy	%r16, %r25			/* struct pt_regs *regs */
	ssm     PSW_SM_I, %r0
	bl	do_signal,%r2
	copy	%r0, %r26			/* sigset_t *oldset = NULL */

	b	intr_restore
	nop

	/* CR28 - saved GR1
	 * CR29 - argument for do_irq_mask */

	/* External interrupts */
intr_extint:
	get_stack
	save_specials	%r29
	virt_map	rfi
	save_general	%r29

	ldo		PT_FR0(%r29), %r24
	save_fp		%r24
	
	loadgp

	copy		%r29, %r24	/* arg2 is pt_regs */
	copy		%r29, %r16	/* save pt_regs */
#ifdef CONFIG_KWDB
	copy		%r29, %r3	/* KWDB - update frame pointer (gr3) */
#endif

	/* sorry to put this cruft in the interrupt path */
	ldil		L%cpu_irq_region, %r25
	ldo		R%cpu_irq_region(%r25), %r25
	bl		do_irq_mask,%r2
#ifdef __LP64__
	LDIL_FIXUP(%r25)
#else
	nop
#endif

	b		intr_return
	nop

	/* Generic interruptions (illegal insn, unaligned, page fault, etc) */

	.export         intr_save, code /* for os_hpmc */

intr_save:
	get_stack
	save_specials	%r29

	mfctl		%cr20, %r1
	STREG		%r1, PT_ISR(%r29)
	mfctl		%cr21, %r1
	STREG		%r1, PT_IOR(%r29)

	virt_map	rfi
	save_general	%r29

	ldo		PT_FR0(%r29), %r25
	save_fp		%r25
	
	loadgp

	copy		%r29, %r25	/* arg1 is pt_regs */
#ifdef CONFIG_KWDB
	copy		%r29, %r3	/* KWDB - update frame pointer (gr3) */
#endif

	bl		handle_interruption,%r2
	copy		%r29, %r16	/* save pt_regs */

	b		intr_return
	nop

	/*
	 * Note for all tlb miss handlers:
	 *
	 * cr24 contains a pointer to the kernel address space
	 * page directory.
	 *
	 * cr25 contains a pointer to the current user address
	 * space page directory.
	 *
	 * sr3 will contain the space id of the user address space
	 * of the current running thread while that thread is
	 * running in the kernel.
	 */

	/*
	 * register number allocations.  Note that these are all
	 * in the shadowed registers
	 */

	t0 = r1		/* temporary register 0 */
	va = r8		/* virtual address for which the trap occured */
	t1 = r9		/* temporary register 1 */
	pte  = r16	/* pte/phys page # */
	prot = r17	/* prot bits */
	spc  = r24	/* space for which the trap occured */
	ptp = r25	/* page directory/page table pointer */

#ifdef __LP64__

dtlb_miss_20w:

	extrd,u         spc,31,7,t1     /* adjust va */
	depd            t1,31,7,va      /* adjust va */
	depdi           0,31,7,spc      /* adjust space */
	mfctl           %cr25,ptp	/* Assume user space miss */
	or,*<>          %r0,spc,%r0     /* If it is user space, nullify */
	mfctl           %cr24,ptp	/* Load kernel pgd instead */
	extrd,u         va,33,9,t1      /* Get pgd index */

	mfsp            %sr7,t0		/* Get current space */
	or,*=           %r0,t0,%r0      /* If kernel, nullify following test */
	comb,<>,n       t0,spc,dtlb_fault /* forward */

	/* First level page table lookup */

	ldd,s           t1(ptp),ptp
	extrd,u         va,42,9,t0     /* get second-level index */
	bb,>=,n 	ptp,_PAGE_PRESENT_BIT,dtlb_fault
	depdi           0,63,12,ptp     /* clear prot bits */

	/* Second level page table lookup */

	ldd,s           t0(ptp),ptp
	extrd,u         va,51,9,t0     /* get third-level index */
	bb,>=,n 	ptp,_PAGE_PRESENT_BIT,dtlb_fault
	depdi           0,63,12,ptp     /* clear prot bits */

	/* Third level page table lookup */

	shladd           t0,3,ptp,ptp
	ldi		_PAGE_ACCESSED,t1
	ldd              0(ptp),pte
	bb,>=,n          pte,_PAGE_PRESENT_BIT,dtlb_fault

	/* Check whether the "accessed" bit was set, otherwise do so */

	or		t1,pte,t0	/* t0 has R bit set */
	and,*<>         t1,pte,%r0      /* test and nullify if already set */
	std             t0,0(ptp)       /* write back pte */

	copy            spc,prot	/* init prot with faulting space */
	
	depd		pte,8,7,prot
	extrd,u,*=	pte,_PAGE_NO_CACHE_BIT+32,1,r0
	depdi		1,12,1,prot
	extrd,u,*=      pte,_PAGE_USER_BIT+32,1,r0
	depdi		7,11,3,prot   /* Set for user space (1 rsvd for read) */
	extrd,u,*= 	pte,_PAGE_GATEWAY_BIT+32,1,r0
	depdi		0,11,2,prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for idtlbt */

	depdi		0,63,12,pte
	extrd,u         pte,56,32,pte
	idtlbt		%r16,%r17

	rfir
	nop
#else

dtlb_miss_11:
	mfctl           %cr25,ptp	/* Assume user space miss */
	or,<>           %r0,spc,%r0	/* If it is user space, nullify */
	mfctl           %cr24,ptp	/* Load kernel pgd instead */
	extru		va,9,10,t1	/* Get pgd index */

	mfsp            %sr7,t0		/* Get current space */
	or,=            %r0,t0,%r0	/* If kernel, nullify following test */
	comb,<>,n       t0,spc,dtlb_fault /* forward */

	/* First level page table lookup */

	ldwx,s		t1(ptp),ptp
	extru		va,19,10,t0	/* get second-level index */
	bb,>=,n 	ptp,_PAGE_PRESENT_BIT,dtlb_fault
	depi		0,31,12,ptp	/* clear prot bits */

	/* Second level page table lookup */

	sh2addl 	 t0,ptp,ptp
	ldi		_PAGE_ACCESSED,t1
	ldw		 0(ptp),pte
	bb,>=,n 	 pte,_PAGE_PRESENT_BIT,dtlb_fault

	/* Check whether the "accessed" bit was set, otherwise do so */

	or		t1,pte,t0	/* t0 has R bit set */
	and,<>		t1,pte,%r0	/* test and nullify if already set */
	stw		t0,0(ptp)	/* write back pte */

	copy            spc,prot	/* init prot with faulting space */
	dep		pte,8,7,prot

	extru,=		pte,_PAGE_NO_CACHE_BIT,1,r0
	depi		1,12,1,prot
	extru,=         pte,_PAGE_USER_BIT,1,r0
	depi		7,11,3,prot   /* Set for user space (1 rsvd for read) */
	extru,= 	pte,_PAGE_GATEWAY_BIT,1,r0
	depi		0,11,2,prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for idtlba */

	depi		0,31,12,pte
	extru		pte,24,25,pte

	mfsp		%sr1,t0  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	idtlba		pte,(%sr1,va)
	idtlbp		prot,(%sr1,va)

	mtsp		t0, %sr1	/* Restore sr1 */

	rfir
	nop

dtlb_miss_20:
	.level 2.0

	mfctl           %cr25,ptp	/* Assume user space miss */
	or,<>           %r0,spc,%r0	/* If it is user space, nullify */
	mfctl           %cr24,ptp	/* Load kernel pgd instead */
	extru		va,9,10,t1	/* Get pgd index */

	mfsp            %sr7,t0		/* Get current space */
	or,=            %r0,t0,%r0	/* If kernel, nullify following test */
	comb,<>,n       t0,spc,dtlb_fault /* forward */

	/* First level page table lookup */

	ldwx,s		t1(ptp),ptp
	extru		va,19,10,t0	/* get second-level index */
	bb,>=,n 	ptp,_PAGE_PRESENT_BIT,dtlb_fault
	depi		0,31,12,ptp	/* clear prot bits */

	/* Second level page table lookup */

	sh2addl 	 t0,ptp,ptp
	ldi		_PAGE_ACCESSED,t1
	ldw		 0(ptp),pte
	bb,>=,n 	 pte,_PAGE_PRESENT_BIT,dtlb_fault

	/* Check whether the "accessed" bit was set, otherwise do so */

	or		t1,pte,t0	/* t0 has R bit set */
	and,<>		t1,pte,%r0	/* test and nullify if already set */
	stw		t0,0(ptp)	/* write back pte */

	copy            spc,prot	/* init prot with faulting space */
	
	depd		pte,8,7,prot
	extrd,u,*=	pte,_PAGE_NO_CACHE_BIT+32,1,r0
	depdi		1,12,1,prot
	extrd,u,*=      pte,_PAGE_USER_BIT+32,1,r0
	depdi		7,11,3,prot   /* Set for user space (1 rsvd for read) */
	extrd,u,*= 	pte,_PAGE_GATEWAY_BIT+32,1,r0
	depdi		0,11,2,prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for idtlbt */

	depdi		0,63,12,pte
	extrd,u		pte,56,25,pte
	idtlbt		%r16,%r17

	.level		1.1

	rfir
	nop
#endif

#ifdef __LP64__
itlb_miss_20w:

	/*
	 * I miss is a little different, since we allow users to fault
	 * on the gateway page which is in the kernel address space.
	 */

	extrd,u         spc,31,7,t1     /* adjust va */
	depd            t1,31,7,va      /* adjust va */
	depdi           0,31,7,spc      /* adjust space */
	cmpib,*=        0,spc,itlb_miss_kernel_20w
	extrd,u         va,33,9,t1      /* Get pgd index */

	mfctl           %cr25,ptp	/* load user pgd */

	mfsp            %sr7,t0		/* Get current space */
	or,*=           %r0,t0,%r0      /* If kernel, nullify following test */
	cmpb,*<>,n      t0,spc,itlb_fault /* forward */

	/* First level page table lookup */

itlb_miss_common_20w:
	ldd,s           t1(ptp),ptp
	extrd,u         va,42,9,t0     /* get second-level index */
	bb,>=,n 	ptp,_PAGE_PRESENT_BIT,itlb_fault
	depdi           0,63,12,ptp     /* clear prot bits */

	/* Second level page table lookup */

	ldd,s           t0(ptp),ptp
	extrd,u         va,51,9,t0     /* get third-level index */
	bb,>=,n         ptp,_PAGE_PRESENT_BIT,itlb_fault
	depdi           0,63,12,ptp     /* clear prot bits */

	/* Third level page table lookup */

	shladd           t0,3,ptp,ptp
	ldi		_PAGE_ACCESSED,t1
	ldd              0(ptp),pte
	bb,>=,n          pte,_PAGE_PRESENT_BIT,itlb_fault

	/* Check whether the "accessed" bit was set, otherwise do so */

	or		t1,pte,t0	/* t0 has R bit set */
	and,*<>         t1,pte,%r0      /* test and nullify if already set */
	std             t0,0(ptp)       /* write back pte */

	copy            spc,prot        /* init prot with faulting space */
	
	depd		pte,8,7,prot
	extrd,u,*=	pte,_PAGE_NO_CACHE_BIT+32,1,r0
	depdi		1,12,1,prot
	extrd,u,*=      pte,_PAGE_USER_BIT+32,1,r0
	depdi		7,11,3,prot   /* Set for user space (1 rsvd for read) */
	extrd,u,*= 	pte,_PAGE_GATEWAY_BIT+32,1,r0
	depdi		0,11,2,prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for iitlbt */

	depdi		0,63,12,pte
	extrd,u         pte,56,32,pte
	iitlbt          %r16,%r17

	rfir
	nop

itlb_miss_kernel_20w:
	b               itlb_miss_common_20w
	mfctl           %cr24,ptp	/* Load kernel pgd */
#else

itlb_miss_11:

	/*
	 * I miss is a little different, since we allow users to fault
	 * on the gateway page which is in the kernel address space.
	 */

	comib,=         0,spc,itlb_miss_kernel_11
	extru		va,9,10,t1	/* Get pgd index */

	mfctl           %cr25,ptp	/* load user pgd */

	mfsp            %sr7,t0		/* Get current space */
	or,=            %r0,t0,%r0	/* If kernel, nullify following test */
	comb,<>,n       t0,spc,itlb_fault /* forward */

	/* First level page table lookup */

itlb_miss_common_11:
	ldwx,s		t1(ptp),ptp
	extru		va,19,10,t0	/* get second-level index */
	bb,>=,n 	ptp,_PAGE_PRESENT_BIT,itlb_fault
	depi		0,31,12,ptp	/* clear prot bits */

	/* Second level page table lookup */

	sh2addl 	 t0,ptp,ptp
	ldi		_PAGE_ACCESSED,t1
	ldw		 0(ptp),pte
	bb,>=,n 	 pte,_PAGE_PRESENT_BIT,itlb_fault

	/* Check whether the "accessed" bit was set, otherwise do so */

	or		t1,pte,t0	/* t0 has R bit set */
	and,<>		t1,pte,%r0	/* test and nullify if already set */
	stw		t0,0(ptp)	/* write back pte */

	copy            spc,prot        /* init prot with faulting space */
	dep		pte,8,7,prot

	extru,=		pte,_PAGE_NO_CACHE_BIT,1,r0
	depi		1,12,1,prot
	extru,=         pte,_PAGE_USER_BIT,1,r0
	depi		7,11,3,prot   /* Set for user space (1 rsvd for read) */
	extru,= 	pte,_PAGE_GATEWAY_BIT,1,r0
	depi		0,11,2,prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for iitlba */

	depi		0,31,12,pte
	extru		pte,24,25,pte

	mfsp		%sr1,t0  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	iitlba		pte,(%sr1,va)
	iitlbp		prot,(%sr1,va)

	mtsp		t0, %sr1	/* Restore sr1 */

	rfir
	nop

itlb_miss_kernel_11:
	b               itlb_miss_common_11
	mfctl           %cr24,ptp	/* Load kernel pgd */

itlb_miss_20:

	/*
	 * I miss is a little different, since we allow users to fault
	 * on the gateway page which is in the kernel address space.
	 */

	comib,=         0,spc,itlb_miss_kernel_20
	extru		va,9,10,t1	/* Get pgd index */

	mfctl           %cr25,ptp	/* load user pgd */

	mfsp            %sr7,t0		/* Get current space */
	or,=            %r0,t0,%r0	/* If kernel, nullify following test */
	comb,<>,n       t0,spc,itlb_fault /* forward */

	/* First level page table lookup */

itlb_miss_common_20:
	ldwx,s		t1(ptp),ptp
	extru		va,19,10,t0	/* get second-level index */
	bb,>=,n 	ptp,_PAGE_PRESENT_BIT,itlb_fault
	depi		0,31,12,ptp	/* clear prot bits */

	/* Second level page table lookup */

	sh2addl 	 t0,ptp,ptp
	ldi		_PAGE_ACCESSED,t1
	ldw		 0(ptp),pte
	bb,>=,n 	 pte,_PAGE_PRESENT_BIT,itlb_fault

	/* Check whether the "accessed" bit was set, otherwise do so */

	or		t1,pte,t0	/* t0 has R bit set */
	and,<>		t1,pte,%r0	/* test and nullify if already set */
	stw		t0,0(ptp)	/* write back pte */

	copy            spc,prot        /* init prot with faulting space */
	
	.level 2.0

	depd		pte,8,7,prot
	extrd,u,*=	pte,_PAGE_NO_CACHE_BIT+32,1,r0
	depdi		1,12,1,prot
	extrd,u,*=      pte,_PAGE_USER_BIT+32,1,r0
	depdi		7,11,3,prot   /* Set for user space (1 rsvd for read) */
	extrd,u,*= 	pte,_PAGE_GATEWAY_BIT+32,1,r0
	depdi		0,11,2,prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for iitlbt */

	depdi		0,63,12,pte
	extrd,u		pte,56,25,pte
	iitlbt          %r16,%r17
	.level		1.1

	rfir
	nop


itlb_miss_kernel_20:
	b               itlb_miss_common_20
	mfctl           %cr24,ptp	/* Load kernel pgd */
#endif

#ifdef __LP64__

dbit_trap_20w:

	extrd,u         spc,31,7,t1     /* adjust va */
	depd            t1,31,7,va      /* adjust va */
	depdi           0,1,2,va        /* adjust va */
	depdi           0,31,7,spc      /* adjust space */
	mfctl           %cr25,ptp	/* Assume user space miss */
	or,*<>          %r0,spc,%r0     /* If it is user space, nullify */
	mfctl           %cr24,ptp	/* Load kernel pgd instead */
	extrd,u         va,33,9,t1      /* Get pgd index */

	mfsp            %sr7,t0		/* Get current space */
	or,*=           %r0,t0,%r0      /* If kernel, nullify following test */
	comb,<>,n       t0,spc,dbit_fault /* forward */

	/* First level page table lookup */

	ldd,s           t1(ptp),ptp
	extrd,u         va,42,9,t0     /* get second-level index */
	bb,>=,n         ptp,_PAGE_PRESENT_BIT,dbit_fault
	depdi           0,63,12,ptp     /* clear prot bits */

	/* Second level page table lookup */

	ldd,s           t0(ptp),ptp
	extrd,u         va,51,9,t0     /* get third-level index */
	bb,>=,n         ptp,_PAGE_PRESENT_BIT,dbit_fault
	depdi           0,63,12,ptp     /* clear prot bits */

	/* Third level page table lookup */

	shladd           t0,3,ptp,ptp
	ldi		(_PAGE_ACCESSED|_PAGE_DIRTY),t1
	ldd              0(ptp),pte
	bb,>=,n          pte,_PAGE_PRESENT_BIT,dbit_fault

	/* Set Accessed and Dirty bits in the pte */

	or		t1,pte,pte
	std             pte,0(ptp)      /* write back pte */

	copy            spc,prot	/* init prot with faulting space */
	
	depd		pte,8,7,prot
	extrd,u,*=	pte,_PAGE_NO_CACHE_BIT+32,1,r0
	depdi		1,12,1,prot
	extrd,u,*=      pte,_PAGE_USER_BIT+32,1,r0
	depdi		7,11,3,prot   /* Set for user space (1 rsvd for read) */
	extrd,u,*= 	pte,_PAGE_GATEWAY_BIT+32,1,r0
	depdi		0,11,2,prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for idtlbt */

	depdi		0,63,12,pte
	extrd,u         pte,56,32,pte
	idtlbt		%r16,%r17

	rfir
	nop
#else

dbit_trap_11:
	mfctl           %cr25,ptp	/* Assume user space trap */
	or,<>           %r0,spc,%r0	/* If it is user space, nullify */
	mfctl           %cr24,ptp	/* Load kernel pgd instead */
	extru		va,9,10,t1	/* Get pgd index */

	mfsp            %sr7,t0		/* Get current space */
	or,=            %r0,t0,%r0	/* If kernel, nullify following test */
	comb,<>,n       t0,spc,dbit_fault /* forward */

	/* First level page table lookup */

	ldwx,s		t1(ptp),ptp
	extru		va,19,10,t0	/* get second-level index */
	bb,>=,n 	ptp,_PAGE_PRESENT_BIT,dbit_fault
	depi		0,31,12,ptp	/* clear prot bits */

	/* Second level page table lookup */

	sh2addl 	 t0,ptp,ptp
	ldi		(_PAGE_ACCESSED|_PAGE_DIRTY),t1
	ldw		 0(ptp),pte
	bb,>=,n 	 pte,_PAGE_PRESENT_BIT,dbit_fault

	/* Set Accessed and Dirty bits in the pte */

	or		t1,pte,pte
	stw		pte,0(ptp)	/* write back pte */

	copy            spc,prot        /* init prot with faulting space */
	dep		pte,8,7,prot

	extru,=		pte,_PAGE_NO_CACHE_BIT,1,r0
	depi		1,12,1,prot
	extru,=         pte,_PAGE_USER_BIT,1,r0
	depi		7,11,3,prot /* Set for user space (1 rsvd for read) */
	extru,= 	pte,_PAGE_GATEWAY_BIT,1,r0
	depi		0,11,2,prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for idtlba */

	depi		0,31,12,pte
	extru		pte,24,25,pte

	mfsp		%sr1,t0  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	idtlba		pte,(%sr1,va)
	idtlbp		prot,(%sr1,va)

	mtsp		t0, %sr1     /* Restore sr1 */

	rfir
	nop

dbit_trap_20:
	mfctl           %cr25,ptp	/* Assume user space trap */
	or,<>           %r0,spc,%r0	/* If it is user space, nullify */
	mfctl           %cr24,ptp	/* Load kernel pgd instead */
	extru		va,9,10,t1	/* Get pgd index */

	mfsp            %sr7,t0		/* Get current space */
	or,=            %r0,t0,%r0	/* If kernel, nullify following test */
	comb,<>,n       t0,spc,dbit_fault /* forward */

	/* First level page table lookup */

	ldwx,s		t1(ptp),ptp
	extru		va,19,10,t0	/* get second-level index */
	bb,>=,n 	ptp,_PAGE_PRESENT_BIT,dbit_fault
	depi		0,31,12,ptp	/* clear prot bits */

	/* Second level page table lookup */

	sh2addl 	 t0,ptp,ptp
	ldi		(_PAGE_ACCESSED|_PAGE_DIRTY),t1
	ldw		 0(ptp),pte
	bb,>=,n 	 pte,_PAGE_PRESENT_BIT,dbit_fault

	/* Set Accessed and Dirty bits in the pte */

	or		t1,pte,pte
	stw		pte,0(ptp)	/* write back pte */

	copy            spc,prot        /* init prot with faulting space */
	
	.level 2.0

	depd		pte,8,7,prot
	extrd,u,*=	pte,_PAGE_NO_CACHE_BIT+32,1,r0
	depdi		1,12,1,prot
	extrd,u,*=      pte,_PAGE_USER_BIT+32,1,r0
	depdi		7,11,3,prot   /* Set for user space (1 rsvd for read) */
	extrd,u,*= 	pte,_PAGE_GATEWAY_BIT+32,1,r0
	depdi		0,11,2,prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for idtlbt */

	depdi		0,63,12,pte
	extrd,u		pte,56,25,pte
	idtlbt		%r16,%r17

	.level		1.1

	rfir
	nop
#endif

	.import handle_interruption,code

kernel_bad_space:
	b		tlb_fault
	ldi		31,%r1	/* Use an unused code */

dbit_fault:
	b		tlb_fault
	ldi		20,%r1

itlb_fault:
	b		tlb_fault
	ldi		6,%r1

dtlb_fault:
	ldi		15,%r1

	/* Fall Through */

tlb_fault:
	mtctl		%r1,%cr29
	mtctl		%r29,%cr31

	get_stack
	save_specials	%r29		/* Note this saves a trashed r1 */

	SAVE_CR		(%cr20, PT_ISR(%r29))
	SAVE_CR		(%cr21, PT_IOR(%r29))

	virt_map	rfir

	STREG		%r1,PT_GR1(%r29)	/* save good value after rfir */

	save_general	%r29

	ldo	PT_FR0(%r29), %r25
	save_fp		%r25
	
	loadgp

	copy		%r29, %r25

	bl		handle_interruption, %r2
	copy		%r29, %r16

	b		intr_return
	nop

	/* Register saving semantics for system calls:

	   %r1		   clobbered by system call macro in userspace
	   %r2		   saved in PT_REGS by gateway page
	   %r3  - %r18	   preserved by C code (saved by signal code)
	   %r19 - %r20	   saved in PT_REGS by gateway page
	   %r21 - %r22	   non-standard syscall args
			   stored in kernel stack by gateway page
	   %r23 - %r26	   arg3-arg0, saved in PT_REGS by gateway page
	   %r27 - %r30	   saved in PT_REGS by gateway page
	   %r31		   syscall return pointer
	 */

	/* Floating point registers (FIXME: what do we do with these?)

	   %fr0  - %fr3	   status/exception, not preserved
	   %fr4  - %fr7	   arguments
	   %fr8	 - %fr11   not preserved by C code
	   %fr12 - %fr21   preserved by C code
	   %fr22 - %fr31   not preserved by C code
	 */

	.macro	reg_save regs
	STREG	%r3, PT_GR3(\regs)
	STREG	%r4, PT_GR4(\regs)
	STREG	%r5, PT_GR5(\regs)
	STREG	%r6, PT_GR6(\regs)
	STREG	%r7, PT_GR7(\regs)
	STREG	%r8, PT_GR8(\regs)
	STREG	%r9, PT_GR9(\regs)
	STREG   %r10,PT_GR10(\regs)
	STREG   %r11,PT_GR11(\regs)
	STREG   %r12,PT_GR12(\regs)
	STREG   %r13,PT_GR13(\regs)
	STREG   %r14,PT_GR14(\regs)
	STREG   %r15,PT_GR15(\regs)
	STREG   %r16,PT_GR16(\regs)
	STREG   %r17,PT_GR17(\regs)
	STREG   %r18,PT_GR18(\regs)
	.endm

	.macro	reg_restore regs
	LDREG	PT_GR3(\regs), %r3
	LDREG	PT_GR4(\regs), %r4
	LDREG	PT_GR5(\regs), %r5
	LDREG	PT_GR6(\regs), %r6
	LDREG	PT_GR7(\regs), %r7
	LDREG	PT_GR8(\regs), %r8
	LDREG	PT_GR9(\regs), %r9
	LDREG   PT_GR10(\regs),%r10
	LDREG   PT_GR11(\regs),%r11
	LDREG   PT_GR12(\regs),%r12
	LDREG   PT_GR13(\regs),%r13
	LDREG   PT_GR14(\regs),%r14
	LDREG   PT_GR15(\regs),%r15
	LDREG   PT_GR16(\regs),%r16
	LDREG   PT_GR17(\regs),%r17
	LDREG   PT_GR18(\regs),%r18
	.endm

	.export sys_fork_wrapper
sys_fork_wrapper:
	ldo	TASK_REGS-TASK_SZ_ALGN-FRAME_SIZE(%r30),%r1	/* get pt regs */
	reg_save %r1

	STREG	%r2,-RP_OFFSET(%r30)
	ldo	FRAME_SIZE(%r30),%r30

	/* These are call-clobbered registers and therefore
	   also syscall-clobbered (we hope). */
	STREG	%r2,PT_GR19(%r1)	/* save for child */
	STREG	%r30,PT_GR20(%r1)
	ldil	L%child_return, %r3
	ldo	R%child_return(%r3), %r3
	LDIL_FIXUP(%r3)
	STREG	%r3,PT_GR21(%r1)	/* save for child */

	LDREG	PT_GR30(%r1),%r25
	copy	%r1,%r24
	bl	sys_clone,%r2
	ldi	SIGCHLD,%r26

	LDREG	-RP_OFFSET-FRAME_SIZE(%r30),%r2
wrapper_exit:
	ldo	-FRAME_SIZE(%r30),%r30		/* get the stackframe */
	ldo	TASK_REGS-TASK_SZ_ALGN-FRAME_SIZE(%r30),%r1    /* get pt regs */

	reg_restore %r1

	bv %r0(%r2)
	nop

	/* Set the return value for the child */
child_return:
	LDREG	TASK_PT_GR19-TASK_SZ_ALGN-128(%r30),%r2
	b	wrapper_exit
	copy	%r0,%r28

	
	.export sys_clone_wrapper
sys_clone_wrapper:
	ldo	TASK_REGS-TASK_SZ_ALGN-FRAME_SIZE(%r30),%r1	/* get pt regs */
	reg_save %r1

	STREG	%r2,-RP_OFFSET(%r30)
	ldo	FRAME_SIZE(%r30),%r30

	STREG	%r30,PT_GR20(%r1)
	ldil	L%child_return,%r3
	ldo	R%child_return(%r3),%r3
	LDIL_FIXUP(%r3)

	bl	sys_clone,%r2
	STREG	%r3,PT_GR21(%r1)	/* save for child */

	b	wrapper_exit
	LDREG	-RP_OFFSET-FRAME_SIZE(%r30),%r2


	.export sys_vfork_wrapper
sys_vfork_wrapper:
	ldo	TASK_REGS-TASK_SZ_ALGN-FRAME_SIZE(%r30),%r1	/* get pt regs */
	reg_save %r1

	STREG	%r2,-RP_OFFSET(%r30)
	ldo	FRAME_SIZE(%r30),%r30

	STREG	%r30,PT_GR20(%r1)
	ldil	L%child_return,%r3
	ldo	R%child_return(%r3),%r3
	LDIL_FIXUP(%r3)
	STREG	%r3,PT_GR21(%r1)	/* save for child */

	bl	sys_vfork,%r2
	copy	%r1,%r26

	b	wrapper_exit
	LDREG	-RP_OFFSET-FRAME_SIZE(%r30),%r2

	
	.macro  execve_wrapper execve
	ldo	TASK_REGS-TASK_SZ_ALGN-FRAME_SIZE(%r30),%r1	    /* get pt regs */

	/*
	 * Do we need to save/restore r3-r18 here?
	 * I don't think so. why would new thread need old
	 * threads registers?
	 */

	/* %arg0 - %arg3 are already saved for us. */

	STREG %r2,-RP_OFFSET(%r30)
	ldo FRAME_SIZE(%r30),%r30
	bl \execve,%r2
	copy %r1,%arg0

	ldo -FRAME_SIZE(%r30),%r30
	LDREG -RP_OFFSET(%r30),%r2

	/* If exec succeeded we need to load the args */

	ldo -1024(%r0),%r1
	comb,>>= %r28,%r1,error_\execve
	copy %r2,%r19

error_\execve:
	bv %r0(%r19)
	nop
	.endm

	.export sys_execve_wrapper
	.import sys_execve

sys_execve_wrapper:
	execve_wrapper sys_execve

#ifdef __LP64__
	.export sys32_execve_wrapper
	.import sys32_execve

sys32_execve_wrapper:
	execve_wrapper sys32_execve
#endif

	.export sys_rt_sigreturn_wrapper
sys_rt_sigreturn_wrapper:
	ldo	TASK_REGS-TASK_SZ_ALGN-FRAME_SIZE(%r30), %r26
	/* Don't save regs, we are going to restore them from sigcontext. */
	STREG	%r2, -RP_OFFSET(%r30)
	bl	sys_rt_sigreturn,%r2
	ldo	FRAME_SIZE(%r30), %r30

	ldo	-FRAME_SIZE(%r30), %r30
	LDREG	-RP_OFFSET(%r30), %r2

	/* FIXME: I think we need to restore a few more things here. */
	ldo	TASK_REGS-TASK_SZ_ALGN-FRAME_SIZE(%r30),%r1	    /* get pt regs */
	reg_restore %r1

	/* If the signal was received while the process was blocked on a
	 * syscall, then r2 will take us to syscall_exit; otherwise r2 will
	 * take us to syscall_exit_rfi and on to intr_return.
	 */
	bv	%r0(%r2)
	LDREG	PT_GR28(%r1),%r28  /* reload original r28 for syscall_exit */

	.export sys_sigaltstack_wrapper
sys_sigaltstack_wrapper:
	/* Get the user stack pointer */
	LDREG	-TASK_SZ_ALGN-FRAME_SIZE+TASK_PT_GR30(%r30), %r24
	STREG	%r2, -RP_OFFSET(%r30)
	bl	do_sigaltstack,%r2
	ldo	FRAME_SIZE(%r30), %r30

	ldo	-FRAME_SIZE(%r30), %r30
	LDREG	-RP_OFFSET(%r30), %r2
	bv	%r0(%r2)
	nop

	.export sys_rt_sigsuspend_wrapper
sys_rt_sigsuspend_wrapper:
	ldo	TASK_REGS-TASK_SZ_ALGN-FRAME_SIZE(%r30), %r24
	reg_save %r24

	STREG	%r2, -RP_OFFSET(%r30)
	bl	sys_rt_sigsuspend,%r2
	ldo	FRAME_SIZE(%r30), %r30

	ldo	-FRAME_SIZE(%r30), %r30
	LDREG	-RP_OFFSET(%r30), %r2

	ldo	TASK_REGS-TASK_SZ_ALGN-FRAME_SIZE(%r30),%r1
	reg_restore %r1

	bv	%r0(%r2)
	nop

	.export syscall_exit
syscall_exit:
	/* NOTE: HP-UX syscalls also come through here
	   after hpux_syscall_exit fixes up return
	   values. */
	/* NOTE: Not all syscalls exit this way.  rt_sigreturn will exit
	 * via syscall_exit_rfi if the signal was received while the process
	 * was running.  All traced processes will probably exit via
	 * syscall_exit_rfi in the future.
	 */

	/* save return value now */

	STREG     %r28,TASK_PT_GR28-TASK_SZ_ALGN-FRAME_SIZE(%r30)

syscall_check_bh:

/* #ifdef NOTNOW */
	/* Check for software interrupts */

	.import irq_stat,data

	ldil    L%irq_stat,%r19
	ldo     R%irq_stat(%r19),%r19
	LDIL_FIXUP(%r19)

#ifdef CONFIG_SMP
	/* sched.h: int processor */
	ldw     TASK_PROCESSOR-TASK_SZ_ALGN-FRAME_SIZE(%r30),%r20 /* get cpu # */
#if (IRQSTAT_SZ == 32)
	dep     %r20,26,27,%r20 /* shift left 5 bits */
#else
#error IRQSTAT_SZ changed, fix dep
#endif /* IRQSTAT_SZ */
	add     %r19,%r20,%r19
#endif /* CONFIG_SMP */

	ldw     IRQSTAT_SI_ACTIVE(%r19),%r20	/* hardirq.h: unsigned int */
	ldw     IRQSTAT_SI_MASK(%r19),%r19	/* hardirq.h: unsigned int */
	and     %r19,%r20,%r20
	comib,<>,n 0,%r20,syscall_do_softirq /* forward */
/* #endif */


syscall_check_resched:

	/* check for reschedule */

	LDREG  TASK_NEED_RESCHED-TASK_SZ_ALGN-FRAME_SIZE(%r30),%r19	/* long */
	comib,<>,n 0,%r19,syscall_do_resched /* forward */

syscall_check_sig:
	ldo     -TASK_SZ_ALGN-FRAME_SIZE(%r30),%r1         /* get task ptr */
	/* check for pending signals */
	ldw     TASK_SIGPENDING(%r1),%r19
	comib,<>,n 0,%r19,syscall_do_signal  /* forward */

syscall_restore:
	/* disable interrupts while dicking with the kernel stack, */
	/* or life can become unpleasant */
	rsm	PSW_SM_I, %r20
	LDREG	TASK_PTRACE(%r1), %r19		/* Are we being ptraced? */
	bb,<,n	%r19,31,syscall_restore_rfi
	LDREG	TASK_PT_GR20(%r1),%r19
	mtctl	%r19, %cr27

	LDREG	TASK_PT_GR2(%r1),%r2		   /* restore user rp */
	LDREG	TASK_PT_GR21(%r1),%r21
	LDREG	TASK_PT_GR22(%r1),%r22
	LDREG	TASK_PT_GR23(%r1),%r23
	LDREG	TASK_PT_GR24(%r1),%r24
	LDREG	TASK_PT_GR25(%r1),%r25
	LDREG	TASK_PT_GR26(%r1),%r26
	LDREG	TASK_PT_GR27(%r1),%r27	   /* restore user dp */
	LDREG	TASK_PT_GR28(%r1),%r28	   /* syscall return value */
	LDREG	TASK_PT_GR29(%r1),%r29
	LDREG	TASK_PT_GR30(%r1),%r30	   /* restore user sp */
	LDREG	TASK_PT_GR31(%r1),%r31	   /* restore syscall rp */
	ldo	TASK_PT_FR31(%r1),%r19		   /* reload fpregs */
	rest_fp	%r19
	LDREG	TASK_PT_SAR(%r1),%r19		   /* restore SAR */
	mtsar	%r19
	LDREG	TASK_PT_GR19(%r1),%r19

	mtctl	%r1,%cr30			   /* intrhandler okay. */
	mfsp	%sr3,%r1			   /* Get users space id */
	mtsp	%r1,%sr4			   /* Restore sr4 */
	mtsp	%r1,%sr5			   /* Restore sr5 */
	mtsp	%r1,%sr6			   /* Restore sr6 */

	depi	3,31,2,%r31			   /* ensure return to user mode. */

	mtsm	%r20				   /* restore irq state  */
	mfctl	%cr27,%r20
	
	/*
	 * Due to a dependency in the tlb miss handlers on sr7, it
	 * is essential that sr7 get set in the delay slot.
	 */

#ifdef __LP64__

	/* Note the be (and mtsp) is executed in narrow mode. This is OK
	 * for 32 bit processes, but won't work once we support 64 bit
	 * processes.
	 */

	rsm	PSW_SM_W, %r0
	be	0(%sr3,%r31)			   /* return to user space */
	mtsp	%r1,%sr7			   /* Restore sr7 */
#else
	be	0(%sr3,%r31)			   /* return to user space */
	mtsp	%r1,%sr7			   /* Restore sr7 */
#endif

	/* We have to return via an RFI, so that PSW T and R bits can be set
	 * appropriately.
	 * This sets up pt_regs so we can return via intr_restore, which is not
	 * the most efficient way of doing things, but it works.
	 */
syscall_restore_rfi:
	ldo	-1(%r0),%r2			   /* Set recovery cntr to -1 */
	mtctl	%r2,%cr0			   /*   for immediate trap */
	copy	%r0,%r2				   /* Create a reasonable PSW */
	/* XXX W bit??? */
	depi	-1,13,1,%r2
	depi	-1,28,1,%r2
	depi	-1,30,1,%r2
	depi	-1,31,1,%r2
	bb,<,n	%r19,15,set_rbit		   /* PT_SINGLESTEP */
	bb,>=,n	%r19,14,set_nobit		   /* PT_BLOCKSTEP, see ptrace.c */
set_tbit:
	depi	-1,7,1,%r2
	b,n	set_nobit
set_rbit:
	depi	-1,27,1,%r2
set_nobit:
	STREG	%r2,TASK_PT_PSW(%r1)
	STREG	%r1,TASK_PT_CR30(%r1)
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
	depi	3,31,2,%r2			   /* ensure return to user mode. */
	STREG	%r2,TASK_PT_IAOQ0(%r1)
	ldo	4(%r2),%r2
	STREG	%r2,TASK_PT_IAOQ1(%r1)
	ldo	TASK_REGS(%r1),%r25
	reg_save %r25				   /* Save r3 to r18 */
	copy	%r25,%r16
	b	intr_restore
	nop

	.import do_softirq,code
syscall_do_softirq:
	bl      do_softirq,%r2
	nop
	b       syscall_check_resched
	ssm     PSW_SM_I, %r0  /* do_softirq returns with I bit off */

	.import schedule,code
syscall_do_resched:
	bl	schedule,%r2
	nop
	b       syscall_check_bh  /* if resched, we start over again */
	nop

	.import do_signal,code
syscall_do_signal:
	/* Save callee-save registers (for sigcontext).
	   FIXME: After this point the process structure should be
	   consistent with all the relevant state of the process
	   before the syscall.  We need to verify this. */
	ldo	TASK_REGS(%r1), %r25		/* struct pt_regs *regs */
	reg_save %r25

	ldi	1, %r24				/* unsigned long in_syscall */

	bl	do_signal,%r2
	copy	%r0, %r26			/* sigset_t *oldset = NULL */

	ldo     -TASK_SZ_ALGN-FRAME_SIZE(%r30), %r1     /* reload task ptr */
	ldo	TASK_REGS(%r1), %r20		/* reload pt_regs */
	reg_restore %r20

	b,n     syscall_restore

#ifdef __LP64__
unimplemented_64bitirq:
	ssm PSW_SM_Q+PSW_SM_I, %r0
	/* indicate that we had an interrupt */
	ldi	0x77, %r28
	ldi	0x77, %r29
	/* save interrupt registers in GRs for diagnosis */
	mfctl %cr17, %r17
	mfctl %cr18, %r18
	mfctl %cr19, %r19
	mfctl %cr20, %r20
	mfctl %cr21, %r21
	mfctl %cr22, %r22
	b,n .
	nop
#endif
