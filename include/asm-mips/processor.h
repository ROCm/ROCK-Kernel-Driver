/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 Waldorf GMBH
 * Copyright (C) 1995, 1996, 1997, 1998, 1999, 2001, 2002, 2003 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PROCESSOR_H
#define _ASM_PROCESSOR_H

#include <linux/config.h>
#include <linux/cache.h>
#include <linux/threads.h>

#include <asm/cachectl.h>
#include <asm/cpu.h>
#include <asm/mipsregs.h>
#include <asm/prefetch.h>
#include <asm/system.h>

#ifdef CONFIG_SGI_IP27
#include <asm/sn/types.h>
#endif

/*
 * Descriptor for a cache
 */
struct cache_desc {
	unsigned short linesz;	/* Size of line in bytes */
	unsigned short ways;	/* Number of ways */
	unsigned short sets;	/* Number of lines per set */
	unsigned int waysize;	/* Bytes per way */
	unsigned int waybit;	/* Bits to select in a cache set */
	unsigned int flags;	/* Flags describing cache properties */
};

/*
 * Flag definitions
 */
#define MIPS_CACHE_NOT_PRESENT	0x00000001
#define MIPS_CACHE_VTAG		0x00000002	/* Virtually tagged cache */
#define MIPS_CACHE_ALIASES	0x00000004	/* Cache could have aliases */
#define MIPS_CACHE_IC_F_DC	0x00000008	/* Ic can refill from D-cache */

struct cpuinfo_mips {
	unsigned long		udelay_val;
	unsigned long		asid_cache;
#if defined(CONFIG_SGI_IP27)
//	cpuid_t		p_cpuid;	/* PROM assigned cpuid */
	cnodeid_t	p_nodeid;	/* my node ID in compact-id-space */
	nasid_t		p_nasid;	/* my node ID in numa-as-id-space */
	unsigned char	p_slice;	/* Physical position on node board */
#endif
#if 0
	unsigned long		loops_per_sec;
	unsigned long		ipi_count;
	unsigned long		irq_attempt[NR_IRQS];
	unsigned long		smp_local_irq_count;
	unsigned long		prof_multiplier;
	unsigned long		prof_counter;
#endif

	/*
	 * Capability and feature descriptor structure for MIPS CPU
	 */
	unsigned long		options;
	unsigned int		processor_id;
	unsigned int		fpu_id;
	unsigned int		cputype;
	int			isa_level;
	int			tlbsize;
	struct cache_desc	icache;	/* Primary I-cache */
	struct cache_desc	dcache;	/* Primary D or combined I/D cache */
	struct cache_desc	scache;	/* Secondary cache */
	struct cache_desc	tcache;	/* Tertiary/split secondary cache */
	void 			*data;	/* Additional data */
} __attribute__((aligned(SMP_CACHE_BYTES)));

extern struct cpuinfo_mips cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]

extern void cpu_probe(void);
extern void cpu_report(void);

/*
 * Return current * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

/*
 * System setup and hardware flags..
 */
extern void (*cpu_wait)(void);

extern unsigned int vced_count, vcei_count;

/*
 * Bus types (default is ISA, but people can check others with these..)
 */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

#ifdef CONFIG_MIPS32
/*
 * User space process size: 2GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 */
#define TASK_SIZE	0x7fff8000UL

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 3))
#endif

#ifdef CONFIG_MIPS64
/*
 * User space process size: 1TB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.  TASK_SIZE
 * is limited to 1TB by the R4000 architecture; R10000 and better can
 * support 16TB; the architectural reserve for future expansion is
 * 8192EB ...
 */
#define TASK_SIZE32	0x7fff8000UL
#define TASK_SIZE	0x10000000000UL

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	((current->thread.mflags & MF_32BIT_ADDR) ? \
	PAGE_ALIGN(TASK_SIZE32 / 3) : PAGE_ALIGN(TASK_SIZE / 3))
#endif

/*
 * Size of io_bitmap in longwords: 32 is ports 0-0x3ff.
 */
#define IO_BITMAP_SIZE	32

#define NUM_FPU_REGS	32

typedef u64 fpureg_t;

struct mips_fpu_hard_struct {
	fpureg_t	fpr[NUM_FPU_REGS];
	unsigned int	fcr31;
};

/*
 * It would be nice to add some more fields for emulator statistics, but there
 * are a number of fixed offsets in offset.h and elsewhere that would have to
 * be recalculated by hand.  So the additional information will be private to
 * the FPU emulator for now.  See asm-mips/fpu_emulator.h.
 */

struct mips_fpu_soft_struct {
	fpureg_t	fpr[NUM_FPU_REGS];
	unsigned int	fcr31;
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

#define ARCH_MIN_TASKALIGN	8

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
#define MF_FIXADE	1		/* Fix address errors in software */
#define MF_LOGADE	2		/* Log address errors to syslog */
#define MF_32BIT_REGS	4		/* also implies 16/32 fprs */
#define MF_32BIT_ADDR	8		/* 32-bit address space (o32/n32) */
	unsigned long mflags;
	unsigned long irix_trampoline;  /* Wheee... */
	unsigned long irix_oldctx;
};

#define MF_ABI_MASK	(MF_32BIT_REGS | MF_32BIT_ADDR)
#define MF_O32		(MF_32BIT_REGS | MF_32BIT_ADDR)
#define MF_N32		MF_32BIT_ADDR
#define MF_N64		0

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
	MF_FIXADE, 0, 0 \
}

#ifdef __KERNEL__

struct task_struct;

/* Free all resources held by a thread. */
#define release_thread(thread) do { } while(0)

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

extern long kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

extern unsigned long thread_saved_pc(struct task_struct *tsk);

/*
 * Do necessary setup to start up a newly executed thread.
 */
extern void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp);

unsigned long get_wchan(struct task_struct *p);

#define __PT_REG(reg) ((long)&((struct pt_regs *)0)->reg - sizeof(struct pt_regs))
#define __KSTK_TOS(tsk) ((unsigned long)(tsk->thread_info) + THREAD_SIZE - 32)
#define KSTK_EIP(tsk) (*(unsigned long *)(__KSTK_TOS(tsk) + __PT_REG(cp0_epc)))
#define KSTK_ESP(tsk) (*(unsigned long *)(__KSTK_TOS(tsk) + __PT_REG(regs[29])))
#define KSTK_STATUS(tsk) (*(unsigned long *)(__KSTK_TOS(tsk) + __PT_REG(cp0_status)))

#define cpu_relax()	barrier()

#endif /* __KERNEL__ */

/*
 * Return_address is a replacement for __builtin_return_address(count)
 * which on certain architectures cannot reasonably be implemented in GCC
 * (MIPS, Alpha) or is unuseable with -fomit-frame-pointer (i386).
 * Note that __builtin_return_address(x>=1) is forbidden because GCC
 * aborts compilation on some CPUs.  It's simply not possible to unwind
 * some CPU's stackframes.
 *
 * __builtin_return_address works only for non-leaf functions.  We avoid the
 * overhead of a function call by forcing the compiler to save the return
 * address register on the stack.
 */
#define return_address() ({__asm__ __volatile__("":::"$31");__builtin_return_address(0);})

/*
 * For now.  The 32-bit cycle counter is screwed up so solving this nicely takes a little
 * brainwork ...
 */
static inline unsigned long long sched_clock(void)
{
	return 0ULL;
}

#ifdef CONFIG_CPU_HAS_PREFETCH

#define ARCH_HAS_PREFETCH

extern inline void prefetch(const void *addr)
{
	__asm__ __volatile__(
	"	.set	mips4		\n"
	"	pref	%0, (%1)	\n"
	"	.set	mips0		\n"
	:
	: "i" (Pref_Load), "r" (addr));
}

#endif

#endif /* _ASM_PROCESSOR_H */
