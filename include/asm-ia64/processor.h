#ifndef _ASM_IA64_PROCESSOR_H
#define _ASM_IA64_PROCESSOR_H

/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 *
 * 11/24/98	S.Eranian	added ia64_set_iva()
 * 12/03/99	D. Mosberger	implement thread_saved_pc() via kernel unwind API
 * 06/16/00	A. Mallick	added csd/ssd/tssd for ia32 support
 */

#include <linux/config.h>

#include <asm/ptrace.h>
#include <asm/kregs.h>

#define IA64_NUM_DBG_REGS	8
/*
 * Limits for PMC and PMD are set to less than maximum architected values
 * but should be sufficient for a while
 */
#define IA64_NUM_PMC_REGS	32
#define IA64_NUM_PMD_REGS	32

#define DEFAULT_MAP_BASE	0x2000000000000000
#define DEFAULT_TASK_SIZE	0xa000000000000000

/*
 * TASK_SIZE really is a mis-named.  It really is the maximum user
 * space address (plus one).  On IA-64, there are five regions of 2TB
 * each (assuming 8KB page size), for a total of 8TB of user virtual
 * address space.
 */
#define TASK_SIZE		(current->thread.task_size)

/*
 * MM_VM_SIZE(mm) gives the maximum address (plus 1) which may contain a mapping for
 * address-space MM.  Note that with 32-bit tasks, this is still DEFAULT_TASK_SIZE,
 * because the kernel may have installed helper-mappings above TASK_SIZE.  For example,
 * for x86 emulation, the LDT and GDT are mapped above TASK_SIZE.
 */
#define MM_VM_SIZE(mm)		DEFAULT_TASK_SIZE

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(current->thread.map_base)

/*
 * Bus types
 */
#define EISA_bus 0
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

#define IA64_THREAD_FPH_VALID	(__IA64_UL(1) << 0)	/* floating-point high state valid? */
#define IA64_THREAD_DBG_VALID	(__IA64_UL(1) << 1)	/* debug registers valid? */
#define IA64_THREAD_PM_VALID	(__IA64_UL(1) << 2)	/* performance registers valid? */
#define IA64_THREAD_UAC_NOPRINT	(__IA64_UL(1) << 3)	/* don't log unaligned accesses */
#define IA64_THREAD_UAC_SIGBUS	(__IA64_UL(1) << 4)	/* generate SIGBUS on unaligned acc. */
#define IA64_THREAD_KRBS_SYNCED	(__IA64_UL(1) << 5)	/* krbs synced with process vm? */
#define IA64_THREAD_FPEMU_NOPRINT (__IA64_UL(1) << 6)	/* don't log any fpswa faults */
#define IA64_THREAD_FPEMU_SIGFPE  (__IA64_UL(1) << 7)	/* send a SIGFPE for fpswa faults */
#define IA64_THREAD_XSTACK	(__IA64_UL(1) << 8)	/* stack executable by default? */

#define IA64_THREAD_UAC_SHIFT	3
#define IA64_THREAD_UAC_MASK	(IA64_THREAD_UAC_NOPRINT | IA64_THREAD_UAC_SIGBUS)
#define IA64_THREAD_FPEMU_SHIFT	6
#define IA64_THREAD_FPEMU_MASK	(IA64_THREAD_FPEMU_NOPRINT | IA64_THREAD_FPEMU_SIGFPE)


/*
 * This shift should be large enough to be able to represent 1000000000/itc_freq with good
 * accuracy while being small enough to fit 10*1000000000<<IA64_NSEC_PER_CYC_SHIFT in 64 bits
 * (this will give enough slack to represent 10 seconds worth of time as a scaled number).
 */
#define IA64_NSEC_PER_CYC_SHIFT	30

#ifndef __ASSEMBLY__

#include <linux/cache.h>
#include <linux/compiler.h>
#include <linux/threads.h>
#include <linux/types.h>

#include <asm/fpu.h>
#include <asm/offsets.h>
#include <asm/page.h>
#include <asm/percpu.h>
#include <asm/rse.h>
#include <asm/unwind.h>
#include <asm/atomic.h>
#ifdef CONFIG_NUMA
#include <asm/nodedata.h>
#endif

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
 * CPU type, hardware bug flags, and per-CPU state.  Frequently used
 * state comes earlier:
 */
struct cpuinfo_ia64 {
	/* irq_stat must be 64-bit aligned */
	union {
		struct {
			__u32 irq_count;
			__u32 bh_count;
		} f;
		__u64 irq_and_bh_counts;
	} irq_stat;
	__u32 softirq_pending;
	__u64 itm_delta;	/* # of clock cycles between clock ticks */
	__u64 itm_next;		/* interval timer mask value to use for next clock tick */
	__u64 *pgd_quick;
	__u64 *pmd_quick;
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
	__u64 nsec_per_cyc;	/* (1000000000<<IA64_NSEC_PER_CYC_SHIFT)/itc_freq */
	__u64 unimpl_va_mask;	/* mask of unimplemented virtual address bits (from PAL) */
	__u64 unimpl_pa_mask;	/* mask of unimplemented physical address bits (from PAL) */
	__u64 ptce_base;
	__u32 ptce_count[2];
	__u32 ptce_stride[2];
	struct task_struct *ksoftirqd;	/* kernel softirq daemon for this CPU */
#ifdef CONFIG_SMP
	int cpu;
	__u64 loops_per_jiffy;
	__u64 ipi_count;
	__u64 prof_counter;
	__u64 prof_multiplier;
#endif
#ifdef CONFIG_NUMA
	struct ia64_node_data *node_data;
#endif
};

DECLARE_PER_CPU(struct cpuinfo_ia64, cpu_info);

/*
 * The "local" data pointer.  It points to the per-CPU data of the currently executing
 * CPU, much like "current" points to the per-task data of the currently executing task.
 */
#define local_cpu_data		(&__get_cpu_var(cpu_info))
#define cpu_data(cpu)		(&per_cpu(cpu_info, cpu))

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

#define SET_FPEMU_CTL(task,value)								\
({												\
	(task)->thread.flags = (((task)->thread.flags & ~IA64_THREAD_FPEMU_MASK)		\
			  | (((value) << IA64_THREAD_FPEMU_SHIFT) & IA64_THREAD_FPEMU_MASK));	\
	0;											\
})
#define GET_FPEMU_CTL(task,addr)								\
({												\
	put_user(((task)->thread.flags & IA64_THREAD_FPEMU_MASK) >> IA64_THREAD_FPEMU_SHIFT,	\
		 (int *) (addr));								\
})

struct thread_struct {
	__u32 flags;			/* various thread flags (see IA64_THREAD_*) */
	/* writing on_ustack is performance-critical, so it's worth spending 8 bits on it... */
	__u8 on_ustack;			/* executing on user-stacks? */
	__u8 pad[3];
	__u64 ksp;			/* kernel stack pointer */
	__u64 map_base;			/* base address for get_unmapped_area() */
	__u64 task_size;		/* limit for task size */
	int last_fph_cpu;		/* CPU that may hold the contents of f32-f127 */

#ifdef CONFIG_IA32_SUPPORT
	__u64 eflag;			/* IA32 EFLAGS reg */
	__u64 fsr;			/* IA32 floating pt status reg */
	__u64 fcr;			/* IA32 floating pt control reg */
	__u64 fir;			/* IA32 fp except. instr. reg */
	__u64 fdr;			/* IA32 fp except. data reg */
	__u64 csd;			/* IA32 code selector descriptor */
	__u64 ssd;			/* IA32 stack selector descriptor */
	__u64 old_k1;			/* old value of ar.k1 */
	__u64 old_iob;			/* old IOBase value */
# define INIT_THREAD_IA32	.eflag =	0,			\
				.fsr =		0,			\
				.fcr =		0x17800000037fULL,	\
				.fir =		0,			\
				.fdr =		0,			\
				.csd =		0,			\
				.ssd =		0,			\
				.old_k1 =	0,			\
				.old_iob =	0,
#else
# define INIT_THREAD_IA32
#endif /* CONFIG_IA32_SUPPORT */
#ifdef CONFIG_PERFMON
	__u64 pmc[IA64_NUM_PMC_REGS];
	__u64 pmd[IA64_NUM_PMD_REGS];
	unsigned long pfm_ovfl_block_reset;/* non-zero if we need to block or reset regs on ovfl */
	void *pfm_context;		/* pointer to detailed PMU context */
	atomic_t pfm_notifiers_check;	/* when >0, will cleanup ctx_notify_task in tasklist */
	atomic_t pfm_owners_check;	/* when >0, will cleanup ctx_owner in tasklist */
	void *pfm_smpl_buf_list;	/* list of sampling buffers to vfree */
# define INIT_THREAD_PM		.pmc =			{0, },	\
				.pmd =			{0, },	\
				.pfm_ovfl_block_reset =	0,	\
				.pfm_context =		NULL,	\
				.pfm_notifiers_check =	{ 0 },	\
				.pfm_owners_check =	{ 0 },	\
				.pfm_smpl_buf_list =	NULL,
#else
# define INIT_THREAD_PM
#endif
	__u64 dbr[IA64_NUM_DBG_REGS];
	__u64 ibr[IA64_NUM_DBG_REGS];
	struct ia64_fpreg fph[96];	/* saved/loaded on demand */
};

#define INIT_THREAD {				\
	.flags =	0,			\
	.on_ustack =	0,			\
	.ksp =		0,			\
	.map_base =	DEFAULT_MAP_BASE,	\
	.task_size =	DEFAULT_TASK_SIZE,	\
	.last_fph_cpu =  0,			\
	INIT_THREAD_IA32			\
	INIT_THREAD_PM				\
	.dbr =		{0, },			\
	.ibr =		{0, },			\
	.fph =		{{{{0}}}, }		\
}

#define start_thread(regs,new_ip,new_sp) do {							\
	set_fs(USER_DS);									\
	regs->cr_ipsr = ((regs->cr_ipsr | (IA64_PSR_BITS_TO_SET | IA64_PSR_CPL))		\
			 & ~(IA64_PSR_BITS_TO_CLEAR | IA64_PSR_RI | IA64_PSR_IS));		\
	regs->cr_iip = new_ip;									\
	regs->ar_rsc = 0xf;		/* eager mode, privilege level 3 */			\
	regs->ar_rnat = 0;									\
	regs->ar_bspstore = IA64_RBS_BOT;							\
	regs->ar_fpsr = FPSR_DEFAULT;								\
	regs->loadrs = 0;									\
	regs->r8 = current->mm->dumpable;	/* set "don't zap registers" flag */		\
	regs->r12 = new_sp - 16;	/* allocate 16 byte scratch area */			\
	if (unlikely(!current->mm->dumpable)) {					\
		/*										\
		 * Zap scratch regs to avoid leaking bits between processes with different	\
		 * uid/privileges.								\
		 */										\
		regs->ar_pfs = 0;								\
		regs->pr = 0;									\
		/*										\
		 * XXX fix me: everything below can go away once we stop preserving scratch	\
		 * regs on a system call.							\
		 */										\
		regs->b6 = 0;									\
		regs->r1 = 0; regs->r2 = 0; regs->r3 = 0;					\
		regs->r13 = 0; regs->r14 = 0; regs->r15 = 0;					\
		regs->r9  = 0; regs->r11 = 0;							\
		regs->r16 = 0; regs->r17 = 0; regs->r18 = 0; regs->r19 = 0;			\
		regs->r20 = 0; regs->r21 = 0; regs->r22 = 0; regs->r23 = 0;			\
		regs->r24 = 0; regs->r25 = 0; regs->r26 = 0; regs->r27 = 0;			\
		regs->r28 = 0; regs->r29 = 0; regs->r30 = 0; regs->r31 = 0;			\
		regs->ar_ccv = 0;								\
		regs->b0 = 0; regs->b7 = 0;							\
		regs->f6.u.bits[0] = 0; regs->f6.u.bits[1] = 0;					\
		regs->f7.u.bits[0] = 0; regs->f7.u.bits[1] = 0;					\
		regs->f8.u.bits[0] = 0; regs->f8.u.bits[1] = 0;					\
		regs->f9.u.bits[0] = 0; regs->f9.u.bits[1] = 0;					\
	}											\
} while (0)

/* Forward declarations, a strange C thing... */
struct mm_struct;
struct task_struct;

/*
 * Free all resources held by a thread. This is called after the
 * parent of DEAD_TASK has collected the exist status of the task via
 * wait().
 */
#ifdef CONFIG_PERFMON
  extern void release_thread (struct task_struct *task);
#else
# define release_thread(dead_task)
#endif

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

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
extern pid_t kernel_thread (int (*fn)(void *), void *arg, unsigned long flags);

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

static inline unsigned long
ia64_get_kr (unsigned long regnum)
{
	unsigned long r = 0;

	switch (regnum) {
	      case 0: asm volatile ("mov %0=ar.k0" : "=r"(r)); break;
	      case 1: asm volatile ("mov %0=ar.k1" : "=r"(r)); break;
	      case 2: asm volatile ("mov %0=ar.k2" : "=r"(r)); break;
	      case 3: asm volatile ("mov %0=ar.k3" : "=r"(r)); break;
	      case 4: asm volatile ("mov %0=ar.k4" : "=r"(r)); break;
	      case 5: asm volatile ("mov %0=ar.k5" : "=r"(r)); break;
	      case 6: asm volatile ("mov %0=ar.k6" : "=r"(r)); break;
	      case 7: asm volatile ("mov %0=ar.k7" : "=r"(r)); break;
	}
	return r;
}

static inline void
ia64_set_kr (unsigned long regnum, unsigned long r)
{
	switch (regnum) {
	      case 0: asm volatile ("mov ar.k0=%0" :: "r"(r)); break;
	      case 1: asm volatile ("mov ar.k1=%0" :: "r"(r)); break;
	      case 2: asm volatile ("mov ar.k2=%0" :: "r"(r)); break;
	      case 3: asm volatile ("mov ar.k3=%0" :: "r"(r)); break;
	      case 4: asm volatile ("mov ar.k4=%0" :: "r"(r)); break;
	      case 5: asm volatile ("mov ar.k5=%0" :: "r"(r)); break;
	      case 6: asm volatile ("mov ar.k6=%0" :: "r"(r)); break;
	      case 7: asm volatile ("mov ar.k7=%0" :: "r"(r)); break;
	}
}

/*
 * The following three macros can't be inline functions because we don't have struct
 * task_struct at this point.
 */

/* Return TRUE if task T owns the fph partition of the CPU we're running on. */
#define ia64_is_local_fpu_owner(t)								\
({												\
	struct task_struct *__ia64_islfo_task = (t);						\
	(__ia64_islfo_task->thread.last_fph_cpu == smp_processor_id()				\
	 && __ia64_islfo_task == (struct task_struct *) ia64_get_kr(IA64_KR_FPU_OWNER));	\
})

/* Mark task T as owning the fph partition of the CPU we're running on. */
#define ia64_set_local_fpu_owner(t) do {						\
	struct task_struct *__ia64_slfo_task = (t);					\
	__ia64_slfo_task->thread.last_fph_cpu = smp_processor_id();			\
	ia64_set_kr(IA64_KR_FPU_OWNER, (unsigned long) __ia64_slfo_task);		\
} while (0)

/* Mark the fph partition of task T as being invalid on all CPUs.  */
#define ia64_drop_fpu(t)	((t)->thread.last_fph_cpu = -1)

extern void __ia64_init_fpu (void);
extern void __ia64_save_fpu (struct ia64_fpreg *fph);
extern void __ia64_load_fpu (struct ia64_fpreg *fph);
extern void ia64_save_debug_regs (unsigned long *save_area);
extern void ia64_load_debug_regs (unsigned long *save_area);

#ifdef CONFIG_IA32_SUPPORT
extern void ia32_save_state (struct task_struct *task);
extern void ia32_load_state (struct task_struct *task);
#endif

#define ia64_fph_enable()	asm volatile (";; rsm psr.dfh;; srlz.d;;" ::: "memory");
#define ia64_fph_disable()	asm volatile (";; ssm psr.dfh;; srlz.d;;" ::: "memory");

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
	asm volatile ("fc %0" :: "r"(addr) : "memory");
}

static inline void
ia64_sync_i (void)
{
	asm volatile (";; sync.i" ::: "memory");
}

static inline void
ia64_srlz_i (void)
{
	asm volatile (";; srlz.i ;;" ::: "memory");
}

static inline void
ia64_srlz_d (void)
{
	asm volatile (";; srlz.d" ::: "memory");
}

static inline __u64
ia64_get_rr (__u64 reg_bits)
{
	__u64 r;
	asm volatile ("mov %0=rr[%1]" : "=r"(r) : "r"(reg_bits) : "memory");
	return r;
}

static inline void
ia64_set_rr (__u64 reg_bits, __u64 rr_val)
{
	asm volatile ("mov rr[%0]=%1" :: "r"(reg_bits), "r"(rr_val) : "memory");
}

static inline __u64
ia64_get_dcr (void)
{
	__u64 r;
	asm volatile ("mov %0=cr.dcr" : "=r"(r));
	return r;
}

static inline void
ia64_set_dcr (__u64 val)
{
	asm volatile ("mov cr.dcr=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_lid (void)
{
	__u64 r;
	asm volatile ("mov %0=cr.lid" : "=r"(r));
	return r;
}

static inline void
ia64_invala (void)
{
	asm volatile ("invala" ::: "memory");
}

static inline __u64
ia64_clear_ic (void)
{
	__u64 psr;
	asm volatile ("mov %0=psr;; rsm psr.i | psr.ic;; srlz.i;;" : "=r"(psr) :: "memory");
	return psr;
}

/*
 * Restore the psr.
 */
static inline void
ia64_set_psr (__u64 psr)
{
	asm volatile (";; mov psr.l=%0;; srlz.d" :: "r" (psr) : "memory");
}

/*
 * Insert a translation into an instruction and/or data translation
 * register.
 */
static inline void
ia64_itr (__u64 target_mask, __u64 tr_num,
	  __u64 vmaddr, __u64 pte,
	  __u64 log_page_size)
{
	asm volatile ("mov cr.itir=%0" :: "r"(log_page_size << 2) : "memory");
	asm volatile ("mov cr.ifa=%0;;" :: "r"(vmaddr) : "memory");
	if (target_mask & 0x1)
		asm volatile ("itr.i itr[%0]=%1"
				      :: "r"(tr_num), "r"(pte) : "memory");
	if (target_mask & 0x2)
		asm volatile (";;itr.d dtr[%0]=%1"
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
	asm volatile ("mov cr.itir=%0" :: "r"(log_page_size << 2) : "memory");
	asm volatile ("mov cr.ifa=%0;;" :: "r"(vmaddr) : "memory");
	/* as per EAS2.6, itc must be the last instruction in an instruction group */
	if (target_mask & 0x1)
		asm volatile ("itc.i %0;;" :: "r"(pte) : "memory");
	if (target_mask & 0x2)
		asm volatile (";;itc.d %0;;" :: "r"(pte) : "memory");
}

/*
 * Purge a range of addresses from instruction and/or data translation
 * register(s).
 */
static inline void
ia64_ptr (__u64 target_mask, __u64 vmaddr, __u64 log_size)
{
	if (target_mask & 0x1)
		asm volatile ("ptr.i %0,%1" :: "r"(vmaddr), "r"(log_size << 2));
	if (target_mask & 0x2)
		asm volatile ("ptr.d %0,%1" :: "r"(vmaddr), "r"(log_size << 2));
}

/* Set the interrupt vector address.  The address must be suitably aligned (32KB).  */
static inline void
ia64_set_iva (void *ivt_addr)
{
	asm volatile ("mov cr.iva=%0;; srlz.i;;" :: "r"(ivt_addr) : "memory");
}

/* Set the page table address and control bits.  */
static inline void
ia64_set_pta (__u64 pta)
{
	/* Note: srlz.i implies srlz.d */
	asm volatile ("mov cr.pta=%0;; srlz.i;;" :: "r"(pta) : "memory");
}

static inline __u64
ia64_get_cpuid (__u64 regnum)
{
	__u64 r;

	asm ("mov %0=cpuid[%r1]" : "=r"(r) : "rO"(regnum));
	return r;
}

static inline void
ia64_eoi (void)
{
	asm ("mov cr.eoi=r0;; srlz.d;;" ::: "memory");
}

static inline void
ia64_set_lrr0 (unsigned long val)
{
	asm volatile ("mov cr.lrr0=%0;; srlz.d" :: "r"(val) : "memory");
}

#define cpu_relax()	barrier()


static inline void
ia64_set_lrr1 (unsigned long val)
{
	asm volatile ("mov cr.lrr1=%0;; srlz.d" :: "r"(val) : "memory");
}

static inline void
ia64_set_pmv (__u64 val)
{
	asm volatile ("mov cr.pmv=%0" :: "r"(val) : "memory");
}

static inline __u64
ia64_get_pmc (__u64 regnum)
{
	__u64 retval;

	asm volatile ("mov %0=pmc[%1]" : "=r"(retval) : "r"(regnum));
	return retval;
}

static inline void
ia64_set_pmc (__u64 regnum, __u64 value)
{
	asm volatile ("mov pmc[%0]=%1" :: "r"(regnum), "r"(value));
}

static inline __u64
ia64_get_pmd (__u64 regnum)
{
	__u64 retval;

	asm volatile ("mov %0=pmd[%1]" : "=r"(retval) : "r"(regnum));
	return retval;
}

static inline void
ia64_set_pmd (__u64 regnum, __u64 value)
{
	asm volatile ("mov pmd[%0]=%1" :: "r"(regnum), "r"(value));
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
thread_saved_pc (struct task_struct *t)
{
	struct unw_frame_info info;
	unsigned long ip;

	unw_init_from_blocked_task(&info, t);
	if (unw_unwind(&info) < 0)
		return 0;
	unw_get_ip(&info, &ip);
	return ip;
}

/*
 * Get the current instruction/program counter value.
 */
#define current_text_addr() \
	({ void *_pc; asm volatile ("mov %0=ip" : "=r" (_pc)); _pc; })

/*
 * Set the correctable machine check vector register
 */
static inline void
ia64_set_cmcv (__u64 val)
{
	asm volatile ("mov cr.cmcv=%0" :: "r"(val) : "memory");
}

/*
 * Read the correctable machine check vector register
 */
static inline __u64
ia64_get_cmcv (void)
{
	__u64 val;

	asm volatile ("mov %0=cr.cmcv" : "=r"(val) :: "memory");
	return val;
}

static inline __u64
ia64_get_ivr (void)
{
	__u64 r;
	asm volatile ("srlz.d;; mov %0=cr.ivr;; srlz.d;;" : "=r"(r));
	return r;
}

static inline void
ia64_set_tpr (__u64 val)
{
	asm volatile ("mov cr.tpr=%0" :: "r"(val));
}

static inline __u64
ia64_get_tpr (void)
{
	__u64 r;
	asm volatile ("mov %0=cr.tpr" : "=r"(r));
	return r;
}

static inline void
ia64_set_irr0 (__u64 val)
{
	asm volatile("mov cr.irr0=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_irr0 (void)
{
	__u64 val;

	/* this is volatile because irr may change unbeknownst to gcc... */
	asm volatile("mov %0=cr.irr0" : "=r"(val));
	return val;
}

static inline void
ia64_set_irr1 (__u64 val)
{
	asm volatile("mov cr.irr1=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_irr1 (void)
{
	__u64 val;

	/* this is volatile because irr may change unbeknownst to gcc... */
	asm volatile("mov %0=cr.irr1" : "=r"(val));
	return val;
}

static inline void
ia64_set_irr2 (__u64 val)
{
	asm volatile("mov cr.irr2=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_irr2 (void)
{
	__u64 val;

	/* this is volatile because irr may change unbeknownst to gcc... */
	asm volatile("mov %0=cr.irr2" : "=r"(val));
	return val;
}

static inline void
ia64_set_irr3 (__u64 val)
{
	asm volatile("mov cr.irr3=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}

static inline __u64
ia64_get_irr3 (void)
{
	__u64 val;

	/* this is volatile because irr may change unbeknownst to gcc... */
	asm volatile ("mov %0=cr.irr3" : "=r"(val));
	return val;
}

static inline __u64
ia64_get_gp(void)
{
	__u64 val;

	asm ("mov %0=gp" : "=r"(val));
	return val;
}

static inline void
ia64_set_ibr (__u64 regnum, __u64 value)
{
	asm volatile ("mov ibr[%0]=%1" :: "r"(regnum), "r"(value));
}

static inline void
ia64_set_dbr (__u64 regnum, __u64 value)
{
	asm volatile ("mov dbr[%0]=%1" :: "r"(regnum), "r"(value));
#ifdef CONFIG_ITANIUM
	asm volatile (";; srlz.d");
#endif
}

static inline __u64
ia64_get_ibr (__u64 regnum)
{
	__u64 retval;

	asm volatile ("mov %0=ibr[%1]" : "=r"(retval) : "r"(regnum));
	return retval;
}

static inline __u64
ia64_get_dbr (__u64 regnum)
{
	__u64 retval;

	asm volatile ("mov %0=dbr[%1]" : "=r"(retval) : "r"(regnum));
#ifdef CONFIG_ITANIUM
	asm volatile (";; srlz.d");
#endif
	return retval;
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

static inline __u64
ia64_tpa (__u64 addr)
{
	__u64 result;
	asm ("tpa %0=%1" : "=r"(result) : "r"(addr));
	return result;
}

#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW
#define ARCH_HAS_SPINLOCK_PREFETCH
#define PREFETCH_STRIDE 256

static inline void
prefetch (const void *x)
{
         __asm__ __volatile__ ("lfetch [%0]" : : "r"(x));
}

static inline void
prefetchw (const void *x)
{
	__asm__ __volatile__ ("lfetch.excl [%0]" : : "r"(x));
}

#define spin_lock_prefetch(x)	prefetchw(x)

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_PROCESSOR_H */
