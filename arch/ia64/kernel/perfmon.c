/*
 * This file contains the code to configure and read/write the ia64 performance
 * monitoring stuff.
 *
 * Originaly Written by Ganesh Venkitachalam, IBM Corp.
 * Modifications by David Mosberger-Tang, Hewlett-Packard Co.
 * Modifications by Stephane Eranian, Hewlett-Packard Co.
 * Copyright (C) 1999 Ganesh Venkitachalam <venkitac@us.ibm.com>
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000-2001 Stephane Eranian <eranian@hpl.hp.com>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/wrapper.h>
#include <linux/mm.h>

#include <asm/bitops.h>
#include <asm/efi.h>
#include <asm/errno.h>
#include <asm/hw_irq.h>
#include <asm/page.h>
#include <asm/pal.h>
#include <asm/perfmon.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/signal.h>
#include <asm/system.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/delay.h> /* for ia64_get_itc() */

#ifdef CONFIG_PERFMON

#define PFM_VERSION		"0.2"
#define PFM_SMPL_HDR_VERSION	1

#define PMU_FIRST_COUNTER	4	/* first generic counter */

#define PFM_WRITE_PMCS		0xa0
#define PFM_WRITE_PMDS		0xa1
#define PFM_READ_PMDS		0xa2
#define PFM_STOP		0xa3
#define PFM_START		0xa4
#define PFM_ENABLE		0xa5	/* unfreeze only */
#define PFM_DISABLE		0xa6	/* freeze only */
#define PFM_RESTART		0xcf
#define PFM_CREATE_CONTEXT	0xa7
/*
 * Those 2 are just meant for debugging. I considered using sysctl() for
 * that but it is a little bit too pervasive. This solution is at least
 * self-contained.
 */
#define PFM_DEBUG_ON		0xe0
#define PFM_DEBUG_OFF		0xe1


/*
 * perfmon API flags
 */
#define PFM_FL_INHERIT_NONE	 0x00	/* never inherit a context across fork (default) */
#define PFM_FL_INHERIT_ONCE	 0x01	/* clone pfm_context only once across fork() */
#define PFM_FL_INHERIT_ALL	 0x02	/* always clone pfm_context across fork() */
#define PFM_FL_SMPL_OVFL_NOBLOCK 0x04	/* do not block on sampling buffer overflow */
#define PFM_FL_SYSTEMWIDE	 0x08	/* create a systemwide context */

/*
 * PMC API flags
 */
#define PFM_REGFL_OVFL_NOTIFY	1		/* send notification on overflow */

/*
 * Private flags and masks
 */
#define PFM_FL_INHERIT_MASK	(PFM_FL_INHERIT_NONE|PFM_FL_INHERIT_ONCE|PFM_FL_INHERIT_ALL)

#ifdef CONFIG_SMP
#define cpu_is_online(i) (cpu_online_map & (1UL << i))
#else
#define cpu_is_online(i)	1
#endif

#define PMC_IS_IMPL(i)		(i < pmu_conf.num_pmcs && pmu_conf.impl_regs[i>>6] & (1<< (i&~(64-1))))
#define PMD_IS_IMPL(i)	(i < pmu_conf.num_pmds &&  pmu_conf.impl_regs[4+(i>>6)] & (1<< (i&~(64-1))))
#define PMD_IS_COUNTER(i)	(i>=PMU_FIRST_COUNTER && i < (PMU_FIRST_COUNTER+pmu_conf.max_counters))
#define PMC_IS_COUNTER(i)	(i>=PMU_FIRST_COUNTER && i < (PMU_FIRST_COUNTER+pmu_conf.max_counters))

/* This is the Itanium-specific PMC layout for counter config */
typedef struct {
	unsigned long pmc_plm:4;	/* privilege level mask */
	unsigned long pmc_ev:1;		/* external visibility */
	unsigned long pmc_oi:1;		/* overflow interrupt */
	unsigned long pmc_pm:1;		/* privileged monitor */
	unsigned long pmc_ig1:1;	/* reserved */
	unsigned long pmc_es:7;		/* event select */
	unsigned long pmc_ig2:1;	/* reserved */
	unsigned long pmc_umask:4;	/* unit mask */
	unsigned long pmc_thres:3;	/* threshold */
	unsigned long pmc_ig3:1;	/* reserved (missing from table on p6-17) */
	unsigned long pmc_ism:2;	/* instruction set mask */
	unsigned long pmc_ig4:38;	/* reserved */
} pmc_counter_reg_t;

/* test for EAR/BTB configuration */
#define PMU_DEAR_EVENT	0x67
#define PMU_IEAR_EVENT	0x23
#define PMU_BTB_EVENT	0x11

#define PMC_IS_DEAR(a)		(((pmc_counter_reg_t *)(a))->pmc_es == PMU_DEAR_EVENT)
#define PMC_IS_IEAR(a)		(((pmc_counter_reg_t *)(a))->pmc_es == PMU_IEAR_EVENT)
#define PMC_IS_BTB(a)		(((pmc_counter_reg_t *)(a))->pmc_es == PMU_BTB_EVENT)

/*
 * This header is at the beginning of the sampling buffer returned to the user.
 * It is exported as Read-Only at this point. It is directly followed with the
 * first record.
 */
typedef struct {
	int		hdr_version;		/* could be used to differentiate formats */
	int		hdr_reserved;
	unsigned long	hdr_entry_size;		/* size of one entry in bytes */
	unsigned long	hdr_count;		/* how many valid entries */
	unsigned long	hdr_pmds;		/* which pmds are recorded */
} perfmon_smpl_hdr_t;

/*
 * Header entry in the buffer as a header as follows.
 * The header is directly followed with the PMDS to saved in increasing index order:
 * PMD4, PMD5, .... How many PMDs are present is determined by the tool which must
 * keep track of it when generating the final trace file.
 */
typedef struct {
	int		pid;		/* identification of process */
	int		cpu;		/* which cpu was used */
	unsigned long	rate;		/* initial value of this counter */
	unsigned long	stamp;		/* timestamp */
	unsigned long	ip;		/* where did the overflow interrupt happened */
	unsigned long	regs;		/* which registers overflowed (up to 64)*/
} perfmon_smpl_entry_t;

/*
 * There is one such data structure per perfmon context. It is used to describe the
 * sampling buffer. It is to be shared among siblings whereas the pfm_context isn't.
 * Therefore we maintain a refcnt which is incremented on fork().
 * This buffer is private to the kernel only the actual sampling buffer including its
 * header are exposed to the user. This construct allows us to export the buffer read-write,
 * if needed, without worrying about security problems.
 */
typedef struct {
	atomic_t		psb_refcnt;	/* how many users for the buffer */
	int			reserved;
	void			*psb_addr;	/* points to location of first entry */
	unsigned long		psb_entries;	/* maximum number of entries */
	unsigned long		psb_size;	/* aligned size of buffer */
	unsigned long		psb_index;	/* next free entry slot */
	unsigned long		psb_entry_size;	/* size of each entry including entry header */
	perfmon_smpl_hdr_t	*psb_hdr;	/* points to sampling buffer header */
} pfm_smpl_buffer_desc_t;


/*
 * This structure is initialized at boot time and contains
 * a description of the PMU main characteristic as indicated
 * by PAL
 */
typedef struct {
	unsigned long pfm_is_disabled;	/* indicates if perfmon is working properly */
	unsigned long perf_ovfl_val;	/* overflow value for generic counters   */
	unsigned long max_counters;	/* upper limit on counter pair (PMC/PMD) */
	unsigned long num_pmcs ;	/* highest PMC implemented (may have holes) */
	unsigned long num_pmds;		/* highest PMD implemented (may have holes) */
	unsigned long impl_regs[16];	/* buffer used to hold implememted PMC/PMD mask */
} pmu_config_t;

#define PERFMON_IS_DISABLED() pmu_conf.pfm_is_disabled

typedef struct {
	__u64		val;		/* virtual 64bit counter value */
	__u64		ival;		/* initial value from user */
	__u64		smpl_rval;	/* reset value on sampling overflow */
	__u64		ovfl_rval;	/* reset value on overflow */
	int		flags;		/* notify/do not notify */
} pfm_counter_t;
#define PMD_OVFL_NOTIFY(ctx, i)	((ctx)->ctx_pmds[i].flags &  PFM_REGFL_OVFL_NOTIFY)

/*
 * perfmon context. One per process, is cloned on fork() depending on inheritance flags
 */
typedef struct {
	unsigned int inherit:2;	/* inherit mode */
	unsigned int noblock:1;	/* block/don't block on overflow with notification */
	unsigned int system:1;	/* do system wide monitoring */
	unsigned int frozen:1;	/* pmu must be kept frozen on ctxsw in */
	unsigned int reserved:27;
} pfm_context_flags_t;

typedef struct pfm_context {

	pfm_smpl_buffer_desc_t	*ctx_smpl_buf;		/* sampling buffer descriptor, if any */
	unsigned long		ctx_dear_counter;	/* which PMD holds D-EAR */
	unsigned long		ctx_iear_counter;	/* which PMD holds I-EAR */
	unsigned long		ctx_btb_counter;	/* which PMD holds BTB */

	pid_t			ctx_notify_pid;	/* who to notify on overflow */
	int			ctx_notify_sig;	/* XXX: SIGPROF or other */
	pfm_context_flags_t	ctx_flags;	/* block/noblock */
	pid_t			ctx_creator;	/* pid of creator (debug) */
	unsigned long		ctx_ovfl_regs;	/* which registers just overflowed (notification) */
	unsigned long		ctx_smpl_regs;	/* which registers to record on overflow */

	struct semaphore	ctx_restart_sem; /* use for blocking notification mode */

	pfm_counter_t		ctx_pmds[IA64_NUM_PMD_COUNTERS]; /* XXX: size should be dynamic */
} pfm_context_t;

#define ctx_fl_inherit	ctx_flags.inherit
#define ctx_fl_noblock	ctx_flags.noblock
#define ctx_fl_system	ctx_flags.system
#define ctx_fl_frozen	ctx_flags.frozen

#define CTX_IS_DEAR(c,n)	((c)->ctx_dear_counter == (n))
#define CTX_IS_IEAR(c,n)	((c)->ctx_iear_counter == (n))
#define CTX_IS_BTB(c,n)		((c)->ctx_btb_counter == (n))
#define CTX_OVFL_NOBLOCK(c)	((c)->ctx_fl_noblock == 1)
#define CTX_INHERIT_MODE(c)	((c)->ctx_fl_inherit)
#define CTX_HAS_SMPL(c)		((c)->ctx_smpl_buf != NULL)

static pmu_config_t pmu_conf;

/* for debug only */
static unsigned long pfm_debug=0;	/* 0= nodebug, >0= debug output on */
#define DBprintk(a) \
	do { \
		if (pfm_debug >0) { printk(__FUNCTION__" "); printk a; } \
	} while (0);

static void perfmon_softint(unsigned long ignored);
static void ia64_reset_pmu(void);

DECLARE_TASKLET(pfm_tasklet, perfmon_softint, 0);

/*
 * structure used to pass information between the interrupt handler
 * and the tasklet.
 */
typedef struct {
	pid_t		to_pid;		/* which process to notify */
	pid_t		from_pid;	/* which process is source of overflow */
	int		sig;		/* with which signal */
	unsigned long	bitvect;	/* which counters have overflowed */
} notification_info_t;

#define notification_is_invalid(i)	(i->to_pid < 2)

/* will need to be cache line padded */
static notification_info_t notify_info[NR_CPUS];

/*
 * We force cache line alignment to avoid false sharing
 * given that we have one entry per CPU.
 */
static struct {
	struct task_struct *owner;
} ____cacheline_aligned pmu_owners[NR_CPUS];
/* helper macros */
#define SET_PMU_OWNER(t)	do { pmu_owners[smp_processor_id()].owner = (t); } while(0);
#define PMU_OWNER()		pmu_owners[smp_processor_id()].owner

/* for debug only */
static struct proc_dir_entry *perfmon_dir;

/*
 * finds the number of PM(C|D) registers given
 * the bitvector returned by PAL
 */
static unsigned long __init
find_num_pm_regs(long *buffer)
{
	int i=3; /* 4 words/per bitvector */

	/* start from the most significant word */
	while (i>=0 && buffer[i] == 0 ) i--;
	if (i< 0) {
		printk(KERN_ERR "perfmon: No bit set in pm_buffer\n");
		return 0;
	}
	return 1+ ia64_fls(buffer[i]) + 64 * i;
}


/*
 * Generates a unique (per CPU) timestamp
 */
static inline unsigned long
perfmon_get_stamp(void)
{
	/*
	 * XXX: maybe find something more efficient
	 */
	return ia64_get_itc();
}

/* Given PGD from the address space's page table, return the kernel
 * virtual mapping of the physical memory mapped at ADR.
 */
static inline unsigned long
uvirt_to_kva(pgd_t *pgd, unsigned long adr)
{
	unsigned long ret = 0UL;
	pmd_t *pmd;
	pte_t *ptep, pte;

	if (!pgd_none(*pgd)) {
		pmd = pmd_offset(pgd, adr);
		if (!pmd_none(*pmd)) {
			ptep = pte_offset(pmd, adr);
			pte = *ptep;
			if (pte_present(pte)) {
				ret = (unsigned long) page_address(pte_page(pte));
				ret |= (adr & (PAGE_SIZE - 1));
			}
		}
	}
	DBprintk(("uv2kva(%lx-->%lx)\n", adr, ret));
	return ret;
}


/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static inline unsigned long
kvirt_to_pa(unsigned long adr)
{
	__u64 pa;
	__asm__ __volatile__ ("tpa %0 = %1" : "=r"(pa) : "r"(adr) : "memory");
	DBprintk(("kv2pa(%lx-->%lx)\n", adr, pa));
	return pa;
}

static void *
rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr, page;

	/* XXX: may have to revisit this part because
	 * vmalloc() does not necessarily return a page-aligned buffer.
	 * This maybe a security problem when mapped at user level
	 */
	mem=vmalloc(size);
	if (mem) {
		memset(mem, 0, size); /* Clear the ram out, no junk to the user */
		adr=(unsigned long) mem;
		while (size > 0) {
			page = kvirt_to_pa(adr);
			mem_map_reserve(virt_to_page(__va(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

static void
rvfree(void *mem, unsigned long size)
{
	unsigned long adr, page;

	if (mem) {
		adr=(unsigned long) mem;
		while (size > 0) {
			page = kvirt_to_pa(adr);
			mem_map_unreserve(virt_to_page(__va(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
}

static pfm_context_t *
pfm_context_alloc(void)
{
	pfm_context_t *pfc;

	/* allocate context descriptor */
	pfc = vmalloc(sizeof(*pfc));
	if (pfc) memset(pfc, 0, sizeof(*pfc));

	return pfc;
}

static void
pfm_context_free(pfm_context_t *pfc)
{
	if (pfc) vfree(pfc);
}

static int
pfm_remap_buffer(unsigned long buf, unsigned long addr, unsigned long size)
{
	unsigned long page;

	while (size > 0) {
		page = kvirt_to_pa(buf);

		if (remap_page_range(addr, page, PAGE_SIZE, PAGE_SHARED)) return -ENOMEM;

		addr  += PAGE_SIZE;
		buf   += PAGE_SIZE;
		size  -= PAGE_SIZE;
	}
	return 0;
}

/*
 * counts the number of PMDS to save per entry.
 * This code is generic enough to accomodate more than 64 PMDS when they become available
 */
static unsigned long
pfm_smpl_entry_size(unsigned long *which, unsigned long size)
{
	unsigned long res = 0;
	int i;

	for (i=0; i < size; i++, which++) res += hweight64(*which);

	DBprintk((" res=%ld\n", res));

	return res;
}

/*
 * Allocates the sampling buffer and remaps it into caller's address space
 */
static int
pfm_smpl_buffer_alloc(pfm_context_t *ctx, unsigned long which_pmds, unsigned long entries, void **user_addr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long addr, size, regcount;
	void *smpl_buf;
	pfm_smpl_buffer_desc_t *psb;

	regcount = pfm_smpl_entry_size(&which_pmds, 1);

	/* note that regcount might be 0, in this case only the header for each
	 * entry will be recorded.
	 */

	/*
	 * 1 buffer hdr and for each entry a header + regcount PMDs to save
	 */
	size = PAGE_ALIGN(  sizeof(perfmon_smpl_hdr_t)
			  + entries * (sizeof(perfmon_smpl_entry_t) + regcount*sizeof(u64)));
	/*
	 * check requested size to avoid Denial-of-service attacks
	 * XXX: may have to refine this test
	 */
	if (size > current->rlim[RLIMIT_MEMLOCK].rlim_cur) return -EAGAIN;

	/* find some free area in address space */
	addr = get_unmapped_area(NULL, 0, size, 0, MAP_PRIVATE);
	if (!addr) goto no_addr;

	DBprintk((" entries=%ld aligned size=%ld, unmapped @0x%lx\n", entries, size, addr));

	/* allocate vma */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!vma) goto no_vma;

	/* XXX: see rvmalloc() for page alignment problem */
	smpl_buf = rvmalloc(size);
	if (smpl_buf == NULL) goto no_buffer;

	DBprintk((" smpl_buf @%p\n", smpl_buf));

	if (pfm_remap_buffer((unsigned long)smpl_buf, addr, size)) goto cant_remap;

	/* allocate sampling buffer descriptor now */
	psb = vmalloc(sizeof(*psb));
	if (psb == NULL) goto no_buffer_desc;

	/* start with something clean */
	memset(smpl_buf, 0x0, size);

	psb->psb_hdr	 = smpl_buf;
	psb->psb_addr    = (char *)smpl_buf+sizeof(perfmon_smpl_hdr_t); /* first entry */
	psb->psb_size    = size; /* aligned size */
	psb->psb_index   = 0;
	psb->psb_entries = entries;

	atomic_set(&psb->psb_refcnt, 1);

	psb->psb_entry_size = sizeof(perfmon_smpl_entry_t) + regcount*sizeof(u64);

	DBprintk((" psb @%p entry_size=%ld hdr=%p addr=%p\n", (void *)psb,psb->psb_entry_size, (void *)psb->psb_hdr, (void *)psb->psb_addr));

	/* initialize some of the fields of header */
	psb->psb_hdr->hdr_version    = PFM_SMPL_HDR_VERSION;
	psb->psb_hdr->hdr_entry_size = sizeof(perfmon_smpl_entry_t)+regcount*sizeof(u64);
	psb->psb_hdr->hdr_pmds	     = which_pmds;

	/* store which PMDS to record */
	ctx->ctx_smpl_regs = which_pmds;

	/* link to perfmon context */
	ctx->ctx_smpl_buf  = psb;

	/*
	 * initialize the vma for the sampling buffer
	 */
	vma->vm_mm	  = mm;
	vma->vm_start	  = addr;
	vma->vm_end	  = addr + size;
	vma->vm_flags	  = VM_READ|VM_MAYREAD;
	vma->vm_page_prot = PAGE_READONLY; /* XXX may need to change */
	vma->vm_ops	  = NULL;
	vma->vm_pgoff	  = 0;
	vma->vm_file	  = NULL;
	vma->vm_raend	  = 0;

	vma->vm_private_data = ctx;	/* link to pfm_context(not yet used) */

	/*
	 * now insert the vma in the vm list for the process
	 */
	insert_vm_struct(mm, vma);

	mm->total_vm  += size >> PAGE_SHIFT;

	/*
	 * that's the address returned to the user
	 */
	*user_addr = (void *)addr;

	return 0;

	/* outlined error handling */
no_addr:
	DBprintk(("Cannot find unmapped area for size %ld\n", size));
	return -ENOMEM;
no_vma:
	DBprintk(("Cannot allocate vma\n"));
	return -ENOMEM;
cant_remap:
	DBprintk(("Can't remap buffer\n"));
	rvfree(smpl_buf, size);
no_buffer:
	DBprintk(("Can't allocate sampling buffer\n"));
	kmem_cache_free(vm_area_cachep, vma);
	return -ENOMEM;
no_buffer_desc:
	DBprintk(("Can't allocate sampling buffer descriptor\n"));
	kmem_cache_free(vm_area_cachep, vma);
	rvfree(smpl_buf, size);
	return -ENOMEM;
}

static int
pfx_is_sane(pfreq_context_t *pfx)
{
	/* valid signal */
	if (pfx->notify_sig < 1 || pfx->notify_sig >= _NSIG) return 0;

	/* cannot send to process 1, 0 means do not notify */
	if (pfx->notify_pid < 0 || pfx->notify_pid == 1) return 0;

	/* probably more to add here */

	return 1;
}

static int
pfm_context_create(struct task_struct *task, int flags, perfmon_req_t *req)
{
	pfm_context_t *ctx;
	perfmon_req_t tmp;
	void *uaddr = NULL;
	int ret = -EFAULT;
	int ctx_flags;

	/* to go away */
	if (flags) {
		printk("perfmon: use context flags instead of perfmon() flags. Obsoleted API\n");
	}

	if (copy_from_user(&tmp, req, sizeof(tmp))) return -EFAULT;

	ctx_flags = tmp.pfr_ctx.flags;

	/* not yet supported */
	if (ctx_flags & PFM_FL_SYSTEMWIDE) return -EINVAL;

	if (!pfx_is_sane(&tmp.pfr_ctx)) return -EINVAL;

	ctx = pfm_context_alloc();
	if (!ctx) return -ENOMEM;

	/* record who the creator is (for debug) */
	ctx->ctx_creator = task->pid;

	ctx->ctx_notify_pid = tmp.pfr_ctx.notify_pid;
	ctx->ctx_notify_sig = SIGPROF;	/* siginfo imposes a fixed signal */

	if (tmp.pfr_ctx.smpl_entries) {
		DBprintk((" sampling entries=%ld\n",tmp.pfr_ctx.smpl_entries));
		if ((ret=pfm_smpl_buffer_alloc(ctx, tmp.pfr_ctx.smpl_regs, tmp.pfr_ctx.smpl_entries, &uaddr)) ) goto buffer_error;
		tmp.pfr_ctx.smpl_vaddr = uaddr;
	}
	/* initialization of context's flags */
	ctx->ctx_fl_inherit = ctx_flags & PFM_FL_INHERIT_MASK;
	ctx->ctx_fl_noblock = (ctx_flags & PFM_FL_SMPL_OVFL_NOBLOCK) ? 1 : 0;
	ctx->ctx_fl_system  = (ctx_flags & PFM_FL_SYSTEMWIDE) ? 1: 0;
	ctx->ctx_fl_frozen  = 0;

	sema_init(&ctx->ctx_restart_sem, 0); /* init this semaphore to locked */

	if (copy_to_user(req, &tmp, sizeof(tmp))) goto buffer_error;

	DBprintk((" context=%p, pid=%d notify_sig %d notify_pid=%d\n",(void *)ctx, task->pid, ctx->ctx_notify_sig, ctx->ctx_notify_pid));
	DBprintk((" context=%p, pid=%d flags=0x%x inherit=%d noblock=%d system=%d\n",(void *)ctx, task->pid, ctx_flags, ctx->ctx_fl_inherit, ctx->ctx_fl_noblock, ctx->ctx_fl_system));

	/* link with task */
	task->thread.pfm_context = ctx;

	return 0;

buffer_error:
	vfree(ctx);

	return ret;
}

static void
pfm_reset_regs(pfm_context_t *ctx)
{
	unsigned long mask = ctx->ctx_ovfl_regs;
	int i, cnum;

	DBprintk((" ovfl_regs=0x%lx\n", mask));
	/*
	 * now restore reset value on sampling overflowed counters
	 */
	for(i=0, cnum=PMU_FIRST_COUNTER; i < pmu_conf.max_counters; i++, cnum++, mask >>= 1) {
		if (mask & 0x1) {
			DBprintk((" reseting PMD[%d]=%lx\n", cnum, ctx->ctx_pmds[i].smpl_rval & pmu_conf.perf_ovfl_val));

			/* upper part is ignored on rval */
			ia64_set_pmd(cnum, ctx->ctx_pmds[i].smpl_rval);
		}
	}
}

static int
pfm_write_pmcs(struct task_struct *ta, perfmon_req_t *req, int count)
{
	struct thread_struct *th = &ta->thread;
	pfm_context_t *ctx = th->pfm_context;
	perfmon_req_t tmp;
	unsigned long cnum;
	int i;

	/* XXX: ctx locking may be required here */

	for (i = 0; i < count; i++, req++) {

		if (copy_from_user(&tmp, req, sizeof(tmp))) return -EFAULT;

		cnum = tmp.pfr_reg.reg_num;

		/* XXX needs to check validity of the data maybe */
		if (!PMC_IS_IMPL(cnum)) {
			DBprintk((" invalid pmc[%ld]\n", cnum));
			return -EINVAL;
		}

		if (PMC_IS_COUNTER(cnum)) {

			/*
			 * we keep track of EARS/BTB to speed up sampling later
			 */
			if (PMC_IS_DEAR(&tmp.pfr_reg.reg_value)) {
				ctx->ctx_dear_counter = cnum;
			} else if (PMC_IS_IEAR(&tmp.pfr_reg.reg_value)) {
				ctx->ctx_iear_counter = cnum;
			} else if (PMC_IS_BTB(&tmp.pfr_reg.reg_value)) {
				ctx->ctx_btb_counter = cnum;
			}

			if (tmp.pfr_reg.reg_flags & PFM_REGFL_OVFL_NOTIFY)
				ctx->ctx_pmds[cnum - PMU_FIRST_COUNTER].flags |= PFM_REGFL_OVFL_NOTIFY;
		}

		ia64_set_pmc(cnum, tmp.pfr_reg.reg_value);
			DBprintk((" setting PMC[%ld]=0x%lx flags=0x%x\n", cnum, tmp.pfr_reg.reg_value, ctx->ctx_pmds[cnum - PMU_FIRST_COUNTER].flags));

	}
	/*
	 * we have to set this here event hough we haven't necessarily started monitoring
	 * because we may be context switched out
	 */
	th->flags |= IA64_THREAD_PM_VALID;

	return 0;
}

static int
pfm_write_pmds(struct task_struct *ta, perfmon_req_t *req, int count)
{
	struct thread_struct *th = &ta->thread;
	pfm_context_t *ctx = th->pfm_context;
	perfmon_req_t tmp;
	unsigned long cnum;
	int i;

	/* XXX: ctx locking may be required here */

	for (i = 0; i < count; i++, req++) {
		int k;

		if (copy_from_user(&tmp, req, sizeof(tmp))) return -EFAULT;

		cnum = tmp.pfr_reg.reg_num;

		k = cnum - PMU_FIRST_COUNTER;

		if (!PMD_IS_IMPL(cnum)) return -EINVAL;

		/* update virtualized (64bits) counter */
		if (PMD_IS_COUNTER(cnum)) {
			ctx->ctx_pmds[k].ival = tmp.pfr_reg.reg_value;
			ctx->ctx_pmds[k].val  = tmp.pfr_reg.reg_value & ~pmu_conf.perf_ovfl_val;
			ctx->ctx_pmds[k].smpl_rval = tmp.pfr_reg.reg_smpl_reset;
			ctx->ctx_pmds[k].ovfl_rval = tmp.pfr_reg.reg_ovfl_reset;
		}

		/* writes to unimplemented part is ignored, so this is safe */
		ia64_set_pmd(cnum, tmp.pfr_reg.reg_value);

		/* to go away */
		ia64_srlz_d();
		DBprintk((" setting PMD[%ld]:  pmd.val=0x%lx pmd.ovfl_rval=0x%lx pmd.smpl_rval=0x%lx pmd=%lx\n",
					cnum,
					ctx->ctx_pmds[k].val,
					ctx->ctx_pmds[k].ovfl_rval,
					ctx->ctx_pmds[k].smpl_rval,
					ia64_get_pmd(cnum) & pmu_conf.perf_ovfl_val));
	}
	/*
	 * we have to set this here event hough we haven't necessarily started monitoring
	 * because we may be context switched out
	 */
	th->flags |= IA64_THREAD_PM_VALID;

	return 0;
}

static int
pfm_read_pmds(struct task_struct *ta, perfmon_req_t *req, int count)
{
	struct thread_struct *th = &ta->thread;
	pfm_context_t *ctx = th->pfm_context;
	unsigned long val=0;
	perfmon_req_t tmp;
	int i;

	/*
	 * XXX: MUST MAKE SURE WE DON"T HAVE ANY PENDING OVERFLOW BEFORE READING
	 * This is required when the monitoring has been stoppped by user of kernel.
	 * If ity is still going on, then that's fine because we a re not gauranteed
	 * to return an accurate value in this case
	 */

	/* XXX: ctx locking may be required here */

	for (i = 0; i < count; i++, req++) {
		if (copy_from_user(&tmp, req, sizeof(tmp))) return -EFAULT;

		if (!PMD_IS_IMPL(tmp.pfr_reg.reg_num)) return -EINVAL;

		if (PMD_IS_COUNTER(tmp.pfr_reg.reg_num)) {
			if (ta == current){
				val = ia64_get_pmd(tmp.pfr_reg.reg_num);
			} else {
				val = th->pmd[tmp.pfr_reg.reg_num];
			}
			val &= pmu_conf.perf_ovfl_val;
			/*
			 * lower part of .val may not be zero, so we must be an addition because of
			 * residual count (see update_counters).
			 */
			val += ctx->ctx_pmds[tmp.pfr_reg.reg_num - PMU_FIRST_COUNTER].val;
		} else {
			/* for now */
			if (ta != current) return -EINVAL;

			val = ia64_get_pmd(tmp.pfr_reg.reg_num);
		}
		tmp.pfr_reg.reg_value = val;

		DBprintk((" reading PMD[%ld]=0x%lx\n", tmp.pfr_reg.reg_num, val));

		if (copy_to_user(req, &tmp, sizeof(tmp))) return -EFAULT;
	}
	return 0;
}

static int
pfm_do_restart(struct task_struct *task)
{
	struct thread_struct *th = &task->thread;
	pfm_context_t *ctx = th->pfm_context;
	void *sem = &ctx->ctx_restart_sem;

	if (task == current) {
		DBprintk((" restartig self %d frozen=%d \n", current->pid, ctx->ctx_fl_frozen));

		pfm_reset_regs(ctx);

		/*
		 * We ignore block/don't block because we never block
		 * for a self-monitoring process.
		 */
		ctx->ctx_fl_frozen = 0;

		if (CTX_HAS_SMPL(ctx)) {
			ctx->ctx_smpl_buf->psb_hdr->hdr_count = 0;
			ctx->ctx_smpl_buf->psb_index = 0;
		}

		/* pfm_reset_smpl_buffers(ctx,th->pfm_ovfl_regs);*/

		/* simply unfreeze */
		ia64_set_pmc(0, 0);
		ia64_srlz_d();

		return 0;
	}

	/* check if blocking */
	if (CTX_OVFL_NOBLOCK(ctx) == 0) {
		DBprintk((" unblocking %d \n", task->pid));
		up(sem);
		return 0;
	}

	/*
	 * in case of non blocking mode, then it's just a matter of
	 * of reseting the sampling buffer (if any) index. The PMU
	 * is already active.
	 */

	/*
	 * must reset the header count first
	 */
	if (CTX_HAS_SMPL(ctx)) {
		DBprintk((" resetting sampling indexes for %d \n", task->pid));
		ctx->ctx_smpl_buf->psb_hdr->hdr_count = 0;
		ctx->ctx_smpl_buf->psb_index = 0;
	}

	return 0;
}


static int
do_perfmonctl (struct task_struct *task, int cmd, int flags, perfmon_req_t *req, int count, struct pt_regs *regs)
{
	perfmon_req_t tmp;
	struct thread_struct *th = &task->thread;
	pfm_context_t *ctx = th->pfm_context;

	memset(&tmp, 0, sizeof(tmp));

	switch (cmd) {
		case PFM_CREATE_CONTEXT:
			/* a context has already been defined */
			if (ctx) return -EBUSY;

			/* may be a temporary limitation */
			if (task != current) return -EINVAL;

			if (req == NULL || count != 1) return -EINVAL;

			if (!access_ok(VERIFY_READ, req, sizeof(struct perfmon_req_t)*count)) return -EFAULT;

			return pfm_context_create(task, flags, req);

		case PFM_WRITE_PMCS:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			if (!access_ok(VERIFY_READ, req, sizeof(struct perfmon_req_t)*count)) return -EFAULT;

			if (!ctx) {
				DBprintk((" PFM_WRITE_PMCS: no context for task %d\n", task->pid));
				return -EINVAL;
			}
			return pfm_write_pmcs(task, req, count);

		case PFM_WRITE_PMDS:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			if (!access_ok(VERIFY_READ, req, sizeof(struct perfmon_req_t)*count)) return -EFAULT;

			if (!ctx) {
				DBprintk((" PFM_WRITE_PMDS: no context for task %d\n", task->pid));
				return -EINVAL;
			}
			return pfm_write_pmds(task, req, count);

		case PFM_START:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			if (!ctx) {
				DBprintk((" PFM_START: no context for task %d\n", task->pid));
				return -EINVAL;
			}

			SET_PMU_OWNER(current);

			/* will start monitoring right after rfi */
			ia64_psr(regs)->up = 1;

			/*
			 * mark the state as valid.
			 * this will trigger save/restore at context switch
			 */
			th->flags |= IA64_THREAD_PM_VALID;

			ia64_set_pmc(0, 0);
			ia64_srlz_d();

		break;

		case PFM_ENABLE:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			if (!ctx) {
				DBprintk((" PFM_ENABLE: no context for task %d\n", task->pid));
				return -EINVAL;
			}

			/* reset all registers to stable quiet state */
			ia64_reset_pmu();

			/* make sure nothing starts */
			ia64_psr(regs)->up = 0;
			ia64_psr(regs)->pp = 0;

			/* do it on the live register as well */
			__asm__ __volatile__ ("rsm psr.pp|psr.pp;;"::: "memory");

			SET_PMU_OWNER(current);

			/*
			 * mark the state as valid.
			 * this will trigger save/restore at context switch
			 */
			th->flags |= IA64_THREAD_PM_VALID;

			/* simply unfreeze */
			ia64_set_pmc(0, 0);
			ia64_srlz_d();
			break;

		case PFM_DISABLE:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			/* simply freeze */
			ia64_set_pmc(0, 1);
			ia64_srlz_d();
			break;

		case PFM_READ_PMDS:
			if (!access_ok(VERIFY_READ, req, sizeof(struct perfmon_req_t)*count)) return -EFAULT;
			if (!access_ok(VERIFY_WRITE, req, sizeof(struct perfmon_req_t)*count)) return -EFAULT;

			if (!ctx) {
				DBprintk((" PFM_READ_PMDS: no context for task %d\n", task->pid));
				return -EINVAL;
			}
			return pfm_read_pmds(task, req, count);

	      case PFM_STOP:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			ia64_set_pmc(0, 1);
			ia64_srlz_d();

			ia64_psr(regs)->up = 0;

			th->flags &= ~IA64_THREAD_PM_VALID;

			SET_PMU_OWNER(NULL);

			/* we probably will need some more cleanup here */
			break;

	      case PFM_DEBUG_ON:
			printk(" debugging on\n");
			pfm_debug = 1;
			break;

	      case PFM_DEBUG_OFF:
			printk(" debugging off\n");
			pfm_debug = 0;
			break;

	      case PFM_RESTART: /* temporary, will most likely end up as a PFM_ENABLE */

			if ((th->flags & IA64_THREAD_PM_VALID) == 0) {
				printk(" PFM_RESTART not monitoring\n");
				return -EINVAL;
			}
			if (!ctx) {
				printk(" PFM_RESTART no ctx for %d\n", task->pid);
				return -EINVAL;
			}
			if (CTX_OVFL_NOBLOCK(ctx) == 0 && ctx->ctx_fl_frozen==0) {
				printk("task %d without pmu_frozen set\n", task->pid);
				return -EINVAL;
			}

			return pfm_do_restart(task); /* we only look at first entry */

	      default:
			DBprintk((" UNknown command 0x%x\n", cmd));
			return -EINVAL;
	}
	return 0;
}

/*
 * XXX: do something better here
 */
static int
perfmon_bad_permissions(struct task_struct *task)
{
	/* stolen from bad_signal() */
	return (current->session != task->session)
	    && (current->euid ^ task->suid) && (current->euid ^ task->uid)
	    && (current->uid ^ task->suid) && (current->uid ^ task->uid);
}

asmlinkage int
sys_perfmonctl (int pid, int cmd, int flags, perfmon_req_t *req, int count, long arg6, long arg7, long arg8, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
	struct task_struct *child = current;
	int ret = -ESRCH;

	/* sanity check:
	 *
	 * ensures that we don't do bad things in case the OS
	 * does not have enough storage to save/restore PMC/PMD
	 */
	if (PERFMON_IS_DISABLED()) return -ENOSYS;

	/* XXX: pid interface is going away in favor of pfm context */
	if (pid != current->pid) {
		read_lock(&tasklist_lock);
		{
			child = find_task_by_pid(pid);
			if (child)
				get_task_struct(child);
		}

		if (!child) goto abort_call;

		ret = -EPERM;

		if (perfmon_bad_permissions(child)) goto abort_call;

		/*
		 * XXX: need to do more checking here
		 */
		if (child->state != TASK_ZOMBIE && child->state != TASK_STOPPED) {
			DBprintk((" warning process %d not in stable state %ld\n", pid, child->state));
		}
	}
	ret = do_perfmonctl(child, cmd, flags, req, count, regs);

abort_call:
	if (child != current) read_unlock(&tasklist_lock);

	return ret;
}


/*
 * This function is invoked on the exit path of the kernel. Therefore it must make sure
 * it does does modify the caller's input registers (in0-in7) in case of entry by system call
 * which can be restarted. That's why it's declared as a system call and all 8 possible args
 * are declared even though not used.
 */
#if __GNUC__ >= 3
void asmlinkage
pfm_overflow_notify(void)
#else
void asmlinkage
pfm_overflow_notify(u64 arg0, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6, u64 arg7)
#endif
{
	struct task_struct *task;
	struct thread_struct *th = &current->thread;
	pfm_context_t *ctx = current->thread.pfm_context;
	struct siginfo si;
	int ret;

	/*
	 * do some sanity checks first
	 */
	if (!ctx) {
		printk("perfmon: process %d has no PFM context\n", current->pid);
		return;
	}
	if (ctx->ctx_notify_pid < 2) {
		printk("perfmon: process %d invalid notify_pid=%d\n", current->pid, ctx->ctx_notify_pid);
		return;
	}

	DBprintk((" current=%d ctx=%p bv=0%lx\n", current->pid, (void *)ctx, ctx->ctx_ovfl_regs));
	/*
	 * NO matter what notify_pid is,
	 * we clear overflow, won't notify again
	 */
	th->pfm_pend_notify = 0;

	/*
	 * When measuring in kernel mode and non-blocking fashion, it is possible to
	 * get an overflow while executing this code. Therefore the state of pend_notify
	 * and ovfl_regs can be altered. The important point is not to loose any notification.
	 * It is fine to get called for nothing. To make sure we do collect as much state as
	 * possible, update_counters() always uses |= to add bit to the ovfl_regs field.
	 *
	 * In certain cases, it is possible to come here, with ovfl_regs == 0;
	 *
	 * XXX: pend_notify and ovfl_regs could be merged maybe !
	 */
	if (ctx->ctx_ovfl_regs == 0) {
		printk("perfmon: spurious overflow notification from pid %d\n", current->pid);
		return;
	}
	read_lock(&tasklist_lock);

	task = find_task_by_pid(ctx->ctx_notify_pid);

	if (task) {
		si.si_signo    = ctx->ctx_notify_sig;
		si.si_errno    = 0;
		si.si_code     = PROF_OVFL; /* goes to user */
		si.si_addr     = NULL;
		si.si_pid      = current->pid; /* who is sending */
		si.si_pfm_ovfl = ctx->ctx_ovfl_regs;

		DBprintk((" SIGPROF to %d @ %p\n", task->pid, (void *)task));

		/* must be done with tasklist_lock locked */
		ret = send_sig_info(ctx->ctx_notify_sig, &si, task);
		if (ret != 0) {
			DBprintk((" send_sig_info(process %d, SIGPROF)=%d\n", ctx->ctx_notify_pid, ret));
			task = NULL; /* will cause return */
		}
	} else {
		printk("perfmon: notify_pid %d not found\n", ctx->ctx_notify_pid);
	}

	read_unlock(&tasklist_lock);

	/* now that we have released the lock handle error condition */
	if (!task || CTX_OVFL_NOBLOCK(ctx)) {
		/* we clear all pending overflow bits in noblock mode */
		ctx->ctx_ovfl_regs = 0;
		return;
	}
	DBprintk((" CPU%d %d before sleep\n", smp_processor_id(), current->pid));

	/*
	 * may go through without blocking on SMP systems
	 * if restart has been received already by the time we call down()
	 */
	ret = down_interruptible(&ctx->ctx_restart_sem);

	DBprintk((" CPU%d %d after sleep ret=%d\n", smp_processor_id(), current->pid, ret));

	/*
	 * in case of interruption of down() we don't restart anything
	 */
	if (ret >= 0) {
		/* we reactivate on context switch */
		ctx->ctx_fl_frozen = 0;
		/*
		 * the ovfl_sem is cleared by the restart task and this is safe because we always
		 * use the local reference
		 */

		pfm_reset_regs(ctx);

		/* now we can clear this mask */
		ctx->ctx_ovfl_regs = 0;

		/*
		 * Unlock sampling buffer and reset index atomically
		 * XXX: not really needed when blocking
		 */
		if (CTX_HAS_SMPL(ctx)) {
			ctx->ctx_smpl_buf->psb_hdr->hdr_count = 0;
			ctx->ctx_smpl_buf->psb_index = 0;
		}

		DBprintk((" CPU%d %d unfreeze PMU\n", smp_processor_id(), current->pid));

		ia64_set_pmc(0, 0);
		ia64_srlz_d();

		/* state restored, can go back to work (user mode) */
	}
}

static void
perfmon_softint(unsigned long ignored)
{
	notification_info_t *info;
	int my_cpu = smp_processor_id();
	struct task_struct *task;
	struct siginfo si;

	info = notify_info+my_cpu;

	DBprintk((" CPU%d current=%d to_pid=%d from_pid=%d bv=0x%lx\n", \
		smp_processor_id(), current->pid, info->to_pid, info->from_pid, info->bitvect));

	/* assumption check */
	if (info->from_pid == info->to_pid) {
		DBprintk((" Tasklet assumption error: from=%d tor=%d\n", info->from_pid, info->to_pid));
		return;
	}

	if (notification_is_invalid(info)) {
		DBprintk((" invalid notification information\n"));
		return;
	}

	/* sanity check */
	if (info->to_pid == 1) {
		DBprintk((" cannot notify init\n"));
		return;
	}
	/*
	 * XXX: needs way more checks here to make sure we send to a task we have control over
	 */
	read_lock(&tasklist_lock);

	task = find_task_by_pid(info->to_pid);

	DBprintk((" after find %p\n", (void *)task));

	if (task) {
		int ret;

		si.si_signo    = SIGPROF;
		si.si_errno    = 0;
		si.si_code     = PROF_OVFL; /* goes to user */
		si.si_addr     =  NULL;
		si.si_pid      = info->from_pid; /* who is sending */
		si.si_pfm_ovfl = info->bitvect;

		DBprintk((" SIGPROF to %d @ %p\n", task->pid, (void *)task));

		/* must be done with tasklist_lock locked */
		ret = send_sig_info(SIGPROF, &si, task);
		if (ret != 0)
			DBprintk((" send_sig_info(process %d, SIGPROF)=%d\n", info->to_pid, ret));

		/* invalidate notification */
		info->to_pid  = info->from_pid = 0;
		info->bitvect = 0;
	}

	read_unlock(&tasklist_lock);

	DBprintk((" after unlock %p\n", (void *)task));

	if (!task) {
		printk("perfmon: CPU%d cannot find process %d\n", smp_processor_id(), info->to_pid);
	}
}

/*
 * main overflow processing routine.
 * it can be called from the interrupt path or explicitely during the context switch code
 * Return:
 *	0 : do not unfreeze the PMU
 *	1 : PMU can be unfrozen
 */
static unsigned long
update_counters (struct task_struct *ta, u64 pmc0, struct pt_regs *regs)
{
	unsigned long mask, i, cnum;
	struct thread_struct *th;
	pfm_context_t *ctx;
	unsigned long bv = 0;
	int my_cpu = smp_processor_id();
	int ret = 1, buffer_is_full = 0;
	int ovfl_is_smpl, can_notify, need_reset_pmd16=0;
	/*
	 * It is never safe to access the task for which the overflow interrupt is destinated
	 * using the current variable as the interrupt may occur in the middle of a context switch
	 * where current does not hold the task that is running yet.
	 *
	 * For monitoring, however, we do need to get access to the task which caused the overflow
	 * to account for overflow on the counters.
	 *
	 * We accomplish this by maintaining a current owner of the PMU per CPU. During context
	 * switch the ownership is changed in a way such that the reflected owner is always the
	 * valid one, i.e. the one that caused the interrupt.
	 */

	if (ta == NULL) {
		DBprintk((" owners[%d]=NULL\n", my_cpu));
		return 0x1;
	}
	th  = &ta->thread;
	ctx = th->pfm_context;

	/*
	 * XXX: debug test
	 * Don't think this could happen given upfront tests
	 */
	if ((th->flags & IA64_THREAD_PM_VALID) == 0) {
		printk("perfmon: Spurious overflow interrupt: process %d not using perfmon\n", ta->pid);
		return 0x1;
	}
	if (!ctx) {
		printk("perfmon: Spurious overflow interrupt: process %d has no PFM context\n", ta->pid);
		return 0;
	}

	/*
	 * sanity test. Should never happen
	 */
	if ((pmc0 & 0x1 )== 0) {
		printk("perfmon: pid %d pmc0=0x%lx assumption error for freeze bit\n", ta->pid, pmc0);
		return 0x0;
	}

	mask = pmc0 >> PMU_FIRST_COUNTER;

	DBprintk(("pmc0=0x%lx pid=%d\n", pmc0, ta->pid));

	DBprintk(("ctx is in %s mode\n", CTX_OVFL_NOBLOCK(ctx) ? "NO-BLOCK" : "BLOCK"));

	if (CTX_HAS_SMPL(ctx)) {
		pfm_smpl_buffer_desc_t *psb = ctx->ctx_smpl_buf;
		unsigned long *e, m, idx=0;
		perfmon_smpl_entry_t *h;
		int j;

		idx = ia64_fetch_and_add(1, &psb->psb_index);
		DBprintk((" trying to record index=%ld entries=%ld\n", idx, psb->psb_entries));

		/*
		 * XXX: there is a small chance that we could run out on index before resetting
		 * but index is unsigned long, so it will take some time.....
		 */
		if (idx > psb->psb_entries) {
			buffer_is_full = 1;
			goto reload_pmds;
		}

		/* first entry is really entry 0, not 1 caused by fetch_and_add */
		idx--;

		h = (perfmon_smpl_entry_t *)(((char *)psb->psb_addr) + idx*(psb->psb_entry_size));

		h->pid  = ta->pid;
		h->cpu  = my_cpu;
		h->rate = 0;
		h->ip   = regs ? regs->cr_iip : 0x0; /* where did the fault happened */
		h->regs = mask; /* which registers overflowed */

		/* guaranteed to monotonically increase on each cpu */
		h->stamp = perfmon_get_stamp();

		e = (unsigned long *)(h+1);
		/*
		 * selectively store PMDs in increasing index number
		 */
		for (j=0, m = ctx->ctx_smpl_regs; m; m >>=1, j++) {
			if (m & 0x1) {
				if (PMD_IS_COUNTER(j))
					*e =  ctx->ctx_pmds[j-PMU_FIRST_COUNTER].val
					    + (ia64_get_pmd(j) & pmu_conf.perf_ovfl_val);
				else
					*e = ia64_get_pmd(j); /* slow */
				DBprintk((" e=%p pmd%d =0x%lx\n", (void *)e, j, *e));
				e++;
			}
		}
		/* make the new entry visible to user, needs to be atomic */
		ia64_fetch_and_add(1, &psb->psb_hdr->hdr_count);

		DBprintk((" index=%ld entries=%ld hdr_count=%ld\n", idx, psb->psb_entries, psb->psb_hdr->hdr_count));

		/* sampling buffer full ? */
		if (idx == (psb->psb_entries-1)) {
			bv = mask;
			buffer_is_full = 1;

			DBprintk((" sampling buffer full must notify bv=0x%lx\n", bv));

			if (!CTX_OVFL_NOBLOCK(ctx)) goto buffer_full;
			/*
			 * here, we have a full buffer but we are in non-blocking mode
			 * so we need to reloads overflowed PMDs with sampling reset values
			 * and restart
			 */
		}
	}
reload_pmds:
	ovfl_is_smpl = CTX_OVFL_NOBLOCK(ctx) && buffer_is_full;
	can_notify   = CTX_HAS_SMPL(ctx) == 0 && ctx->ctx_notify_pid;

	for (i = 0, cnum = PMU_FIRST_COUNTER; mask ; cnum++, i++, mask >>= 1) {

		if ((mask & 0x1) == 0) continue;

		DBprintk((" PMD[%ld] overflowed pmd=0x%lx pmod.val=0x%lx\n", cnum, ia64_get_pmd(cnum), ctx->ctx_pmds[i].val));

		/*
		 * Because we sometimes (EARS/BTB) reset to a specific value, we cannot simply use
		 * val to count the number of times we overflowed. Otherwise we would loose the current value
		 * in the PMD (which can be >0). So to make sure we don't loose
		 * the residual counts we set val to contain full 64bits value of the counter.
		 *
		 * XXX: is this needed for EARS/BTB ?
		 */
		ctx->ctx_pmds[i].val += 1 + pmu_conf.perf_ovfl_val
				      + (ia64_get_pmd(cnum) & pmu_conf.perf_ovfl_val); /* slow */

		DBprintk((" pmod[%ld].val=0x%lx pmd=0x%lx\n", i, ctx->ctx_pmds[i].val, ia64_get_pmd(cnum)&pmu_conf.perf_ovfl_val));

		if (can_notify && PMD_OVFL_NOTIFY(ctx, i)) {
			DBprintk((" CPU%d should notify process %d with signal %d\n", my_cpu, ctx->ctx_notify_pid, ctx->ctx_notify_sig));
			bv |= 1 << i;
		} else {
			DBprintk((" CPU%d PMD[%ld] overflow, no notification\n", my_cpu, cnum));
			/*
			 * In case no notification is requested, we reload the reset value right away
			 * otherwise we wait until the notify_pid process has been called and has
			 * has finished processing data. Check out pfm_overflow_notify()
			 */

			/* writes to upper part are ignored, so this is safe */
			if (ovfl_is_smpl) {
				DBprintk((" CPU%d PMD[%ld] reloaded with smpl_val=%lx\n", my_cpu, cnum,ctx->ctx_pmds[i].smpl_rval));
				ia64_set_pmd(cnum, ctx->ctx_pmds[i].smpl_rval);
			} else {
				DBprintk((" CPU%d PMD[%ld] reloaded with ovfl_val=%lx\n", my_cpu, cnum,ctx->ctx_pmds[i].smpl_rval));
				ia64_set_pmd(cnum, ctx->ctx_pmds[i].ovfl_rval);
			}
		}
		if (cnum == ctx->ctx_btb_counter) need_reset_pmd16=1;
	}
	/*
	 * In case of BTB, overflow
	 * we need to reset the BTB index.
	 */
	if (need_reset_pmd16) {
		DBprintk(("reset PMD16\n"));
		ia64_set_pmd(16, 0);
	}
buffer_full:
	/* see pfm_overflow_notify() on details for why we use |= here */
	ctx->ctx_ovfl_regs  |= bv;

	/* nobody to notify, return and unfreeze */
	if (!bv) return 0x0;


	if (ctx->ctx_notify_pid == ta->pid) {
		struct siginfo si;

		si.si_errno    = 0;
		si.si_addr     = NULL;
		si.si_pid      = ta->pid; /* who is sending */


		si.si_signo    = ctx->ctx_notify_sig; /* is SIGPROF */
		si.si_code     = PROF_OVFL; /* goes to user */
		si.si_pfm_ovfl = bv;


		/*
		 * in this case, we don't stop the task, we let it go on. It will
		 * necessarily go to the signal handler (if any) when it goes back to
		 * user mode.
		 */
		DBprintk((" sending %d notification to self %d\n", si.si_signo, ta->pid));


		/* this call is safe in an interrupt handler */
		ret = send_sig_info(ctx->ctx_notify_sig, &si, ta);
		if (ret != 0)
			printk(" send_sig_info(process %d, SIGPROF)=%d\n", ta->pid, ret);
		/*
		 * no matter if we block or not, we keep PMU frozen and do not unfreeze on ctxsw
		 */
		ctx->ctx_fl_frozen = 1;

	} else {
#if 0
			/*
			 * The tasklet is guaranteed to be scheduled for this CPU only
			 */
			notify_info[my_cpu].to_pid   = ctx->notify_pid;
			notify_info[my_cpu].from_pid = ta->pid; /* for debug only */
			notify_info[my_cpu].bitvect  = bv;
			/* tasklet is inserted and active */
			tasklet_schedule(&pfm_tasklet);
#endif
			/*
			 * stored the vector of overflowed registers for use in notification
			 * mark that a notification/blocking is pending (arm the trap)
			 */
		th->pfm_pend_notify = 1;

		/*
		 * if we do block, then keep PMU frozen until restart
		 */
		if (!CTX_OVFL_NOBLOCK(ctx)) ctx->ctx_fl_frozen = 1;

		DBprintk((" process %d notify ovfl_regs=0x%lx\n", ta->pid, bv));
	}
	/*
	 * keep PMU frozen (and overflowed bits cleared) when we have to stop,
	 * otherwise return a resume 'value' for PMC[0]
	 *
	 * XXX: maybe that's enough to get rid of ctx_fl_frozen ?
	 */
	DBprintk((" will return pmc0=0x%x\n",ctx->ctx_fl_frozen ? 0x1 : 0x0));
	return ctx->ctx_fl_frozen ? 0x1 : 0x0;
}

static void
perfmon_interrupt (int irq, void *arg, struct pt_regs *regs)
{
	u64 pmc0;
	struct task_struct *ta;

	pmc0 = ia64_get_pmc(0); /* slow */

	/*
	 * if we have some pending bits set
	 * assumes : if any PM[0].bit[63-1] is set, then PMC[0].fr = 1
	 */
	if ((pmc0 & ~0x1) && (ta=PMU_OWNER())) {

		/* assumes, PMC[0].fr = 1 at this point */
		pmc0 = update_counters(ta, pmc0, regs);

		/*
		 * if pmu_frozen = 0
		 *	pmc0 = 0 and we resume monitoring right away
		 * else
		 *	pmc0 = 0x1 frozen but all pending bits are cleared
		 */
		ia64_set_pmc(0, pmc0);
		ia64_srlz_d();
	} else {
		printk("perfmon: Spurious PMU overflow interrupt: pmc0=0x%lx owner=%p\n", pmc0, (void *)PMU_OWNER());
	}
}

/* for debug only */
static int
perfmon_proc_info(char *page)
{
	char *p = page;
	u64 pmc0 = ia64_get_pmc(0);
	int i;

	p += sprintf(p, "PMC[0]=%lx\nPerfmon debug: %s\n", pmc0, pfm_debug ? "On" : "Off");
	for(i=0; i < NR_CPUS; i++) {
		if (cpu_is_online(i))
			p += sprintf(p, "CPU%d.PMU %d\n", i, pmu_owners[i].owner ? pmu_owners[i].owner->pid: 0);
	}
	return p - page;
}

/* for debug only */
static int
perfmon_read_entry(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = perfmon_proc_info(page);

	if (len <= off+count) *eof = 1;

	*start = page + off;
	len   -= off;

	if (len>count) len = count;
	if (len<0) len = 0;

	return len;
}

static struct irqaction perfmon_irqaction = {
	handler:	perfmon_interrupt,
	flags:		SA_INTERRUPT,
	name:		"perfmon"
};

void __init
perfmon_init (void)
{
	pal_perf_mon_info_u_t pm_info;
	s64 status;

	register_percpu_irq(IA64_PERFMON_VECTOR, &perfmon_irqaction);

	ia64_set_pmv(IA64_PERFMON_VECTOR);
	ia64_srlz_d();

	pmu_conf.pfm_is_disabled = 1;

	printk("perfmon: version %s (sampling format v%d)\n", PFM_VERSION, PFM_SMPL_HDR_VERSION);
	printk("perfmon: Interrupt vectored to %u\n", IA64_PERFMON_VECTOR);

	if ((status=ia64_pal_perf_mon_info(pmu_conf.impl_regs, &pm_info)) != 0) {
		printk("perfmon: PAL call failed (%ld)\n", status);
		return;
	}
	pmu_conf.perf_ovfl_val = (1L << pm_info.pal_perf_mon_info_s.width) - 1;
	pmu_conf.max_counters  = pm_info.pal_perf_mon_info_s.generic;
	pmu_conf.num_pmds      = find_num_pm_regs(pmu_conf.impl_regs);
	pmu_conf.num_pmcs      = find_num_pm_regs(&pmu_conf.impl_regs[4]);

	printk("perfmon: %d bits counters (max value 0x%lx)\n", pm_info.pal_perf_mon_info_s.width, pmu_conf.perf_ovfl_val);
	printk("perfmon: %ld PMC/PMD pairs, %ld PMCs, %ld PMDs\n", pmu_conf.max_counters, pmu_conf.num_pmcs, pmu_conf.num_pmds);

	/* sanity check */
	if (pmu_conf.num_pmds >= IA64_NUM_PMD_REGS || pmu_conf.num_pmcs >= IA64_NUM_PMC_REGS) {
		printk(KERN_ERR "perfmon: ERROR not enough PMC/PMD storage in kernel, perfmon is DISABLED\n");
		return; /* no need to continue anyway */
	}
	/* we are all set */
	pmu_conf.pfm_is_disabled = 0;

	/*
	 * Insert the tasklet in the list.
	 * It is still disabled at this point, so it won't run
	printk(__FUNCTION__" tasklet is %p state=%d, count=%d\n", &perfmon_tasklet, perfmon_tasklet.state, perfmon_tasklet.count);
	 */

	/*
	 * for now here for debug purposes
	 */
	perfmon_dir = create_proc_read_entry ("perfmon", 0, 0, perfmon_read_entry, NULL);
}

void
perfmon_init_percpu (void)
{
	ia64_set_pmv(IA64_PERFMON_VECTOR);
	ia64_srlz_d();
}

/*
 * XXX: for system wide this function MUST never be called
 */
void
pfm_save_regs (struct task_struct *ta)
{
	struct task_struct *owner;
	struct thread_struct *t;
	u64 pmc0, psr;
	int i;

	if (ta == NULL) {
		panic(__FUNCTION__" task is NULL\n");
	}
	t = &ta->thread;
	/*
	 * We must make sure that we don't loose any potential overflow
	 * interrupt while saving PMU context. In this code, external
	 * interrupts are always enabled.
	 */

	/*
	 * save current PSR: needed because we modify it
	 */
	__asm__ __volatile__ ("mov %0=psr;;": "=r"(psr) :: "memory");

	/*
	 * stop monitoring:
	 * This is the only way to stop monitoring without destroying overflow
	 * information in PMC[0].
	 * This is the last instruction which can cause overflow when monitoring
	 * in kernel.
	 * By now, we could still have an overflow interrupt in-flight.
	 */
	__asm__ __volatile__ ("rum psr.up;;"::: "memory");

	/*
	 * Mark the PMU as not owned
	 * This will cause the interrupt handler to do nothing in case an overflow
	 * interrupt was in-flight
	 * This also guarantees that pmc0 will contain the final state
	 * It virtually gives us full control over overflow processing from that point
	 * on.
	 * It must be an atomic operation.
	 */
	owner = PMU_OWNER();
	SET_PMU_OWNER(NULL);

	/*
	 * read current overflow status:
	 *
	 * we are guaranteed to read the final stable state
	 */
	ia64_srlz_d();
	pmc0 = ia64_get_pmc(0); /* slow */

	/*
	 * freeze PMU:
	 *
	 * This destroys the overflow information. This is required to make sure
	 * next process does not start with monitoring on if not requested
	 */
	ia64_set_pmc(0, 1);
	ia64_srlz_d();

	/*
	 * Check for overflow bits and proceed manually if needed
	 *
	 * It is safe to call the interrupt handler now because it does
	 * not try to block the task right away. Instead it will set a
	 * flag and let the task proceed. The blocking will only occur
	 * next time the task exits from the kernel.
	 */
	if (pmc0 & ~0x1) {
		if (owner != ta) printk(__FUNCTION__" owner=%p task=%p\n", (void *)owner, (void *)ta);
		printk(__FUNCTION__" Warning: pmc[0]=0x%lx explicit call\n", pmc0);

		pmc0 = update_counters(owner, pmc0, NULL);
		/* we will save the updated version of pmc0 */
	}

	/*
	 * restore PSR for context switch to save
	 */
	__asm__ __volatile__ ("mov psr.l=%0;; srlz.i;;"::"r"(psr): "memory");


	/*
	 * XXX needs further optimization.
	 * Also must take holes into account
	 */
	for (i=0; i< pmu_conf.num_pmds; i++) {
		t->pmd[i] = ia64_get_pmd(i);
	}

	/* skip PMC[0], we handle it separately */
	for (i=1; i< pmu_conf.num_pmcs; i++) {
		t->pmc[i] = ia64_get_pmc(i);
	}

	/*
	 * Throughout this code we could have gotten an overflow interrupt. It is transformed
	 * into a spurious interrupt as soon as we give up pmu ownership.
	 */
}

void
pfm_load_regs (struct task_struct *ta)
{
	struct thread_struct *t = &ta->thread;
	pfm_context_t *ctx = ta->thread.pfm_context;
	int i;

	/*
	 * XXX needs further optimization.
	 * Also must take holes into account
	 */
	for (i=0; i< pmu_conf.num_pmds; i++) {
		ia64_set_pmd(i, t->pmd[i]);
	}

	/* skip PMC[0] to avoid side effects */
	for (i=1; i< pmu_conf.num_pmcs; i++) {
		ia64_set_pmc(i, t->pmc[i]);
	}

	/*
	 * we first restore ownership of the PMU to the 'soon to be current'
	 * context. This way, if, as soon as we unfreeze the PMU at the end
	 * of this function, we get an interrupt, we attribute it to the correct
	 * task
	 */
	SET_PMU_OWNER(ta);

#if 0
	/*
	 * check if we had pending overflow before context switching out
	 * If so, we invoke the handler manually, i.e. simulate interrupt.
	 *
	 * XXX: given that we do not use the tasklet anymore to stop, we can
	 * move this back to the pfm_save_regs() routine.
	 */
	if (t->pmc[0] & ~0x1) {
		/* freeze set in pfm_save_regs() */
		DBprintk((" pmc[0]=0x%lx manual interrupt\n",t->pmc[0]));
		update_counters(ta, t->pmc[0], NULL);
	}
#endif

	/*
	 * unfreeze only when possible
	 */
	if (ctx->ctx_fl_frozen == 0) {
		ia64_set_pmc(0, 0);
		ia64_srlz_d();
	}
}


/*
 * This function is called when a thread exits (from exit_thread()).
 * This is a simplified pfm_save_regs() that simply flushes hthe current
 * register state into the save area taking into account any pending
 * overflow. This time no notification is sent because the taks is dying
 * anyway. The inline processing of overflows avoids loosing some counts.
 * The PMU is frozen on exit from this call and is to never be reenabled
 * again for this task.
 */
void
pfm_flush_regs (struct task_struct *ta)
{
	pfm_context_t *ctx;
	u64 pmc0, psr, mask;
	int i,j;

	if (ta == NULL) {
		panic(__FUNCTION__" task is NULL\n");
	}
	ctx = ta->thread.pfm_context;
	if (ctx == NULL) {
		panic(__FUNCTION__" no PFM ctx is NULL\n");
	}
	/*
	 * We must make sure that we don't loose any potential overflow
	 * interrupt while saving PMU context. In this code, external
	 * interrupts are always enabled.
	 */

	/*
	 * save current PSR: needed because we modify it
	 */
	__asm__ __volatile__ ("mov %0=psr;;": "=r"(psr) :: "memory");

	/*
	 * stop monitoring:
	 * This is the only way to stop monitoring without destroying overflow
	 * information in PMC[0].
	 * This is the last instruction which can cause overflow when monitoring
	 * in kernel.
	 * By now, we could still have an overflow interrupt in-flight.
	 */
	__asm__ __volatile__ ("rsm psr.up;;"::: "memory");

	/*
	 * Mark the PMU as not owned
	 * This will cause the interrupt handler to do nothing in case an overflow
	 * interrupt was in-flight
	 * This also guarantees that pmc0 will contain the final state
	 * It virtually gives us full control on overflow processing from that point
	 * on.
	 * It must be an atomic operation.
	 */
	SET_PMU_OWNER(NULL);

	/*
	 * read current overflow status:
	 *
	 * we are guaranteed to read the final stable state
	 */
	ia64_srlz_d();
	pmc0 = ia64_get_pmc(0); /* slow */

	/*
	 * freeze PMU:
	 *
	 * This destroys the overflow information. This is required to make sure
	 * next process does not start with monitoring on if not requested
	 */
	ia64_set_pmc(0, 1);
	ia64_srlz_d();

	/*
	 * restore PSR for context switch to save
	 */
	__asm__ __volatile__ ("mov psr.l=%0;;srlz.i;"::"r"(psr): "memory");

	/*
	 * This loop flushes the PMD into the PFM context.
	 * IT also processes overflow inline.
	 *
	 * IMPORTANT: No notification is sent at this point as the process is dying.
	 * The implicit notification will come from a SIGCHILD or a return from a
	 * waitpid().
	 *
	 * XXX: must take holes into account
	 */
	mask = pmc0 >> PMU_FIRST_COUNTER;
	for (i=0,j=PMU_FIRST_COUNTER; i< pmu_conf.max_counters; i++,j++) {

		/* collect latest results */
		ctx->ctx_pmds[i].val += ia64_get_pmd(j) & pmu_conf.perf_ovfl_val;

		/* take care of overflow inline */
		if (mask & 0x1) {
			ctx->ctx_pmds[i].val += 1 + pmu_conf.perf_ovfl_val;
			DBprintk((" PMD[%d] overflowed pmd=0x%lx pmds.val=0x%lx\n",
			j, ia64_get_pmd(j), ctx->ctx_pmds[i].val));
		}
	}
}

/*
 * XXX: this routine is not very portable for PMCs
 * XXX: make this routine able to work with non current context
 */
static void
ia64_reset_pmu(void)
{
	int i;

	/* PMU is frozen, no pending overflow bits */
	ia64_set_pmc(0,1);

	/* extra overflow bits + counter configs cleared */
	for(i=1; i< PMU_FIRST_COUNTER + pmu_conf.max_counters ; i++) {
		ia64_set_pmc(i,0);
	}

	/* opcode matcher set to all 1s */
	ia64_set_pmc(8,~0);
	ia64_set_pmc(9,~0);

	/* I-EAR config cleared, plm=0 */
	ia64_set_pmc(10,0);

	/* D-EAR config cleared, PMC[11].pt must be 1 */
	ia64_set_pmc(11,1 << 28);

	/* BTB config. plm=0 */
	ia64_set_pmc(12,0);

	/* Instruction address range, PMC[13].ta must be 1 */
	ia64_set_pmc(13,1);

	/* clears all PMD registers */
	for(i=0;i< pmu_conf.num_pmds; i++) {
		if (PMD_IS_IMPL(i)) ia64_set_pmd(i,0);
	}
	ia64_srlz_d();
}

/*
 * task is the newly created task
 */
int
pfm_inherit(struct task_struct *task)
{
	pfm_context_t *ctx = current->thread.pfm_context;
	pfm_context_t *nctx;
	struct thread_struct *th = &task->thread;
	int i, cnum;

	/*
	 * takes care of easiest case first
	 */
	if (CTX_INHERIT_MODE(ctx) == PFM_FL_INHERIT_NONE) {
		DBprintk((" removing PFM context for %d\n", task->pid));
		task->thread.pfm_context     = NULL;
		task->thread.pfm_pend_notify = 0;
		/* copy_thread() clears IA64_THREAD_PM_VALID */
		return 0;
	}
	nctx = pfm_context_alloc();
	if (nctx == NULL) return -ENOMEM;

	/* copy content */
	*nctx = *ctx;

	if (ctx->ctx_fl_inherit == PFM_FL_INHERIT_ONCE) {
		nctx->ctx_fl_inherit = PFM_FL_INHERIT_NONE;
		DBprintk((" downgrading to INHERIT_NONE for %d\n", task->pid));
	}

	/* initialize counters in new context */
	for(i=0, cnum= PMU_FIRST_COUNTER; i < pmu_conf.max_counters; cnum++, i++) {
		nctx->ctx_pmds[i].val = nctx->ctx_pmds[i].ival & ~pmu_conf.perf_ovfl_val;
		th->pmd[cnum]	      = nctx->ctx_pmds[i].ival & pmu_conf.perf_ovfl_val;

	}
	/* clear BTB index register */
	th->pmd[16] = 0;

	/* if sampling then increment number of users of buffer */
	if (nctx->ctx_smpl_buf) {
		atomic_inc(&nctx->ctx_smpl_buf->psb_refcnt);
	}

	nctx->ctx_fl_frozen = 0;
	nctx->ctx_ovfl_regs = 0;
	sema_init(&nctx->ctx_restart_sem, 0); /* reset this semaphore to locked */

	/* clear pending notification */
	th->pfm_pend_notify = 0;

	/* link with new task */
	th->pfm_context     = nctx;

	DBprintk((" nctx=%p for process %d\n", (void *)nctx, task->pid));

	/*
	 * the copy_thread routine automatically clears
	 * IA64_THREAD_PM_VALID, so we need to reenable it, if it was used by the caller
	 */
	if (current->thread.flags & IA64_THREAD_PM_VALID) {
		DBprintk(("  setting PM_VALID for %d\n", task->pid));
		th->flags |= IA64_THREAD_PM_VALID;
	}

	return 0;
}

/* called from exit_thread() */
void
pfm_context_exit(struct task_struct *task)
{
	pfm_context_t *ctx = task->thread.pfm_context;

	if (!ctx) {
		DBprintk((" invalid context for %d\n", task->pid));
		return;
	}

	/* check is we have a sampling buffer attached */
	if (ctx->ctx_smpl_buf) {
		pfm_smpl_buffer_desc_t *psb = ctx->ctx_smpl_buf;

		/* if only user left, then remove */
		DBprintk((" pid %d: task %d sampling psb->refcnt=%d\n", current->pid, task->pid, psb->psb_refcnt.counter));

		if (atomic_dec_and_test(&psb->psb_refcnt) ) {
			rvfree(psb->psb_hdr, psb->psb_size);
			vfree(psb);
			DBprintk((" pid %d: cleaning task %d sampling buffer\n", current->pid, task->pid ));
		}
	}
	DBprintk((" pid %d: task %d pfm_context is freed @%p\n", current->pid, task->pid, (void *)ctx));
	pfm_context_free(ctx);
}

#else /* !CONFIG_PERFMON */

asmlinkage int
sys_perfmonctl (int pid, int cmd, int flags, perfmon_req_t *req, int count, long arg6, long arg7, long arg8, long stack)
{
	return -ENOSYS;
}

#endif /* !CONFIG_PERFMON */
