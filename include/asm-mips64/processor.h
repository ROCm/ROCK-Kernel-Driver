/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 Waldorf GMBH
 * Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000 Ralf Baechle
 * Modified further for R[236]000 compatibility by Paul M. Antoine
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PROCESSOR_H
#define _ASM_PROCESSOR_H

#include <linux/config.h>

/*
 * Return current * instruction pointer ("program counter").
 *
 * Two implementations.  The ``la'' version results in shorter code for
 * the kernel which we assume to reside in the 32-bit compat address space.
 * The  ``jal'' version is for use by modules which live in outer space.
 * This is just a single instruction unlike the long dla macro expansion.
 */
#ifdef MODULE
#define current_text_addr()						\
({									\
	void *_a;							\
									\
	__asm__ ("jal\t1f, 1f\n\t"					\
		"1:"							\
		: "=r" (_a));						\
									\
	_a;								\
})
#else
#define current_text_addr()						\
({									\
	void *_a;							\
									\
	__asm__ ("dla\t%0, 1f\n\t"					\
		"1:"							\
		: "=r" (_a));						\
									\
	_a;								\
})
#endif

#if !defined (_LANGUAGE_ASSEMBLY)
#include <asm/cachectl.h>
#include <asm/mipsregs.h>
#include <asm/reg.h>
#include <asm/system.h>

#if (defined(CONFIG_SGI_IP27))
#include <asm/sn/types.h>
#include <asm/sn/intr_public.h>
#endif

struct cpuinfo_mips {
	unsigned long udelay_val;
	unsigned long *pgd_quick;
	unsigned long *pmd_quick;
	unsigned long *pte_quick;
	unsigned long pgtable_cache_sz;
	unsigned long last_asn;
	unsigned long asid_cache;
#if defined(CONFIG_SGI_IP27)
	cpuid_t		p_cpuid;	/* PROM assigned cpuid */
	cnodeid_t	p_nodeid;	/* my node ID in compact-id-space */
	nasid_t		p_nasid;	/* my node ID in numa-as-id-space */
	unsigned char	p_slice;	/* Physical position on node board */
	hub_intmasks_t	p_intmasks;	/* SN0 per-CPU interrupt masks */
#endif
} __attribute__((aligned(128)));

/*
 * System setup and hardware flags..
 * XXX: Should go into mips_cpuinfo.
 */
extern char wait_available;		/* only available on R4[26]00 */
extern char cyclecounter_available;	/* only available from R4000 upwards. */
extern char dedicated_iv_available;	/* some embedded MIPS like Nevada */
extern char vce_available;		/* Supports VCED / VCEI exceptions */
extern char mips4_available;		/* CPU has MIPS IV ISA or better */

extern unsigned int vced_count, vcei_count;
extern struct cpuinfo_mips cpu_data[];

#ifdef CONFIG_SMP
#define current_cpu_data cpu_data[smp_processor_id()]
#else
#define current_cpu_data cpu_data[0]
#endif

/*
 * Bus types (default is ISA, but people can check others with these..)
 * MCA_bus hardcoded to 0 for now.
 *
 * This needs to be extended since MIPS systems are being delivered with
 * numerous different types of bus systems.
 */
extern int EISA_bus;
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * MIPS has no problems with write protection
 */
#define wp_works_ok 1
#define wp_works_ok__is_a_macro /* for versions in ksyms.c */

/* Lazy FPU handling on uni-processor */
extern struct task_struct *last_task_used_math;

#ifndef CONFIG_SMP
#define IS_FPU_OWNER()		(last_task_used_math == current)
#define CLEAR_FPU_OWNER()	last_task_used_math = NULL;
#else
#define IS_FPU_OWNER()		(current->flags & PF_USEDFPU)
#define CLEAR_FPU_OWNER()	current->flags &= ~PF_USEDFPU;
#endif

/*
 * User space process size: 1TB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.  TASK_SIZE
 * is limited to 1TB by the R4000 architecture; R10000 and better can
 * support 16TB.
 */
#define TASK_SIZE32	   0x80000000UL
#define TASK_SIZE	0x10000000000UL

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	((current->thread.mflags & MF_32BIT) ? \
	(TASK_SIZE32 / 3) : (TASK_SIZE / 3))

/*
 * Size of io_bitmap in longwords: 32 is ports 0-0x3ff.
 */
#define IO_BITMAP_SIZE	32

#define NUM_FPU_REGS	32

struct mips_fpu_hard_struct {
	unsigned long fp_regs[NUM_FPU_REGS];
	unsigned int control;
};

/*
 * FIXME: no fpu emulator yet (but who cares anyway?)
 */
struct mips_fpu_soft_struct {
	long	dummy;
};

union mips_fpu_union {
        struct mips_fpu_hard_struct hard;
        struct mips_fpu_soft_struct soft;
};

#define INIT_FPU { \
	{{0,},} \
}

typedef struct {
	unsigned long seg;
} mm_segment_t;

/*
 * If you change thread_struct remember to change the #defines below too!
 */
struct thread_struct {
        /* Saved main processor registers. */
        unsigned long reg16;
	unsigned long reg17, reg18, reg19, reg20, reg21, reg22, reg23;
        unsigned long reg29, reg30, reg31;

	/* Saved cp0 stuff. */
	unsigned long cp0_status;

	/* Saved fpu/fpu emulator stuff. */
	union mips_fpu_union fpu;

	/* Other stuff associated with the thread. */
	unsigned long cp0_badvaddr;	/* Last user fault */
	unsigned long cp0_baduaddr;	/* Last kernel fault accessing USEG */
	unsigned long error_code;
	unsigned long trap_no;
#define MF_FIXADE 1			/* Fix address errors in software */
#define MF_LOGADE 2			/* Log address errors to syslog */
#define MF_32BIT  4			/* Process is in 32-bit compat mode */
	unsigned long mflags;
	mm_segment_t current_ds;
	unsigned long irix_trampoline;  /* Wheee... */
	unsigned long irix_oldctx;
};

#endif /* !defined (_LANGUAGE_ASSEMBLY) */

#define INIT_MMAP { &init_mm, KSEG0, KSEG1, NULL, PAGE_SHARED, \
                    VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

#define INIT_THREAD  { \
        /* \
         * saved main processor registers \
         */ \
	0, 0, 0, 0, 0, 0, 0, 0, \
	               0, 0, 0, \
	/* \
	 * saved cp0 stuff \
	 */ \
	0, \
	/* \
	 * saved fpu/fpu emulator stuff \
	 */ \
	INIT_FPU, \
	/* \
	 * Other stuff associated with the process \
	 */ \
	0, 0, 0, 0, \
	/* \
	 * For now the default is to fix address errors \
	 */ \
	MF_FIXADE, { 0 }, 0, 0 \
}

#ifdef __KERNEL__

#define KERNEL_STACK_SIZE 0x4000

#if !defined (_LANGUAGE_ASSEMBLY)

/* Free all resources held by a thread. */
#define release_thread(thread) do { } while(0)

extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/* Copy and release all segment info associated with a VM */
#define copy_segments(p, mm) do { } while(0)
#define release_segments(mm) do { } while(0)

/*
 * Return saved PC of a blocked thread.
 */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	extern void ret_from_sys_call(void);

	/* New born processes are a special case */
	if (t->reg31 == (unsigned long) ret_from_sys_call)
		return t->reg31;

	return ((unsigned long*)t->reg29)[11];
}

#define user_mode(regs)	(((regs)->cp0_status & ST0_KSU) == KSU_USER)

/*
 * Do necessary setup to start up a newly executed thread.
 */
#define start_thread(regs, pc, sp) 					\
do {									\
	unsigned long __status;						\
									\
	/* New thread looses kernel privileges. */			\
	__status = regs->cp0_status & ~(ST0_CU0|ST0_FR|ST0_KSU);	\
	__status |= KSU_USER;						\
	__status |= (current->thread.mflags & MF_32BIT) ? 0 : ST0_FR;	\
	regs->cp0_status = __status;					\
	regs->cp0_epc = pc;						\
	regs->regs[29] = sp;						\
	current->thread.current_ds = USER_DS;				\
} while(0)

unsigned long get_wchan(struct task_struct *p);

#define __PT_REG(reg) ((long)&((struct pt_regs *)0)->reg - sizeof(struct pt_regs))
#define __KSTK_TOS(tsk) ((unsigned long)(tsk) + KERNEL_STACK_SIZE - 32)
#define KSTK_EIP(tsk) (*(unsigned long *)(__KSTK_TOS(tsk) + __PT_REG(cp0_epc)))
#define KSTK_ESP(tsk) (*(unsigned long *)(__KSTK_TOS(tsk) + __PT_REG(regs[29])))

/* Allocation and freeing of basic task resources. */
/*
 * NOTE! The task struct and the stack go together
 */
#define THREAD_SIZE (2*PAGE_SIZE)
#define alloc_task_struct() \
	((struct task_struct *) __get_free_pages(GFP_KERNEL, 2))
#define free_task_struct(p)	free_pages((unsigned long)(p), 2)
#define get_task_struct(tsk)	atomic_inc(&virt_to_page(tsk)->count)

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif /* !defined (_LANGUAGE_ASSEMBLY) */
#endif /* __KERNEL__ */

/*
 * Return_address is a replacement for __builtin_return_address(count)
 * which on certain architectures cannot reasonably be implemented in GCC
 * (MIPS, Alpha) or is unuseable with -fomit-frame-pointer (i386).
 * Note that __builtin_return_address(x>=1) is forbidden because GCC
 * aborts compilation on some CPUs.  It's simply not possible to unwind
 * some CPU's stackframes.
 *
 * In gcc 2.8 and newer  __builtin_return_address works only for non-leaf
 * functions.  We avoid the overhead of a function call by forcing the
 * compiler to save the return address register on the stack.
 */
#define return_address() ({__asm__ __volatile__("":::"$31");__builtin_return_address(0);})

#endif /* _ASM_PROCESSOR_H */
