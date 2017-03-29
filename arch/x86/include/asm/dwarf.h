/*
 * Copyright (C) 2002-2017 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 *	Jiri Slaby <jirislaby@kernel.org>
 * The code below is released under version 2 of the GNU GPL.
 */

#ifndef _ASM_X86_DWARF_H
#define _ASM_X86_DWARF_H

#ifdef CONFIG_DWARF_UNWIND

#include <linux/uaccess.h>

/*
 * On x86-64, we might need to account for the special exception and interrupt
 * handling stacks here, since normally
 *	EXCEPTION_STACK_ORDER < THREAD_ORDER < IRQSTACK_ORDER,
 * but the construct is needed only for getting across the stack switch to
 * the interrupt stack - thus considering the IRQ stack itself is unnecessary,
 * and the overhead of comparing against all exception handling stacks seems
 * not desirable.
 */
#define STACK_LIMIT(ptr)	(((ptr) - 1) & ~(THREAD_SIZE - 1))

#define DW_PC(frame)		((frame)->u.regs.ip)
#define DW_SP(frame)		((frame)->u.regs.sp)
#ifdef CONFIG_FRAME_POINTER
# define DW_FP(frame)		((frame)->u.regs.bp)
# define FRAME_LINK_OFFSET	0
# define STACK_BOTTOM(tsk)	STACK_LIMIT((tsk)->thread.sp0)
# define TSK_STACK_TOP(tsk)	((tsk)->thread.sp0)
#else
# define DW_FP(frame)		((void)(frame), 0UL)
#endif

#ifdef CONFIG_X86_64

#include <asm/vsyscall.h>

#define FRAME_RETADDR_OFFSET 8

#define DW_REGISTER_INFO \
	PTREGS_INFO(ax), \
	PTREGS_INFO(dx), \
	PTREGS_INFO(cx), \
	PTREGS_INFO(bx), \
	PTREGS_INFO(si), \
	PTREGS_INFO(di), \
	PTREGS_INFO(bp), \
	PTREGS_INFO(sp), \
	PTREGS_INFO(r8), \
	PTREGS_INFO(r9), \
	PTREGS_INFO(r10), \
	PTREGS_INFO(r11), \
	PTREGS_INFO(r12), \
	PTREGS_INFO(r13), \
	PTREGS_INFO(r14), \
	PTREGS_INFO(r15), \
	PTREGS_INFO(ip)

#else /* X86_32 */

#define FRAME_RETADDR_OFFSET 4

#define DW_REGISTER_INFO \
	PTREGS_INFO(ax), \
	PTREGS_INFO(cx), \
	PTREGS_INFO(dx), \
	PTREGS_INFO(bx), \
	PTREGS_INFO(sp), \
	PTREGS_INFO(bp), \
	PTREGS_INFO(si), \
	PTREGS_INFO(di), \
	PTREGS_INFO(ip)

#endif

#define DW_DEFAULT_RA(raItem, data_align) \
	((raItem).where == MEMORY && \
	 !((raItem).value * (data_align) + sizeof(void *)))

static inline void arch_dwarf_init_frame_info(struct unwind_state *info,
		struct pt_regs *regs)
{
#ifdef CONFIG_X86_64
	info->u.regs = *regs;
#else
	if (user_mode(regs))
		info->u.regs = *regs;
	else {
		memcpy(&info->u.regs, regs, offsetof(struct pt_regs, sp));
		info->u.regs.sp = (unsigned long)&regs->sp;
		info->u.regs.ss = __KERNEL_DS;
	}
#endif
}

static inline void arch_dwarf_init_blocked(struct unwind_state *info)
{
	struct inactive_task_frame *frame = (void *)info->task->thread.sp;

	probe_kernel_address(&frame->ret_addr, info->u.regs.ip);
	probe_kernel_address(&frame->bp, info->u.regs.bp);
	info->u.regs.sp = info->task->thread.sp;
	info->u.regs.cs = __KERNEL_CS;
	info->u.regs.ss = __KERNEL_DS;
#ifdef CONFIG_X86_32
	info->u.regs.ds = __USER_DS;
	info->u.regs.es = __USER_DS;
#endif
}

static inline int arch_dwarf_user_mode(struct unwind_state *info)
{
	return user_mode(&info->u.regs)
#ifdef CONFIG_X86_64
	       || (long)info->u.regs.ip >= 0
	       || (info->u.regs.ip >= VSYSCALL_ADDR &&
		   info->u.regs.ip < VSYSCALL_ADDR + PAGE_SIZE)
	       || (long)info->u.regs.sp >= 0;
#else
	       || info->u.regs.ip < PAGE_OFFSET
	       || info->u.regs.sp < PAGE_OFFSET;
#endif
}

#else /* CONFIG_DWARF_UNWIND */

#if 0 /* is this needed at all? */
#define DW_PC(frame) ((void)(frame), 0UL)
#define DW_SP(frame) ((void)(frame), 0UL)
#define DW_FP(frame) ((void)(frame), 0UL)
#endif

#endif /* CONFIG_DWARF_UNWIND */

#endif /* _ASM_X86_DWARF_H */
