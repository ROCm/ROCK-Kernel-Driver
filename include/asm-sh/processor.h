/*
 * include/asm-sh/processor.h
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka
 * Copyright (C) 2002 Paul Mundt
 */

#ifndef __ASM_SH_PROCESSOR_H
#define __ASM_SH_PROCESSOR_H

#include <asm/page.h>
#include <asm/types.h>
#include <asm/cache.h>
#include <linux/threads.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; __asm__("mova	1f, %0\n1:":"=z" (pc)); pc; })

/* Core Processor Version Register */
#define CCN_PVR		0xff000030
#define CCN_PRR		0xff000044
/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 */
enum cpu_type {
	CPU_SH7604,		/* Represents 7604 */
	CPU_SH7708,		/* Represents 7707, 7708, 7708S, 7708R, 7709 */
	CPU_SH7729,		/* Represents 7709A, 7729 */
	CPU_SH7750,     	/* Represents 7750 */
	CPU_SH7750S,		/* Represents 7750S */
	CPU_SH7750R,		/* Represents 7750R */
	CPU_SH7751,		/* Represents 7751 */
	CPU_SH7751R,		/* Represents 7751R */
	CPU_ST40RA,		/* Represents ST40RA (formerly ST40STB1) */
	CPU_ST40GX1,		/* Represents ST40GX1 */
	CPU_SH_NONE
};

struct sh_cpuinfo {
	enum cpu_type type;
	char	hard_math;
	unsigned long loops_per_jiffy;

	unsigned int cpu_clock, master_clock, bus_clock, module_clock;
#ifdef CONFIG_CPU_SUBTYPE_ST40STB1
	unsigned int memory_clock;
#endif

	struct cache_info icache;
	struct cache_info dcache;

	unsigned long flags;
};

#ifdef CONFIG_SMP
extern struct sh_cpuinfo cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#else
extern struct sh_cpuinfo boot_cpu_data;

#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data
#endif

/*
 * User space process size: 2GB.
 *
 * Since SH7709 and SH7750 have "area 7", we can't use 0x7c000000--0x7fffffff
 */
#define TASK_SIZE	0x7c000000UL

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 3)

/*
 * Bit of SR register
 *
 * FD-bit:
 *     When it's set, it means the processor doesn't have right to use FPU,
 *     and it results exception when the floating operation is executed.
 *
 * IMASK-bit:
 *     Interrupt level mask
 */
#define SR_FD    0x00008000
#define SR_IMASK 0x000000f0

/*
 * FPU structure and data
 */

struct sh_fpu_hard_struct {
	unsigned long fp_regs[16];
	unsigned long xfp_regs[16];
	unsigned long fpscr;
	unsigned long fpul;

	long status; /* software status information */
};

/* Dummy fpu emulator  */
struct sh_fpu_soft_struct {
	unsigned long fp_regs[16];
	unsigned long xfp_regs[16];
	unsigned long fpscr;
	unsigned long fpul;

	unsigned char lookahead;
	unsigned long entry_pc;
};

union sh_fpu_union {
	struct sh_fpu_hard_struct hard;
	struct sh_fpu_soft_struct soft;
};

#define CPU_HAS_FPU	0x0001

struct thread_struct {
	unsigned long sp;
	unsigned long pc;

	unsigned long trap_no, error_code;
	unsigned long address;
	/* Hardware debugging registers may come here */

	/* floating point info */
	union sh_fpu_union fpu;
};

#define INIT_THREAD  {						\
	sizeof(init_stack) + (long) &init_stack, /* sp */	\
	0,					 /* pc */	\
	0, 0, 							\
	0, 							\
	{{{0,}},} 				/* fpu state */	\
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
#define start_thread(regs, new_pc, new_sp)	 \
	set_fs(USER_DS);			 \
	regs->pr = 0;   		 	 \
	regs->sr = 0;		/* User mode. */ \
	regs->pc = new_pc;			 \
	regs->regs[15] = new_sp

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

/*
 * create a kernel thread without removing it from tasklists
 */
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/*
 * Bus types
 */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/* Copy and release all segment info associated with a VM */
#define copy_segments(p, mm)	do { } while(0)
#define release_segments(mm)	do { } while(0)

/*
 * FPU lazy state save handling.
 */

static __inline__ void release_fpu(void)
{
	unsigned long __dummy;

	/* Set FD flag in SR */
	__asm__ __volatile__("stc	sr, %0\n\t"
			     "or	%1, %0\n\t"
			     "ldc	%0, sr"
			     : "=&r" (__dummy)
			     : "r" (SR_FD));
}

static __inline__ void grab_fpu(void)
{
	unsigned long __dummy;

	/* Clear out FD flag in SR */
	__asm__ __volatile__("stc	sr, %0\n\t"
			     "and	%1, %0\n\t"
			     "ldc	%0, sr"
			     : "=&r" (__dummy)
			     : "r" (~SR_FD));
}

#ifdef CONFIG_CPU_SH4
extern void save_fpu(struct task_struct *__tsk);
#else
#define save_fpu(tsk)	do { } while (0)
#endif

#define unlazy_fpu(tsk) do { 				\
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) {	\
		save_fpu(tsk); 				\
	}						\
} while (0)

#define clear_fpu(tsk) do { 					\
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) { 		\
		clear_tsk_thread_flag(tsk, TIF_USEDFPU); 	\
		release_fpu();					\
	}							\
} while (0)

/* Double presision, NANS as NANS, rounding to nearest, no exceptions */
#define FPSCR_INIT  0x00080000

#define	FPSCR_CAUSE_MASK	0x0001f000	/* Cause bits */
#define	FPSCR_FLAG_MASK		0x0000007c	/* Flag bits */

/*
 * Return saved PC of a blocked thread.
 */
#define thread_saved_pc(tsk)	(tsk->thread.pc)

extern unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)  ((tsk)->thread.pc)
#define KSTK_ESP(tsk)  ((tsk)->thread.sp)

#define cpu_relax()	barrier()

#endif /* __ASM_SH_PROCESSOR_H */
