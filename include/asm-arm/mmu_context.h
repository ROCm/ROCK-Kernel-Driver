/*
 *  linux/include/asm-arm/mmu_context.h
 *
 *  Copyright (C) 1996 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 */
#ifndef __ASM_ARM_MMU_CONTEXT_H
#define __ASM_ARM_MMU_CONTEXT_H

#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/arch/memory.h>
#include <asm/proc-fns.h>

#define destroy_context(mm)		do { } while(0)
#define init_new_context(tsk,mm)	0

/*
 * This is called when "tsk" is about to enter lazy TLB mode.
 *
 * mm:  describes the currently active mm context
 * tsk: task which is entering lazy tlb
 * cpu: cpu number which is entering lazy tlb
 *
 * tsk->mm will be NULL
 */
static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}

/*
 * This is the actual mm switch as far as the scheduler
 * is concerned.  No registers are touched.
 */
static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk, unsigned int cpu)
{
	if (prev != next)
		cpu_switch_mm(next->pgd, tsk);
}

#define activate_mm(prev, next) \
	switch_mm((prev),(next),NULL,smp_processor_id())

/*
 * Find first bit set in a 168-bit bitmap, where the first
 * 128 bits are unlikely to be set.
 */
static inline int sched_find_first_bit(unsigned long *b)
{
#if MAX_RT_PRIO != 128 || MAX_PRIO != 168
#error update this function
#endif

	if (unlikely(b[0]))
		return __ffs(b[0]);
	if (unlikely(b[1]))
		return __ffs(b[1]) + 32;
	if (unlikely(b[2]))
		return __ffs(b[2]) + 64;
	if (unlikely(b[3]))
		return __ffs(b[3]) + 96;
	if (b[4])
		return __ffs(b[4]) + MAX_RT_PRIO;
	return __ffs(b[5]) + 32 + MAX_RT_PRIO;
}

#endif
