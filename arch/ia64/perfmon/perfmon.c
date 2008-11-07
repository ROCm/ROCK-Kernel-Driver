/*
 * This file implements the IA-64 specific
 * support for the perfmon2 interface
 *
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
  */
#include <linux/module.h>
#include <linux/perfmon_kern.h>

struct pfm_arch_session {
	u32	pfs_sys_use_dbr;    /* syswide session uses dbr */
	u32	pfs_ptrace_use_dbr; /* a thread uses dbr via ptrace()*/
};

DEFINE_PER_CPU(u32, pfm_syst_info);

static struct pfm_arch_session pfm_arch_sessions;
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(pfm_arch_sessions_lock);

static inline void pfm_clear_psr_pp(void)
{
	ia64_rsm(IA64_PSR_PP);
}

static inline void pfm_set_psr_pp(void)
{
	ia64_ssm(IA64_PSR_PP);
}

static inline void pfm_clear_psr_up(void)
{
	ia64_rsm(IA64_PSR_UP);
}

static inline void pfm_set_psr_up(void)
{
	ia64_ssm(IA64_PSR_UP);
}

static inline void pfm_set_psr_l(u64 val)
{
	ia64_setreg(_IA64_REG_PSR_L, val);
}

static inline void pfm_restore_ibrs(u64 *ibrs, unsigned int nibrs)
{
	unsigned int i;

	for (i = 0; i < nibrs; i++) {
		ia64_set_ibr(i, ibrs[i]);
		ia64_dv_serialize_instruction();
	}
	ia64_srlz_i();
}

static inline void pfm_restore_dbrs(u64 *dbrs, unsigned int ndbrs)
{
	unsigned int i;

	for (i = 0; i < ndbrs; i++) {
		ia64_set_dbr(i, dbrs[i]);
		ia64_dv_serialize_data();
	}
	ia64_srlz_d();
}

irqreturn_t pmu_interrupt_handler(int irq, void *arg)
{
	struct pt_regs *regs;
	regs = get_irq_regs();
	irq_enter();
	pfm_interrupt_handler(instruction_pointer(regs), regs);
	irq_exit();
	return IRQ_HANDLED;
}
static struct irqaction perfmon_irqaction = {
	.handler = pmu_interrupt_handler,
	.flags = IRQF_DISABLED, /* means keep interrupts masked */
	.name = "perfmon"
};

void pfm_arch_quiesce_pmu_percpu(void)
{
	u64 dcr;
	/*
	 * make sure no measurement is active
	 * (may inherit programmed PMCs from EFI).
	 */
	pfm_clear_psr_pp();
	pfm_clear_psr_up();

	/*
	 * ensure dcr.pp is cleared
	 */
	dcr = ia64_getreg(_IA64_REG_CR_DCR);
	ia64_setreg(_IA64_REG_CR_DCR, dcr & ~IA64_DCR_PP);

	/*
	 * we run with the PMU not frozen at all times
	 */
	ia64_set_pmc(0, 0);
	ia64_srlz_d();
}

void pfm_arch_init_percpu(void)
{
	pfm_arch_quiesce_pmu_percpu();
	/*
	 * program PMU interrupt vector
	 */
	ia64_setreg(_IA64_REG_CR_PMV, IA64_PERFMON_VECTOR);
	ia64_srlz_d();
}

int pfm_arch_context_create(struct pfm_context *ctx, u32 ctx_flags)
{
	struct pfm_arch_context *ctx_arch;

	ctx_arch = pfm_ctx_arch(ctx);

	ctx_arch->flags.use_dbr = 0;
	ctx_arch->flags.insecure = (ctx_flags & PFM_ITA_FL_INSECURE) ? 1: 0;

	PFM_DBG("insecure=%d", ctx_arch->flags.insecure);

	return 0;
}

/*
 * Called from pfm_ctxsw(). Task is guaranteed to be current.
 * Context is locked. Interrupts are masked. Monitoring may be active.
 * PMU access is guaranteed. PMC and PMD registers are live in PMU.
 *
 * Return:
 * 	non-zero : did not save PMDs (as part of stopping the PMU)
 * 	       0 : saved PMDs (no need to save them in caller)
 */
int pfm_arch_ctxswout_thread(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch;
	struct pfm_event_set *set;
	u64 psr, tmp;

	ctx_arch = pfm_ctx_arch(ctx);
	set = ctx->active_set;

	/*
	 * save current PSR: needed because we modify it
	 */
	ia64_srlz_d();
	psr = ia64_getreg(_IA64_REG_PSR);

	/*
	 * stop monitoring:
	 * This is the last instruction which may generate an overflow
	 *
	 * we do not clear ipsr.up
	 */
	pfm_clear_psr_up();
	ia64_srlz_d();

	/*
	 * extract overflow status bits
	 */
	tmp =  ia64_get_pmc(0) & ~0xf;

	/*
	 * keep a copy of psr.up (for reload)
	 */
	ctx_arch->ctx_saved_psr_up = psr & IA64_PSR_UP;

	/*
	 * save overflow status bits
	 */
	set->povfl_pmds[0] = tmp;

	/*
	 * record how many pending overflows
	 * XXX: assume identity mapping for counters
	 */
	set->npend_ovfls = ia64_popcnt(tmp);

	/*
	 * make sure the PMU is unfrozen for the next task
	 */
	if (set->npend_ovfls) {
		ia64_set_pmc(0, 0);
		ia64_srlz_d();
	}
	return 1;
}

/*
 * Called from pfm_ctxsw(). Task is guaranteed to be current.
 * set cannot be NULL. Context is locked. Interrupts are masked.
 * Caller has already restored all PMD and PMC registers.
 *
 * must reactivate monitoring
 */
void pfm_arch_ctxswin_thread(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch;

	ctx_arch = pfm_ctx_arch(ctx);

	/*
	 * when monitoring is not explicitly started
	 * then psr_up = 0, in which case we do not
	 * need to restore
	 */
	if (likely(ctx_arch->ctx_saved_psr_up)) {
		pfm_set_psr_up();
		ia64_srlz_d();
	}
}

int pfm_arch_reserve_session(struct pfm_context *ctx, u32 cpu)
{
	struct pfm_arch_context *ctx_arch;
	int is_system;
	int ret = 0;

	ctx_arch = pfm_ctx_arch(ctx);
	is_system = ctx->flags.system;

	spin_lock(&pfm_arch_sessions_lock);

	if (is_system && ctx_arch->flags.use_dbr) {
		PFM_DBG("syswide context uses dbregs");

		if (pfm_arch_sessions.pfs_ptrace_use_dbr) {
			PFM_DBG("cannot reserve syswide context: "
				  "dbregs in use by ptrace");
			ret = -EBUSY;
		} else {
			pfm_arch_sessions.pfs_sys_use_dbr++;
		}
	}
	spin_unlock(&pfm_arch_sessions_lock);

	return ret;
}

void pfm_arch_release_session(struct pfm_context *ctx, u32 cpu)
{
	struct pfm_arch_context *ctx_arch;
	int is_system;

	ctx_arch = pfm_ctx_arch(ctx);
	is_system = ctx->flags.system;

	spin_lock(&pfm_arch_sessions_lock);

	if (is_system && ctx_arch->flags.use_dbr)
		pfm_arch_sessions.pfs_sys_use_dbr--;
	spin_unlock(&pfm_arch_sessions_lock);
}

/*
 * function called from pfm_load_context_*(). Task is not guaranteed to be
 * current task. If not then other task is guaranteed stopped and off any CPU.
 * context is locked and interrupts are masked.
 *
 * On PFM_LOAD_CONTEXT, the interface guarantees monitoring is stopped.
 *
 * For system-wide task is NULL
 */
int pfm_arch_load_context(struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch;
	struct pt_regs *regs;
	int ret = 0;

	ctx_arch = pfm_ctx_arch(ctx);

	/*
	 * cannot load a context which is using range restrictions,
	 * into a thread that is being debugged.
	 *
	 * if one set out of several is using the debug registers, then
	 * we assume the context as whole is using them.
	 */
	if (ctx_arch->flags.use_dbr) {
		if (ctx->flags.system) {
			spin_lock(&pfm_arch_sessions_lock);

			if (pfm_arch_sessions.pfs_ptrace_use_dbr) {
				PFM_DBG("cannot reserve syswide context: "
					"dbregs in use by ptrace");
				ret = -EBUSY;
			} else {
				pfm_arch_sessions.pfs_sys_use_dbr++;
				PFM_DBG("pfs_sys_use_dbr=%u",
					pfm_arch_sessions.pfs_sys_use_dbr);
			}
			spin_unlock(&pfm_arch_sessions_lock);

		} else if (ctx->task->thread.flags & IA64_THREAD_DBG_VALID) {
			PFM_DBG("load_pid [%d] thread is debugged, cannot "
				  "use range restrictions", ctx->task->pid);
			ret = -EBUSY;
		}
		if (ret)
			return ret;
	}

	/*
	 * We need to intervene on context switch to toggle the
	 * psr.pp bit in system-wide. As such, we set the TIF
	 * flag so that pfm_arch_ctxswout_sys() and the
	 * pfm_arch_ctxswin_sys() functions get called
	 * from pfm_ctxsw_sys();
	 */
	if (ctx->flags.system) {
		set_thread_flag(TIF_PERFMON_CTXSW);
		PFM_DBG("[%d] set TIF", current->pid);
		return 0;
	}

	regs = task_pt_regs(ctx->task);

	/*
	 * self-monitoring systematically allows user level control
	 */
	if (ctx->task != current) {
		/*
		 * when not current, task is stopped, so this is safe
		 */
		ctx_arch->ctx_saved_psr_up = 0;
		ia64_psr(regs)->up = ia64_psr(regs)->pp = 0;
	} else
		ctx_arch->flags.insecure = 1;

	/*
	 * allow user level control (start/stop/read pmd) if:
	 * 	- self-monitoring
	 * 	- requested at context creation (PFM_IA64_FL_INSECURE)
	 *
	 * There is not security hole with PFM_IA64_FL_INSECURE because
	 * when not self-monitored, the caller must have permissions to
	 * attached to the task.
	 */
	if (ctx_arch->flags.insecure) {
		ia64_psr(regs)->sp = 0;
		PFM_DBG("clearing psr.sp for [%d]", ctx->task->pid);
	}
	return 0;
}

int pfm_arch_setfl_sane(struct pfm_context *ctx, u32 flags)
{
#define PFM_SETFL_BOTH_SWITCH	(PFM_SETFL_OVFL_SWITCH|PFM_SETFL_TIME_SWITCH)
#define PFM_ITA_SETFL_BOTH_INTR	(PFM_ITA_SETFL_INTR_ONLY|\
				 PFM_ITA_SETFL_EXCL_INTR)

/* exclude return value field */
#define PFM_SETFL_ALL_MASK	(PFM_ITA_SETFL_BOTH_INTR \
				 | PFM_SETFL_BOTH_SWITCH	\
				 | PFM_ITA_SETFL_IDLE_EXCL)

	if ((flags & ~PFM_SETFL_ALL_MASK)) {
		PFM_DBG("invalid flags=0x%x", flags);
		return -EINVAL;
	}

	if ((flags & PFM_ITA_SETFL_BOTH_INTR) == PFM_ITA_SETFL_BOTH_INTR) {
		PFM_DBG("both excl intr and ontr only are set");
		return -EINVAL;
	}

	if ((flags & PFM_ITA_SETFL_IDLE_EXCL) && !ctx->flags.system) {
		PFM_DBG("idle exclude flag only for system-wide context");
		return -EINVAL;
	}
	return 0;
}

/*
 * function called from pfm_unload_context_*(). Context is locked.
 * interrupts are masked. task is not guaranteed to be current task.
 * Access to PMU is not guaranteed.
 *
 * function must do whatever arch-specific action is required on unload
 * of a context.
 *
 * called for both system-wide and per-thread. task is NULL for ssytem-wide
 */
void pfm_arch_unload_context(struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch;
	struct pt_regs *regs;

	ctx_arch = pfm_ctx_arch(ctx);

	if (ctx->flags.system) {
		/*
		 * disable context switch hook
		 */
		clear_thread_flag(TIF_PERFMON_CTXSW);

		if (ctx_arch->flags.use_dbr) {
			spin_lock(&pfm_arch_sessions_lock);
			pfm_arch_sessions.pfs_sys_use_dbr--;
			PFM_DBG("sys_use_dbr=%u", pfm_arch_sessions.pfs_sys_use_dbr);
			spin_unlock(&pfm_arch_sessions_lock);
		}
	} else {
		regs = task_pt_regs(ctx->task);

		/*
		 * cancel user level control for per-task context
		 */
		ia64_psr(regs)->sp = 1;
		PFM_DBG("setting psr.sp for [%d]", ctx->task->pid);
	}
}

/*
 * mask monitoring by setting the privilege level to 0
 * we cannot use psr.pp/psr.up for this, it is controlled by
 * the user
 */
void pfm_arch_mask_monitoring(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *arch_info;
	unsigned long mask;
	unsigned int i;

	arch_info = pfm_pmu_info();
	/*
	 * as an optimization we look at the first 64 PMC
	 * registers only starting at PMC4.
	 */
	mask = arch_info->mask_pmcs[0] >> PFM_ITA_FCNTR;
	for (i = PFM_ITA_FCNTR; mask; i++, mask >>= 1) {
		if (likely(mask & 0x1))
			ia64_set_pmc(i, set->pmcs[i] & ~0xfUL);
	}
	/*
	 * make changes visisble
	 */
	ia64_srlz_d();
}

/*
 * function called from pfm_switch_sets(), pfm_context_load_thread(),
 * pfm_context_load_sys(), pfm_ctxsw(), pfm_switch_sets()
 * context is locked. Interrupts are masked. set cannot be NULL.
 * Access to the PMU is guaranteed.
 *
 * function must restore all PMD registers from set.
 */
void pfm_arch_restore_pmds(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_context *ctx_arch;
	unsigned long *mask;
	u16 i, num;

	ctx_arch = pfm_ctx_arch(ctx);

	if (ctx_arch->flags.insecure) {
		num = ctx->regs.num_rw_pmd;
		mask = ctx->regs.rw_pmds;
	} else {
		num = set->nused_pmds;
		mask = set->used_pmds;
	}
	/*
	 * must restore all implemented read-write PMDS to avoid leaking
	 * information especially when PFM_IA64_FL_INSECURE is set.
	 *
	 * XXX: should check PFM_IA64_FL_INSECURE==0 and use used_pmd instead
	 */
	for (i = 0; num; i++) {
		if (likely(test_bit(i, mask))) {
			pfm_arch_write_pmd(ctx, i, set->pmds[i].value);
			num--;
		}
	}
	ia64_srlz_d();
}

/*
 * function called from pfm_switch_sets(), pfm_context_load_thread(),
 * pfm_context_load_sys(), pfm_ctxsw(), pfm_switch_sets()
 * context is locked. Interrupts are masked. set cannot be NULL.
 * Access to the PMU is guaranteed.
 *
 * function must restore all PMC registers from set if needed
 */
void pfm_arch_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *arch_info;
	u64 mask2 = 0, val, plm;
	unsigned long impl_mask, mask_pmcs;
	unsigned int i;

	arch_info = pfm_pmu_info();
	/*
	 * as an optimization we only look at the first 64
	 * PMC registers. In fact, we should never scan the
	 * entire impl_pmcs because ibr/dbr are implemented
	 * separately.
	 *
	 * always skip PMC0-PMC3. PMC0 taken care of when saving
	 * state. PMC1-PMC3 not used until we get counters in
	 * the 60 and above index range.
	 */
	impl_mask = ctx->regs.pmcs[0] >> PFM_ITA_FCNTR;
	mask_pmcs = arch_info->mask_pmcs[0] >> PFM_ITA_FCNTR;
	plm = ctx->state == PFM_CTX_MASKED ? ~0xf : ~0x0;

	for (i = PFM_ITA_FCNTR;
	     impl_mask;
	     i++, impl_mask >>= 1, mask_pmcs >>= 1) {
		if (likely(impl_mask & 0x1)) {
			mask2 = mask_pmcs & 0x1 ? plm : ~0;
			val = set->pmcs[i] & mask2;
			ia64_set_pmc(i, val);
			PFM_DBG_ovfl("pmc%u=0x%lx", i, val);
		}
	}
	/*
	 * restore DBR/IBR
	 */
	if (set->priv_flags & PFM_ITA_SETFL_USE_DBR) {
		pfm_restore_ibrs(set->pmcs+256, 8);
		pfm_restore_dbrs(set->pmcs+264, 8);
	}
	ia64_srlz_d();
}

void pfm_arch_unmask_monitoring(struct pfm_context *ctx, struct pfm_event_set *set)
{
	u64 psr;
	int is_system;

	is_system = ctx->flags.system;

	psr = ia64_getreg(_IA64_REG_PSR);

	/*
	 * monitoring is masked via the PMC.plm
	 *
	 * As we restore their value, we do not want each counter to
	 * restart right away. We stop monitoring using the PSR,
	 * restore the PMC (and PMD) and then re-establish the psr
	 * as it was. Note that there can be no pending overflow at
	 * this point, because monitoring is still MASKED.
	 *
	 * Because interrupts are masked we can avoid changing
	 * DCR.pp.
	 */
	if (is_system)
		pfm_clear_psr_pp();
	else
		pfm_clear_psr_up();

	ia64_srlz_d();

	pfm_arch_restore_pmcs(ctx, set);

	/*
	 * restore psr
	 *
	 * monitoring may start right now but interrupts
	 * are still masked
	 */
	pfm_set_psr_l(psr);
	ia64_srlz_d();
}

/*
 * Called from pfm_stop()
 *
 * For per-thread:
 *   task is not necessarily current. If not current task, then
 *   task is guaranteed stopped and off any cpu. Access to PMU
 *   is not guaranteed. Interrupts are masked. Context is locked.
 *   Set is the active set.
 *
 * must disable active monitoring. ctx cannot be NULL
 */
void pfm_arch_stop(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch;
	struct pt_regs *regs;
	u64 dcr, psr;

	ctx_arch = pfm_ctx_arch(ctx);
	regs = task_pt_regs(task);

	if (!ctx->flags.system) {
		/*
		 * in ZOMBIE state we always have task == current due to
		 * pfm_exit_thread()
		 */
		ia64_psr(regs)->up = 0;
		ctx_arch->ctx_saved_psr_up = 0;

		/*
		 * in case of ZOMBIE state, there is no unload to clear
		 * insecure monitoring, so we do it in stop instead.
		 */
		if (ctx->state == PFM_CTX_ZOMBIE)
			ia64_psr(regs)->sp = 1;

		if (task == current) {
			pfm_clear_psr_up();
			ia64_srlz_d();
		}
	} else if (ctx->flags.started) { /* do not stop twice */
		dcr = ia64_getreg(_IA64_REG_CR_DCR);
		psr = ia64_getreg(_IA64_REG_PSR);

		ia64_psr(regs)->pp = 0;
		ia64_setreg(_IA64_REG_CR_DCR, dcr & ~IA64_DCR_PP);
		pfm_clear_psr_pp();
		ia64_srlz_d();

		if (ctx->active_set->flags & PFM_ITA_SETFL_IDLE_EXCL) {
			PFM_DBG("disabling idle exclude");
			__get_cpu_var(pfm_syst_info) &= ~PFM_ITA_CPUINFO_IDLE_EXCL;
		}
	}
}

/*
 * called from pfm_start()
 *
 * Interrupts are masked. Context is locked. Set is the active set.
 *
 * For per-thread:
 * 	Task is not necessarily current. If not current task, then task
 * 	is guaranteed stopped and off any cpu. No access to PMU is task
 *	is not current.
 *
 * For system-wide:
 * 	task is always current
 *
 * must enable active monitoring.
 */
void pfm_arch_start(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch;
	struct pt_regs *regs;
	u64 dcr, dcr_pp, psr_pp;
	u32 flags;

	ctx_arch = pfm_ctx_arch(ctx);
	regs = task_pt_regs(task);
	flags = ctx->active_set->flags;

	/*
	 * per-thread mode
	 */
	if (!ctx->flags.system) {

		ia64_psr(regs)->up = 1;

		if (task == current) {
			pfm_set_psr_up();
			ia64_srlz_d();
		} else {
			/*
			 * activate monitoring at next ctxswin
			 */
			ctx_arch->ctx_saved_psr_up = IA64_PSR_UP;
		}
		return;
	}

	/*
	 * system-wide mode
	 */
	dcr = ia64_getreg(_IA64_REG_CR_DCR);
	if (flags & PFM_ITA_SETFL_INTR_ONLY) {
		dcr_pp = 1;
		psr_pp = 0;
	} else if (flags & PFM_ITA_SETFL_EXCL_INTR) {
		dcr_pp = 0;
		psr_pp = 1;
	} else {
		dcr_pp = psr_pp = 1;
	}
	PFM_DBG("dcr_pp=%lu psr_pp=%lu", dcr_pp, psr_pp);

	/*
	 * update dcr_pp and psr_pp
	 */
	if (dcr_pp)
		ia64_setreg(_IA64_REG_CR_DCR, dcr | IA64_DCR_PP);
	else
		ia64_setreg(_IA64_REG_CR_DCR, dcr & ~IA64_DCR_PP);

	if (psr_pp) {
		pfm_set_psr_pp();
		ia64_psr(regs)->pp = 1;
	} else {
		pfm_clear_psr_pp();
		ia64_psr(regs)->pp = 0;
	}
	ia64_srlz_d();

	if (ctx->active_set->flags & PFM_ITA_SETFL_IDLE_EXCL) {
		PFM_DBG("enable idle exclude");
		__get_cpu_var(pfm_syst_info) |= PFM_ITA_CPUINFO_IDLE_EXCL;
	}
}

/*
 * Only call this function when a process is trying to
 * write the debug registers (reading is always allowed)
 * called from arch/ia64/kernel/ptrace.c:access_uarea()
 */
int __pfm_use_dbregs(struct task_struct *task)
{
	struct pfm_arch_context *ctx_arch;
	struct pfm_context *ctx;
	unsigned long flags;
	int ret = 0;

	PFM_DBG("called for [%d]", task->pid);

	ctx = task->pfm_context;

	/*
	 * do it only once
	 */
	if (task->thread.flags & IA64_THREAD_DBG_VALID) {
		PFM_DBG("IA64_THREAD_DBG_VALID already set");
		return 0;
	}
	if (ctx) {
		spin_lock_irqsave(&ctx->lock, flags);
		ctx_arch = pfm_ctx_arch(ctx);

		if (ctx_arch->flags.use_dbr == 1) {
			PFM_DBG("PMU using dbregs already, no ptrace access");
			ret = -1;
		}
		spin_unlock_irqrestore(&ctx->lock, flags);
		if (ret)
			return ret;
	}

	spin_lock(&pfm_arch_sessions_lock);

	/*
	 * We cannot allow setting breakpoints when system wide monitoring
	 * sessions are using the debug registers.
	 */
	if (!pfm_arch_sessions.pfs_sys_use_dbr)
		pfm_arch_sessions.pfs_ptrace_use_dbr++;
	else
		ret = -1;

	PFM_DBG("ptrace_use_dbr=%u  sys_use_dbr=%u by [%d] ret = %d",
		  pfm_arch_sessions.pfs_ptrace_use_dbr,
		  pfm_arch_sessions.pfs_sys_use_dbr,
		  task->pid, ret);

	spin_unlock(&pfm_arch_sessions_lock);
	if (ret)
		return ret;
#ifndef CONFIG_SMP
	/*
	 * in UP, we need to check whether the current
	 * owner of the PMU is not using the debug registers
	 * for monitoring. Because we are using a lazy
	 * save on ctxswout, we must force a save in this
	 * case because the debug registers are being
	 * modified by another task. We save the current
	 * PMD registers, and clear ownership. In ctxswin,
	 * full state will be reloaded.
	 *
	 * Note: we overwrite task.
	 */
	task = __get_cpu_var(pmu_owner);
	ctx = __get_cpu_var(pmu_ctx);

	if (task == NULL)
		return 0;

	ctx_arch = pfm_ctx_arch(ctx);

	if (ctx_arch->flags.use_dbr)
		pfm_save_pmds_release(ctx);
#endif
	return 0;
}

/*
 * This function is called for every task that exits with the
 * IA64_THREAD_DBG_VALID set. This indicates a task which was
 * able to use the debug registers for debugging purposes via
 * ptrace(). Therefore we know it was not using them for
 * perfmormance monitoring, so we only decrement the number
 * of "ptraced" debug register users to keep the count up to date
 */
int __pfm_release_dbregs(struct task_struct *task)
{
	int ret;

	spin_lock(&pfm_arch_sessions_lock);

	if (pfm_arch_sessions.pfs_ptrace_use_dbr == 0) {
		PFM_ERR("invalid release for [%d] ptrace_use_dbr=0", task->pid);
		ret = -1;
	}  else {
		pfm_arch_sessions.pfs_ptrace_use_dbr--;
		ret = 0;
	}
	spin_unlock(&pfm_arch_sessions_lock);

	return ret;
}

int pfm_ia64_mark_dbregs_used(struct pfm_context *ctx,
			      struct pfm_event_set *set)
{
	struct pfm_arch_context *ctx_arch;
	struct task_struct *task;
	struct thread_struct *thread;
	int ret = 0, state;
	int i, can_access_pmu = 0;
	int is_loaded, is_system;

	ctx_arch = pfm_ctx_arch(ctx);
	state = ctx->state;
	task = ctx->task;
	is_loaded = state == PFM_CTX_LOADED || state == PFM_CTX_MASKED;
	is_system = ctx->flags.system;
	can_access_pmu = __get_cpu_var(pmu_owner) == task || is_system;

	if (is_loaded == 0)
		goto done;

	if (is_system == 0) {
		thread = &(task->thread);

		/*
		 * cannot use debug registers for montioring if they are
		 * already used for debugging
		 */
		if (thread->flags & IA64_THREAD_DBG_VALID) {
			PFM_DBG("debug registers already in use for [%d]",
				  task->pid);
			return -EBUSY;
		}
	}

	/*
	 * check for debug registers in system wide mode
	 */
	spin_lock(&pfm_arch_sessions_lock);

	if (is_system) {
		if (pfm_arch_sessions.pfs_ptrace_use_dbr)
			ret = -EBUSY;
		else
			pfm_arch_sessions.pfs_sys_use_dbr++;
	}

	spin_unlock(&pfm_arch_sessions_lock);

	if (ret != 0)
		return ret;

	/*
	 * clear hardware registers to make sure we don't
	 * pick up stale state.
	 */
	if (can_access_pmu) {
		PFM_DBG("clearing ibrs, dbrs");
		for (i = 0; i < 8; i++) {
			ia64_set_ibr(i, 0);
			ia64_dv_serialize_instruction();
		}
		ia64_srlz_i();
		for (i = 0; i < 8; i++) {
			ia64_set_dbr(i, 0);
			ia64_dv_serialize_data();
		}
		ia64_srlz_d();
	}
done:
	/*
	 * debug registers are now in use
	 */
	ctx_arch->flags.use_dbr = 1;
	set->priv_flags |= PFM_ITA_SETFL_USE_DBR;
	PFM_DBG("set%u use_dbr=1", set->id);
	return 0;
}
EXPORT_SYMBOL(pfm_ia64_mark_dbregs_used);

char *pfm_arch_get_pmu_module_name(void)
{
	switch (local_cpu_data->family) {
	case 0x07:
		return "perfmon_itanium";
	case 0x1f:
		return "perfmon_mckinley";
	case 0x20:
		return "perfmon_montecito";
	default:
		return "perfmon_generic";
	}
	return NULL;
}

/*
 * global arch-specific intialization, called only once
 */
int __init pfm_arch_init(void)
{
	int ret;

	spin_lock_init(&pfm_arch_sessions_lock);

#ifdef CONFIG_IA64_PERFMON_COMPAT
	ret = pfm_ia64_compat_init();
	if (ret)
		return ret;
#endif
	register_percpu_irq(IA64_PERFMON_VECTOR, &perfmon_irqaction);


	return 0;
}
