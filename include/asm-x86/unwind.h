#ifndef _ASM_X86_UNWIND_H
#define _ASM_X86_UNWIND_H

/*
 * Copyright (C) 2002-2007 Novell, Inc.
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

#ifdef CONFIG_X86_64

#include <asm/vsyscall.h>

#define UNW_PC(frame)        (frame)->regs.rip
#define UNW_SP(frame)        (frame)->regs.rsp
#ifdef CONFIG_FRAME_POINTER
#define UNW_FP(frame)        (frame)->regs.rbp
#define FRAME_RETADDR_OFFSET 8
#define FRAME_LINK_OFFSET    0
#define STACK_BOTTOM(tsk)    (((tsk)->thread.rsp0 - 1) & ~(THREAD_SIZE - 1))
#define STACK_TOP(tsk)       ((tsk)->thread.rsp0)
#endif
/* Might need to account for the special exception and interrupt handling
   stacks here, since normally
	EXCEPTION_STACK_ORDER < THREAD_ORDER < IRQSTACK_ORDER,
   but the construct is needed only for getting across the stack switch to
   the interrupt stack - thus considering the IRQ stack itself is unnecessary,
   and the overhead of comparing against all exception handling stacks seems
   not desirable. */
#define STACK_LIMIT(ptr)     (((ptr) - 1) & ~(THREAD_SIZE - 1))

#define UNW_REGISTER_INFO \
	PTREGS_INFO(rax), \
	PTREGS_INFO(rdx), \
	PTREGS_INFO(rcx), \
	PTREGS_INFO(rbx), \
	PTREGS_INFO(rsi), \
	PTREGS_INFO(rdi), \
	PTREGS_INFO(rbp), \
	PTREGS_INFO(rsp), \
	PTREGS_INFO(r8), \
	PTREGS_INFO(r9), \
	PTREGS_INFO(r10), \
	PTREGS_INFO(r11), \
	PTREGS_INFO(r12), \
	PTREGS_INFO(r13), \
	PTREGS_INFO(r14), \
	PTREGS_INFO(r15), \
	PTREGS_INFO(rip)

#else

#include <asm/fixmap.h>

#define UNW_PC(frame)        (frame)->regs.eip
#define UNW_SP(frame)        (frame)->regs.esp
#ifdef CONFIG_FRAME_POINTER
#define UNW_FP(frame)        (frame)->regs.ebp
#define FRAME_RETADDR_OFFSET 4
#define FRAME_LINK_OFFSET    0
#define STACK_BOTTOM(tsk)    STACK_LIMIT((tsk)->thread.esp0)
#define STACK_TOP(tsk)       ((tsk)->thread.esp0)
#else
#define UNW_FP(frame) ((void)(frame), 0UL)
#endif
#define STACK_LIMIT(ptr)     (((ptr) - 1) & ~(THREAD_SIZE - 1))

#define UNW_REGISTER_INFO \
	PTREGS_INFO(eax), \
	PTREGS_INFO(ecx), \
	PTREGS_INFO(edx), \
	PTREGS_INFO(ebx), \
	PTREGS_INFO(esp), \
	PTREGS_INFO(ebp), \
	PTREGS_INFO(esi), \
	PTREGS_INFO(edi), \
	PTREGS_INFO(eip)

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
		memcpy(&info->regs, regs, offsetof(struct pt_regs, esp));
		info->regs.esp = (unsigned long)&regs->esp;
		info->regs.xss = __KERNEL_DS;
	}
#endif
}

static inline void arch_unw_init_blocked(struct unwind_frame_info *info)
{
#ifdef CONFIG_X86_64
	extern const char thread_return[];

	memset(&info->regs, 0, sizeof(info->regs));
	info->regs.rip = (unsigned long)thread_return;
	info->regs.cs = __KERNEL_CS;
	probe_kernel_address(info->task->thread.rsp, info->regs.rbp);
	info->regs.rsp = info->task->thread.rsp;
	info->regs.ss = __KERNEL_DS;
#else
	memset(&info->regs, 0, sizeof(info->regs));
	info->regs.eip = info->task->thread.eip;
	info->regs.xcs = __KERNEL_CS;
	probe_kernel_address(info->task->thread.esp, info->regs.ebp);
	info->regs.esp = info->task->thread.esp;
	info->regs.xss = __KERNEL_DS;
	info->regs.xds = __USER_DS;
	info->regs.xes = __USER_DS;
#endif
}

extern asmlinkage int
arch_unwind_init_running(struct unwind_frame_info *,
                         asmlinkage int (*callback)(struct unwind_frame_info *,
                                                    void *arg),
                         void *arg);

static inline int arch_unw_user_mode(/*const*/ struct unwind_frame_info *info)
{
#ifdef CONFIG_X86_64
	return user_mode(&info->regs)
	       || (long)info->regs.rip >= 0
	       || (info->regs.rip >= VSYSCALL_START && info->regs.rip < VSYSCALL_END)
	       || (long)info->regs.rsp >= 0;
#else
	return user_mode_vm(&info->regs)
	       || info->regs.eip < PAGE_OFFSET
	       || (info->regs.eip >= __fix_to_virt(FIX_VDSO)
	           && info->regs.eip < __fix_to_virt(FIX_VDSO) + PAGE_SIZE)
	       || info->regs.esp < PAGE_OFFSET;
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
