#include <linux/config.h> /* for CONFIG_ARCH_xxxx */
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/constants.h>
#include <asm/errno.h>
#include <asm/hardware.h>
#include <asm/arch/irqs.h>
#include <asm/proc-fns.h>

#ifndef MODE_SVC
#define MODE_SVC 0x13
#endif

		.macro	zero_fp
#ifndef CONFIG_NO_FRAME_POINTER
		mov	fp, #0
#endif
		.endm

		.text

@ Bad Abort numbers
@ -----------------
@
#define BAD_PREFETCH	0
#define BAD_DATA	1
#define BAD_ADDREXCPTN	2
#define BAD_IRQ		3
#define BAD_UNDEFINSTR	4

#define PT_TRACESYS	0x00000002

@ OS version number used in SWIs
@  RISC OS is 0
@  RISC iX is 8
@
#define OS_NUMBER	9
#define ARMSWI_OFFSET	0x000f0000

@
@ Stack format (ensured by USER_* and SVC_*)
@
#define S_FRAME_SIZE	72
#ifdef CONFIG_CPU_32
#define S_OLD_R0	68
#define S_PSR		64
#else
#define S_OLD_R0	64
#define S_PSR		60
#define S_PC		60
#endif

#define S_PC		60
#define S_LR		56
#define S_SP		52
#define S_IP		48
#define S_FP		44
#define S_R10		40
#define S_R9		36
#define S_R8		32
#define S_R7		28
#define S_R6		24
#define S_R5		20
#define S_R4		16
#define S_R3		12
#define S_R2		8
#define S_R1		4
#define S_R0		0
#define S_OFF		8

#ifdef CONFIG_CPU_32
		.macro	save_user_regs
		sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ Calling r0 - r12
		add	r8, sp, #S_PC
		stmdb	r8, {sp, lr}^			@ Calling sp, lr
		mrs	r7, spsr
		str	lr, [sp, #S_PC]			@ Save calling PC
		str	r7, [sp, #S_PSR]		@ Save CPSR
		str	r0, [sp, #S_OLD_R0]		@ Save OLD_R0
		.endm

		.macro	restore_user_regs
		ldr	r0, [sp, #S_PSR]		@ Get calling cpsr
		mov	ip, #I_BIT | MODE_SVC
		msr	cpsr_c, ip			@ disable IRQs
		msr	spsr, r0			@ save in spsr_svc
		ldr	lr, [sp, #S_PC]			@ Get PC
		ldmia	sp, {r0 - lr}^			@ Get calling r0 - lr
		mov	r0, r0
		add	sp, sp, #S_FRAME_SIZE
		movs	pc, lr				@ return & move spsr_svc into cpsr
		.endm

		.macro	fast_restore_user_regs
		mov	ip, #I_BIT | MODE_SVC
		msr	cpsr_c, ip			@ disable IRQs
		ldr	r1, [sp, #S_OFF + S_PSR]	@ get calling cpsr
		ldr	lr, [sp, #S_OFF + S_PC]!	@ get pc
 		msr	spsr, r1			@ save in spsr_svc
		ldmdb	sp, {r1 - lr}^			@ get calling r1 - lr
		mov	r0, r0
		add	sp, sp, #S_FRAME_SIZE - S_PC
		movs	pc, lr				@ return & move spsr_svc into cpsr
		.endm

		.macro	mask_pc, rd, rm
		.endm

		.macro	enable_irqs, temp
		mov	\temp, #MODE_SVC
		msr	cpsr_c, \temp
		.endm

		.macro	get_current_task, rd
		mov	\rd, sp, lsr #13
		mov	\rd, \rd, lsl #13
		.endm

		/*
		 * Like adr, but force SVC mode (if required)
		 */
		.macro	adrsvc, cond, reg, label
		adr\cond	\reg, \label
		.endm

		.macro	alignment_trap, rbase, rtemp, sym
#ifdef CONFIG_ALIGNMENT_TRAP
#define OFF_CR_ALIGNMENT(x)	cr_alignment - x

		ldr	\rtemp, [\rbase, #OFF_CR_ALIGNMENT(\sym)]
		mcr	p15, 0, \rtemp, c1, c0
#endif
		.endm

#else
		.macro	save_user_regs
		str	r0, [sp, #-4]!
		str	lr, [sp, #-4]!
		sub	sp, sp, #15*4
		stmia	sp, {r0 - lr}^
		mov	r0, r0
		.endm

		.macro	restore_user_regs
		ldmia	sp, {r0 - lr}^
		mov	r0, r0
		ldr	lr, [sp, #15*4]
		add	sp, sp, #15*4+8
		movs	pc, lr
		.endm

		.macro	fast_restore_user_regs
		add	sp, sp, #S_OFF
		ldmib	sp, {r1 - lr}^
		mov	r0, r0
		ldr	lr, [sp, #15*4]
		add	sp, sp, #15*4+8
		movs	pc, lr
		.endm

		.macro	mask_pc, rd, rm
		bic	\rd, \rm, #PCMASK
		.endm

		.macro	enable_irqs, temp
		teqp	pc, #0x00000003
		.endm

		.macro	initialise_traps_extra
		.endm

		.macro	get_current_task, rd
		mov	\rd, sp, lsr #13
		mov	\rd, \rd, lsl #13
		.endm

		/*
		 * Like adr, but force SVC mode (if required)
		 */
		.macro	adrsvc, cond, reg, label
		adr\cond	\reg, \label
		orr\cond	\reg, \reg, #0x08000003
		.endm

#endif

/*
 * These are the registers used in the syscall handler, and allow us to
 * have in theory up to 7 arguments to a function.  Note that tbl == why
 * is intentional.
 *
 * We must set at least "tsk" and "why" when calling ret_with_reschedule.
 */
scno	.req	r9				@ syscall number
tbl	.req	r8				@ syscall table pointer
why	.req	r8				@ Linux syscall (!= 0)
tsk	.req	r7				@ current task


