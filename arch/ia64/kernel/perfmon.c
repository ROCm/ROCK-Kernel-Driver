/*
 * This file contains the code to configure and read/write the ia64 performance
 * monitoring stuff.
 *
 * Originaly Written by Ganesh Venkitachalam, IBM Corp.
 * Modifications by David Mosberger-Tang, Hewlett-Packard Co.
 * Modifications by Stephane Eranian, Hewlett-Packard Co.
 * Copyright (C) 1999 Ganesh Venkitachalam <venkitac@us.ibm.com>
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000 Stephane Eranian <eranian@hpl.hp.com>
 */

#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/proc_fs.h>

#include <asm/errno.h>
#include <asm/hw_irq.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pal.h>

/* Long blurb on how this works: 
 * We set dcr.pp, psr.pp, and the appropriate pmc control values with
 * this.  Notice that we go about modifying _each_ task's pt_regs to
 * set cr_ipsr.pp.  This will start counting when "current" does an
 * _rfi_. Also, since each task's cr_ipsr.pp, and cr_ipsr is inherited
 * across forks, we do _not_ need additional code on context
 * switches. On stopping of the counters we dont need to go about
 * changing every task's cr_ipsr back to where it wuz, because we can
 * just set pmc[0]=1. But we do it anyways becuase we will probably
 * add thread specific accounting later.
 *
 * The obvious problem with this is that on SMP systems, it is a bit
 * of work (when someone wants to do it:-)) - it would be easier if we
 * just added code to the context-switch path, but if we wanted to support
 * per-thread accounting, the context-switch path might be long unless 
 * we introduce a flag in the task_struct. Right now, the following code 
 * will NOT work correctly on MP (for more than one reason:-)).
 *
 * The short answer is that to make this work on SMP,  we would need 
 * to lock the run queue to ensure no context switches, send 
 * an IPI to each processor, and in that IPI handler, set processor regs,
 * and just modify the psr bit of only the _current_ thread, since we have 
 * modified the psr bit correctly in the kernel stack for every process 
 * which is not running. Also, we need pmd arrays per-processor, and 
 * the READ_PMD command will need to get values off of other processors. 
 * IPIs are the answer, irrespective of what the question is. Might 
 * crash on SMP systems without the lock_kernel().
 */

#ifdef CONFIG_PERFMON

#define MAX_PERF_COUNTER	4	/* true for Itanium, at least */
#define PMU_FIRST_COUNTER	4	/* first generic counter */

#define PFM_WRITE_PMCS		0xa0
#define PFM_WRITE_PMDS		0xa1
#define PFM_READ_PMDS		0xa2
#define PFM_STOP		0xa3
#define PFM_START		0xa4
#define PFM_ENABLE		0xa5	/* unfreeze only */
#define PFM_DISABLE		0xa6	/* freeze only */
/* 
 * Those 2 are just meant for debugging. I considered using sysctl() for
 * that but it is a little bit too pervasive. This solution is at least
 * self-contained.
 */
#define PFM_DEBUG_ON		0xe0	
#define PFM_DEBUG_OFF		0xe1

#ifdef CONFIG_SMP
#define cpu_is_online(i) (cpu_online_map & (1UL << i))
#else
#define cpu_is_online(i)	1
#endif

#define PMC_IS_IMPL(i)		(pmu_conf.impl_regs[i>>6] & (1<< (i&~(64-1))))
#define PMD_IS_IMPL(i)  	(pmu_conf.impl_regs[4+(i>>6)] & (1<< (i&~(64-1))))
#define PMD_IS_COUNTER(i)	(i>=PMU_FIRST_COUNTER && i < (PMU_FIRST_COUNTER+pmu_conf.max_counters))
#define PMC_IS_COUNTER(i)	(i>=PMU_FIRST_COUNTER && i < (PMU_FIRST_COUNTER+pmu_conf.max_counters))

/*
 * this structure needs to be enhanced
 */
typedef struct {
	unsigned long	pfr_reg_num;	/* which register */
	unsigned long	pfr_reg_value;	/* configuration (PMC) or initial value (PMD) */
	unsigned long	pfr_reg_reset;	/* reset value on overflow (PMD) */
	void		*pfr_smpl_buf;	/* pointer to user buffer for EAR/BTB */
	unsigned long	pfr_smpl_size;	/* size of user buffer for EAR/BTB */
	pid_t		pfr_notify_pid;	/* process to notify */
	int		pfr_notify_sig;	/* signal for notification, 0=no notification */
} perfmon_req_t;

#if 0
typedef struct {
	unsigned long pmu_reg_data;	/* generic PMD register */
	unsigned long pmu_reg_num;	/* which register number */
} perfmon_reg_t; 
#endif

/*
 * This structure is initialize at boot time and contains
 * a description of the PMU main characteristic as indicated
 * by PAL
 */
typedef struct {
	unsigned long perf_ovfl_val;	/* overflow value for generic counters   */
	unsigned long max_counters;	/* upper limit on counter pair (PMC/PMD) */
	unsigned long impl_regs[16];	/* buffer used to hold implememted PMC/PMD mask */
} pmu_config_t;

static pmu_config_t pmu_conf;

/* for debug only */
static unsigned long pfm_debug=1;	/* 0= nodebug, >0= debug output on */
#define DBprintk(a)	{\
	if (pfm_debug >0) { printk a; } \
}

/*
 * could optimize to avoid cache line conflicts in SMP
 */
static struct task_struct *pmu_owners[NR_CPUS];

static int
do_perfmonctl (struct task_struct *task, int cmd, int flags, perfmon_req_t *req, int count, struct pt_regs *regs)
{
        perfmon_req_t tmp;
        int i;

        switch (cmd) {
		case PFM_WRITE_PMCS:          
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			if (!access_ok(VERIFY_READ, req, sizeof(struct perfmon_req_t)*count)) return -EFAULT;

			for (i = 0; i < count; i++, req++) {
				copy_from_user(&tmp, req, sizeof(tmp));

				/* XXX needs to check validity of the data maybe */

				if (!PMC_IS_IMPL(tmp.pfr_reg_num)) {
					DBprintk((__FUNCTION__ " invalid pmc[%ld]\n", tmp.pfr_reg_num));
					return -EINVAL;
				}

				/* XXX: for counters, need to some checks */
				if (PMC_IS_COUNTER(tmp.pfr_reg_num)) {
					current->thread.pmu_counters[tmp.pfr_reg_num - PMU_FIRST_COUNTER].sig = tmp.pfr_notify_sig;
					current->thread.pmu_counters[tmp.pfr_reg_num - PMU_FIRST_COUNTER].pid = tmp.pfr_notify_pid;

					DBprintk((__FUNCTION__" setting PMC[%ld] send sig %d to %d\n",tmp.pfr_reg_num, tmp.pfr_notify_sig, tmp.pfr_notify_pid));
				}
				ia64_set_pmc(tmp.pfr_reg_num, tmp.pfr_reg_value);

				DBprintk((__FUNCTION__" setting PMC[%ld]=0x%lx\n", tmp.pfr_reg_num, tmp.pfr_reg_value));
			}
			/*
			 * we have to set this here event hough we haven't necessarily started monitoring
			 * because we may be context switched out
			 */
			current->thread.flags |= IA64_THREAD_PM_VALID;
                	break;

		case PFM_WRITE_PMDS:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			if (!access_ok(VERIFY_READ, req, sizeof(struct perfmon_req_t)*count)) return -EFAULT;

			for (i = 0; i < count; i++, req++) {
				copy_from_user(&tmp, req, sizeof(tmp));

				if (!PMD_IS_IMPL(tmp.pfr_reg_num)) return -EINVAL;

				/* update virtualized (64bits) counter */
				if (PMD_IS_COUNTER(tmp.pfr_reg_num)) {
					current->thread.pmu_counters[tmp.pfr_reg_num - PMU_FIRST_COUNTER].val  = tmp.pfr_reg_value & ~pmu_conf.perf_ovfl_val;
					current->thread.pmu_counters[tmp.pfr_reg_num - PMU_FIRST_COUNTER].rval = tmp.pfr_reg_reset;
				}
				/* writes to unimplemented part is ignored, so this is safe */
				ia64_set_pmd(tmp.pfr_reg_num, tmp.pfr_reg_value);
				/* to go away */
				ia64_srlz_d();
				DBprintk((__FUNCTION__" setting PMD[%ld]:  pmod.val=0x%lx pmd=0x%lx rval=0x%lx\n", tmp.pfr_reg_num, current->thread.pmu_counters[tmp.pfr_reg_num - PMU_FIRST_COUNTER].val, ia64_get_pmd(tmp.pfr_reg_num),current->thread.pmu_counters[tmp.pfr_reg_num - PMU_FIRST_COUNTER].rval));
			}
			/*
			 * we have to set this here event hough we haven't necessarily started monitoring
			 * because we may be context switched out
			 */
			current->thread.flags |= IA64_THREAD_PM_VALID;
                	break;

		case PFM_START:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			pmu_owners[smp_processor_id()] = current;

			/* will start monitoring right after rfi */
			ia64_psr(regs)->up = 1;

			/* 
		 	 * mark the state as valid.
		 	 * this will trigger save/restore at context switch
		 	 */
			current->thread.flags |= IA64_THREAD_PM_VALID;

			ia64_set_pmc(0, 0);

                	break;

		case PFM_ENABLE:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			pmu_owners[smp_processor_id()] = current;

			/* 
		 	 * mark the state as valid.
		 	 * this will trigger save/restore at context switch
		 	 */
			current->thread.flags |= IA64_THREAD_PM_VALID;

			/* simply unfreeze */
			ia64_set_pmc(0, 0);
			break;

		case PFM_DISABLE:
			/* we don't quite support this right now */
			if (task != current) return -EINVAL;

			/* simply unfreeze */
			ia64_set_pmc(0, 1);
			ia64_srlz_d();
			break;

	        case PFM_READ_PMDS:
			if (!access_ok(VERIFY_READ, req, sizeof(struct perfmon_req_t)*count)) return -EFAULT;
			if (!access_ok(VERIFY_WRITE, req, sizeof(struct perfmon_req_t)*count)) return -EFAULT;

		/* This looks shady, but IMHO this will work fine. This is  
		 * the sequence that I could come up with to avoid races
		 * with the interrupt handler. See explanation in the 
		 * following comment.
		 */
#if 0
/* irrelevant with user monitors */
		local_irq_save(flags);
		__asm__ __volatile__("rsm psr.pp\n");
		dcr = ia64_get_dcr();
		dcr &= ~IA64_DCR_PP;
		ia64_set_dcr(dcr);
		local_irq_restore(flags);
#endif
		/*
		 * We cannot write to pmc[0] to stop counting here, as
		 * that particular instruction might cause an overflow
		 * and the mask in pmc[0] might get lost. I'm _not_ 
		 * sure of the hardware behavior here. So we stop
		 * counting by psr.pp = 0. And we reset dcr.pp to
		 * prevent an interrupt from mucking up psr.pp in the
		 * meanwhile. Perfmon interrupts are pended, hence the
		 * above code should be ok if one of the above instructions 
		 * caused overflows, i.e the interrupt should get serviced
		 * when we re-enabled interrupts. When I muck with dcr, 
		 * is the irq_save/restore needed?
		 */

		for (i = 0; i < count; i++, req++) {
			unsigned long val=0;

			copy_from_user(&tmp, req, sizeof(tmp));

			if (!PMD_IS_IMPL(tmp.pfr_reg_num)) return -EINVAL;

			if (PMD_IS_COUNTER(tmp.pfr_reg_num)) {
				if (task == current){
					val = ia64_get_pmd(tmp.pfr_reg_num) & pmu_conf.perf_ovfl_val;
				} else {
					val = task->thread.pmd[tmp.pfr_reg_num - PMU_FIRST_COUNTER] & pmu_conf.perf_ovfl_val;
				}
				val += task->thread.pmu_counters[tmp.pfr_reg_num - PMU_FIRST_COUNTER].val;
			} else {
				/* for now */
				if (task != current) return -EINVAL;

				val = ia64_get_pmd(tmp.pfr_reg_num);
			}
			tmp.pfr_reg_value = val;

DBprintk((__FUNCTION__" reading PMD[%ld]=0x%lx\n", tmp.pfr_reg_num, val));

			if (copy_to_user(req, &tmp, sizeof(tmp))) return -EFAULT;
		}
#if 0
/* irrelevant with user monitors */
		local_irq_save(flags);
		__asm__ __volatile__("ssm psr.pp");
		dcr = ia64_get_dcr();
		dcr |= IA64_DCR_PP;
		ia64_set_dcr(dcr);
		local_irq_restore(flags);
#endif
                break;

	      case PFM_STOP:
		/* we don't quite support this right now */
		if (task != current) return -EINVAL;

		ia64_set_pmc(0, 1);
		ia64_srlz_d();

		ia64_psr(regs)->up = 0;

		current->thread.flags &= ~IA64_THREAD_PM_VALID;

		pmu_owners[smp_processor_id()] = NULL;

#if 0
/* irrelevant with user monitors */
		local_irq_save(flags);
		dcr = ia64_get_dcr();
		dcr &= ~IA64_DCR_PP;
		ia64_set_dcr(dcr);
		local_irq_restore(flags);
		ia64_psr(regs)->up = 0;
#endif

		break;

	      case PFM_DEBUG_ON:
			printk(__FUNCTION__" debuggin on\n");
			pfm_debug = 1;
			break;

	      case PFM_DEBUG_OFF:
			printk(__FUNCTION__" debuggin off\n");
			pfm_debug = 0;
			break;

	      default:
		DBprintk((__FUNCTION__" UNknown command 0x%x\n", cmd));
		return -EINVAL;
		break;
        }
        return 0;
}

asmlinkage int
sys_perfmonctl (int pid, int cmd, int flags, perfmon_req_t *req, int count, long arg6, long arg7, long arg8, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
	struct task_struct *child = current;
	int ret;

	if (pid != current->pid) {
		read_lock(&tasklist_lock);
		{
			child = find_task_by_pid(pid);
			if (child)
				get_task_struct(child);
		}
		if (!child) { 
			read_unlock(&tasklist_lock);
			return -ESRCH;
		}
		/*
		 * XXX: need to do more checking here
		 */
		if (child->state != TASK_ZOMBIE) {
			DBprintk((__FUNCTION__" warning process %d not in stable state %ld\n", pid, child->state));
		}
	} 
	ret = do_perfmonctl(child, cmd, flags, req, count, regs);

	if (child != current) read_unlock(&tasklist_lock);

	return ret;
}


static inline int
update_counters (u64 pmc0)
{
	unsigned long mask, i, cnum;
	struct thread_struct *th;
	struct task_struct *ta;

	if (pmu_owners[smp_processor_id()] == NULL) {
		DBprintk((__FUNCTION__" Spurious overflow interrupt: PMU not owned\n"));
		return 0;
	}
	
	/*
	 * It is never safe to access the task for which the overflow interrupt is destinated
	 * using the current variable as the interrupt may occur in the middle of a context switch
	 * where current does not hold the task that is running yet.
	 *
	 * For monitoring, however, we do need to get access to the task which caused the overflow
	 * to account for overflow on the counters.
	 * We accomplish this by maintaining a current owner of the PMU per CPU. During context
	 * switch the ownership is changed in a way such that the reflected owner is always the 
	 * valid one, i.e. the one that caused the interrupt.
	 */
	ta = pmu_owners[smp_processor_id()];
	th = &pmu_owners[smp_processor_id()]->thread;

	/*
	 * Don't think this could happen given first test. Keep as sanity check
	 */
	if ((th->flags & IA64_THREAD_PM_VALID) == 0) {
		DBprintk((__FUNCTION__" Spurious overflow interrupt: process %d not using perfmon\n", ta->pid));
		return 0;
	}

	/*
	 * if PMU not frozen: spurious from previous context 
	 * if PMC[0] = 0x1 : frozen but no overflow reported: leftover from previous context
	 *
	 * in either case we don't touch the state upon return from handler
	 */
	if ((pmc0 & 0x1) == 0 || pmc0 == 0x1) { 
		DBprintk((__FUNCTION__" Spurious overflow interrupt: process %d freeze=0\n",ta->pid));
		return 0;
	}

	mask = pmc0 >> 4;

	for (i = 0, cnum = PMU_FIRST_COUNTER; i < pmu_conf.max_counters; cnum++, i++, mask >>= 1) {

		if (mask & 0x1) {
			DBprintk((__FUNCTION__ " PMD[%ld] overflowed pmd=0x%lx pmod.val=0x%lx\n", cnum, ia64_get_pmd(cnum), th->pmu_counters[i].val)); 
			
			/*
			 * Because we somtimes (EARS/BTB) reset to a specific value, we cannot simply use 
			 * val to count the number of times we overflowed. Otherwise we would loose the value
			 * current in the PMD (which can be >0). So to make sure we don't loose
			 * the residual counts we set val to contain full 64bits value of the counter.
			 */
			th->pmu_counters[i].val += 1+pmu_conf.perf_ovfl_val+(ia64_get_pmd(cnum) &pmu_conf.perf_ovfl_val);

			/* writes to upper part are ignored, so this is safe */
			ia64_set_pmd(cnum, th->pmu_counters[i].rval);

			DBprintk((__FUNCTION__ " pmod[%ld].val=0x%lx pmd=0x%lx\n", i, th->pmu_counters[i].val, ia64_get_pmd(cnum)&pmu_conf.perf_ovfl_val)); 

			if (th->pmu_counters[i].pid != 0 && th->pmu_counters[i].sig>0) {
				DBprintk((__FUNCTION__ " shouild notify process %d with signal %d\n",th->pmu_counters[i].pid, th->pmu_counters[i].sig)); 
			}
		}
	}
	return 1;
}

static void
perfmon_interrupt (int irq, void *arg, struct pt_regs *regs)
{
	/* unfreeze if not spurious */
	if ( update_counters(ia64_get_pmc(0)) ) {
		ia64_set_pmc(0, 0);
		ia64_srlz_d();
	}
}

static struct irqaction perfmon_irqaction = {
	handler:	perfmon_interrupt,
	flags:		SA_INTERRUPT,
	name:		"perfmon"
};

static int
perfmon_proc_info(char *page)
{
	char *p = page;
	u64 pmc0 = ia64_get_pmc(0);
	int i;

	p += sprintf(p, "PMC[0]=%lx\nPerfmon debug: %s\n", pmc0, pfm_debug ? "On" : "Off");
	for(i=0; i < NR_CPUS; i++) {
		if (cpu_is_online(i)) 
			p += sprintf(p, "CPU%d.PMU %d\n", i, pmu_owners[i] ? pmu_owners[i]->pid: -1);
	}
	return p - page;
}

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

static struct proc_dir_entry *perfmon_dir;

void __init
perfmon_init (void)
{
	pal_perf_mon_info_u_t pm_info;
	s64 status;
	
	irq_desc[PERFMON_IRQ].status |= IRQ_PER_CPU;
	irq_desc[PERFMON_IRQ].handler = &irq_type_ia64_sapic;
	setup_irq(PERFMON_IRQ, &perfmon_irqaction);

	ia64_set_pmv(PERFMON_IRQ);
	ia64_srlz_d();

	printk("perfmon: Initialized vector to %u\n",PERFMON_IRQ);

	if ((status=ia64_pal_perf_mon_info(pmu_conf.impl_regs, &pm_info)) != 0) {
		printk(__FUNCTION__ " pal call failed (%ld)\n", status);
		return;
	} 
	pmu_conf.perf_ovfl_val = (1L << pm_info.pal_perf_mon_info_s.width) - 1; 

	/* XXX need to use PAL instead */
	pmu_conf.max_counters  = pm_info.pal_perf_mon_info_s.generic;

	printk("perfmon: Counters are %d bits\n", pm_info.pal_perf_mon_info_s.width);
	printk("perfmon: Maximum counter value 0x%lx\n", pmu_conf.perf_ovfl_val);

	/*
	 * for now here for debug purposes
	 */
	perfmon_dir = create_proc_read_entry ("perfmon", 0, 0, perfmon_read_entry, NULL);
}

void
perfmon_init_percpu (void)
{
	ia64_set_pmv(PERFMON_IRQ);
	ia64_srlz_d();
}

/*
 * XXX: for system wide this function MUST never be called
 */
void
ia64_save_pm_regs (struct task_struct *ta)
{
	struct thread_struct *t = &ta->thread;
	u64 pmc0, psr;
	int i,j;

	/*
	 * We must maek sure that we don't loose any potential overflow
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
	 * information in PMC[0..3].
	 * This is the last instruction which can cause overflow when monitoring
	 * in kernel.
	 * By now, we could still have an overflow interrupt in flight.
	 */
	__asm__ __volatile__ ("rsm psr.up;;"::: "memory");
	
	/*
	 * read current overflow status:
	 *
	 * We may be reading stale information at this point, if we got interrupt
	 * just before the read(pmc0) but that's all right. However, if we did
	 * not get the interrupt before, this read reflects LAST state.
	 *
	 */
	pmc0 = ia64_get_pmc(0);

	/*
	 * freeze PMU:
	 *
	 * This destroys the overflow information. This is required to make sure
	 * next process does not start with monitoring on if not requested
	 * (PSR.up may not be enough).
	 *
	 * We could still get an overflow interrupt by now. However the handler
	 * will not do anything if is sees PMC[0].fr=1 but no overflow bits
	 * are set. So PMU will stay in frozen state. This implies that pmc0
	 * will still be holding the correct unprocessed information.
	 *
	 */
	ia64_set_pmc(0, 1);
	ia64_srlz_d();

	/*
	 * check for overflow bits set:
	 *
	 * If pmc0 reports PMU frozen, this means we have a pending overflow,
	 * therefore we invoke the handler. Handler is reentrant with regards
	 * to PMC[0] so it is safe to call it twice.
	 *
	 * IF pmc0 reports overflow, we need to reread current PMC[0] value
	 * in case the handler was invoked right after the first pmc0 read.
	 * it is was not invoked then pmc0==PMC[0], otherwise it's been invoked
	 * and overflow information has been processed, so we don't need to call.
	 *
	 * Test breakdown:
	 *	- pmc0 & ~0x1: test if overflow happened
	 * 	- second part: check if current register reflects this as well.
	 *
	 * NOTE: testing for pmc0 & 0x1 is not enough has it would trigger call
	 * when PM_VALID and PMU.fr which is common when setting up registers
	 * just before actually starting monitors.
	 *
	 */
	if ((pmc0 & ~0x1) && ((pmc0=ia64_get_pmc(0)) &~0x1) ) {
		printk(__FUNCTION__" Warning: pmc[0]=0x%lx\n", pmc0);
		update_counters(pmc0);
		/* 
		 * XXX: not sure that's enough. the next task may still get the
		 * interrupt.
		 */
	}

	/*
	 * restore PSR for context switch to save
	 */
	__asm__ __volatile__ ("mov psr.l=%0;;"::"r"(psr): "memory");

	/*
	 * XXX: this will need to be extended beyong just counters
	 */
	for (i=0,j=4; i< IA64_NUM_PMD_COUNTERS; i++,j++) {
		t->pmd[i] = ia64_get_pmd(j);
		t->pmc[i] = ia64_get_pmc(j);
	}
	/*
	 * PMU is frozen, PMU context is saved: nobody owns the PMU on this CPU
	 * At this point, we should not receive any pending interrupt from the 
	 * 'switched out' task
	 */
	pmu_owners[smp_processor_id()] = NULL;
}

void
ia64_load_pm_regs (struct task_struct *ta)
{
	struct thread_struct *t = &ta->thread;
	int i,j;

	/*
	 * we first restore ownership of the PMU to the 'soon to be current'
	 * context. This way, if, as soon as we unfreeze the PMU at the end
	 * of this function, we get an interrupt, we attribute it to the correct
	 * task
	 */
	pmu_owners[smp_processor_id()] = ta;

	/*
	 * XXX: this will need to be extended beyong just counters 
	 */
	for (i=0,j=4; i< IA64_NUM_PMD_COUNTERS; i++,j++) {
		ia64_set_pmd(j, t->pmd[i]);
		ia64_set_pmc(j, t->pmc[i]);
	}
	/*
	 * unfreeze PMU
	 */
	ia64_set_pmc(0, 0);
	ia64_srlz_d();
}

#else /* !CONFIG_PERFMON */

asmlinkage unsigned long
sys_perfmonctl (int cmd, int count, void *ptr)
{
	return -ENOSYS;
}

#endif /* !CONFIG_PERFMON */
