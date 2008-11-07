/*
 * perfmon_intr.c: perfmon2 interrupt handling
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
 *
 * The initial version of perfmon.c was written by
 * Ganesh Venkitachalam, IBM Corp.
 *
 * Then it was modified for perfmon-1.x by Stephane Eranian and
 * David Mosberger, Hewlett Packard Co.
 *
 * Version Perfmon-2.x is a complete rewrite of perfmon-1.x
 * by Stephane Eranian, Hewlett Packard Co.
 *
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *                David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * More information about perfmon available at:
 * 	http://perfmon2.sf.net
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

/**
 * pfm_intr_process_64bit_ovfls - handle 64-bit counter emulation
 * @ctx: context to operate on
 * @set: set to operate on
 *
 * The function returns the number of 64-bit overflows detected.
 *
 * 64-bit software pmds are updated for overflowed pmd registers
 * the set->reset_pmds is updated to the list of pmds to reset
 *
 * In any case, set->npend_ovfls is cleared
 */
static u16 pfm_intr_process_64bit_ovfls(struct pfm_context *ctx,
					struct pfm_event_set *set,
					u32 *ovfl_ctrl)
{
	u16 i, num_ovfls, max_pmd, max_intr;
	u16 num_64b_ovfls, has_ovfl_sw, must_switch;
	u64 ovfl_thres, old_val, new_val, ovfl_mask;

	num_64b_ovfls = must_switch = 0;

	ovfl_mask = pfm_pmu_conf->ovfl_mask;
	max_pmd = ctx->regs.max_pmd;
	max_intr = ctx->regs.max_intr_pmd;

	num_ovfls = set->npend_ovfls;
	has_ovfl_sw = set->flags & PFM_SETFL_OVFL_SWITCH;

	bitmap_zero(cast_ulp(set->reset_pmds), max_pmd);

	for (i = ctx->regs.first_intr_pmd; num_ovfls; i++) {
		/*
		 * skip pmd which did not overflow
		 */
		if (!test_bit(i, cast_ulp(set->povfl_pmds)))
			continue;

		num_ovfls--;

		/*
		 * Update software value for counters ONLY
		 *
		 * Note that the pmd is not necessarily 0 at this point as
		 * qualified events may have happened before the PMU was
		 * frozen. The residual count is not taken into consideration
		 * here but will be with any read of the pmd
		 */
		ovfl_thres = set->pmds[i].ovflsw_thres;

		if (likely(test_bit(i, cast_ulp(ctx->regs.cnt_pmds)))) {
			old_val = new_val = set->pmds[i].value;
			new_val += 1 + ovfl_mask;
			set->pmds[i].value = new_val;
		}  else {
			/*
			 * for non counters which interrupt, e.g., AMD IBS,
			 * we consider this equivalent to a 64-bit counter
			 * overflow.
			 */
			old_val = 1; new_val = 0;
		}

		/*
		 * check for 64-bit overflow condition
		 */
		if (likely(old_val > new_val)) {
			num_64b_ovfls++;
			if (has_ovfl_sw && ovfl_thres > 0) {
				if (ovfl_thres == 1)
					must_switch = 1;
				set->pmds[i].ovflsw_thres = ovfl_thres - 1;
			}

			/*
			 * what to reset because of this overflow
			 * - the overflowed register
			 * - its reset_smpls
			 */
			__set_bit(i, cast_ulp(set->reset_pmds));

			bitmap_or(cast_ulp(set->reset_pmds),
				  cast_ulp(set->reset_pmds),
				  cast_ulp(set->pmds[i].reset_pmds),
				  max_pmd);
		} else {
			/*
			 * only keep track of 64-bit overflows or
			 * assimilated
			 */
			__clear_bit(i, cast_ulp(set->povfl_pmds));

			/*
			 * on some PMU, it may be necessary to re-arm the PMD
			 */
			pfm_arch_ovfl_reset_pmd(ctx, i);
		}

		PFM_DBG_ovfl("ovfl=%s pmd%u new=0x%llx old=0x%llx "
			     "hw_pmd=0x%llx o_pmds=0x%llx must_switch=%u "
			     "o_thres=%llu o_thres_ref=%llu",
			     old_val > new_val ? "64-bit" : "HW",
			     i,
			     (unsigned long long)new_val,
			     (unsigned long long)old_val,
			     (unsigned long long)pfm_read_pmd(ctx, i),
			     (unsigned long long)set->povfl_pmds[0],
			     must_switch,
			     (unsigned long long)set->pmds[i].ovflsw_thres,
			     (unsigned long long)set->pmds[i].ovflsw_ref_thres);
	}
	/*
	 * update public bitmask of 64-bit overflowed pmds
	 */
	if (num_64b_ovfls)
		bitmap_copy(cast_ulp(set->ovfl_pmds), cast_ulp(set->povfl_pmds),
			    max_intr);

	if (must_switch)
		*ovfl_ctrl |= PFM_OVFL_CTRL_SWITCH;

	/*
	 * mark the overflows as consumed
	 */
	set->npend_ovfls = 0;
	bitmap_zero(cast_ulp(set->povfl_pmds), max_intr);

	return num_64b_ovfls;
}

/**
 * pfm_intr_get_smpl_pmds_values - copy 64-bit pmd values for sampling format
 * @ctx: context to work on
 * @set: current event set
 * @arg: overflow arg to be passed to format
 * @smpl_pmds: list of PMDs of interest for the overflowed register
 *
 * build an array of 46-bit PMD values based on smpl_pmds. Values are
 * stored in increasing order of the PMD indexes
 */
static void pfm_intr_get_smpl_pmds_values(struct pfm_context *ctx,
					  struct pfm_event_set *set,
					  struct pfm_ovfl_arg *arg,
					  u64 *smpl_pmds)
{
	u16 j, k, max_pmd;
	u64 new_val, ovfl_mask;
	u64 *cnt_pmds;

	cnt_pmds = ctx->regs.cnt_pmds;
	max_pmd = ctx->regs.max_pmd;
	ovfl_mask = pfm_pmu_conf->ovfl_mask;

	for (j = k = 0; j < max_pmd; j++) {

		if (!test_bit(j, cast_ulp(smpl_pmds)))
			continue;

		new_val = pfm_read_pmd(ctx, j);

		/* for counters, build 64-bit value */
		if (test_bit(j, cast_ulp(cnt_pmds)))
			new_val = (set->pmds[j].value & ~ovfl_mask)
				| (new_val & ovfl_mask);

		arg->smpl_pmds_values[k++] = new_val;

		PFM_DBG_ovfl("s_pmd_val[%u]=pmd%u=0x%llx", k, j,
			     (unsigned long long)new_val);
	}
	arg->num_smpl_pmds = k;
}

/**
 * pfm_intr_process_smpl_fmt -- handle sampling format callback
 * @ctx: context to work on
 * @set: current event set
 * @ip: interrupted instruction pointer
 * @now: timestamp
 * @num_ovfls: number of 64-bit overflows
 * @ovfl_ctrl: set of controls for interrupt handler tail processing
 * @regs: register state
 *
 * Prepare argument (ovfl_arg) to be passed to sampling format callback, then
 * invoke the callback (fmt_handler)
 */
static int pfm_intr_process_smpl_fmt(struct pfm_context *ctx,
				    struct pfm_event_set *set,
				    unsigned long ip,
				    u64 now,
				    u64 num_ovfls,
				    u32 *ovfl_ctrl,
				    struct pt_regs *regs)
{
	struct pfm_ovfl_arg *ovfl_arg;
	u64 start_cycles, end_cycles;
	u16 i, max_pmd;
	int ret = 0;

	ovfl_arg = &ctx->ovfl_arg;

	ovfl_arg->active_set = set->id;
	max_pmd = ctx->regs.max_pmd;

	/*
	 * first_intr_pmd: first PMD which can generate PMU interrupts
	 */
	for (i = ctx->regs.first_intr_pmd; num_ovfls; i++) {
		/*
		 * skip pmd which did not have 64-bit overflows
		 */
		if (!test_bit(i, cast_ulp(set->ovfl_pmds)))
			continue;

		num_ovfls--;

		/*
		 * prepare argument to fmt_handler
		 */
		ovfl_arg->ovfl_pmd = i;
		ovfl_arg->ovfl_ctrl = 0;

		ovfl_arg->pmd_last_reset = set->pmds[i].lval;
		ovfl_arg->pmd_eventid = set->pmds[i].eventid;
		ovfl_arg->num_smpl_pmds = 0;

		/*
		 * copy values of pmds of interest, if any
		 * Sampling format may use them
		 * We do not initialize the unused smpl_pmds_values
		 */
		if (!bitmap_empty(cast_ulp(set->pmds[i].smpl_pmds), max_pmd))
			pfm_intr_get_smpl_pmds_values(ctx, set, ovfl_arg,
						      set->pmds[i].smpl_pmds);

		pfm_stats_inc(fmt_handler_calls);

		/*
		 * call format record (handler) routine
		 */
		start_cycles = sched_clock();
		ret = (*ctx->smpl_fmt->fmt_handler)(ctx, ip, now, regs);
		end_cycles = sched_clock();

		/*
		 * The reset_pmds mask is constructed automatically
		 * on overflow. When the actual reset takes place
		 * depends on the masking, switch and notification
		 * status. It may be deferred until pfm_restart().
		 */
		*ovfl_ctrl |= ovfl_arg->ovfl_ctrl;

		pfm_stats_add(fmt_handler_ns, end_cycles - start_cycles);
	}
	/*
	 * when the format cannot handle the rest of the overflow, we abort
	 */
	if (ret)
		PFM_DBG_ovfl("handler aborted at PMD%u ret=%d", i, ret);
	return ret;
}
/**
 * pfm_overflow_handler - main overflow processing routine.
 * @ctx: context to work on (always current context)
 * @set: current event set
 * @ip: interrupt instruction pointer
 * @regs: machine state
 *
 * set->num_ovfl_pmds is 0 when returning from this function even though
 * set->ovfl_pmds[] may have bits set. When leaving set->num_ovfl_pmds
 * must never be used to determine if there was a pending overflow.
 */
static void pfm_overflow_handler(struct pfm_context *ctx,
				 struct pfm_event_set *set,
				 unsigned long ip,
				 struct pt_regs *regs)
{
	struct pfm_event_set *set_orig;
	u64 now;
	u32 ovfl_ctrl;
	u16 max_intr, max_pmd;
	u16 num_ovfls;
	int ret, has_notify;

	/*
	 * take timestamp
	 */
	now = sched_clock();

	max_pmd = ctx->regs.max_pmd;
	max_intr = ctx->regs.max_intr_pmd;

	set_orig = set;
	ovfl_ctrl = 0;

	/*
	 * skip ZOMBIE case
	 */
	if (unlikely(ctx->state == PFM_CTX_ZOMBIE))
		goto stop_monitoring;

	PFM_DBG_ovfl("intr_pmds=0x%llx npend=%u ip=%p, blocking=%d "
		     "u_pmds=0x%llx use_fmt=%u",
		     (unsigned long long)set->povfl_pmds[0],
		     set->npend_ovfls,
		     (void *)ip,
		     ctx->flags.block,
		     (unsigned long long)set->used_pmds[0],
		     !!ctx->smpl_fmt);

	/*
	 * return number of 64-bit overflows
	 */
	num_ovfls = pfm_intr_process_64bit_ovfls(ctx, set, &ovfl_ctrl);

	/*
	 * there were no 64-bit overflows
	 * nothing else to do
	 */
	if (!num_ovfls)
		return;

	/*
	 * tmp_ovfl_notify = ovfl_pmds & ovfl_notify
	 * with:
	 *   - ovfl_pmds: last 64-bit overflowed pmds
	 *   - ovfl_notify: notify on overflow registers
	 */
	bitmap_and(cast_ulp(ctx->tmp_ovfl_notify),
		   cast_ulp(set->ovfl_pmds),
		   cast_ulp(set->ovfl_notify),
		   max_intr);

	has_notify = !bitmap_empty(cast_ulp(ctx->tmp_ovfl_notify), max_intr);

	/*
	 * check for sampling format and invoke fmt_handler
	 */
	if (likely(ctx->smpl_fmt)) {
		pfm_intr_process_smpl_fmt(ctx, set, ip, now, num_ovfls,
					  &ovfl_ctrl, regs);
	} else {
		/*
		 * When no sampling format is used, the default
		 * is:
		 * 	- mask monitoring if not switching
		 * 	- notify user if requested
		 *
		 * If notification is not requested, monitoring is masked
		 * and overflowed registers are not reset (saturation).
		 * This mimics the behavior of the default sampling format.
		 */
		ovfl_ctrl |= PFM_OVFL_CTRL_NOTIFY;
		if (has_notify || !(ovfl_ctrl & PFM_OVFL_CTRL_SWITCH))
			ovfl_ctrl |= PFM_OVFL_CTRL_MASK;
	}

	PFM_DBG_ovfl("set%u o_notify=0x%llx o_pmds=0x%llx "
		     "r_pmds=0x%llx ovfl_ctrl=0x%x",
		     set->id,
		     (unsigned long long)ctx->tmp_ovfl_notify[0],
		     (unsigned long long)set->ovfl_pmds[0],
		     (unsigned long long)set->reset_pmds[0],
		     ovfl_ctrl);

	/*
	 * execute the various controls
	 *        ORDER MATTERS
	 */


	/*
	 * mask monitoring
	 */
	if (ovfl_ctrl & PFM_OVFL_CTRL_MASK) {
		pfm_mask_monitoring(ctx, set);
		/*
		 * when masking, reset is deferred until
		 * pfm_restart()
		 */
		ovfl_ctrl &= ~PFM_OVFL_CTRL_RESET;

		/*
		 * when masking, switching is deferred until
		 * pfm_restart and we need to remember it
		 */
		if (ovfl_ctrl & PFM_OVFL_CTRL_SWITCH) {
			set->priv_flags |= PFM_SETFL_PRIV_SWITCH;
			ovfl_ctrl &= ~PFM_OVFL_CTRL_SWITCH;
		}
	}

	/*
	 * switch event set
	 */
	if (ovfl_ctrl & PFM_OVFL_CTRL_SWITCH) {
		pfm_switch_sets_from_intr(ctx);
		/* update view of active set */
		set = ctx->active_set;
	}
	/*
	 * send overflow notification
	 *
	 * only necessary if at least one overflowed
	 * register had the notify flag set
	 */
	if (has_notify && (ovfl_ctrl & PFM_OVFL_CTRL_NOTIFY)) {
		/*
		 * block on notify, not on masking
		 */
		if (ctx->flags.block)
			pfm_post_work(current, ctx, PFM_WORK_BLOCK);

		/*
		 * send notification and passed original set id
		 * if error, queue full, for instance, then default
		 * to masking monitoring, i.e., saturate
		 */
		ret = pfm_ovfl_notify(ctx, set_orig, ip);
		if (unlikely(ret)) {
			if (ctx->state == PFM_CTX_LOADED) {
				pfm_mask_monitoring(ctx, set);
				ovfl_ctrl &= ~PFM_OVFL_CTRL_RESET;
			}
		} else {
			ctx->flags.can_restart++;
			PFM_DBG_ovfl("can_restart=%u", ctx->flags.can_restart);
		}
	}

	/*
	 * reset overflowed registers
	 */
	if (ovfl_ctrl & PFM_OVFL_CTRL_RESET) {
		u16 nn;
		nn = bitmap_weight(cast_ulp(set->reset_pmds), max_pmd);
		if (nn)
			pfm_reset_pmds(ctx, set, nn, PFM_PMD_RESET_SHORT);
	}
	return;

stop_monitoring:
	/*
	 * Does not happen for a system-wide context nor for a
	 * self-monitored context. We cannot attach to kernel-only
	 * thread, thus it is safe to set TIF bits, i.e., the thread
	 * will eventually leave the kernel or die and either we will
	 * catch the context and clean it up in pfm_handler_work() or
	 * pfm_exit_thread().
	 *
	 * Mask until we get to pfm_handle_work()
	 */
	pfm_mask_monitoring(ctx, set);

	PFM_DBG_ovfl("ctx is zombie, converted to spurious");
	pfm_post_work(current, ctx, PFM_WORK_ZOMBIE);
}

/**
 * __pfm_interrupt_handler - 1st level interrupt handler
 * @ip: interrupted instruction pointer
 * @regs: machine state
 *
 * Function is static because we use a wrapper to easily capture timing infos.
 *
 *
 * Context locking necessary to avoid concurrent accesses from other CPUs
 * 	- For per-thread, we must prevent pfm_restart() which works when
 * 	  context is LOADED or MASKED
 */
static void __pfm_interrupt_handler(unsigned long ip, struct pt_regs *regs)
{
	struct task_struct *task;
	struct pfm_context *ctx;
	struct pfm_event_set *set;


	task = __get_cpu_var(pmu_owner);
	ctx = __get_cpu_var(pmu_ctx);

	/*
	 * verify if there is a context on this CPU
	 */
	if (unlikely(ctx == NULL)) {
		PFM_DBG_ovfl("no ctx");
		goto spurious;
	}

	/*
	 * we need to lock context because it could be accessed
	 * from another CPU. Depending on the priority level of
	 * the PMU interrupt or the arch, it may be necessary to
	 * mask interrupts alltogether to avoid race condition with
	 * the timer interrupt in case of time-based set switching,
	 * for instance.
	 */
	spin_lock(&ctx->lock);

	set = ctx->active_set;

	/*
	 * For SMP per-thread, it is not possible to have
	 * owner != NULL && task != current.
	 *
	 * For UP per-thread, because of lazy save, it
	 * is possible to receive an interrupt in another task
	 * which is not using the PMU. This means
	 * that the interrupt was in-flight at the
	 * time of pfm_ctxswout_thread(). In that
	 * case, it will be replayed when the task
	 * is scheduled again. Hence we convert to spurious.
	 *
	 * The basic rule is that an overflow is always
	 * processed in the context of the task that
	 * generated it for all per-thread contexts.
	 *
	 * for system-wide, task is always NULL
	 */
#ifndef CONFIG_SMP
	if (unlikely((task && current->pfm_context != ctx))) {
		PFM_DBG_ovfl("spurious: not owned by current task");
		goto spurious;
	}
#endif
	if (unlikely(ctx->state == PFM_CTX_MASKED)) {
		PFM_DBG_ovfl("spurious: monitoring masked");
		goto spurious;
	}

	/*
	 * check that monitoring is active, otherwise convert
	 * to spurious
	 */
	if (unlikely(!pfm_arch_is_active(ctx))) {
		PFM_DBG_ovfl("spurious: monitoring non active");
		goto spurious;
	}

	/*
	 * freeze PMU and collect overflowed PMD registers
	 * into set->povfl_pmds. Number of overflowed PMDs
	 * reported in set->npend_ovfls
	 */
	pfm_arch_intr_freeze_pmu(ctx, set);

	/*
	 * no overflow detected, interrupt may have come
	 * from the previous thread running on this CPU
	 */
	if (unlikely(!set->npend_ovfls)) {
		PFM_DBG_ovfl("no npend_ovfls");
		goto spurious;
	}

	pfm_stats_inc(ovfl_intr_regular_count);

	/*
	 * invoke actual handler
	 */
	pfm_overflow_handler(ctx, set, ip, regs);

	/*
	 * unfreeze PMU, monitoring may not actual be restarted
	 * if context is MASKED
	 */
	pfm_arch_intr_unfreeze_pmu(ctx);

	spin_unlock(&ctx->lock);

	return;

spurious:
	/* ctx may be NULL */
	pfm_arch_intr_unfreeze_pmu(ctx);
	if (ctx)
		spin_unlock(&ctx->lock);

	pfm_stats_inc(ovfl_intr_spurious_count);
}


/**
 * pfm_interrupt_handler - 1st level interrupt handler
 * @ip: interrupt instruction pointer
 * @regs: machine state
 *
 * Function called from the low-level assembly code or arch-specific perfmon
 * code. Simple wrapper used for timing purpose. Actual work done in
 * __pfm_overflow_handler()
 */
void pfm_interrupt_handler(unsigned long ip, struct pt_regs *regs)
{
	u64 start;

	pfm_stats_inc(ovfl_intr_all_count);

	BUG_ON(!irqs_disabled());

	start = sched_clock();

	__pfm_interrupt_handler(ip, regs);

	pfm_stats_add(ovfl_intr_ns, sched_clock() - start);
}
EXPORT_SYMBOL(pfm_interrupt_handler);

