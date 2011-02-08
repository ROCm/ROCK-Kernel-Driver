#ifndef _ASM_X86_PERF_EVENT_H
#define _ASM_X86_PERF_EVENT_H

#ifdef CONFIG_PERF_EVENTS

/*
 * Abuse bit 3 of the cpu eflags register to indicate proper PEBS IP fixups.
 * This flag is otherwise unused and ABI specified to be 0, so nobody should
 * care what we do with it.
 */
#define PERF_EFLAGS_EXACT	(1UL << 3)

#define perf_instruction_pointer(regs) instruction_pointer(regs)

#define perf_misc_flags(regs) ({ \
	struct pt_regs *_r_ = (regs); \
	unsigned long _f_ = user_mode(_r_) ? PERF_RECORD_MISC_USER \
					   : PERF_RECORD_MISC_KERNEL; \
	_r_->flags & PERF_EFLAGS_EXACT ? _f_ | PERF_RECORD_MISC_EXACT_IP : _f_; \
})

#include <asm/stacktrace.h>

/*
 * We abuse bit 3 from flags to pass exact information, see perf_misc_flags
 * and the comment with PERF_EFLAGS_EXACT.
 */
#define perf_arch_fetch_caller_regs(regs, __ip)		{	\
	(regs)->ip = (__ip);					\
	(regs)->bp = caller_frame_pointer();			\
	(regs)->cs = __KERNEL_CS;				\
	regs->flags = 0;					\
}

#endif

#endif /* _ASM_X86_PERF_EVENT_H */
