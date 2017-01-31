#ifndef _ASM_X86_UNWIND_H
#define _ASM_X86_UNWIND_H

#include <linux/sched.h>
#include <linux/ftrace.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

struct unwind_state {
	struct stack_info stack_info;
	unsigned long stack_mask;
	struct task_struct *task;
	int graph_idx;
#ifdef CONFIG_DWARF_UNWIND
	union {
		struct pt_regs regs;
		char regs_arr[sizeof(struct pt_regs)];
	} u;
	unsigned call_frame:1,
		 on_cpu:1;
	unsigned long dw_sp;
#elif defined(CONFIG_FRAME_POINTER)
	unsigned long *bp, *orig_sp;
	struct pt_regs *regs;
#else
	unsigned long *sp;
#endif
};

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs, unsigned long *first_frame);

bool unwind_next_frame(struct unwind_state *state);

unsigned long unwind_get_return_address(struct unwind_state *state);

static inline bool unwind_done(struct unwind_state *state)
{
	return state->stack_info.type == STACK_TYPE_UNKNOWN;
}

static inline
void unwind_start(struct unwind_state *state, struct task_struct *task,
		  struct pt_regs *regs, unsigned long *first_frame)
{
	first_frame = first_frame ? : get_stack_pointer(task, regs);

	__unwind_start(state, task, regs, first_frame);
}

#ifdef CONFIG_DWARF_UNWIND

#define UNW_PC(frame)      ((frame)->u.regs.ip)
#define UNW_SP(frame)      ((frame)->u.regs.sp)
#ifdef CONFIG_FRAME_POINTER
#define UNW_FP(frame)      ((frame)->u.regs.bp)
#define FRAME_LINK_OFFSET  0
#define STACK_BOTTOM(tsk)  STACK_LIMIT((tsk)->thread.sp0)
#define TSK_STACK_TOP(tsk) ((tsk)->thread.sp0)
#else
#define UNW_FP(frame)      ((void)(frame), 0UL)
#endif
/*
 * On x86-64, might need to account for the special exception and interrupt
 * handling stacks here, since normally
 *	EXCEPTION_STACK_ORDER < THREAD_ORDER < IRQSTACK_ORDER,
 * but the construct is needed only for getting across the stack switch to
 * the interrupt stack - thus considering the IRQ stack itself is unnecessary,
 * and the overhead of comparing against all exception handling stacks seems
 * not desirable.
 */
#define STACK_LIMIT(ptr)   (((ptr) - 1) & ~(THREAD_SIZE - 1))

static inline
unsigned long *unwind_get_return_address_ptr(struct unwind_state *state)
{
	if (unwind_done(state))
		return NULL;

	return &UNW_PC(state);
}

static inline struct pt_regs *unwind_get_entry_regs(struct unwind_state *state)
{
	if (unwind_done(state))
		return NULL;

	return NULL;//&state->u.regs;
}

#elif defined(CONFIG_FRAME_POINTER)

static inline
unsigned long *unwind_get_return_address_ptr(struct unwind_state *state)
{
	if (unwind_done(state))
		return NULL;

	return state->regs ? &state->regs->ip : state->bp + 1;
}

static inline struct pt_regs *unwind_get_entry_regs(struct unwind_state *state)
{
	if (unwind_done(state))
		return NULL;

	return state->regs;
}

#else /* !CONFIG_FRAME_POINTER */

static inline
unsigned long *unwind_get_return_address_ptr(struct unwind_state *state)
{
	return NULL;
}

static inline struct pt_regs *unwind_get_entry_regs(struct unwind_state *state)
{
	return NULL;
}

#endif /* CONFIG_FRAME_POINTER */

/*
 * Copyright (C) 2002-2009 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 * The code below is released under version 2 of the GNU GPL.
 */

#ifdef CONFIG_DWARF_UNWIND

#include <linux/uaccess.h>

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

#else

#define UNW_PC(frame) ((void)(frame), 0UL)
#define UNW_SP(frame) ((void)(frame), 0UL)
#define UNW_FP(frame) ((void)(frame), 0UL)

static inline int arch_dwarf_user_mode(const void *info)
{
	return 0;
}

#endif

#endif /* _ASM_X86_UNWIND_H */
