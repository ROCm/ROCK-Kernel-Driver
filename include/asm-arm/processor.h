/*
 *  linux/include/asm-arm/processor.h
 *
 *  Copyright (C) 1995 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_PROCESSOR_H
#define __ASM_ARM_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#ifdef __KERNEL__

#define EISA_bus 0
#define MCA_bus 0
#define MCA_bus__is_a_macro

#include <asm/atomic.h>
#include <asm/ptrace.h>
#include <asm/procinfo.h>
#include <asm/arch/memory.h>
#include <asm/proc/processor.h>
#include <asm/types.h>

union debug_insn {
	u32	arm;
	u16	thumb;
};

struct debug_entry {
	u32			address;
	union debug_insn	insn;
};

struct debug_info {
	int			nsaved;
	struct debug_entry	bp[2];
};

struct thread_struct {
							/* fault info	  */
	unsigned long		address;
	unsigned long		trap_no;
	unsigned long		error_code;
							/* debugging	  */
	struct debug_info	debug;
};

#define INIT_THREAD  {	}

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

unsigned long get_wchan(struct task_struct *p);

#define cpu_relax()			barrier()

/*
 * Create a new kernel thread
 */
extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

/*
 * Prefetching support - only ARMv5.
 */
#if __LINUX_ARM_ARCH__ >= 5

#define ARCH_HAS_PREFETCH
#define prefetch(ptr)				\
	({					\
		__asm__ __volatile__(		\
		"pld\t%0"			\
		:				\
		: "o" (*(char *)(ptr))		\
		: "cc");			\
	})

#define ARCH_HAS_PREFETCHW
#define prefetchw(ptr)	prefetch(ptr)

#define ARCH_HAS_SPINLOCK_PREFETCH
#define spin_lock_prefetch(x) do { } while (0)

#endif

#endif

#endif /* __ASM_ARM_PROCESSOR_H */
