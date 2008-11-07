/*
 * This file implements the X86 specific support for the perfmon2 interface
 *
 * Copyright (c) 2005-2007 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * Copyright (c) 2007 Advanced Micro Devices, Inc.
 * Contributed by Robert Richter <robert.richter@amd.com>
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
#include <linux/interrupt.h>
#include <linux/perfmon_kern.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/nmi.h>

#include <asm/apic.h>

DEFINE_PER_CPU(unsigned long, real_iip);
DEFINE_PER_CPU(int, pfm_using_nmi);
DEFINE_PER_CPU(unsigned long, saved_lvtpc);

/**
 * pfm_arch_ctxswin_thread - thread context switch in
 * @task: task switched in
 * @ctx: context for the task
 *
 * Called from pfm_ctxsw(). Task is guaranteed to be current.
 * set cannot be NULL. Context is locked. Interrupts are masked.
 *
 * Caller has already restored all PMD and PMC registers, if
 * necessary (i.e., lazy restore scheme).
 *
 * On x86, the only common code just needs to unsecure RDPMC if necessary
 *
 * On model-specific features, e.g., PEBS, IBS, are taken care of in the
 * corresponding PMU description module
 */
void pfm_arch_ctxswin_thread(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch;

	ctx_arch = pfm_ctx_arch(ctx);

	/*
	 *  restore saved real iip
	 */
	if (ctx->active_set->npend_ovfls)
		__get_cpu_var(real_iip) = ctx_arch->saved_real_iip;

	/*
	 * enable RDPMC on this CPU
	 */
	if (ctx_arch->flags.insecure)
		set_in_cr4(X86_CR4_PCE);
}

/**
 * pfm_arch_ctxswout_thread - context switch out thread
 * @task: task switched out
 * @ctx : context switched out
 *
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
	struct pfm_arch_pmu_info *pmu_info;

	ctx_arch = pfm_ctx_arch(ctx);
	pmu_info = pfm_pmu_info();

	/*
	 * disable lazy restore of PMCS on ctxswin because
	 * we modify some of them.
	 */
	ctx->active_set->priv_flags |= PFM_SETFL_PRIV_MOD_PMCS;

	if (ctx->active_set->npend_ovfls)
		ctx_arch->saved_real_iip = __get_cpu_var(real_iip);

	/*
	 * disable RDPMC on this CPU
	 */
	if (ctx_arch->flags.insecure)
		clear_in_cr4(X86_CR4_PCE);

	if (ctx->state == PFM_CTX_MASKED)
		return 1;

	return pmu_info->stop_save(ctx, ctx->active_set);
}

/**
 * pfm_arch_stop - deactivate monitoring
 * @task: task to stop
 * @ctx: context to stop
 *
 * Called from pfm_stop()
 * Interrupts are masked. Context is locked. Set is the active set.
 *
 * For per-thread:
 *   task is not necessarily current. If not current task, then
 *   task is guaranteed stopped and off any cpu. Access to PMU
 *   is not guaranteed.
 *
 * For system-wide:
 * 	task is current
 *
 * must disable active monitoring. ctx cannot be NULL
 */
void pfm_arch_stop(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *pmu_info;

	pmu_info = pfm_pmu_info();

	/*
	 * no need to go through stop_save()
	 * if we are already stopped
	 */
	if (!ctx->flags.started || ctx->state == PFM_CTX_MASKED)
		return;

	if (task != current)
		return;

	pmu_info->stop_save(ctx, ctx->active_set);
}


/**
 * pfm_arch_start - activate monitoring
 * @task: task to start
 * @ctx: context to stop
 *
 * Interrupts are masked. Context is locked.
 *
 * For per-thread:
 * 	Task is not necessarily current. If not current task, then task
 * 	is guaranteed stopped and off any cpu. No access to PMU is task
 *	is not current.
 *
 * For system-wide:
 * 	task is always current
 */
void pfm_arch_start(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_event_set *set;

	set = ctx->active_set;

	if (task != current)
		return;

	/*
	 * cannot restore PMC if no access to PMU. Will be done
	 * when the thread is switched back in
	 */

	pfm_arch_restore_pmcs(ctx, set);
}

/**
 * pfm_arch_restore_pmds - reload PMD registers
 * @ctx: context to restore from
 * @set: current event set
 *
 * function called from pfm_switch_sets(), pfm_context_load_thread(),
 * pfm_context_load_sys(), pfm_ctxsw()
 *
 * Context is locked. Interrupts are masked. Set cannot be NULL.
 * Access to the PMU is guaranteed.
 */
void pfm_arch_restore_pmds(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *pmu_info;
	u16 i, num;

	pmu_info = pfm_pmu_info();

	num = set->nused_pmds;

	/*
	 * model-specific override
	 */
	if (pmu_info->restore_pmds) {
		pmu_info->restore_pmds(ctx, set);
		return;
	}

	/*
	 * we can restore only the PMD we use because:
	 *
	 * 	- can only read with pfm_read_pmds() the registers
	 * 	  declared used via pfm_write_pmds(), smpl_pmds, reset_pmds
	 *
	 * 	- if cr4.pce=1, only counters are exposed to user. RDPMC
	 * 	  does not work with other types of PMU registers.Thus, no
	 * 	  address is ever exposed by counters
	 *
	 * 	- there is never a dependency between one pmd register and
	 * 	  another
	 */
	for (i = 0; num; i++) {
		if (likely(test_bit(i, cast_ulp(set->used_pmds)))) {
			pfm_write_pmd(ctx, i, set->pmds[i].value);
			num--;
		}
	}
}

/**
 * pfm_arch_restore_pmcs - reload PMC registers
 * @ctx: context to restore from
 * @set: current event set
 *
 * function called from pfm_switch_sets(), pfm_context_load_thread(),
 * pfm_context_load_sys(), pfm_ctxsw().
 *
 * Context is locked. Interrupts are masked. set cannot be NULL.
 * Access to the PMU is guaranteed.
 *
 * function must restore all PMC registers from set
 */
void pfm_arch_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *pmu_info;
	u64 *mask;
	u16 i, num;

	pmu_info = pfm_pmu_info();

	/*
	 * we need to restore PMCs only when:
	 * 	- context is not masked
	 * 	- monitoring activated
	 *
	 * Masking monitoring after an overflow does not change the
	 * value of flags.started
	 */
	if (ctx->state == PFM_CTX_MASKED || !ctx->flags.started)
		return;

	/*
	 * model-specific override
	 */
	if (pmu_info->restore_pmcs) {
		pmu_info->restore_pmcs(ctx, set);
		return;
	}
	/*
	 * restore all pmcs
	 *
	 * It is not possible to restore only the pmcs we used because
	 * certain PMU models (e.g. Pentium 4) have dependencies. Thus
	 * we do not want one application using stale PMC coming from
	 * another one.
	 *
	 * On PMU models where there is no dependencies between pmc, then
	 * it is possible to optimize by only restoring the registers that
	 * are used, and this can be done with the models-specific override
	 * for this function.
	 *
	 * The default code takes the safest approach, i.e., assume the worse
	 */
	mask = ctx->regs.pmcs;
	num = ctx->regs.num_pmcs;
	for (i = 0; num; i++) {
		if (test_bit(i, cast_ulp(mask))) {
			pfm_arch_write_pmc(ctx, i, set->pmcs[i]);
			num--;
		}
	}
}

/**
 * smp_pmu_interrupt - lowest level PMU interrupt handler for X86
 * @regs: machine state
 *
 * The PMU interrupt is handled through an interrupt gate, therefore
 * the CPU automatically clears the EFLAGS.IF, i.e., masking interrupts.
 *
 * The perfmon interrupt handler MUST run with interrupts disabled due
 * to possible race with other, higher priority interrupts, such as timer
 * or IPI function calls.
 *
 * See description in IA-32 architecture manual, Vol 3 section 5.8.1
 */
void smp_pmu_interrupt(struct pt_regs *regs)
{
	struct pfm_arch_pmu_info *pmu_info;
	struct pfm_context *ctx;
	unsigned long iip;
	int using_nmi;

	using_nmi = __get_cpu_var(pfm_using_nmi);

	ack_APIC_irq();

	irq_enter();

	/*
	 * when using NMI, pfm_handle_nmi() gets called
	 * first. It stops monitoring and record the
	 * iip into real_iip, then it repost the interrupt
	 * using the lower priority vector LOCAL_PERFMON_VECTOR
	 *
	 * On some processors, e.g., P4, it may be that some
	 * state is already recorded from pfm_handle_nmi()
	 * and it only needs to be copied back into the normal
	 * fields so it can be used transparently by higher level
	 * code.
	 */
	if (using_nmi) {
		ctx = __get_cpu_var(pmu_ctx);
		pmu_info = pfm_pmu_info();
		iip = __get_cpu_var(real_iip);
		if (ctx && pmu_info->nmi_copy_state)
			pmu_info->nmi_copy_state(ctx);
	} else
		iip = instruction_pointer(regs);

	pfm_interrupt_handler(iip, regs);

	/*
	 * On Intel P6, Pentium M, P4, Intel Core:
	 * 	- it is necessary to clear the MASK field for the LVTPC
	 * 	  vector. Otherwise interrupts remain masked. See
	 * 	  section 8.5.1
	 * AMD X86-64:
	 * 	- the documentation does not stipulate the behavior.
	 * 	  To be safe, we also rewrite the vector to clear the
	 * 	  mask field
	 */
	if (!using_nmi && current_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		apic_write(APIC_LVTPC, LOCAL_PERFMON_VECTOR);

	irq_exit();
}

/**
 * pfm_handle_nmi - PMU NMI handler notifier callback
 * @nb ; notifier block
 * @val: type of die notifier
 * @data: die notifier-specific data
 *
 * called from notify_die() notifier from an trap handler path. We only
 * care about NMI related callbacks, and ignore everything else.
 *
 * Cannot grab any locks, include the perfmon context lock
 *
 * Must detect if NMI interrupt comes from perfmon, and if so it must
 * stop the PMU and repost a lower-priority interrupt. The perfmon interrupt
 * handler needs to grab the context lock, thus is cannot be run directly
 * from the NMI interrupt call path.
 */
static int __kprobes pfm_handle_nmi(struct notifier_block *nb,
				    unsigned long val,
				    void *data)
{
	struct die_args *args = data;
	struct pfm_context *ctx;
	struct pfm_arch_pmu_info *pmu_info;

	/*
	 * only NMI related calls
	 */
	if (val != DIE_NMI_IPI)
		return NOTIFY_DONE;

	/*
	 * perfmon not using NMI
	 */
	if (!__get_cpu_var(pfm_using_nmi))
		return NOTIFY_DONE;

	/*
	 * No context
	 */
	ctx = __get_cpu_var(pmu_ctx);
	if (!ctx) {
		PFM_DBG_ovfl("no ctx");
		return NOTIFY_DONE;
	}

	/*
	 * Detect if we have overflows, i.e., NMI interrupt
	 * caused by PMU
	 */
	pmu_info = pfm_pmu_conf->pmu_info;
	if (!pmu_info->has_ovfls(ctx)) {
		PFM_DBG_ovfl("no ovfl");
		return NOTIFY_DONE;
	}

	/*
	 * we stop the PMU to avoid further overflow before this
	 * one is treated by lower priority interrupt handler
	 */
	pmu_info->quiesce();

	/*
	 * record actual instruction pointer
	 */
	__get_cpu_var(real_iip) = instruction_pointer(args->regs);

	/*
	 * post lower priority interrupt (LOCAL_PERFMON_VECTOR)
	 */
	pfm_arch_resend_irq(ctx);

	pfm_stats_inc(ovfl_intr_nmi_count);

	/*
	 * we need to rewrite the APIC vector on Intel
	 */
	if (current_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		apic_write(APIC_LVTPC, APIC_DM_NMI);

	/*
	 * the notification was for us
	 */
	return NOTIFY_STOP;
}

static struct notifier_block pfm_nmi_nb = {
	.notifier_call = pfm_handle_nmi
};

/**
 * pfm_arch_get_pmu_module_name - get PMU description module name for autoload
 *
 * called from pfm_pmu_request_module
 */
char *pfm_arch_get_pmu_module_name(void)
{
	switch (current_cpu_data.x86) {
	case 6:
		switch (current_cpu_data.x86_model) {
		case 3: /* Pentium II */
		case 7 ... 11:
		case 13:
			return "perfmon_p6";
		case 15: /* Merom */
		case 23: /* Penryn */
			return "perfmon_intel_core";
		case 28: /* Atom/Silverthorne */
			return "perfmon_intel_atom";
		case 29: /* Dunnington */
			return "perfmon_intel_core";
		default:
			goto try_arch;
		}
	case 15:
	case 16:
		/* All Opteron processors */
		if (current_cpu_data.x86_vendor == X86_VENDOR_AMD)
			return "perfmon_amd64";

		switch (current_cpu_data.x86_model) {
		case 0 ... 6:
			return "perfmon_p4";
		}
		/* FALL THROUGH */
	default:
try_arch:
		if (boot_cpu_has(X86_FEATURE_ARCH_PERFMON))
			return "perfmon_intel_arch";
		return NULL;
	}
	return NULL;
}

/**
 * pfm_arch_resend_irq - post perfmon interrupt on regular vector
 *
 * called from pfm_ctxswin_thread() and pfm_handle_nmi()
 */
void pfm_arch_resend_irq(struct pfm_context *ctx)
{
	unsigned long val, dest;
	/*
	 * we cannot use hw_resend_irq() because it goes to
	 * the I/O APIC. We need to go to the Local APIC.
	 *
	 * The "int vec" is not the right solution either
	 * because it triggers a software intr. We need
	 * to regenerate the interrupt and have it pended
	 * until we unmask interrupts.
	 *
	 * Instead we send ourself an IPI on the perfmon
	 * vector.
	 */
	val  = APIC_DEST_SELF|APIC_INT_ASSERT|
	       APIC_DM_FIXED|LOCAL_PERFMON_VECTOR;

	dest = apic_read(APIC_ID);
	apic_write(APIC_ICR2, dest);
	apic_write(APIC_ICR, val);
}

/**
 * pfm_arch_pmu_acquire_percpu - setup APIC per CPU
 * @data: contains pmu flags
 */
static void pfm_arch_pmu_acquire_percpu(void *data)
{

	struct pfm_arch_pmu_info *pmu_info;
	unsigned int tmp, vec;
	unsigned long flags = (unsigned long)data;
	unsigned long lvtpc;

	pmu_info = pfm_pmu_conf->pmu_info;

	/*
	 * we only reprogram the LVTPC vector if we have detected
	 * no sharing, otherwise it means the APIC is already programmed
	 * and we use whatever vector (likely NMI) is there
	 */
	if (!(flags & PFM_X86_FL_SHARING)) {
		if (flags & PFM_X86_FL_USE_NMI)
			vec = APIC_DM_NMI;
		else
			vec = LOCAL_PERFMON_VECTOR;

		tmp = apic_read(APIC_LVTERR);
		apic_write(APIC_LVTERR, tmp | APIC_LVT_MASKED);
		apic_write(APIC_LVTPC, vec);
		apic_write(APIC_LVTERR, tmp);
	}
	lvtpc = (unsigned long)apic_read(APIC_LVTPC);

	__get_cpu_var(pfm_using_nmi) = lvtpc == APIC_DM_NMI;

	PFM_DBG("LTVPC=0x%lx using_nmi=%d", lvtpc, __get_cpu_var(pfm_using_nmi));

	/*
	 * invoke model specific acquire routine. May be used for
	 * model-specific initializations
	 */
	if (pmu_info->acquire_pmu_percpu)
		pmu_info->acquire_pmu_percpu();
}

/**
 * pfm_arch_pmu_acquire - acquire PMU resource from system
 * @unavail_pmcs : bitmask to use to set unavailable pmcs
 * @unavail_pmds : bitmask to use to set unavailable pmds
 *
 * interrupts are not masked
 *
 * Grab PMU registers from lower level MSR allocator
 *
 * Program the APIC according the possible interrupt vector
 * either LOCAL_PERFMON_VECTOR or NMI
 */
int pfm_arch_pmu_acquire(u64 *unavail_pmcs, u64 *unavail_pmds)
{
	struct pfm_arch_pmu_info *pmu_info;
	struct pfm_regmap_desc *d;
	u16 i, nlost;

	pmu_info = pfm_pmu_conf->pmu_info;
	pmu_info->flags &= ~PFM_X86_FL_SHARING;

	nlost = 0;

	d = pfm_pmu_conf->pmc_desc;
	for (i = 0; i < pfm_pmu_conf->num_pmc_entries;  i++, d++) {
		if (!(d->type & PFM_REG_I))
			continue;

		if (d->type & PFM_REG_V)
			continue;
		/*
		 * reserve register with lower-level allocator
		 */
		if (!reserve_evntsel_nmi(d->hw_addr)) {
			PFM_DBG("pmc%d(%s) already used", i, d->desc);
			__set_bit(i, cast_ulp(unavail_pmcs));
			nlost++;
			continue;
		}
	}
	PFM_DBG("nlost=%d info_flags=0x%x\n", nlost, pmu_info->flags);
	/*
	 * some PMU models (e.g., P6) do not support sharing
	 * so check if we found less than the expected number of PMC registers
	 */
	if (nlost) {
		if (pmu_info->flags & PFM_X86_FL_NO_SHARING) {
			PFM_INFO("PMU already used by another subsystem, "
				 "PMU does not support sharing, "
				 "try disabling Oprofile or "
				 "reboot with nmi_watchdog=0");
			goto undo;
		}
		pmu_info->flags |= PFM_X86_FL_SHARING;
	}

	d = pfm_pmu_conf->pmd_desc;
	for (i = 0; i < pfm_pmu_conf->num_pmd_entries;  i++, d++) {
		if (!(d->type & PFM_REG_I))
			continue;

		if (d->type & PFM_REG_V)
			continue;

		if (!reserve_perfctr_nmi(d->hw_addr)) {
			PFM_DBG("pmd%d(%s) already used", i, d->desc);
			__set_bit(i, cast_ulp(unavail_pmds));
		}
	}
	/*
	 * program APIC on each CPU
	 */
	on_each_cpu(pfm_arch_pmu_acquire_percpu,
		    (void *)(unsigned long)pmu_info->flags , 1);

	return 0;
undo:
	/*
	 * must undo reservation of pmcs in case of error
	 */
	d = pfm_pmu_conf->pmc_desc;
	for (i = 0; i < pfm_pmu_conf->num_pmc_entries;  i++, d++) {
		if (!(d->type & (PFM_REG_I|PFM_REG_V)))
			continue;
		if (!test_bit(i, cast_ulp(unavail_pmcs)))
			release_evntsel_nmi(d->hw_addr);
	}
	return -EBUSY;
}
/**
 * pfm-arch_pmu_release_percpu - clear NMI state for one CPU
 *
 */
static void pfm_arch_pmu_release_percpu(void *data)
{
	struct pfm_arch_pmu_info *pmu_info;

	pmu_info = pfm_pmu_conf->pmu_info;

	__get_cpu_var(pfm_using_nmi) = 0;

	/*
	 * invoke model specific release routine.
	 * May be used to undo certain initializations
	 * or free some model-specific ressources.
	 */
	if (pmu_info->release_pmu_percpu)
		pmu_info->release_pmu_percpu();
}

/**
 * pfm_arch_pmu_release - release PMU resource to system
 *
 * called from pfm_pmu_release()
 * interrupts are not masked
 *
 * On x86, we return the PMU registers to the MSR allocator
 */
void pfm_arch_pmu_release(void)
{
	struct pfm_regmap_desc *d;
	u16 i, n;

	d = pfm_pmu_conf->pmc_desc;
	n = pfm_pmu_conf->regs_all.num_pmcs;
	for (i = 0; n; i++, d++) {
		if (!test_bit(i, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
			continue;
		release_evntsel_nmi(d->hw_addr);
		n--;
		PFM_DBG("pmc%u released", i);
	}
	d = pfm_pmu_conf->pmd_desc;
	n = pfm_pmu_conf->regs_all.num_pmds;
	for (i = 0; n; i++, d++) {
		if (!test_bit(i, cast_ulp(pfm_pmu_conf->regs_all.pmds)))
			continue;
		release_perfctr_nmi(d->hw_addr);
		n--;
		PFM_DBG("pmd%u released", i);
	}

	/* clear NMI variable if used */
	if (__get_cpu_var(pfm_using_nmi))
		on_each_cpu(pfm_arch_pmu_release_percpu, NULL , 1);
}

/**
 * pfm_arch_pmu_config_init - validate PMU description structure
 * @cfg: PMU description structure
 *
 * return:
 * 	0 if valid
 * 	errno otherwise
 *
 * called from pfm_pmu_register()
 */
int pfm_arch_pmu_config_init(struct pfm_pmu_config *cfg)
{
	struct pfm_arch_pmu_info *pmu_info;

	pmu_info = pfm_pmu_info();
	if (!pmu_info) {
		PFM_DBG("%s missing pmu_info", cfg->pmu_name);
		return -EINVAL;
	}
	if (!pmu_info->has_ovfls) {
		PFM_DBG("%s missing has_ovfls callback", cfg->pmu_name);
		return -EINVAL;
	}
	if (!pmu_info->quiesce) {
		PFM_DBG("%s missing quiesce callback", cfg->pmu_name);
		return -EINVAL;
	}
	if (!pmu_info->stop_save) {
		PFM_DBG("%s missing stop_save callback", cfg->pmu_name);
		return -EINVAL;
	}
	return 0;
}

/**
 * pfm_arch_init - one time global arch-specific initialization
 *
 * called from pfm_init()
 */
int __init pfm_arch_init(void)
{
	/*
	 * we need to register our NMI handler when the kernels boots
	 * to avoid a deadlock condition with the NMI watchdog or Oprofile
	 * if we were to try and register/unregister on-demand.
	 */
	register_die_notifier(&pfm_nmi_nb);
	return 0;
}
