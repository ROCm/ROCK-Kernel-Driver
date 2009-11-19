/*
 * perfmon_sets.c: perfmon2 event sets and multiplexing functions
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

static struct kmem_cache	*pfm_set_cachep;

/**
 * pfm_reload_switch_thresholds - reload overflow-based switch thresholds per set
 * @set: the set for which to reload thresholds
 *
 */
static void pfm_reload_switch_thresholds(struct pfm_context *ctx,
					 struct pfm_event_set *set)
{
	u64 *used_pmds;
	u16 i, max, first;

	used_pmds = set->used_pmds;
	first = ctx->regs.first_intr_pmd;
	max = ctx->regs.max_intr_pmd;

	for (i = first; i < max; i++) {
		if (test_bit(i, cast_ulp(used_pmds))) {
			set->pmds[i].ovflsw_thres = set->pmds[i].ovflsw_ref_thres;

			PFM_DBG("set%u pmd%u ovflsw_thres=%llu",
				set->id,
				i,
				(unsigned long long)set->pmds[i].ovflsw_thres);
		}
	}
}

/**
 * pfm_prepare_sets - initialize sets on pfm_load_context
 * @ctx : context to operate on
 * @load_set: set to activate first
 *
 * connect all sets, reset internal fields
 */
struct pfm_event_set *pfm_prepare_sets(struct pfm_context *ctx, u16 load_set)
{
	struct pfm_event_set *set, *p;
	u16 max;

	/*
	 * locate first set to activate
	 */
	set = pfm_find_set(ctx, load_set, 0);
	if (!set)
		return NULL;

	if (set->flags & PFM_SETFL_OVFL_SWITCH)
		pfm_reload_switch_thresholds(ctx, set);

	max = ctx->regs.max_intr_pmd;

	list_for_each_entry(p, &ctx->set_list, list) {
		/*
		 * cleanup bitvectors
		 */
		bitmap_zero(cast_ulp(p->ovfl_pmds), max);
		bitmap_zero(cast_ulp(p->povfl_pmds), max);

		p->npend_ovfls = 0;

		/*
		 * we cannot just use plain clear because of arch-specific flags
		 */
		p->priv_flags &= ~(PFM_SETFL_PRIV_MOD_BOTH|PFM_SETFL_PRIV_SWITCH);
		/*
		 * neither duration nor runs are reset because typically loading/unloading
		 * does not mean counts are reset. To reset, the set must be modified
		 */
	}
	return set;
}

/*
 * called by hrtimer_interrupt()
 *
 * This is the only function where we come with
 * cpu_base->lock held before ctx->lock
 *
 * interrupts are disabled
 */
enum hrtimer_restart pfm_handle_switch_timeout(struct hrtimer *t)
{
	struct pfm_event_set *set;
	struct pfm_context *ctx;
	unsigned long flags;
	enum hrtimer_restart ret = HRTIMER_NORESTART;

	/*
	 * prevent against race with unload
	 */
	ctx  = __get_cpu_var(pmu_ctx);
	if (!ctx)
		return HRTIMER_NORESTART;

	spin_lock_irqsave(&ctx->lock, flags);

	set = ctx->active_set;

	/*
	 * switching occurs only when context is attached
	 */
	if (ctx->state != PFM_CTX_LOADED)
		goto done;
	/*
	 * timer does not run while monitoring is inactive (not started)
	 */
	if (!pfm_arch_is_active(ctx))
		goto done;

	pfm_stats_inc(handle_timeout_count);

	ret  = pfm_switch_sets(ctx, NULL, PFM_PMD_RESET_SHORT, 0);
done:
	spin_unlock_irqrestore(&ctx->lock, flags);
	return ret;
}

/*
 *
 * always operating on the current task
 * interrupts are masked
 *
 * input:
 * 	- new_set: new set to switch to, if NULL follow normal chain
 */
enum hrtimer_restart pfm_switch_sets(struct pfm_context *ctx,
				     struct pfm_event_set *new_set,
				     int reset_mode,
				     int no_restart)
{
	struct pfm_event_set *set;
	u64 now, end;
	u32 new_flags;
	int is_system, is_active, nn;
	enum hrtimer_restart ret = HRTIMER_NORESTART;

	now = sched_clock();
	set = ctx->active_set;
	is_active = pfm_arch_is_active(ctx);

	/*
	 * if no set is explicitly requested,
	 * use the set_switch_next field
	 */
	if (!new_set) {
		/*
		 * we use round-robin unless the user specified
		 * a particular set to go to.
		 */
		new_set = list_first_entry(&set->list, struct pfm_event_set, list);
		if (&new_set->list == &ctx->set_list)
			new_set = list_first_entry(&ctx->set_list, struct pfm_event_set, list);
	}

	PFM_DBG_ovfl("state=%d act=%d cur_set=%u cur_runs=%llu cur_npend=%d next_set=%u "
		  "next_runs=%llu new_npend=%d reset_mode=%d reset_pmds=%llx",
		  ctx->state,
		  is_active,
		  set->id,
		  (unsigned long long)set->runs,
		  set->npend_ovfls,
		  new_set->id,
		  (unsigned long long)new_set->runs,
		  new_set->npend_ovfls,
		  reset_mode,
		  (unsigned long long)new_set->reset_pmds[0]);

	is_system = ctx->flags.system;
	new_flags = new_set->flags;

	/*
	 * nothing more to do
	 */
	if (new_set == set)
		goto skip_same_set;

	if (is_active) {
		pfm_arch_stop(current, ctx);
		pfm_save_pmds(ctx, set);
		/*
		 * compute elapsed ns for active set
		 */
		set->duration += now - set->duration_start;
	}

	pfm_arch_restore_pmds(ctx, new_set);
	/*
	 * if masked, we must restore the pmcs such that they
	 * do not capture anything.
	 */
	pfm_arch_restore_pmcs(ctx, new_set);

	if (new_set->npend_ovfls) {
		pfm_arch_resend_irq(ctx);
		pfm_stats_inc(ovfl_intr_replay_count);
	}

	new_set->priv_flags &= ~PFM_SETFL_PRIV_MOD_BOTH;

skip_same_set:
	new_set->runs++;
	/*
	 * reset switch threshold
	 */
	if (new_flags & PFM_SETFL_OVFL_SWITCH)
		pfm_reload_switch_thresholds(ctx, new_set);

	/*
	 * reset overflowed PMD registers in new set
	 */
	nn = bitmap_weight(cast_ulp(new_set->reset_pmds), ctx->regs.max_pmd);
	if (nn)
		pfm_reset_pmds(ctx, new_set, nn, reset_mode);


	/*
	 * This is needed when coming from pfm_start()
	 *
	 * When switching to the same set, there is no
	 * need to restart
	 */
	if (no_restart)
		goto skip_restart;

	if (is_active) {
		/*
		 * do not need to restart when same set
		 */
		if (new_set != set) {
			ctx->active_set = new_set;
			new_set->duration_start = now;
			pfm_arch_start(current, ctx);
		}
		/*
		 * install new timeout if necessary
		 */
		if (new_flags & PFM_SETFL_TIME_SWITCH) {
			struct hrtimer *h;
			h = &__get_cpu_var(pfm_hrtimer);
			hrtimer_forward(h, h->base->get_time(), new_set->hrtimer_exp);
			new_set->hrtimer_rem = new_set->hrtimer_exp;
			ret = HRTIMER_RESTART;
		}
	}

skip_restart:
	ctx->active_set = new_set;

	end = sched_clock();

	pfm_stats_inc(set_switch_count);
	pfm_stats_add(set_switch_ns, end - now);

	return ret;
}

/*
 * called from __pfm_overflow_handler() to switch event sets.
 * monitoring is stopped, task is current, interrupts are masked.
 * compared to pfm_switch_sets(), this version is simplified because
 * it knows about the call path. There is no need to stop monitoring
 * because it is already frozen by PMU handler.
 */
void pfm_switch_sets_from_intr(struct pfm_context *ctx)
{
	struct pfm_event_set *set, *new_set;
	u64 now, end;
	u32 new_flags;
	int is_system, n;

	now = sched_clock();
	set = ctx->active_set;
	new_set = list_first_entry(&set->list, struct pfm_event_set, list);
	if (&new_set->list == &ctx->set_list)
		new_set = list_first_entry(&ctx->set_list, struct pfm_event_set, list);

	PFM_DBG_ovfl("state=%d cur_set=%u cur_runs=%llu cur_npend=%d next_set=%u "
		  "next_runs=%llu new_npend=%d new_r_pmds=%llx",
		  ctx->state,
		  set->id,
		  (unsigned long long)set->runs,
		  set->npend_ovfls,
		  new_set->id,
		  (unsigned long long)new_set->runs,
		  new_set->npend_ovfls,
		  (unsigned long long)new_set->reset_pmds[0]);

	is_system = ctx->flags.system;
	new_flags = new_set->flags;

	/*
	 * nothing more to do
	 */
	if (new_set == set)
		goto skip_same_set;

	/*
	 * switch on intr only when set has OVFL_SWITCH
	 */
	BUG_ON(set->flags & PFM_SETFL_TIME_SWITCH);

	/*
	 * when called from PMU intr handler, monitoring
	 * is already stopped
	 *
	 * save current PMD registers, we use a special
	 * form for performance reason. On some architectures,
	 * such as x86, the pmds are already saved when entering
	 * the PMU interrupt handler via pfm-arch_intr_freeze()
	 * so we don't need to save them again. On the contrary,
	 * on IA-64, they are not saved by freeze, thus we have to
	 * to it here.
	 */
	pfm_arch_save_pmds_from_intr(ctx, set);

	/*
	 * compute elapsed ns for active set
	 */
	set->duration += now - set->duration_start;

	pfm_arch_restore_pmds(ctx, new_set);

	/*
	 * must not be restored active as we are still executing in the
	 * PMU interrupt handler. activation is deferred to unfreeze PMU
	 */
	pfm_arch_restore_pmcs(ctx, new_set);

	/*
	 * check for pending interrupt on incoming set.
	 * interrupts are masked so handler call deferred
	 */
	if (new_set->npend_ovfls) {
		pfm_arch_resend_irq(ctx);
		pfm_stats_inc(ovfl_intr_replay_count);
	}
	/*
	 * no need to restore anything, that is already done
	 */
	new_set->priv_flags &= ~PFM_SETFL_PRIV_MOD_BOTH;
	/*
	 * reset duration counter
	 */
	new_set->duration_start = now;

skip_same_set:
	new_set->runs++;

	/*
	 * reset switch threshold
	 */
	if (new_flags & PFM_SETFL_OVFL_SWITCH)
		pfm_reload_switch_thresholds(ctx, new_set);

	/*
	 * reset overflowed PMD registers
	 */
	n = bitmap_weight(cast_ulp(new_set->reset_pmds), ctx->regs.max_pmd);
	if (n)
		pfm_reset_pmds(ctx, new_set, n, PFM_PMD_RESET_SHORT);

	/*
	 * XXX: isactive?
	 *
	 * Came here following a interrupt which triggered a switch, i.e.,
	 * previous set was using OVFL_SWITCH, thus we just need to arm
	 * check if the next set is using timeout, and if so arm the timer.
	 *
	 * Timeout is always at least one tick away. No risk of having to
	 * invoke the timeout handler right now. In any case, cb_mode is
	 * set to HRTIMER_CB_IRQSAFE_NO_SOFTIRQ such that hrtimer_start
	 * will not try to wakeup the softirqd which could cause a locking
	 * problem.
	 */
	if (new_flags & PFM_SETFL_TIME_SWITCH) {
		hrtimer_start(&__get_cpu_var(pfm_hrtimer), set->hrtimer_exp, HRTIMER_MODE_REL);
		PFM_DBG("armed new timeout for set%u", new_set->id);
	}

	ctx->active_set = new_set;

	end = sched_clock();

	pfm_stats_inc(set_switch_count);
	pfm_stats_add(set_switch_ns, end - now);
}


static int pfm_setfl_sane(struct pfm_context *ctx, u32 flags)
{
#define PFM_SETFL_BOTH_SWITCH	(PFM_SETFL_OVFL_SWITCH|PFM_SETFL_TIME_SWITCH)
	int ret;

	ret = pfm_arch_setfl_sane(ctx, flags);
	if (ret)
		return ret;

	if ((flags & PFM_SETFL_BOTH_SWITCH) == PFM_SETFL_BOTH_SWITCH) {
		PFM_DBG("both switch ovfl and switch time are set");
		return -EINVAL;
	}
	return 0;
}

/*
 * it is never possible to change the identification of an existing set
 */
static int pfm_change_evtset(struct pfm_context *ctx,
			       struct pfm_event_set *set,
			       struct pfarg_setdesc *req)
{
	struct timeval tv;
	struct timespec ts;
	ktime_t kt;
	long d, res_ns;
	s32 rem;
	u32 flags;
	int ret;
	u16 set_id;

	BUG_ON(ctx->state == PFM_CTX_LOADED);

	set_id = req->set_id;
	flags = req->set_flags;

	ret = pfm_setfl_sane(ctx, flags);
	if (ret) {
		PFM_DBG("invalid flags 0x%x set %u", flags, set_id);
		return -EINVAL;
	}

	/*
	 * compute timeout value
	 */
	if (flags & PFM_SETFL_TIME_SWITCH) {
		/*
		 * timeout value of zero is illegal
		 */
		if (req->set_timeout == 0) {
			PFM_DBG("invalid timeout 0");
			return -EINVAL;
		}

		hrtimer_get_res(CLOCK_MONOTONIC, &ts);
		res_ns = (long)ktime_to_ns(timespec_to_ktime(ts));

		/*
		 * round-up to multiple of clock resolution
		 * timeout = ((req->set_timeout+res_ns-1)/res_ns)*res_ns;
		 *
		 * u64 division missing on 32-bit arch, so use div_s64_rem
		 */
		d = div_s64_rem(req->set_timeout, res_ns, &rem);

		PFM_DBG("set%u flags=0x%x req_timeout=%lluns "
				"HZ=%u TICK_NSEC=%lu clock_res=%ldns rem=%dns",
				set_id,
				flags,
				(unsigned long long)req->set_timeout,
				HZ, TICK_NSEC,
				res_ns,
				rem);

		/*
		 * Only accept timeout, we can actually achieve.
		 * users can invoke clock_getres(CLOCK_MONOTONIC)
		 * to figure out resolution and adjust timeout
		 */
		if (rem) {
			PFM_DBG("set%u invalid timeout=%llu",
				set_id,
				(unsigned long long)req->set_timeout);
			return -EINVAL;
		}

		tv = ns_to_timeval(req->set_timeout);
		kt = timeval_to_ktime(tv);
		set->hrtimer_exp = kt;
	} else {
		set->hrtimer_exp = ktime_set(0, 0);
	}

	/*
	 * commit changes
	 */
	set->id = set_id;
	set->flags = flags;
	set->priv_flags = 0;

	/*
	 * activation and duration counters are reset as
	 * most likely major things will change in the set
	 */
	set->runs = 0;
	set->duration = 0;

	return 0;
}

/*
 * this function does not modify the next field
 */
static void pfm_initialize_set(struct pfm_context *ctx,
			       struct pfm_event_set *set)
{
	u64 *impl_pmcs;
	u16 i, max_pmc;

	max_pmc = ctx->regs.max_pmc;
	impl_pmcs =  ctx->regs.pmcs;

	/*
	 * install default values for all PMC  registers
	 */
	for (i = 0; i < max_pmc; i++) {
		if (test_bit(i, cast_ulp(impl_pmcs))) {
			set->pmcs[i] = pfm_pmu_conf->pmc_desc[i].dfl_val;
			PFM_DBG("set%u pmc%u=0x%llx",
				set->id,
				i,
				(unsigned long long)set->pmcs[i]);
		}
	}

	/*
	 * PMD registers are set to 0 when the event set is allocated,
	 * hence we do not need to explicitly initialize them.
	 *
	 * For virtual PMD registers (i.e., those tied to a SW resource)
	 * their value becomes meaningful once the context is attached.
	 */
}

/*
 * look for an event set using its identification. If the set does not
 * exist:
 * 	- if alloc == 0 then return error
 * 	- if alloc == 1  then allocate set
 *
 * alloc is one ONLY when coming from pfm_create_evtsets() which can only
 * be called when the context is detached, i.e. monitoring is stopped.
 */
struct pfm_event_set *pfm_find_set(struct pfm_context *ctx, u16 set_id, int alloc)
{
	struct pfm_event_set *set = NULL, *prev, *new_set;

	PFM_DBG("looking for set=%u", set_id);

	prev = NULL;
	list_for_each_entry(set, &ctx->set_list, list) {
		if (set->id == set_id)
			return set;
		if (set->id > set_id)
			break;
		prev = set;
	}

	if (!alloc)
		return NULL;

	/*
	 * we are holding the context spinlock and interrupts
	 * are unmasked. We must use GFP_ATOMIC as we cannot
	 * sleep while holding a spin lock.
	 */
	new_set = kmem_cache_zalloc(pfm_set_cachep, GFP_ATOMIC);
	if (!new_set)
		return NULL;

	new_set->id = set_id;

	INIT_LIST_HEAD(&new_set->list);

	if (prev == NULL) {
		list_add(&(new_set->list), &ctx->set_list);
	} else {
		PFM_DBG("add after set=%u", prev->id);
		list_add(&(new_set->list), &prev->list);
	}
	return new_set;
}

/**
 * pfm_create_initial_set - create initial set from __pfm_c reate_context
 * @ctx: context to atatched the set to
 */
int pfm_create_initial_set(struct pfm_context *ctx)
{
	struct pfm_event_set *set;

	/*
	 * create initial set0
	 */
	if (!pfm_find_set(ctx, 0, 1))
		return -ENOMEM;

	set = list_first_entry(&ctx->set_list, struct pfm_event_set, list);

	pfm_initialize_set(ctx, set);

	return 0;
}

/*
 * context is unloaded for this command. Interrupts are enabled
 */
int __pfm_create_evtsets(struct pfm_context *ctx, struct pfarg_setdesc *req,
			int count)
{
	struct pfm_event_set *set;
	u16 set_id;
	int i, ret;

	for (i = 0; i < count; i++, req++) {
		set_id = req->set_id;

		PFM_DBG("set_id=%u", set_id);

		set = pfm_find_set(ctx, set_id, 1);
		if (set == NULL)
			goto error_mem;

		ret = pfm_change_evtset(ctx, set, req);
		if (ret)
			goto error_params;

		pfm_initialize_set(ctx, set);
	}
	return 0;
error_mem:
	PFM_DBG("cannot allocate set %u", set_id);
	return -ENOMEM;
error_params:
	return ret;
}

int __pfm_getinfo_evtsets(struct pfm_context *ctx, struct pfarg_setinfo *req,
				 int count)
{
	struct pfm_event_set *set;
	int i, is_system, is_loaded, is_self, ret;
	u16 set_id;
	u64 end;

	end = sched_clock();

	is_system = ctx->flags.system;
	is_loaded = ctx->state == PFM_CTX_LOADED;
	is_self   = ctx->task == current || is_system;

	ret = -EINVAL;
	for (i = 0; i < count; i++, req++) {

		set_id = req->set_id;

		list_for_each_entry(set, &ctx->set_list, list) {
			if (set->id == set_id)
				goto found;
			if (set->id > set_id)
				goto error;
		}
found:
		req->set_flags = set->flags;

		/*
		 * compute leftover timeout
		 *
		 * lockdep may complain about lock inversion
		 * because of get_remaining() however, this
		 * applies to self-montoring only, thus the
		 * thread cannot be in the timeout handler
		 * and here at the same time given that we
		 * run with interrupts disabled
		 */
		if (is_loaded && is_self) {
			struct hrtimer *h;
			h = &__get_cpu_var(pfm_hrtimer);
			req->set_timeout = ktime_to_ns(hrtimer_get_remaining(h));
		} else {
			/*
			 * hrtimer_rem zero when not using
			 * timeout-based switching
			 */
			req->set_timeout = ktime_to_ns(set->hrtimer_rem);
		}

		req->set_runs = set->runs;
		req->set_act_duration = set->duration;

		/*
		 * adjust for active set if needed
		 */
		if (is_system && is_loaded && ctx->flags.started
		    && set == ctx->active_set)
			req->set_act_duration  += end - set->duration_start;

		/*
		 * copy the list of pmds which last overflowed
		 */
		bitmap_copy(cast_ulp(req->set_ovfl_pmds),
			    cast_ulp(set->ovfl_pmds),
			    PFM_MAX_PMDS);

		/*
		 * copy bitmask of available PMU registers
		 *
		 * must copy over the entire vector to avoid
		 * returning bogus upper bits pass by user
		 */
		bitmap_copy(cast_ulp(req->set_avail_pmcs),
			    cast_ulp(ctx->regs.pmcs),
			    PFM_MAX_PMCS);

		bitmap_copy(cast_ulp(req->set_avail_pmds),
			    cast_ulp(ctx->regs.pmds),
			    PFM_MAX_PMDS);

		PFM_DBG("set%u flags=0x%x eff_usec=%llu runs=%llu "
			"a_pmcs=0x%llx a_pmds=0x%llx",
			set_id,
			set->flags,
			(unsigned long long)req->set_timeout,
			(unsigned long long)set->runs,
			(unsigned long long)ctx->regs.pmcs[0],
			(unsigned long long)ctx->regs.pmds[0]);
	}
	ret = 0;
error:
	return ret;
}

/*
 * context is unloaded for this command. Interrupts are enabled
 */
int __pfm_delete_evtsets(struct pfm_context *ctx, void *arg, int count)
{
	struct pfarg_setdesc *req = arg;
	struct pfm_event_set *set;
	u16 set_id;
	int i, ret;

	ret = -EINVAL;
	for (i = 0; i < count; i++, req++) {
		set_id = req->set_id;

		list_for_each_entry(set, &ctx->set_list, list) {
			if (set->id == set_id)
				goto found;
			if (set->id > set_id)
				goto error;
		}
		goto error;
found:
		/*
		 * clear active set if necessary.
		 * will be updated when context is loaded
		 */
		if (set == ctx->active_set)
			ctx->active_set = NULL;

		list_del(&set->list);

		kmem_cache_free(pfm_set_cachep, set);

		PFM_DBG("set%u deleted", set_id);
	}
	ret = 0;
error:
	return ret;
}

/*
 * called from pfm_context_free() to free all sets
 */
void pfm_free_sets(struct pfm_context *ctx)
{
	struct pfm_event_set *set, *tmp;

	list_for_each_entry_safe(set, tmp, &ctx->set_list, list) {
		list_del(&set->list);
		kmem_cache_free(pfm_set_cachep, set);
	}
}

/**
 * pfm_restart_timer - restart hrtimer taking care of expired timeout
 * @ctx : context to work with
 * @set : current active set
 *
 * Must be called on the processor on which the timer is to be armed.
 * Assumes context is locked and interrupts are masked
 *
 * Upon return the active set for the context may have changed
 */
void pfm_restart_timer(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct hrtimer *h;
	enum hrtimer_restart ret;

	h = &__get_cpu_var(pfm_hrtimer);

	PFM_DBG_ovfl("hrtimer=%lld", (long long)ktime_to_ns(set->hrtimer_rem));

	if (ktime_to_ns(set->hrtimer_rem) > 0) {
		hrtimer_start(h, set->hrtimer_rem, HRTIMER_MODE_REL);
	} else {
		/*
		 * timer was not re-armed because it has already expired
		 * timer was not enqueued, we need to switch set now
		 */
		pfm_stats_inc(set_switch_exp);

		ret = pfm_switch_sets(ctx, NULL, 1, 0);
		set = ctx->active_set;
		if (ret == HRTIMER_RESTART)
			hrtimer_start(h, set->hrtimer_rem, HRTIMER_MODE_REL);
	}
}

int __init pfm_init_sets(void)
{
	pfm_set_cachep = kmem_cache_create("pfm_event_set",
					   sizeof(struct pfm_event_set),
					   SLAB_HWCACHE_ALIGN, 0, NULL);
	if (!pfm_set_cachep) {
		PFM_ERR("cannot initialize event set slab");
		return -ENOMEM;
	}
	return 0;
}
