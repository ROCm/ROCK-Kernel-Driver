/*
 * perfmon_smpl.c: perfmon2 sampling management
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/uaccess.h>
#include <linux/perfmon_kern.h>

#include "perfmon_priv.h"

/**
 * pfm_smpl_buf_alloc - allocate memory for sampling buffer
 * @ctx: context to operate on
 * @rsize: requested size
 *
 * called from pfm_smpl_buffer_alloc_old() (IA64-COMPAT)
 * and pfm_setup_smpl_fmt()
 *
 * interrupts are enabled, context is not locked.
 *
 * function is not static because it is called from the IA-64
 * compatibility module (perfmon_compat.c)
 */
int pfm_smpl_buf_alloc(struct pfm_context *ctx, size_t rsize)
{
#if PFM_ARCH_SMPL_ALIGN_SIZE > 0
#define PFM_ALIGN_SMPL(a, f) (void *)((((unsigned long)(a))+(f-1)) & ~(f-1))
#else
#define PFM_ALIGN_SMPL(a, f) (a)
#endif
	void *addr, *real_addr;
	size_t size, real_size;
	int ret;

	might_sleep();

	/*
	 * align page boundary
	 */
	size = PAGE_ALIGN(rsize);

	/*
	 * On some arch, it may be necessary to get an alignment greater
	 * than page size to avoid certain cache effects (e.g., MIPS).
	 * This is the reason for PFM_ARCH_SMPL_ALIGN_SIZE.
	 */
	real_size = size + PFM_ARCH_SMPL_ALIGN_SIZE;

	PFM_DBG("req_size=%zu size=%zu real_size=%zu",
		rsize,
		size,
		real_size);

	ret = pfm_smpl_buf_space_acquire(ctx, real_size);
	if (ret)
		return ret;

	/*
	 * vmalloc can sleep. we do not hold
	 * any spinlock and interrupts are enabled
	 */
	real_addr = addr = vmalloc(real_size);
	if (!real_addr) {
		PFM_DBG("cannot allocate sampling buffer");
		goto unres;
	}

	/*
	 * align the useable sampling buffer address to the arch requirement
	 * This is a nop on most architectures
	 */
	addr = PFM_ALIGN_SMPL(real_addr, PFM_ARCH_SMPL_ALIGN_SIZE);

	memset(addr, 0, real_size);

	/*
	 * due to cache aliasing, it may be necessary to flush the pages
	 * on certain architectures (e.g., MIPS)
	 */
	pfm_cacheflush(addr, real_size);

	/*
	 * what needs to be freed
	 */
	ctx->smpl_real_addr = real_addr;
	ctx->smpl_real_size = real_size;

	/*
	 * what is actually available to user
	 */
	ctx->smpl_addr = addr;
	ctx->smpl_size = size;

	PFM_DBG("addr=%p real_addr=%p", addr, real_addr);

	return 0;
unres:
	/*
	 * smpl_addr is NULL, no double freeing possible in pfm_context_free()
	 */
	pfm_smpl_buf_space_release(ctx, real_size);

	return -ENOMEM;
}

/**
 * pfm_smpl_buf_free - free resources associated with sampling
 * @ctx: context to operate on
 */
void pfm_smpl_buf_free(struct pfm_context *ctx)
{
	struct pfm_smpl_fmt *fmt;

	fmt = ctx->smpl_fmt;

	/*
	 * some formats may not use a buffer, yet they may
	 * need to be called on exit
	 */
	if (fmt) {
		if (fmt->fmt_exit)
			(*fmt->fmt_exit)(ctx->smpl_addr);
		/*
		 * decrease refcount of sampling format
		 */
		pfm_smpl_fmt_put(fmt);
	}

	if (ctx->smpl_addr) {
		pfm_smpl_buf_space_release(ctx, ctx->smpl_real_size);

		PFM_DBG("free buffer real_addr=0x%p real_size=%zu",
			ctx->smpl_real_addr,
			ctx->smpl_real_size);

		vfree(ctx->smpl_real_addr);
	}
}

/**
 * pfm_setup_smpl_fmt - initialization of sampling format and buffer
 * @ctx: context to operate on
 * @fmt_arg: smapling format arguments
 * @ctx_flags: context flags as passed by user
 * @filp: file descriptor associated with context
 *
 * called from __pfm_create_context()
 */
int pfm_setup_smpl_fmt(struct pfm_context *ctx, u32 ctx_flags, void *fmt_arg,
		       struct file *filp)
{
	struct pfm_smpl_fmt *fmt;
	size_t size = 0;
	int ret = 0;

	fmt = ctx->smpl_fmt;

	/*
	 * validate parameters
	 */
	if (fmt->fmt_validate) {
		ret = (*fmt->fmt_validate)(ctx_flags,
					   ctx->regs.num_pmds,
					   fmt_arg);
		PFM_DBG("validate(0x%x,%p)=%d", ctx_flags, fmt_arg, ret);
		if (ret)
			goto error;
	}

	/*
	 * check if buffer format needs buffer allocation
	 */
	size = 0;
	if (fmt->fmt_getsize) {
		ret = (*fmt->fmt_getsize)(ctx_flags, fmt_arg, &size);
		if (ret) {
			PFM_DBG("cannot get size ret=%d", ret);
			goto error;
		}
	}

	/*
	 * allocate buffer
	 * v20_compat is for IA-64 backward compatibility with perfmon v2.0
	 */
	if (size) {
#ifdef CONFIG_IA64_PERFMON_COMPAT
		/*
		 * backward compatibility with perfmon v2.0 on Ia-64
		 */
		if (ctx->flags.ia64_v20_compat)
			ret = pfm_smpl_buf_alloc_compat(ctx, size, filp);
		else
#endif
			ret = pfm_smpl_buf_alloc(ctx, size);

		if (ret)
			goto error;

	}

	if (fmt->fmt_init) {
		ret = (*fmt->fmt_init)(ctx, ctx->smpl_addr, ctx_flags,
				       ctx->regs.num_pmds,
				       fmt_arg);
	}
	/*
	 * if there was an error, the buffer/resource will be freed by
	 * via pfm_context_free()
	 */
error:
	return ret;
}

void pfm_mask_monitoring(struct pfm_context *ctx, struct pfm_event_set *set)
{
	u64 now;

	now = sched_clock();

	/*
	 * we save the PMD values such that we can read them while
	 * MASKED without having the thread stopped
	 * because monitoring is stopped
	 *
	 * pfm_save_pmds() could be avoided if we knew
	 * that pfm_arch_intr_freeze() had saved them already
	 */
	pfm_save_pmds(ctx, set);
	pfm_arch_mask_monitoring(ctx, set);
	/*
	 * accumulate the set duration up to this point
	 */
	set->duration += now - set->duration_start;

	ctx->state = PFM_CTX_MASKED;

	/*
	 * need to stop timer and remember remaining time
	 * will be reloaded in pfm_unmask_monitoring
	 * hrtimer is cancelled in the tail of the interrupt
	 * handler once the context is unlocked
	 */
	if (set->flags & PFM_SETFL_TIME_SWITCH) {
		struct hrtimer *h = &__get_cpu_var(pfm_hrtimer);
		hrtimer_cancel(h);
		set->hrtimer_rem = hrtimer_get_remaining(h);
	}
	PFM_DBG_ovfl("can_restart=%u", ctx->flags.can_restart);
}

/**
 * pfm_unmask_monitoring - unmask monitoring
 * @ctx: context to work with
 * @set: current active set
 *
 * interrupts are masked when entering this function.
 * context must be in MASKED state when calling.
 *
 * Upon return, the active set may have changed when using timeout
 * based switching.
 */
static void pfm_unmask_monitoring(struct pfm_context *ctx, struct pfm_event_set *set)
{
	if (ctx->state != PFM_CTX_MASKED)
		return;

	PFM_DBG_ovfl("unmasking monitoring");

	/*
	 * must be done before calling
	 * pfm_arch_unmask_monitoring()
	 */
	ctx->state = PFM_CTX_LOADED;

	/*
	 * we need to restore the PMDs because they
	 * may have been modified by user while MASKED in
	 * which case the actual registers have no yet
	 * been updated
	 */
	pfm_arch_restore_pmds(ctx, set);

	/*
	 * call arch specific handler
	 */
	pfm_arch_unmask_monitoring(ctx, set);

	/*
	 * clear force reload flag. May have been set
	 * in pfm_write_pmcs or pfm_write_pmds
	 */
	set->priv_flags &= ~PFM_SETFL_PRIV_MOD_BOTH;

	/*
	 * reset set duration timer
	 */
	set->duration_start = sched_clock();

	/*
	 * restart hrtimer if needed
	 */
	if (set->flags & PFM_SETFL_TIME_SWITCH) {
		pfm_restart_timer(ctx, set);
		/* careful here as pfm_restart_timer may switch sets */
	}
}

void pfm_reset_pmds(struct pfm_context *ctx,
		    struct pfm_event_set *set,
		    int num_pmds,
		    int reset_mode)
{
	u64 val, mask, new_seed;
	struct pfm_pmd *reg;
	unsigned int i, not_masked;

	not_masked = ctx->state != PFM_CTX_MASKED;

	PFM_DBG_ovfl("%s r_pmds=0x%llx not_masked=%d",
		reset_mode == PFM_PMD_RESET_LONG ? "long" : "short",
		(unsigned long long)set->reset_pmds[0],
		not_masked);

	pfm_stats_inc(reset_pmds_count);

	for (i = 0; num_pmds; i++) {
		if (test_bit(i, cast_ulp(set->reset_pmds))) {
			num_pmds--;

			reg = set->pmds + i;

			val = reset_mode == PFM_PMD_RESET_LONG ?
			       reg->long_reset : reg->short_reset;

			if (reg->flags & PFM_REGFL_RANDOM) {
				mask = reg->mask;
				new_seed = random32();

				/* construct a full 64-bit random value: */
				if ((unlikely(mask >> 32) != 0))
					new_seed |= (u64)random32() << 32;

				/* counter values are negative numbers! */
				val -= (new_seed & mask);
			}

			set->pmds[i].value = val;
			reg->lval = val;

			/*
			 * not all PMD to reset are necessarily
			 * counters
			 */
			if (not_masked)
				pfm_write_pmd(ctx, i, val);

			PFM_DBG_ovfl("set%u pmd%u sval=0x%llx",
					set->id,
					i,
					(unsigned long long)val);
		}
	}

	/*
	 * done with reset
	 */
	bitmap_zero(cast_ulp(set->reset_pmds), i);

	/*
	 * make changes visible
	 */
	if (not_masked)
		pfm_arch_serialize();
}

/*
 * called from pfm_handle_work() and __pfm_restart()
 * for system-wide and per-thread context to resume
 * monitoring after a user level notification.
 *
 * In both cases, the context is locked and interrupts
 * are disabled.
 */
void pfm_resume_after_ovfl(struct pfm_context *ctx)
{
	struct pfm_smpl_fmt *fmt;
	u32 rst_ctrl;
	struct pfm_event_set *set;
	u64 *reset_pmds;
	void *hdr;
	int state, ret;

	hdr = ctx->smpl_addr;
	fmt = ctx->smpl_fmt;
	state = ctx->state;
	set = ctx->active_set;
	ret = 0;

	if (hdr) {
		rst_ctrl = 0;
		prefetch(hdr);
	} else {
		rst_ctrl = PFM_OVFL_CTRL_RESET;
	}

	/*
	 * if using a sampling buffer format and it has a restart callback,
	 * then invoke it. hdr may be NULL, if the format does not use a
	 * perfmon buffer
	 */
	if (fmt && fmt->fmt_restart)
		ret = (*fmt->fmt_restart)(state == PFM_CTX_LOADED, &rst_ctrl,
					  hdr);

	reset_pmds = set->reset_pmds;

	PFM_DBG("fmt_restart=%d reset_count=%d set=%u r_pmds=0x%llx switch=%d "
		"ctx_state=%d",
		ret,
		ctx->flags.reset_count,
		set->id,
		(unsigned long long)reset_pmds[0],
		(set->priv_flags & PFM_SETFL_PRIV_SWITCH),
		state);

	if (!ret) {
		/*
		 * switch set if needed
		 */
		if (set->priv_flags & PFM_SETFL_PRIV_SWITCH) {
			set->priv_flags &= ~PFM_SETFL_PRIV_SWITCH;
			pfm_switch_sets(ctx, NULL, PFM_PMD_RESET_LONG, 0);
			set = ctx->active_set;
		} else if (rst_ctrl & PFM_OVFL_CTRL_RESET) {
			int nn;
			nn = bitmap_weight(cast_ulp(set->reset_pmds),
					   ctx->regs.max_pmd);
			if (nn)
				pfm_reset_pmds(ctx, set, nn, PFM_PMD_RESET_LONG);
		}

		if (!(rst_ctrl & PFM_OVFL_CTRL_MASK))
			pfm_unmask_monitoring(ctx, set);
		else
			PFM_DBG("stopping monitoring?");
		ctx->state = PFM_CTX_LOADED;
	}
}

/*
 * This function is called when we need to perform asynchronous
 * work on a context. This function is called ONLY when about to
 * return to user mode (very much like with signal handling).
 *
 * There are several reasons why we come here:
 *
 *  - per-thread mode, not self-monitoring, to reset the counters
 *    after a pfm_restart()
 *
 *  - we are zombie and we need to cleanup our state
 *
 *  - we need to block after an overflow notification
 *    on a context with the PFM_OVFL_NOTIFY_BLOCK flag
 *
 * This function is never called for a system-wide context.
 *
 * pfm_handle_work() can be called with interrupts enabled
 * (TIF_NEED_RESCHED) or disabled. The down_interruptible
 * call may sleep, therefore we must re-enable interrupts
 * to avoid deadlocks. It is safe to do so because this function
 * is called ONLY when returning to user level, in which case
 * there is no risk of kernel stack overflow due to deep
 * interrupt nesting.
 */
void pfm_handle_work(struct pt_regs *regs)
{
	struct pfm_context *ctx;
	unsigned long flags, dummy_flags;
	int type, ret, info;

#ifdef CONFIG_PPC
	/*
	 * This is just a temporary fix. Obviously we'd like to fix the powerpc
	 * code to make that check before calling __pfm_handle_work() to
	 * prevent the function call overhead, but the call is made from
	 * assembly code, so it will take a little while to figure out how to
	 * perform the check correctly.
	 */
	if (!test_thread_flag(TIF_PERFMON_WORK))
		return;
#endif

	if (!user_mode(regs))
		return;

	clear_thread_flag(TIF_PERFMON_WORK);

	pfm_stats_inc(handle_work_count);

	ctx = current->pfm_context;
	if (ctx == NULL) {
		PFM_DBG("[%d] has no ctx", current->pid);
		return;
	}

	BUG_ON(ctx->flags.system);

	spin_lock_irqsave(&ctx->lock, flags);

	type = ctx->flags.work_type;
	ctx->flags.work_type = PFM_WORK_NONE;

	PFM_DBG("work_type=%d reset_count=%d",
		type,
		ctx->flags.reset_count);

	switch (type) {
	case PFM_WORK_ZOMBIE:
		goto do_zombie;
	case PFM_WORK_RESET:
		/* simply reset, no blocking */
		goto skip_blocking;
	case PFM_WORK_NONE:
		PFM_DBG("unexpected PFM_WORK_NONE");
		goto nothing_todo;
	case PFM_WORK_BLOCK:
		break;
	default:
		PFM_DBG("unkown type=%d", type);
		goto nothing_todo;
	}

	/*
	 * restore interrupt mask to what it was on entry.
	 * Could be enabled/disabled.
	 */
	spin_unlock_irqrestore(&ctx->lock, flags);

	/*
	 * force interrupt enable because of down_interruptible()
	 */
	local_irq_enable();

	PFM_DBG("before block sleeping");

	/*
	 * may go through without blocking on SMP systems
	 * if restart has been received already by the time we call down()
	 */
	ret = wait_for_completion_interruptible(&ctx->restart_complete);

	PFM_DBG("after block sleeping ret=%d", ret);

	/*
	 * lock context and mask interrupts again
	 * We save flags into a dummy because we may have
	 * altered interrupts mask compared to entry in this
	 * function.
	 */
	spin_lock_irqsave(&ctx->lock, dummy_flags);

	if (ctx->state == PFM_CTX_ZOMBIE)
		goto do_zombie;

	/*
	 * in case of interruption of down() we don't restart anything
	 */
	if (ret < 0)
		goto nothing_todo;

skip_blocking:
	/*
	 * iterate over the number of pending resets
	 * There are certain situations where there may be
	 * multiple notifications sent before a pfm_restart().
	 * As such, it may be that multiple pfm_restart() are
	 * issued before the monitored thread gets to
	 * pfm_handle_work(). To avoid losing restarts, pfm_restart()
	 * increments a counter (reset_counts). Here, we take this
	 * into account by potentially calling pfm_resume_after_ovfl()
	 * multiple times. It is up to the sampling format to take the
	 * appropriate actions.
	 */
	while (ctx->flags.reset_count) {
		pfm_resume_after_ovfl(ctx);
		/* careful as active set may have changed */
		ctx->flags.reset_count--;
	}

nothing_todo:
	/*
	 * restore flags as they were upon entry
	 */
	spin_unlock_irqrestore(&ctx->lock, flags);
	return;

do_zombie:
	PFM_DBG("context is zombie, bailing out");

	__pfm_unload_context(ctx, &info);

	/*
	 * keep the spinlock check happy
	 */
	spin_unlock(&ctx->lock);

	/*
	 * enable interrupt for vfree()
	 */
	local_irq_enable();

	/*
	 * cancel timer now that context is unlocked
	 */
	if (info & 0x2) {
		ret = hrtimer_cancel(&__get_cpu_var(pfm_hrtimer));
		PFM_DBG("timeout cancel=%d", ret);
	}

	/*
	 * actual context free
	 */
	pfm_free_context(ctx);

	/*
	 * restore interrupts as they were upon entry
	 */
	local_irq_restore(flags);

	/* always true */
	if (info & 0x1)
		pfm_session_release(0, 0);
}

/**
 * __pfm_restart - resume monitoring after user-level notification
 * @ctx: context to operate on
 * @info: return information used to free resource once unlocked
 *
 * function called from sys_pfm_restart(). It is used when overflow
 * notification is requested. For each notification received, the user
 * must call pfm_restart() to indicate to the kernel that it is done
 * processing the notification.
 *
 * When the caller is doing user level sampling, this function resets
 * the overflowed counters and resumes monitoring which is normally stopped
 * during notification (always the consequence of a counter overflow).
 *
 * When using a sampling format, the format restart() callback is invoked,
 * overflowed PMDS may be reset based upon decision from sampling format.
 *
 * When operating in per-thread mode, and when not self-monitoring, the
 * monitored thread DOES NOT need to be stopped, unlike for many other calls.
 *
 * This means that the effect of the restart may not necessarily be observed
 * right when returning from the call. For instance, counters may not already
 * be reset in the other thread.
 *
 * When operating in system-wide, the caller must be running on the monitored
 * CPU.
 *
 * The context is locked and interrupts are disabled.
 *
 * info value upon return:
 * 	- bit 0: when set, mudt issue complete() on restart semaphore
 */
int __pfm_restart(struct pfm_context *ctx, int *info)
{
	int state;

	state = ctx->state;

	PFM_DBG("state=%d can_restart=%d reset_count=%d",
		state,
		ctx->flags.can_restart,
		ctx->flags.reset_count);

	*info = 0;

	switch (state) {
	case PFM_CTX_MASKED:
		break;
	case PFM_CTX_LOADED:
		if (ctx->smpl_addr && ctx->smpl_fmt->fmt_restart)
			break;
	default:
		PFM_DBG("invalid state=%d", state);
		return -EBUSY;
	}

	/*
	 * first check if allowed to restart, i.e., notifications received
	 */
	if (!ctx->flags.can_restart) {
		PFM_DBG("no restart can_restart=0");
		return -EBUSY;
	}

	pfm_stats_inc(pfm_restart_count);

	/*
	 * at this point, the context is either LOADED or MASKED
	 */
	ctx->flags.can_restart--;

	/*
	 * handle self-monitoring case and system-wide
	 */
	if (ctx->task == current || ctx->flags.system) {
		pfm_resume_after_ovfl(ctx);
		return 0;
	}

	/*
	 * restart another task
	 */

	/*
	 * if blocking, then post the semaphore if PFM_CTX_MASKED, i.e.
	 * the task is blocked or on its way to block. That's the normal
	 * restart path. If the monitoring is not masked, then the task
	 * can be actively monitoring and we cannot directly intervene.
	 * Therefore we use the trap mechanism to catch the task and
	 * force it to reset the buffer/reset PMDs.
	 *
	 * if non-blocking, then we ensure that the task will go into
	 * pfm_handle_work() before returning to user mode.
	 *
	 * We cannot explicitly reset another task, it MUST always
	 * be done by the task itself. This works for system wide because
	 * the tool that is controlling the session is logically doing
	 * "self-monitoring".
	 */
	if (ctx->flags.block && state == PFM_CTX_MASKED) {
		PFM_DBG("unblocking [%d]", ctx->task->pid);
		/*
		 * It is not possible to call complete() with the context locked
		 * otherwise we have a potential deadlock with the PMU context
		 * switch code due to a lock inversion between task_rq_lock()
		 * and the context lock.
		 * Instead we mark whether or not we need to issue the complete
		 * and we invoke the function once the context lock is released
		 * in sys_pfm_restart()
		 */
		*info = 1;
	} else {
		PFM_DBG("[%d] armed exit trap", ctx->task->pid);
		pfm_post_work(ctx->task, ctx, PFM_WORK_RESET);
	}
	ctx->flags.reset_count++;
	return 0;
}

/**
 * pfm_get_smpl_arg -- copy user arguments to pfm_create_context() related to sampling format
 * @name: format name as passed by user
 * @fmt_arg: format optional argument as passed by user
 * @uszie: size of structure pass in fmt_arg
 * @arg: kernel copy of fmt_arg
 * @fmt: pointer to sampling format upon success
 *
 * arg is kmalloc'ed, thus it needs a kfree by caller
 */
int pfm_get_smpl_arg(char __user *fmt_uname, void __user *fmt_uarg, size_t usize, void **arg,
		     struct pfm_smpl_fmt **fmt)
{
	struct pfm_smpl_fmt *f;
	char *fmt_name;
	void *addr = NULL;
	size_t sz;
	int ret;

	fmt_name = getname(fmt_uname);
	if (!fmt_name) {
		PFM_DBG("getname failed");
		return -ENOMEM;
	}

	/*
	 * find fmt and increase refcount
	 */
	f = pfm_smpl_fmt_get(fmt_name);

	putname(fmt_name);

	if (f == NULL) {
		PFM_DBG("buffer format not found");
		return -EINVAL;
	}

	/*
	 * expected format argument size
	 */
	sz = f->fmt_arg_size;

	/*
	 * check user size matches expected size
	 * usize = -1 is for IA-64 backward compatibility
	 */
	ret = -EINVAL;
	if (sz != usize && usize != -1) {
		PFM_DBG("invalid arg size %zu, format expects %zu",
			usize, sz);
		goto error;
	}

	if (sz) {
		ret = -ENOMEM;
		addr = kmalloc(sz, GFP_KERNEL);
		if (addr == NULL)
			goto error;

		ret = -EFAULT;
		if (copy_from_user(addr, fmt_uarg, sz))
			goto error;
	}
	*arg = addr;
	*fmt = f;
	return 0;

error:
	kfree(addr);
	pfm_smpl_fmt_put(f);
	return ret;
}
