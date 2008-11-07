/*
 * perfmon_cxtsw.c: perfmon2 context switch code
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
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

void pfm_save_pmds(struct pfm_context *ctx, struct pfm_event_set *set)
{
	u64 val, ovfl_mask;
	u64 *used_pmds, *cnt_pmds;
	u16 i, num;

	ovfl_mask = pfm_pmu_conf->ovfl_mask;
	num = set->nused_pmds;
	cnt_pmds = ctx->regs.cnt_pmds;
	used_pmds = set->used_pmds;

	/*
	 * save HW PMD, for counters, reconstruct 64-bit value
	 */
	for (i = 0; num; i++) {
		if (test_bit(i, cast_ulp(used_pmds))) {
			val = pfm_read_pmd(ctx, i);
			if (likely(test_bit(i, cast_ulp(cnt_pmds))))
				val = (set->pmds[i].value & ~ovfl_mask) |
					(val & ovfl_mask);
			set->pmds[i].value = val;
			num--;
		}
	}
	pfm_arch_clear_pmd_ovfl_cond(ctx, set);
}

/*
 * interrupts are  disabled (no preemption)
 */
void __pfm_ctxswin_thread(struct task_struct *task,
				 struct pfm_context *ctx, u64 now)
{
	u64 cur_act;
	struct pfm_event_set *set;
	int reload_pmcs, reload_pmds;
	int mycpu, is_active;

	mycpu = smp_processor_id();

	cur_act = __get_cpu_var(pmu_activation_number);
	/*
	 * we need to lock context because it could be accessed
	 * from another CPU. Normally the schedule() functions
	 * has masked interrupts which should be enough to
	 * protect against PMU interrupts.
	 */
	spin_lock(&ctx->lock);

	is_active = pfm_arch_is_active(ctx);

	set = ctx->active_set;

	/*
	 * in case fo zombie, we do not complete ctswin of the
	 * PMU, and we force a call to pfm_handle_work() to finish
	 * cleanup, i.e., free context + smpl_buff. The reason for
	 * deferring to pfm_handle_work() is that it is not possible
	 * to vfree() with interrupts disabled.
	 */
	if (unlikely(ctx->state == PFM_CTX_ZOMBIE)) {
		pfm_post_work(task, ctx, PFM_WORK_ZOMBIE);
		goto done;
	}

	/*
	 * if we were the last user of the PMU on that CPU,
	 * then nothing to do except restore psr
	 */
	if (ctx->last_cpu == mycpu && ctx->last_act == cur_act) {
		/*
		 * check for forced reload conditions
		 */
		reload_pmcs = set->priv_flags & PFM_SETFL_PRIV_MOD_PMCS;
		reload_pmds = set->priv_flags & PFM_SETFL_PRIV_MOD_PMDS;
	} else {
#ifndef CONFIG_SMP
		pfm_check_save_prev_ctx();
#endif
		reload_pmcs = 1;
		reload_pmds = 1;
	}
	/* consumed */
	set->priv_flags &= ~PFM_SETFL_PRIV_MOD_BOTH;

	if (reload_pmds)
		pfm_arch_restore_pmds(ctx, set);

	/*
	 * need to check if had in-flight interrupt in
	 * pfm_ctxswout_thread(). If at least one bit set, then we must replay
	 * the interrupt to avoid losing some important performance data.
	 *
	 * npend_ovfls is cleared in interrupt handler
	 */
	if (set->npend_ovfls) {
		pfm_arch_resend_irq(ctx);
		pfm_stats_inc(ovfl_intr_replay_count);
	}

	if (reload_pmcs)
		pfm_arch_restore_pmcs(ctx, set);

	/*
	 * record current activation for this context
	 */
	__get_cpu_var(pmu_activation_number)++;
	ctx->last_cpu = mycpu;
	ctx->last_act = __get_cpu_var(pmu_activation_number);

	/*
	 * establish new ownership.
	 */
	pfm_set_pmu_owner(task, ctx);

	pfm_arch_ctxswin_thread(task, ctx);
	/*
	 * set->duration does not count when context in MASKED state.
	 * set->duration_start is reset in unmask_monitoring()
	 */
	set->duration_start = now;

	/*
	 * re-arm switch timeout, if necessary
	 * Timeout is active only if monitoring is active,
	 * i.e., LOADED + started
	 *
	 * We reload the remainder timeout or the full timeout.
	 * Remainder is recorded on context switch out or in
	 * pfm_load_context()
	 */
	if (ctx->state == PFM_CTX_LOADED
	    && (set->flags & PFM_SETFL_TIME_SWITCH) && is_active) {
		pfm_restart_timer(ctx, set);
		/* careful here as pfm_restart_timer may switch sets */
	}
done:
	spin_unlock(&ctx->lock);
}

/*
 * interrupts are masked, runqueue lock is held.
 *
 * In UP. we simply stop monitoring and leave the state
 * in place, i.e., lazy save
 */
void __pfm_ctxswout_thread(struct task_struct *task,
				  struct pfm_context *ctx, u64 now)
{
	struct pfm_event_set *set;
	int need_save_pmds, is_active;

	/*
	 * we need to lock context because it could be accessed
	 * from another CPU. Normally the schedule() functions
	 * has masked interrupts which should be enough to
	 * protect against PMU interrupts.
	 */

	spin_lock(&ctx->lock);

	is_active = pfm_arch_is_active(ctx);
	set = ctx->active_set;

	/*
	 * stop monitoring and
	 * collect pending overflow information
	 * needed on ctxswin. We cannot afford to lose
	 * a PMU interrupt.
	 */
	need_save_pmds = pfm_arch_ctxswout_thread(task, ctx);

	if (ctx->state == PFM_CTX_LOADED) {
		/*
		 * accumulate only when set is actively monitoring,
		 */
		set->duration += now - set->duration_start;

		/*
		 * record remaining timeout
		 * reload in pfm_ctxsw_in()
		 */
		if (is_active && (set->flags & PFM_SETFL_TIME_SWITCH)) {
			struct hrtimer *h = NULL;
			h = &__get_cpu_var(pfm_hrtimer);
			hrtimer_cancel(h);
			set->hrtimer_rem = hrtimer_get_remaining(h);
			PFM_DBG_ovfl("hrtimer=%lld",
				     (long long)set->hrtimer_rem.tv64);
		}
	}

#ifdef CONFIG_SMP
	/*
	 * in SMP, release ownership of this PMU.
	 * PMU interrupts are masked, so nothing
	 * can happen.
	 */
	pfm_set_pmu_owner(NULL, NULL);

	/*
	 * On some architectures, it is necessary to read the
	 * PMD registers to check for pending overflow in
	 * pfm_arch_ctxswout_thread(). In that case, saving of
	 * the PMDs  may be  done there and not here.
	 */
	if (need_save_pmds)
		pfm_save_pmds(ctx, set);
#endif
	spin_unlock(&ctx->lock);
}

/*
 *
 */
static void __pfm_ctxswout_sys(struct task_struct *prev,
			       struct task_struct *next)
{
	struct pfm_context *ctx;

	ctx = __get_cpu_var(pmu_ctx);
	BUG_ON(!ctx);

	/*
	 * propagate TIF_PERFMON_CTXSW to ensure that:
	 * - previous task has TIF_PERFMON_CTXSW cleared, in case it is
	 *   scheduled onto another CPU where there is syswide monitoring
	 * - next task has TIF_PERFMON_CTXSW set to ensure it will come back
	 *   here when context switched out
	 */
	clear_tsk_thread_flag(prev, TIF_PERFMON_CTXSW);
	set_tsk_thread_flag(next, TIF_PERFMON_CTXSW);

	/*
	 * nothing to do until actually started
	 * XXX: assumes no mean to start from user level
	 */
	if (!ctx->flags.started)
		return;

	pfm_arch_ctxswout_sys(prev, ctx);
}

/*
 *
 */
static void __pfm_ctxswin_sys(struct task_struct *prev,
			      struct task_struct *next)
{
	struct pfm_context *ctx;

	ctx = __get_cpu_var(pmu_ctx);
	BUG_ON(!ctx);

	/*
	 * nothing to do until actually started
	 * XXX: assumes no mean to start from user level
	 */
	if (!ctx->flags.started)
		return;

	pfm_arch_ctxswin_sys(next, ctx);
}

void pfm_ctxsw_out(struct task_struct *prev,
		   struct task_struct *next)
{
	struct pfm_context *ctxp;
	u64 now;

	now = sched_clock();

	ctxp = prev->pfm_context;

	if (ctxp)
		__pfm_ctxswout_thread(prev, ctxp, now);
	else
		__pfm_ctxswout_sys(prev, next);

	pfm_stats_inc(ctxswout_count);
	pfm_stats_add(ctxswout_ns, sched_clock() - now);
}

void pfm_ctxsw_in(struct task_struct *prev,
		  struct task_struct *next)
{
	struct pfm_context *ctxn;
	u64 now;

	now = sched_clock();

	ctxn = next->pfm_context;

	if (ctxn)
		__pfm_ctxswin_thread(next, ctxn, now);
	else
		__pfm_ctxswin_sys(prev, next);

	pfm_stats_inc(ctxswin_count);
	pfm_stats_add(ctxswin_ns, sched_clock() - now);
}
