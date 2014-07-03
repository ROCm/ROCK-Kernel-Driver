#ifndef _ASM_X86_UNWIND_H
#define _ASM_X86_UNWIND_H

/*
 * Copyright (C) 2002-2009 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 * This code is released under version 2 of the GNU GPL.
 */

#ifdef CONFIG_STACK_UNWIND

#include <linux/sched.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>

struct unwind_frame_info
{
	struct pt_regs regs;
	struct task_struct *task;
	unsigned call_frame:1;
};

#define UNW_PC(frame)      (frame)->regs.ip
#define UNW_SP(frame)      (frame)->regs.sp
#ifdef CONFIG_FRAME_POINTER
#define UNW_FP(frame)      (frame)->regs.bp
#define FRAME_LINK_OFFSET  0
#define STACK_BOTTOM(tsk)  STACK_LIMIT((tsk)->thread.sp0)
#define TSK_STACK_TOP(tsk) ((tsk)->thread.sp0)
#else
#define UNW_FP(frame)      ((void)(frame), 0UL)
#endif
/* On x86-64, might need to account for the special exception and interrupt
   handling stacks here, since normally
	EXCEPTION_STACK_ORDER < THREAD_ORDER < IRQSTACK_ORDER,
   but the construct is needed only for getting across the stack switch to
   the interrupt stack - thus considering the IRQ stack itself is unnecessary,
   and the overhead of comparing against all exception handling stacks seems
   not desirable. */
#define STACK_LIMIT(ptr)   (((ptr) - 1) & ~(THREAD_SIZE - 1))

#ifdef CONFIG_X86_64

#include <asm/vsyscall.h>

#define FRAME_RETADDR_OFFSET 8

#define UNW_REGISTER_INFO \
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

#define UNW_REGISTER_INFO \
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

#define UNW_DEFAULT_RA(raItem, dataAlign) \
	((raItem).where == Memory && \
	 !((raItem).value * (dataAlign) + sizeof(void *)))

static inline void arch_unw_init_frame_info(struct unwind_frame_info *info,
                                            /*const*/ struct pt_regs *regs)
{
#ifdef CONFIG_X86_64
	info->regs = *regs;
#else
	if (user_mode_vm(regs))
		info->regs = *regs;
	else {
		memcpy(&info->regs, regs, offsetof(struct pt_regs, sp));
		info->regs.sp = (unsigned long)&regs->sp;
		info->regs.ss = __KERNEL_DS;
	}
#endif
}

static inline void arch_unw_init_blocked(struct unwind_frame_info *info)
{
#ifdef CONFIG_X86_64
	extern const char thread_return[];

	memset(&info->regs, 0, sizeof(info->regs));
	info->regs.ip = (unsigned long)thread_return;
	info->regs.cs = __KERNEL_CS;
	probe_kernel_address(info->task->thread.sp, info->regs.bp);
	info->regs.sp = info->task->thread.sp;
	info->regs.ss = __KERNEL_DS;
#else
	memset(&info->regs, 0, sizeof(info->regs));
	info->regs.ip = info->task->thread.ip;
	info->regs.cs = __KERNEL_CS;
	probe_kernel_address(info->task->thread.sp, info->regs.bp);
	info->regs.sp = info->task->thread.sp;
	info->regs.ss = __KERNEL_DS;
	info->regs.ds = __USER_DS;
	info->regs.es = __USER_DS;
#endif
}

extern asmlinkage int
arch_unwind_init_running(struct unwind_frame_info *,
			 unwind_callback_fn,
			 const struct stacktrace_ops *, void *data);

static inline int arch_unw_user_mode(/*const*/ struct unwind_frame_info *info)
{
#ifdef CONFIG_X86_64
	return user_mode(&info->regs)
	       || (long)info->regs.ip >= 0
	       || (info->regs.ip >= VSYSCALL_ADDR &&
		   info->regs.ip < VSYSCALL_ADDR + PAGE_SIZE)
	       || (long)info->regs.sp >= 0;
#else
	return user_mode_vm(&info->regs)
	       || info->regs.ip < PAGE_OFFSET
	       || info->regs.sp < PAGE_OFFSET;
#endif
}

#else

#define UNW_PC(frame) ((void)(frame), 0UL)
#define UNW_SP(frame) ((void)(frame), 0UL)
#define UNW_FP(frame) ((void)(frame), 0UL)

static inline int arch_unw_user_mode(const void *info)
{
	return 0;
}

#endif

#endif /* _ASM_X86_UNWIND_H */
