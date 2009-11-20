/*
 * perfmon_attach.c: perfmon2 load/unload functions
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
#include <linux/fs.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

/**
 * __pfm_load_context_sys - attach context to a CPU in system-wide mode
 * @ctx: context to operate on
 * @set_id: set to activate first
 * @cpu: CPU to monitor
 *
 * The cpu specified in the pfarg_load.load_pid  argument must be the current
 * CPU.
 *
 * The function must be called with the context locked and interrupts disabled.
 */
static int pfm_load_ctx_sys(struct pfm_context *ctx, u16 set_id, u32 cpu)
{
	struct pfm_event_set *set;
	int mycpu;
	int ret;

	mycpu = smp_processor_id();

	/*
	 * system-wide: check we are running on the desired CPU
	 */
	if (cpu != mycpu) {
		PFM_DBG("wrong CPU: asking %u but on %u", cpu, mycpu);
		return -EINVAL;
	}

	/*
	 * initialize sets
	 */
	set = pfm_prepare_sets(ctx, set_id);
	if (!set) {
		PFM_DBG("event set%u does not exist", set_id);
		return -EINVAL;
	}

	PFM_DBG("set=%u set_flags=0x%x", set->id, set->flags);

	ctx->cpu = mycpu;
	ctx->task = NULL;
	ctx->active_set = set;

	/*
	 * perform any architecture specific actions
	 */
	ret = pfm_arch_load_context(ctx);
	if (ret)
		goto error_noload;

	ret = pfm_smpl_buf_load_context(ctx);
	if (ret)
		goto error;

	/*
	 * now reserve the session, before we can proceed with
	 * actually accessing the PMU hardware
	 */
	ret = pfm_session_acquire(1, mycpu);
	if (ret)
		goto error_smpl;


	/*
	 * caller must be on monitored CPU to access PMU, thus this is
	 * a form of self-monitoring
	 */
	ctx->flags.is_self = 1;

	set->runs++;

	/*
	 * load PMD from set
	 * load PMC from set
	 */
	pfm_arch_restore_pmds(ctx, set);
	pfm_arch_restore_pmcs(ctx, set);

	/*
	 * set new ownership
	 */
	pfm_set_pmu_owner(NULL, ctx);

	/*
	 * reset pending work
	 */
	ctx->flags.work_type = PFM_WORK_NONE;
	ctx->flags.reset_count = 0;

	/*
	 * reset message queue
	 */
	ctx->msgq_head = ctx->msgq_tail = 0;

	ctx->state = PFM_CTX_LOADED;

	return 0;
error_smpl:
	pfm_smpl_buf_unload_context(ctx);
error:
	pfm_arch_unload_context(ctx);
error_noload:
	return ret;
}

/**
 * __pfm_load_context_thread - attach context to a thread
 * @ctx: context to operate on
 * @set_id: first set
 * @task: threadf to attach to
 *
 * The function must be called with the context locked and interrupts disabled.
 */
static int pfm_load_ctx_thread(struct pfm_context *ctx, u16 set_id,
			       struct task_struct *task)
{
	struct pfm_event_set *set;
	struct pfm_context *old;
	int ret;

	PFM_DBG("load_pid=%d set=%u", task->pid, set_id);
	/*
	 * per-thread:
	 *   - task to attach to is checked in sys_pfm_load_context() to avoid
	 *     locking issues. if found, and not self,  task refcount was
	 *     incremented.
	 */
	old = cmpxchg(&task->pfm_context, NULL, ctx);
	if (old) {
		PFM_DBG("load_pid=%d has a context "
			"old=%p new=%p cur=%p",
			task->pid,
			old,
			ctx,
			task->pfm_context);
		return -EEXIST;
	}

	/*
	 * initialize sets
	 */
	set = pfm_prepare_sets(ctx, set_id);
	if (!set) {
		PFM_DBG("event set%u does not exist", set_id);
		return -EINVAL;
	}


	ctx->task = task;
	ctx->cpu = -1;
	ctx->active_set = set;

	/*
	 * perform any architecture specific actions
	 */
	ret = pfm_arch_load_context(ctx);
	if (ret)
		goto error_noload;

	ret = pfm_smpl_buf_load_context(ctx);
	if (ret)
		goto error;
	/*
	 * now reserve the session, before we can proceed with
	 * actually accessing the PMU hardware
	 */
	ret = pfm_session_acquire(0, -1);
	if (ret)
		goto error_smpl;


	set->runs++;
	if (ctx->task != current) {

		ctx->flags.is_self = 0;

		/* force a full reload */
		ctx->last_act = PFM_INVALID_ACTIVATION;
		ctx->last_cpu = -1;
		set->priv_flags |= PFM_SETFL_PRIV_MOD_BOTH;

	} else {
		pfm_check_save_prev_ctx();

		ctx->last_cpu = smp_processor_id();
		__get_cpu_var(pmu_activation_number)++;
		ctx->last_act = __get_cpu_var(pmu_activation_number);

		ctx->flags.is_self = 1;

		/*
		 * load PMD from set
		 * load PMC from set
		 */
		pfm_arch_restore_pmds(ctx, set);
		pfm_arch_restore_pmcs(ctx, set);

		/*
		 * set new ownership
		 */
		pfm_set_pmu_owner(ctx->task, ctx);
	}
	set_tsk_thread_flag(task, TIF_PERFMON_CTXSW);

	/*
	 * reset pending work
	 */
	ctx->flags.work_type = PFM_WORK_NONE;
	ctx->flags.reset_count = 0;

	/*
	 * reset message queue
	 */
	ctx->msgq_head = ctx->msgq_tail = 0;

	ctx->state = PFM_CTX_LOADED;

	return 0;

error_smpl:
	pfm_smpl_buf_unload_context(ctx);
error:
	pfm_arch_unload_context(ctx);
	ctx->task = NULL;
error_noload:
	/*
	 * detach context
	 */
	task->pfm_context = NULL;
	return ret;
}

/**
 * __pfm_load_context - attach context to a CPU or thread
 * @ctx: context to operate on
 * @load: pfarg_load as passed by user
 * @task: thread to attach to, NULL for system-wide
 */
int __pfm_load_context(struct pfm_context *ctx, struct pfarg_load *load,
		       struct task_struct *task)
{
	if (ctx->flags.system)
		return pfm_load_ctx_sys(ctx, load->load_set, load->load_pid);
	return pfm_load_ctx_thread(ctx, load->load_set, task);
}

/**
 * pfm_update_ovfl_pmds - account for pending ovfls on PMDs
 * @ctx: context to operate on
 *
 * This function is always called after pfm_stop has been issued
 */
static void pfm_update_ovfl_pmds(struct pfm_context *ctx)
{
	struct pfm_event_set *set;
	u64 *cnt_pmds;
	u64 ovfl_mask;
	u16 num_ovfls, i, first;

	ovfl_mask = pfm_pmu_conf->ovfl_mask;
	first = ctx->regs.first_intr_pmd;
	cnt_pmds = ctx->regs.cnt_pmds;

	/*
	 * look for pending interrupts and adjust PMD values accordingly
	 */
	list_for_each_entry(set, &ctx->set_list, list) {

		if (!set->npend_ovfls)
			continue;

		num_ovfls = set->npend_ovfls;
		PFM_DBG("set%u nintrs=%u", set->id, num_ovfls);

		for (i = first; num_ovfls; i++) {
			if (test_bit(i, cast_ulp(set->povfl_pmds))) {
				/* only correct value for counters */
				if (test_bit(i, cast_ulp(cnt_pmds)))
					set->pmds[i].value += 1 + ovfl_mask;
				num_ovfls--;
			}
			PFM_DBG("pmd%u set=%u val=0x%llx",
				i,
				set->id,
				(unsigned long long)set->pmds[i].value);
		}
		/*
		 * we need to clear to prevent a pfm_getinfo_evtsets() from
		 * returning stale data even after the context is unloaded
		 */
		set->npend_ovfls = 0;
		bitmap_zero(cast_ulp(set->povfl_pmds), ctx->regs.max_intr_pmd);
	}
}


/**
 * __pfm_unload_context - detach context from CPU or thread
 * @ctx: context to operate on
 * @release_info: pointer to return info (see below)
 *
 * The function must be called with the context locked and interrupts disabled.
 *
 * release_info value upon return:
 * 	- bit 0: when set, must free context
 * 	- bit 1: when set, must cancel hrtimer
 */
int __pfm_unload_context(struct pfm_context *ctx, int *release_info)
{
	struct task_struct *task;
	int ret;

	PFM_DBG("ctx_state=%d task [%d]",
		ctx->state,
		ctx->task ? ctx->task->pid : -1);

	*release_info = 0;

	/*
	 * unload only when necessary
	 */
	if (ctx->state == PFM_CTX_UNLOADED)
		return 0;

	task = ctx->task;

	/*
	 * stop monitoring
	 */
	ret = __pfm_stop(ctx, release_info);
	if (ret)
		return ret;

	ctx->state = PFM_CTX_UNLOADED;
	ctx->flags.can_restart = 0;

	/*
	 * save active set
	 * UP:
	 * 	if not current task and due to lazy, state may
	 * 	still be live
	 * for system-wide, guaranteed to run on correct CPU
	 */
	if (__get_cpu_var(pmu_ctx) == ctx) {
		/*
		 * pending overflows have been saved by pfm_stop()
		 */
		pfm_save_pmds(ctx, ctx->active_set);
		pfm_set_pmu_owner(NULL, NULL);
		PFM_DBG("released ownership");
	}

	/*
	 * account for pending overflows
	 */
	pfm_update_ovfl_pmds(ctx);

	pfm_smpl_buf_unload_context(ctx);

	/*
	 * arch-specific unload operations
	 */
	pfm_arch_unload_context(ctx);

	/*
	 * per-thread: disconnect from monitored task
	 */
	if (task) {
		task->pfm_context = NULL;
		ctx->task = NULL;
		clear_tsk_thread_flag(task, TIF_PERFMON_CTXSW);
		clear_tsk_thread_flag(task, TIF_PERFMON_WORK);
		pfm_arch_disarm_handle_work(task);
	}
	/*
	 * session can be freed, must have interrupts enabled
	 * thus we release in the caller. Bit 0 signals to the
	 * caller that the session can be released.
	 */
	*release_info |= 0x1;

	return 0;
}

/**
 * __pfm_exit_thread - detach and free context on thread exit
 */
void __pfm_exit_thread(void)
{
	struct pfm_context *ctx;
	unsigned long flags;
	int free_ok = 0, release_info = 0;
	int ret;

	ctx  = current->pfm_context;

	BUG_ON(ctx->flags.system);

	spin_lock_irqsave(&ctx->lock, flags);

	PFM_DBG("state=%d is_self=%d", ctx->state, ctx->flags.is_self);

	/*
	 * __pfm_unload_context() cannot fail
	 * in the context states we are interested in
	 */
	switch (ctx->state) {
	case PFM_CTX_LOADED:
	case PFM_CTX_MASKED:
		__pfm_unload_context(ctx, &release_info);
		/*
		 * end notification only sent for non
		 * self-monitoring context
		 */
		if (!ctx->flags.is_self)
			pfm_end_notify(ctx);
		break;
	case PFM_CTX_ZOMBIE:
		__pfm_unload_context(ctx, &release_info);
		free_ok = 1;
		break;
	default:
		BUG_ON(ctx->state != PFM_CTX_LOADED);
		break;
	}
	spin_unlock_irqrestore(&ctx->lock, flags);

	/*
	 * cancel timer now that context is unlocked
	 */
	if (release_info & 0x2) {
		ret = hrtimer_cancel(&__get_cpu_var(pfm_hrtimer));
		PFM_DBG("timeout cancel=%d", ret);
	}

	if (release_info & 0x1)
		pfm_session_release(0, 0);

	/*
	 * All memory free operations (especially for vmalloc'ed memory)
	 * MUST be done with interrupts ENABLED.
	 */
	if (free_ok)
		pfm_free_context(ctx);
}
