#ifndef _ASM_IA64_PROCESSOR_H
#define _ASM_IA64_PROCESSOR_H

/*
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1998-2000 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 *
 * 11/24/98	S.Eranian	added ia64_set_iva()
 * 12/03/99	D. Mosberger	implement thread_saved_pc() via kernel unwind API
 * 06/16/00	A. Mallick	added csd/ssd/tssd for ia32 support
 */

#include <linux/config.h>

#include <asm/ptrace.h>
#include <asm/types.h>

#define IA64_NUM_DBG_REGS	8
/*
 * Limits for PMC and PMD are set to less than maximum architected values
 * but should be sufficient for a while
 */
#define IA64_NUM_PMC_REGS	32
#define IA64_NUM_PMD_REGS	32
#define IA64_NUM_PMD_COUNTERS	4

/*
 * TASK_SIZE really is a mis-named.  It really is the maximum user
 * space address (plus one).  On IA-64, there are five regions of 2TB
 * each (assuming 8KB page size), for a total of 8TB of user virtual
 * address space.
 */
#define TASK_SIZE		(current->thread.task_size)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(current->thread.map_base)

/*
 * Bus types
 */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/* Processor status register bits: */
#define IA64_PSR_BE_BIT		1
#define IA64_PSR_UP_BIT		2
#define IA64_PSR_AC_BIT		3
#define IA64_PSR_MFL_BIT	4
#define IA64_PSR_MFH_BIT	5
#define IA64_PSR_IC_BIT		13
#define IA64_PSR_I_BIT		14
#define IA64_PSR_PK_BIT		15
#define IA64_PSR_DT_BIT		17
#define IA64_PSR_DFL_BIT	18
#define IA64_PSR_DFH_BIT	19
#define IA64_PSR_SP_BIT		20
#define IA64_PSR_PP_BIT		21
#define IA64_PSR_DI_BIT		22
#define IA64_PSR_SI_BIT		23
#define IA64_PSR_DB_BIT		24
#define IA64_PSR_LP_BIT		25
#define IA64_PSR_TB_BIT		26
#define IA64_PSR_RT_BIT		27
/* The following are not affected by save_flags()/restore_flags(): */
#define IA64_PSR_CPL0_BIT	32
#define IA64_PSR_CPL1_BIT	33
#define IA64_PSR_IS_BIT		34
#define IA64_PSR_MC_BIT		35
#define IA64_PSR_IT_BIT		36
#define IA64_PSR_ID_BIT		37
#define IA64_PSR_DA_BIT		38
#define IA64_PSR_DD_BIT		39
#define IA64_PSR_SS_BIT		40
#define IA64_PSR_RI_BIT		41
#define IA64_PSR_ED_BIT		43
#define IA64_PSR_BN_BIT		44

#define IA64_PSR_BE	(__IA64_UL(1) << IA64_PSR_BE_BIT)
#define IA64_PSR_UP	(__IA64_UL(1) << IA64_PSR_UP_BIT)
#define IA64_PSR_AC	(__IA64_UL(1) << IA64_PSR_AC_BIT)
#define IA64_PSR_MFL	(__IA64_UL(1) << IA64_PSR_MFL_BIT)
#define IA64_PSR_MFH	(__IA64_UL(1) << IA64_PSR_MFH_BIT)
#define IA64_PSR_IC	(__IA64_UL(1) << IA64_PSR_IC_BIT)
#define IA64_PSR_I	(__IA64_UL(1) << IA64_PSR_I_BIT)
#define IA64_PSR_PK	(__IA64_UL(1) << IA64_PSR_PK_BIT)
#define IA64_PSR_DT	(__IA64_UL(1) << IA64_PSR_DT_BIT)
#define IA64_PSR_DFL	(__IA64_UL(1) << IA64_PSR_DFL_BIT)
#define IA64_PSR_DFH	(__IA64_UL(1) << IA64_PSR_DFH_BIT)
#define IA64_PSR_SP	(__IA64_UL(1) << IA64_PSR_SP_BIT)
#define IA64_PSR_PP	(__IA64_UL(1) << IA64_PSR_PP_BIT)
#define IA64_PSR_DI	(__IA64_UL(1) << IA64_PSR_DI_BIT)
#define IA64_PSR_SI	(__IA64_UL(1) << IA64_PSR_SI_BIT)
#define IA64_PSR_DB	(__IA64_UL(1) << IA64_PSR_DB_BIT)
#define IA64_PSR_LP	(__IA64_UL(1) << IA64_PSR_LP_BIT)
#define IA64_PSR_TB	(__IA64_UL(1) << IA64_PSR_TB_BIT)
#define IA64_PSR_RT	(__IA64_UL(1) << IA64_PSR_RT_BIT)
/* The following are not affected by save_flags()/restore_flags(): */
#define IA64_PSR_IS	(__IA64_UL(1) << IA64_PSR_IS_BIT)
#define IA64_PSR_MC	(__IA64_UL(1) << IA64_PSR_MC_BIT)
#define IA64_PSR_IT	(__IA64_UL(1) << IA64_PSR_IT_BIT)
#define IA64_PSR_ID	(__IA64_UL(1) << IA64_PSR_ID_BIT)
#define IA64_PSR_DA	(__IA64_UL(1) << IA64_PSR_DA_BIT)
#define IA64_PSR_DD	(__IA64_UL(1) << IA64_PSR_DD_BIT)
#define IA64_PSR_SS	(__IA64_UL(1) << IA64_PSR_SS_BIT)
#define IA64_PSR_RI	(__IA64_UL(3) << IA64_PSR_RI_BIT)
#define IA64_PSR_ED	(__IA64_UL(1) << IA64_PSR_ED_BIT)
#define IA64_PSR_BN	(__IA64_UL(1) << IA64_PSR_BN_BIT)

/* User mask bits: */
#define IA64_PSR_UM	(IA64_PSR_BE | IA64_PSR_UP | IA64_PSR_AC | IA64_PSR_MFL | IA64_PSR_MFH)

/* Default Control Register */
#define IA64_DCR_PP_BIT		 0	/* privileged performance monitor default */
#define IA64_DCR_BE_BIT		 1	/* big-endian default */
#define IA64_DCR_LC_BIT		 2	/* ia32 lock-check enable */
#define IA64_DCR_DM_BIT		 8	/* defer TLB miss faults */
#define IA64_DCR_DP_BIT		 9	/* defer page-not-present faults */
#define IA64_DCR_DK_BIT		10	/* defer key miss faults */
#define IA64_DCR_DX_BIT		11	/* defer key permission faults */
#define IA64_DCR_DR_BIT		12	/* defer access right faults */
#define IA64_DCR_DA_BIT		13	/* defer access bit faults */
#define IA64_DCR_DD_BIT		14	/* defer debug faults */

#define IA64_DCR_PP	(__IA64_UL(1) << IA64_DCR_PP_BIT)
#define IA64_DCR_BE	(__IA64_UL(1) << IA64_DCR_BE_BIT)
#define IA64_DCR_LC	(__IA64_UL(1) << IA64_DCR_LC_BIT)
#define IA64_DCR_DM	(__IA64_UL(1) << IA64_DCR_DM_BIT)
#define IA64_DCR_DP	(__IA64_UL(1) << IA64_DCR_DP_BIT)
#define IA64_DCR_DK	(__IA64_UL(1) << IA64_DCR_DK_BIT)
#define IA64_DCR_DX	(__IA64_UL(1) << IA64_DCR_DX_BIT)
#define IA64_DCR_DR	(__IA64_UL(1) << IA64_DCR_DR_BIT)
#define IA64_DCR_DA	(__IA64_UL(1) << IA64_DCR_DA_BIT)
#define IA64_DCR_DD	(__IA64_UL(1) << IA64_DCR_DD_BIT)

/* Interrupt Status Register */
#define IA64_ISR_X_BIT		32	/* execute access */
#define IA64_ISR_W_BIT		33	/* write access */
#define IA64_ISR_R_BIT		34	/* read access */
#define IA64_ISR_NA_BIT		35	/* non-access */
#define IA64_ISR_SP_BIT		36	/* speculative load exception */
#define IA64_ISR_RS_BIT		37	/* mandatory register-stack exception */
#define IA64_ISR_IR_BIT		38	/* invalid register frame exception */

#define IA64_ISR_X	(__IA64_UL(1) << IA64_ISR_X_BIT)
#define IA64_ISR_W	(__IA64_UL(1) << IA64_ISR_W_BIT)
#define IA64_ISR_R	(__IA64_UL(1) << IA64_ISR_R_BIT)
#define IA64_ISR_NA	(__IA64_UL(1) << IA64_ISR_NA_BIT)
#define IA64_ISR_SP	(__IA64_UL(1) << IA64_ISR_SP_BIT)
#define IA64_ISR_RS	(__IA64_UL(1) << IA64_ISR_RS_BIT)
#define IA64_ISR_IR	(__IA64_UL(1) << IA64_ISR_IR_BIT)

#define IA64_THREAD_FPH_VALID	(__IA64_UL(1) << 0)	/* floating-point high state valid? */
#define IA64_THREAD_DBG_VALID	(__IA64_UL(1) << 1)	/* debug registers valid? */
#define IA64_THREAD_PM_VALID	(__IA64_UL(1) << 2)	/* performance registers valid? */
#define IA64_THREAD_UAC_NOPRINT	(__IA64_UL(1) << 3)	/* don't log unaligned accesses */
#define IA64_THREAD_UAC_SIGBUS	(__IA64_UL(1) << 4)	/* generate SIGBUS on unaligned acc. */
#define IA64_THREAD_KRBS_SYNCED	(__IA64_UL(1) << 5)	/* krbs synced with process vm? */
#define IA64_THREAD_MAP_SHARED	(__IA64_UL(1) << 6)	/* ugly: just a tmp flag for mmap() */
#define IA64_KERNEL_DEATH	(__IA64_UL(1) << 63)	/* see die_if_kernel()... */

#define IA64_THREAD_UAC_SHIFT	3
#define IA64_THREAD_UAC_MASK	(IA64_THREAD_UAC_NOPRINT | IA64_THREAD_UAC_SIGBUS)

#ifndef __ASSEMBLY__

#include <linux/smp.h>
#include <linux/threads.h>

#include <asm/fpu.h>
#include <asm/offsets.h>
#include <asm/page.h>
#include <asm/rse.h>
#include <asm/unwind.h>

/* like above but expressed as bitfields for more efficient access: */
struct ia64_psr {
	__u64 reserved0 : 1;
	__u64 be : 1;
	__u64 up : 1;
	__u64 ac : 1;
	__u64 mfl : 1;
	__u64 mfh : 1;
	__u64 reserved1 : 7;
	__u64 ic : 1;
	__u64 i : 1;
	__u64 pk : 1;
	__u64 reserved2 : 1;
	__u64 dt : 1;
	__u64 dfl : 1;
	__u64 dfh : 1;
	__u64 sp : 1;
	__u64 pp : 1;
	__u64 di : 1;
	__u64 si : 1;
	__u64 db : 1;
	__u64 lp : 1;
	__u64 tb : 1;
	__u64 rt : 1;
	__u64 reserved3 : 4;
	__u64 cpl : 2;
	__u64 is : 1;
	__u64 mc : 1;
	__u64 it : 1;
	__u64 id : 1;
	__u64 da : 1;
	__u64 dd : 1;
	__u64 ss : 1;
	__u64 ri : 2;
	__u64 ed : 1;
	__u64 bn : 1;
	__u64 reserved4 : 19;
};

/*
 * This shift should be large enough to be able to represent
 * 1000000/itc_freq with good accuracy while being small enough to fit
 * 1000000<<IA64_USEC_PER_CYC_SHIFT in 64 bits.
 */
#define IA64_USEC_PER_CYC_SHIFT	41

/*
 * CPU type, hardware bug flags, and per-CPU state.
 */
struct cpuinfo_ia64 {
	__u64 *pgd_quick;
	__u64 *pmd_quick;
	__u64 *pte_quick;
	__u64 pgtable_cache_sz;
	/* CPUID-derived information: */
	__u64 ppn;
	__u64 features;
	__u8 number;
	__u8 revision;
	__u8 model;
	__u8 family;
	__u8 archrev;
	char vendor[16];
	__u64 itc_freq;		/* frequency of ITC counter */
	__u64 proc_freq;	/* frequency of processor */
	__u64 cyc_per_usec;	/* itc_freq/1000000 */
	__u64 usec_per_cyc;	/* 2^IA64_USEC_PER_CYC_SHIFT*1000000/itc_freq */
	__u64 unimpl_va_mask;	/* mask of unimplemented virtual address bits (from PAL) */
	__u64 unimpl_pa_mask;	/* mask of unimplemented physical address bits (from PAL) */
	__u64 ptce_base;
	__u32 ptce_count[2];
	__u32 ptce_stride[2];
#ifdef CONFIG_SMP
	__u64 loops_per_jiffy;
	__u64 ipi_count;
	__u64 prof_counter;
	__u64 prof_multiplier;
#endif
};

#define my_cpu_data		cpu_data[smp_processor_id()]

extern struct cpuinfo_ia64 cpu_data[NR_CPUS];

extern void identify_cpu (struct cpuinfo_ia64 *);
extern void print_cpu_info (struct cpuinfo_ia64 *);

typedef struct {
	unsigned long seg;
} mm_segment_t;

#define SET_UNALIGN_CTL(task,value)								\
({												\
	(task)->thread.flags = (((task)->thread.flags & ~IA64_THREAD_UAC_MASK)			\
				| (((value) << IA64_THREAD_UAC_SHIFT) & IA64_THREAD_UAC_MASK));	\
	0;											\
})
#define GET_UNALIGN_CTL(task,addr)								\
({												\
	put_user(((task)->thread.flags & IA64_THREAD_UAC_MASK) >> IA64_THREAD_UAC_SHIFT,	\
		 (int *) (addr));								\
})

struct siginfo;

struct thread_struct {
	__u64 ksp;			/* kernel stack pointer */
	unsigned long flags;		/* various flags */
	struct ia64_fpreg fph[96];	/* saved/loaded on demand */
	__u64 dbr[IA64_NUM_DBG_REGS];
	__u64 ibr[IA64_NUM_DBG_REGS];
#ifdef CONFIG_PERFMON
	__u64 pmc[IA64_NUM_PMC_REGS];
	__u64 pmd[IA64_NUM_PMD_REGS];
	struct {
		__u64		val;	/* virtual 64bit counter */
		__u64		rval;	/* reset value on overflow */
		int		sig;	/* signal used to notify */
		int		pid;	/* process to notify */
	} pmu_counters[IA64_NUM_PMD_COUNTERS];
# define INIT_THREAD_PM		{0, }, {0, }, {{ 0, 0, 0, 0}, },
#else
# define INIT_THREAD_PM
#endif
	__u64 map_base;			/* base address for get_unmapped_area() */
	__u64 task_size;		/* limit for task size */
#ifdef CONFIG_IA32_SUPPORT
	__u64 eflag;			/* IA32 EFLAGS reg */
	__u64 fsr;			/* IA32 floating pt status reg */
	__u64 fcr;			/* IA32 floating pt control reg */
	__u64 fir;			/* IA32 fp except. instr. reg */
	__u64 fdr;			/* IA32 fp except. data reg */
	__u64 csd;			/* IA32 code selector descriptor */
	__u64 ssd;			/* IA32 stack selector descriptor */
	__u64 tssd;			/* IA32 TSS descriptor */
	__u64 old_iob;			/* old IOBase value */
	union {
		__u64 sigmask;		/* aligned mask for sigsuspend scall */
	} un;
# define INIT_THREAD_IA32	0, 0, 0x17800000037fULL, 0, 0, 0, 0, 0, 0, {0},
#else
# define INIT_THREAD_IA32
#endif /* CONFIG_IA32_SUPPORT */
	struct siginfo *siginfo;	/* current siginfo struct for ptrace() */
};

#define INIT_MMAP {								\
	&init_mm, PAGE_OFFSET, PAGE_OFFSET + 0x10000000, NULL, PAGE_SHARED,	\
        VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL				\
}

#define INIT_THREAD {					\
	0,				/* ksp */	\
	0,				/* flags */	\
	{{{{0}}}, },			/* fph */	\
	{0, },				/* dbr */	\
	{0, },				/* ibr */	\
	INIT_THREAD_PM					\
	0x2000000000000000,		/* map_base */	\
	0xa000000000000000,		/* task_size */	\
	INIT_THREAD_IA32				\
	0				/* siginfo */	\
}

#define start_thread(regs,new_ip,new_sp) do {					\
	set_fs(USER_DS);							\
	ia64_psr(regs)->dfh = 1;	/* disable fph */			\
	ia64_psr(regs)->mfh = 0;	/* clear mfh */				\
	ia64_psr(regs)->cpl = 3;	/* set user mode */			\
	ia64_psr(regs)->ri = 0;		/* clear return slot number */		\
	ia64_psr(regs)->is = 0;		/* IA-64 instruction set */		\
	regs->cr_iip = new_ip;							\
	regs->ar_rsc = 0xf;		/* eager mode, privilege level 3 */	\
	regs->r12 = new_sp - 16;	/* allocate 16 byte scratch area */	\
	regs->ar_bspstore = IA64_RBS_BOT;					\
	regs->ar_rnat = 0;							\
	regs->loadrs = 0;							\
} while (0)

/* Forward declarations, a strange C thing... */
struct mm_struct;
struct task_struct;

/*
 * Free all resources held by a thread. This is called after the
 * parent of DEAD_TASK has collected the exist status of the task via
 * wait().  This is a no-op on IA-64.
 */
#define release_thread(dead_task)

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE 1: Only a kernel-only process (ie the swapper or direct
 * descendants who haven't done an "execve()") should use this: it
 * will work within a system call from a "real" process, but the
 * process memory space will not be free'd until both the parent and
 * the child have exited.
 *
 * NOTE 2: This MUST NOT be an inlined function.  Otherwise, we get
 * into trouble in init/main.c when the child thread returns to
 * do_basic_setup() and the timing is such that free_initmem() has
 * been called already.
 */
extern int kernel_thread (int (*fn)(void *), void *arg, unsigned long flags);

/* Copy and release all segment info associated with a VM */
#define copy_segments(tsk, mm)			do { } while (0)
#define release_segments(mm)			do { } while (0)

/* Get wait channel for task P.  */
extern unsigned long get_wchan (struct task_struct *p);

/* Return instruction pointer of blocked task TSK.  */
#define KSTK_EIP(tsk)					\
  ({							\
	struct pt_regs *_regs = ia64_task_regs(tsk);	\
	_regs->cr_iip + ia64_psr(_regs)->ri;		\
  })

/* Return stack pointer of blocked task TSK.  */
#define KSTK_ESP(tsk)  ((tsk)->thread.ksp)

#ifndef CONFIG_SMP

static inline struct task_struct *
ia64_get_fpu_owner (void)
{
	struct task_struct *t;
	__asm__ ("mov %0=ar.k5" : "=r"(t));
	return t;
}

static inline void
ia64_set_fpu_owner (struct task_struct *t)
{
	__asm__ __volatile__ ("mov ar.k5=%0" :: "r"(t));
}

#endif /* !CONFIG_SMP */

extern void __ia64_init_fpu (void);
extern void __ia64_save_fpu (struct ia64_fpreg *fph);
extern void __ia64_load_fpu (struct ia64_fpreg *fph);
extern void ia64_save_debug_regs (unsigned long *save_area);
extern void ia64_load_debug_regs (unsigned long *save_area);

#ifdef CONFIG_IA32_SUPPORT
extern void ia32_save_state (struct thread_struct *thread);
extern void ia32_load_state (struct thread_struct *thread);
#endif

#ifdef CONFIG_PERFMON
extern void ia64_save_pm_regs (struct task_struct *task);
extern void ia64_load_pm_regs (struct task_struct *task);
#endif

#define ia64_fph_enable()	__asm__ __volatile__ (";; rsm psr.dfh;; srlz.d;;" ::: "memory");
#define ia64_fph_disable()	__asm__ __volatile__ (";; ssm psr.dfh;; srlz.d;;" ::: "memory");

/* load fp 0.0 into fph */
static inline void
ia64_init_fpu (void) {
	ia64_fph_enable();
	__ia64_init_fpu();
	ia64_fph_disable();
}

/* save f32-f127 at FPH */
static inline void
ia64_save_fpu (struct ia64_fpreg *fph) {
	ia64_fph_enable();
	__ia64_save_fpu(fph);
	ia64_fph_disable();
}

/* load f32-f127 from FPH */
static inline void
ia64_load_fpu (struct ia64_fpreg *fph) {
	ia64_fph_enable();
	__ia64_load_fpu(fph);
	ia64_fph_disable();
}

static inline void
ia64_fc (void *addr)
{
	__asm__ __volatile__ ("fc %0" :: "r"(addr) : "memory");
}

static inline void
ia64_sync_i (void)
{
	__asm__ __volatile__ (";; sync.i" ::: "memory");
}

static inline void
ia64_srlz_i (void)
{
	__asm__ __volatile__ (";; srlz.i ;;" ::: "memory");
}

static inline void
ia64_srlz_d (void)
{
	__asm__ __volatile__ (";; srlz.d" ::: "memory");
}

static inline __u64
ia64_get_rr (__u64 reg_bits)
{
	__u64 r;
	__asm__ __volatile__ ("mov %0=rr[%1]" : "=r"(r) : "r"(reg_bits) : "memory");
	return r;
}

static inline void
ia64_set_rr (__u64 reg_bits, __u64 rr_val)
{
	__asm__ __volatile__ ("mov rr[%0]=%1" :: "r"(reg_bits), "r"(rr_val) : "memory");
}

static inline __u64
ia64_get_dcr (void)
{
	__u64 r;
	__asm__ ("mov %0=cr.dcr" : "=r"(r));
	return r;
}

static inline void
ia64_set_dcr (__u64 val)
{
	__asm__ __volatile__ ("mov cr.dcr=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_lid (void)
{
	__u64 r;
	__asm__ ("mov %0=cr.lid" : "=r"(r));
	return r;
}

static inline void
ia64_invala (void)
{
	__asm__ __volatile__ ("invala" ::: "memory");
}

/*
 * Save the processor status flags in FLAGS and then clear the
 * interrupt collection and interrupt enable bits.
 */
#define ia64_clear_ic(flags)							\
	__asm__ __volatile__ ("mov %0=psr;; rsm psr.i | psr.ic;; srlz.i;;"	\
			      : "=r"(flags) :: "memory");

/*
 * Insert a translation into an instruction and/or data translation
 * register.
 */
static inline void
ia64_itr (__u64 target_mask, __u64 tr_num,
	  __u64 vmaddr, __u64 pte,
	  __u64 log_page_size)
{
	__asm__ __volatile__ ("mov cr.itir=%0" :: "r"(log_page_size << 2) : "memory");
	__asm__ __volatile__ ("mov cr.ifa=%0;;" :: "r"(vmaddr) : "memory");
	if (target_mask & 0x1)
		__asm__ __volatile__ ("itr.i itr[%0]=%1"
				      :: "r"(tr_num), "r"(pte) : "memory");
	if (target_mask & 0x2)
		__asm__ __volatile__ (";;itr.d dtr[%0]=%1"
				      :: "r"(tr_num), "r"(pte) : "memory");
}

/*
 * Insert a translation into the instruction and/or data translation
 * cache.
 */
static inline void
ia64_itc (__u64 target_mask, __u64 vmaddr, __u64 pte,
	  __u64 log_page_size)
{
	__asm__ __volatile__ ("mov cr.itir=%0" :: "r"(log_page_size << 2) : "memory");
	__asm__ __volatile__ ("mov cr.ifa=%0;;" :: "r"(vmaddr) : "memory");
	/* as per EAS2.6, itc must be the last instruction in an instruction group */
	if (target_mask & 0x1)
		__asm__ __volatile__ ("itc.i %0;;" :: "r"(pte) : "memory");
	if (target_mask & 0x2)
		__asm__ __volatile__ (";;itc.d %0;;" :: "r"(pte) : "memory");
}

/*
 * Purge a range of addresses from instruction and/or data translation
 * register(s).
 */
static inline void
ia64_ptr (__u64 target_mask, __u64 vmaddr, __u64 log_size)
{
	if (target_mask & 0x1)
		__asm__ __volatile__ ("ptr.i %0,%1" :: "r"(vmaddr), "r"(log_size << 2));
	if (target_mask & 0x2)
		__asm__ __volatile__ ("ptr.d %0,%1" :: "r"(vmaddr), "r"(log_size << 2));
}

/* Set the interrupt vector address.  The address must be suitably aligned (32KB).  */
static inline void
ia64_set_iva (void *ivt_addr)
{
	__asm__ __volatile__ ("mov cr.iva=%0;; srlz.i;;" :: "r"(ivt_addr) : "memory");
}

/* Set the page table address and control bits.  */
static inline void
ia64_set_pta (__u64 pta)
{
	/* Note: srlz.i implies srlz.d */
	__asm__ __volatile__ ("mov cr.pta=%0;; srlz.i;;" :: "r"(pta) : "memory");
}

static inline __u64
ia64_get_cpuid (__u64 regnum)
{
	__u64 r;

	__asm__ ("mov %0=cpuid[%r1]" : "=r"(r) : "rO"(regnum));
	return r;
}

static inline void
ia64_eoi (void)
{
	__asm__ ("mov cr.eoi=r0;; srlz.d;;" ::: "memory");
}

static inline void
ia64_set_lrr0 (__u8 vector, __u8 masked)
{
	if (masked > 1)
		masked = 1;

	__asm__ __volatile__ ("mov cr.lrr0=%0;; srlz.d"
			      :: "r"((masked << 16) | vector) : "memory");
}


static inline void
ia64_set_lrr1 (__u8 vector, __u8 masked)
{
	if (masked > 1)
		masked = 1;

	__asm__ __volatile__ ("mov cr.lrr1=%0;; srlz.d"
			      :: "r"((masked << 16) | vector) : "memory");
}

static inline void
ia64_set_pmv (__u64 val)
{
	__asm__ __volatile__ ("mov cr.pmv=%0" :: "r"(val) : "memory");
}

static inline __u64
ia64_get_pmc (__u64 regnum)
{
	__u64 retval;

	__asm__ __volatile__ ("mov %0=pmc[%1]" : "=r"(retval) : "r"(regnum));
	return retval;
}

static inline void
ia64_set_pmc (__u64 regnum, __u64 value)
{
	__asm__ __volatile__ ("mov pmc[%0]=%1" :: "r"(regnum), "r"(value));
}

static inline __u64
ia64_get_pmd (__u64 regnum)
{
	__u64 retval;

	__asm__ __volatile__ ("mov %0=pmd[%1]" : "=r"(retval) : "r"(regnum));
	return retval;
}

static inline void
ia64_set_pmd (__u64 regnum, __u64 value)
{
	__asm__ __volatile__ ("mov pmd[%0]=%1" :: "r"(regnum), "r"(value));
}

/*
 * Given the address to which a spill occurred, return the unat bit
 * number that corresponds to this address.
 */
static inline __u64
ia64_unat_pos (void *spill_addr)
{
	return ((__u64) spill_addr >> 3) & 0x3f;
}

/*
 * Set the NaT bit of an integer register which was spilled at address
 * SPILL_ADDR.  UNAT is the mask to be updated.
 */
static inline void
ia64_set_unat (__u64 *unat, void *spill_addr, unsigned long nat)
{
	__u64 bit = ia64_unat_pos(spill_addr);
	__u64 mask = 1UL << bit;

	*unat = (*unat & ~mask) | (nat << bit);
}

/*
 * Return saved PC of a blocked thread.
 * Note that the only way T can block is through a call to schedule() -> switch_to().
 */
static inline unsigned long
thread_saved_pc (struct thread_struct *t)
{
	struct unw_frame_info info;
	unsigned long ip;

	/* XXX ouch: Linus, please pass the task pointer to thread_saved_pc() instead! */
	struct task_struct *p = (void *) ((unsigned long) t - IA64_TASK_THREAD_OFFSET);

	unw_init_from_blocked_task(&info, p);
	if (unw_unwind(&info) < 0)
		return 0;
	unw_get_ip(&info, &ip);
	return ip;
}

/*
 * Get the current instruction/program counter value.
 */
#define current_text_addr() \
	({ void *_pc; __asm__ ("mov %0=ip" : "=r" (_pc)); _pc; })

#define THREAD_SIZE	IA64_STK_OFFSET
/* NOTE: The task struct and the stacks are allocated together.  */
#define alloc_task_struct() \
        ((struct task_struct *) __get_free_pages(GFP_KERNEL, IA64_TASK_STRUCT_LOG_NUM_PAGES))
#define free_task_struct(p)     free_pages((unsigned long)(p), IA64_TASK_STRUCT_LOG_NUM_PAGES)
#define get_task_struct(tsk)	atomic_inc(&virt_to_page(tsk)->count)

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

/*
 * Set the correctable machine check vector register
 */
static inline void
ia64_set_cmcv (__u64 val)
{
	__asm__ __volatile__ ("mov cr.cmcv=%0" :: "r"(val) : "memory");
}

/*
 * Read the correctable machine check vector register
 */
static inline __u64
ia64_get_cmcv (void)
{
	__u64 val;

	__asm__ ("mov %0=cr.cmcv" : "=r"(val) :: "memory");
	return val;
}

static inline __u64
ia64_get_ivr (void)
{
	__u64 r;
	__asm__ __volatile__ ("srlz.d;; mov %0=cr.ivr;; srlz.d;;" : "=r"(r));
	return r;
}

static inline void
ia64_set_tpr (__u64 val)
{
	__asm__ __volatile__ ("mov cr.tpr=%0" :: "r"(val));
}

static inline __u64
ia64_get_tpr (void)
{
	__u64 r;
	__asm__ ("mov %0=cr.tpr" : "=r"(r));
	return r;
}

static inline void
ia64_set_irr0 (__u64 val)
{
	__asm__ __volatile__("mov cr.irr0=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_irr0 (void)
{
	__u64 val;

	/* this is volatile because irr may change unbeknownst to gcc... */
	__asm__ __volatile__("mov %0=cr.irr0" : "=r"(val));
	return val;
}

static inline void
ia64_set_irr1 (__u64 val)
{
	__asm__ __volatile__("mov cr.irr1=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_irr1 (void)
{
	__u64 val;

	/* this is volatile because irr may change unbeknownst to gcc... */
	__asm__ __volatile__("mov %0=cr.irr1" : "=r"(val));
	return val;
}

static inline void
ia64_set_irr2 (__u64 val)
{
	__asm__ __volatile__("mov cr.irr2=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_irr2 (void)
{
	__u64 val;

	/* this is volatile because irr may change unbeknownst to gcc... */
	__asm__ __volatile__("mov %0=cr.irr2" : "=r"(val));
	return val;
}

static inline void
ia64_set_irr3 (__u64 val)
{
	__asm__ __volatile__("mov cr.irr3=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_irr3 (void)
{
	__u64 val;

	/* this is volatile because irr may change unbeknownst to gcc... */
	__asm__ __volatile__("mov %0=cr.irr3" : "=r"(val));
	return val;
}

static inline __u64
ia64_get_gp(void)
{
	__u64 val;

	__asm__ ("mov %0=gp" : "=r"(val));
	return val;
}

/* XXX remove the handcoded version once we have a sufficiently clever compiler... */
#ifdef SMART_COMPILER
# define ia64_rotr(w,n)				\
  ({						\
	__u64 _w = (w), _n = (n);		\
						\
	(_w >> _n) | (_w << (64 - _n));		\
  })
#else
# define ia64_rotr(w,n)							\
  ({									\
	__u64 result;							\
	asm ("shrp %0=%1,%1,%2" : "=r"(result) : "r"(w), "i"(n));	\
	result;								\
  })
#endif

#define ia64_rotl(w,n)	ia64_rotr((w),(64)-(n))

static inline __u64
ia64_thash (__u64 addr)
{
	__u64 result;
	asm ("thash %0=%1" : "=r"(result) : "r" (addr));
	return result;
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_PROCESSOR_H */
