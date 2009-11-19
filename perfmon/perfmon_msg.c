/*
 * perfmon_msg.c: perfmon2 notification message queue management
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
#include <linux/poll.h>
#include <linux/perfmon_kern.h>

/**
 * pfm_get_new_msg - get a new message slot from the queue
 * @ctx: context to operate on
 *
 * if queue if full NULL is returned
 */
static union pfarg_msg *pfm_get_new_msg(struct pfm_context *ctx)
{
	int next;

	next = ctx->msgq_head & PFM_MSGQ_MASK;

	if ((ctx->msgq_head - ctx->msgq_tail) == PFM_MSGS_COUNT)
		return NULL;

	/*
	 * move to next possible slot
	 */
	ctx->msgq_head++;

	PFM_DBG_ovfl("head=%d tail=%d msg=%d",
		ctx->msgq_head & PFM_MSGQ_MASK,
		ctx->msgq_tail & PFM_MSGQ_MASK,
		next);

	return ctx->msgq+next;
}

/**
 * pfm_notify_user - wakeup any thread wiating on msg queue,  post SIGIO
 * @ctx: context to operate on
 *
 * message is already enqueued
 */
static void pfm_notify_user(struct pfm_context *ctx)
{
	if (ctx->state == PFM_CTX_ZOMBIE) {
		PFM_DBG("no notification, context is zombie");
		return;
	}

	PFM_DBG_ovfl("waking up");

	wake_up_interruptible(&ctx->msgq_wait);

	/*
	 * it is safe to call kill_fasync() from an interrupt
	 * handler. kill_fasync()  grabs two RW locks (fasync_lock,
	 * tasklist_lock) in read mode. There is conflict only in
	 * case the PMU interrupt occurs during a write mode critical
	 * section. This cannot happen because for both locks, the
	 * write mode is always using interrupt masking (write_lock_irq).
	 */
	kill_fasync(&ctx->async_queue, SIGIO, POLL_IN);
}

/**
 * pfm_ovfl_notify - send overflow notification
 * @ctx: context to operate on
 * @set: which set the overflow comes from
 * @ip: overflow interrupt instruction address (IIP)
 *
 * Appends an overflow notification message to context queue.
 * call pfm_notify() to wakeup any threads and/or send a signal
 *
 * Context is locked and interrupts are disabled (no preemption).
 */
int pfm_ovfl_notify(struct pfm_context *ctx,
			struct pfm_event_set *set,
			unsigned long ip)
{
	union pfarg_msg *msg = NULL;
	u64 *ovfl_pmds;

	if (!ctx->flags.no_msg) {
		msg = pfm_get_new_msg(ctx);
		if (msg == NULL) {
			/*
			 * when message queue fills up it is because the user
			 * did not extract the message, yet issued
			 * pfm_restart(). At this point, we stop sending
			 * notification, thus the user will not be able to get
			 * new samples when using the default format.
			 */
			PFM_DBG_ovfl("no more notification msgs");
			return -1;
		}

		msg->pfm_ovfl_msg.msg_type = PFM_MSG_OVFL;
		msg->pfm_ovfl_msg.msg_ovfl_pid = current->tgid;
		msg->pfm_ovfl_msg.msg_active_set = set->id;

		ovfl_pmds = msg->pfm_ovfl_msg.msg_ovfl_pmds;

		/*
		 * copy bitmask of all pmd that interrupted last
		 */
		bitmap_copy(cast_ulp(ovfl_pmds), cast_ulp(set->ovfl_pmds),
			    ctx->regs.max_intr_pmd);

		msg->pfm_ovfl_msg.msg_ovfl_cpu = smp_processor_id();
		msg->pfm_ovfl_msg.msg_ovfl_tid = current->pid;
		msg->pfm_ovfl_msg.msg_ovfl_ip = ip;

		pfm_stats_inc(ovfl_notify_count);
	}

	PFM_DBG_ovfl("ip=0x%lx o_pmds=0x%llx",
		     ip,
		     (unsigned long long)set->ovfl_pmds[0]);

	pfm_notify_user(ctx);
	return 0;
}

/**
 * pfm_end_notify_user - notify of thread termination
 * @ctx: context to operate on
 *
 * In per-thread mode, when not self-monitoring, perfmon
 * sends a 'end' notification message when the monitored
 * thread where the context is attached is exiting.
 *
 * This helper message alleviates the need to track the activity
 * of the thread/process when it is not directly related, i.e.,
 * was attached. In other words, no needto keep the thread
 * ptraced.
 *
 * The context must be locked and interrupts disabled.
 */
int pfm_end_notify(struct pfm_context *ctx)
{
	union pfarg_msg *msg;

	msg = pfm_get_new_msg(ctx);
	if (msg == NULL) {
		PFM_ERR("%s no more msgs", __func__);
		return -1;
	}
	/* no leak */
	memset(msg, 0, sizeof(*msg));

	msg->type = PFM_MSG_END;

	PFM_DBG("end msg: msg=%p no_msg=%d",
		msg,
		ctx->flags.no_msg);

	pfm_notify_user(ctx);
	return 0;
}

/**
 * pfm_get_next_msg - copy the oldest message from the queue and move tail
 * @ctx: context to use
 * @m: where to copy the message into
 *
 * The tail of the queue is moved as a consequence of this call
 */
void pfm_get_next_msg(struct pfm_context *ctx, union pfarg_msg *m)
{
	union pfarg_msg *next;

	PFM_DBG_ovfl("in head=%d tail=%d",
		ctx->msgq_head & PFM_MSGQ_MASK,
		ctx->msgq_tail & PFM_MSGQ_MASK);

	/*
	 * get oldest message
	 */
	next = ctx->msgq + (ctx->msgq_tail & PFM_MSGQ_MASK);

	/*
	 * move tail forward
	 */
	ctx->msgq_tail++;

	/*
	 * copy message, we cannot simply point to it
	 * as it may be re-used before we copy it out
	 */
	*m = *next;

	PFM_DBG_ovfl("out head=%d tail=%d type=%d",
		ctx->msgq_head & PFM_MSGQ_MASK,
		ctx->msgq_tail & PFM_MSGQ_MASK,
		m->type);
}
