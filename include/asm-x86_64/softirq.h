#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/pda.h>

#define __cpu_bh_enable() do { \
	barrier(); sub_pda(__local_bh_count,1); preempt_enable(); } while (0)
#define cpu_bh_disable() do { \
	preempt_disable(); add_pda(__local_bh_count,1); barrier(); } while (0)

#define local_bh_disable()	cpu_bh_disable()
#define __local_bh_enable()	__cpu_bh_enable()

#define in_softirq() (read_pda(__local_bh_count) != 0)

#define _local_bh_enable() do {							\
	asm volatile(								\
		"decl %%gs:%c1;"						\
		"jnz 1f;"							\
		"cmpl $0,%%gs:%c0;"						\
		"jnz 2f;"							\
		"1:;"								\
		".section .text.lock,\"ax\";"					\
		"2: call do_softirq_thunk;"					\
		"jmp 1b;"							\
		".previous" 							\
		:: "i" (pda_offset(__softirq_pending)), \
		   "i" (pda_offset(__local_bh_count)) : \
		"memory");	\
} while (0)
#define local_bh_enable() do { _local_bh_enable(); preempt_enable(); } while(0)
		
#endif	/* __ASM_SOFTIRQ_H */
