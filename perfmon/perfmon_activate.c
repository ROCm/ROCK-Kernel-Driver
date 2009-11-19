/*
 * perfmon_activate.c: perfmon2 start/stop functions
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
 *
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

/**
 * __pfm_start - activate monitoring
 * @ctx: context to operate on
 * @start: pfarg_start as passed by user
 *
 * When operating in per-thread mode and not self-monitoring, the monitored
 * thread must be stopped. Activation will be effective next time the thread
 * is context switched in.
 *
 * The pfarg_start argument is optional and may be used to designate
 * the initial event set to activate. When not provided, the last active
 * set is used. For the first activation, set0 is used when start is NULL.
 *
 * On some architectures, e.g., IA-64, it may be possible to start monitoring
 * without calling this function under certain conditions (per-thread and self
 * monitoring). In this case, either set0 or the last active set is used.
 *
 * the context is locked and interrupts are disabled.
 */
int __pfm_start(struct pfm_context *ctx, struct pfarg_start *start)
{
	struct task_struct *task, *owner_task;
	struct pfm_smpl_fmt *fmt;
	struct pfm_event_set *new_set, *old_set;
	int is_self;

	task = ctx->task;

	/*
	 * UNLOADED: error
	 * LOADED  : normal start, nop if started unless set is different
	 * MASKED  : nop or change set when unmasking
	 * ZOMBIE  : cannot happen
	 */
	if (ctx->state == PFM_CTX_UNLOADED)
		return -EINVAL;

	old_set = new_set = ctx->active_set;
	fmt = ctx->smpl_fmt;

	/*
	 * always the case for system-wide
	 */
	if (task == NULL)
		task = current;

	is_self = task == current;

	/*
	 * argument is provided?
	 */
	if (start) {
		/*
		 * find the set to load first
		 */
		new_set = pfm_find_set(ctx, start->start_set, 0);
		if (new_set == NULL) {
			PFM_DBG("event set%u does not exist",
				start->start_set);
			return -EINVAL;
		}
	}

	PFM_DBG("cur_set=%u req_set=%u", old_set->id, new_set->id);

	/*
	 * if we need to change the active set we need
	 * to check if we can access the PMU
	 */
	if (new_set != old_set) {

		owner_task = __get_cpu_var(pmu_owner);
		/*
		 * system-wide: must run on the right CPU
		 * per-thread : must be the owner of the PMU context
		 *
		 * pfm_switch_sets() returns with monitoring stopped
		 */
		if (is_self) {
			pfm_switch_sets(ctx, new_set, PFM_PMD_RESET_LONG, 1);
		} else {
			/*
			 * In a UP kernel, the PMU may contain the state
			 * of the task we want to operate on, yet the task
			 * may be switched out (lazy save). We need to save
			 * current state (old_set), switch active_set and
			 * mark it for reload.
			 */
			if (owner_task == task)
				pfm_save_pmds(ctx, old_set);
			ctx->active_set = new_set;
			new_set->priv_flags |= PFM_SETFL_PRIV_MOD_BOTH;
		}
	}

	/*
	 * mark as started
	 * must be done before calling pfm_arch_start()
	 */
	ctx->flags.started = 1;

	pfm_arch_start(task, ctx);

	if (fmt && fmt->fmt_start)
		(*fmt->fmt_start)(ctx);

	/*
	 * we check whether we had a pending ovfl before restarting.
	 * If so we need to regenerate the interrupt to make sure we
	 * keep recorded samples. For non-self monitoring this check
	 * is done in the pfm_ctxswin_thread() routine.
	 *
	 * we check new_set/old_set because pfm_switch_sets() already
	 * takes care of replaying the pending interrupts
	 */
	if (is_self && new_set != old_set && new_set->npend_ovfls) {
		pfm_arch_resend_irq(ctx);
		pfm_stats_inc(ovfl_intr_replay_count);
	}

	/*
	 * always start with full timeout
	 */
	new_set->hrtimer_rem = new_set->hrtimer_exp;

	/*
	 * activate timeout for system-wide, self-montoring
	 * Always start with full timeout
	 * Timeout is at least one tick away, so no risk of
	 * having hrtimer_start() trying to wakeup softirqd
	 * and thus causing troubles. This cannot happen anmyway
	 * because cb_mode = HRTIMER_CB_IRQSAFE_NO_SOFTIRQ
	 */
	if (is_self && new_set->flags & PFM_SETFL_TIME_SWITCH) {
		hrtimer_start(&__get_cpu_var(pfm_hrtimer),
			      new_set->hrtimer_rem,
			      HRTIMER_MODE_REL);

		PFM_DBG("set%u started timeout=%lld",
			new_set->id,
			(unsigned long long)new_set->hrtimer_rem.tv64);
	}

	/*
	 * we restart total duration even if context was
	 * already started. In that case, counts are simply
	 * reset.
	 *
	 * For per-thread, if not self-monitoring, the statement
	 * below will have no effect because thread is stopped.
	 * The field is reset of ctxsw in.
	 */
	new_set->duration_start = sched_clock();

	return 0;
}

/**
 * __pfm_stop - stop monitoring
 * @ctx: context to operate on
 * @release_info: infos for caller (see below)
 *
 * When operating in per-thread* mode and when not self-monitoring,
 * the monitored thread must be stopped.
 *
 * the context is locked and interrupts are disabled.
 *
 * release_info value upon return:
 * 	- bit 0 : unused
 * 	- bit 1 : when set, must cancel hrtimer
 */
int __pfm_stop(struct pfm_context *ctx, int *release_info)
{
	struct pfm_event_set *set;
	struct task_struct *task;
	struct pfm_smpl_fmt *fmt;
	u64 now;
	int state;

	*release_info = 0;

	now = sched_clock();
	state = ctx->state;
	set = ctx->active_set;

	/*
	 * context must be attached (zombie cannot happen)
	 */
	if (state == PFM_CTX_UNLOADED)
		return -EINVAL;

	task = ctx->task;
	fmt = ctx->smpl_fmt;

	PFM_DBG("ctx_task=[%d] ctx_state=%d is_system=%d",
		task ? task->pid : -1,
		state,
		!task);

	/*
	 * this happens for system-wide context
	 */
	if (task == NULL)
		task = current;

	/*
	 * compute elapsed time
	 *
	 * unless masked, compute elapsed duration, stop timeout
	 */
	if (task == current && state == PFM_CTX_LOADED) {
		/*
		 * timeout cancel must be deferred until context is
		 * unlocked to avoid race with pfm_handle_switch_timeout()
		 */
		if (set->flags & PFM_SETFL_TIME_SWITCH)
			*release_info |= 0x2;

		set->duration += now - set->duration_start;
	}

	pfm_arch_stop(task, ctx);

	ctx->flags.started = 0;

	if (fmt && fmt->fmt_stop)
		(*fmt->fmt_stop)(ctx);

	/*
	 * starting now, in-flight PMU interrupt for this context
	 * are treated as spurious
	 */
	return 0;
}
