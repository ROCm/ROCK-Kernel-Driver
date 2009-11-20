/*
 * perfmon.c: perfmon2 PMC/PMD read/write system calls
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
 * 	http://perfmon2.sf.net/
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
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

#define PFM_REGFL_PMC_ALL	(PFM_REGFL_NO_EMUL64)
#define PFM_REGFL_PMD_ALL	(PFM_REGFL_RANDOM|PFM_REGFL_OVFL_NOTIFY)

/**
 * handle_dep_pmcs - update used_pmds based on dep_pmcs for the pmd
 * @ctx: context to use
 * @set: set to use
 * @cnum: PMD to use
 *
 * return:
 * 	0 : success
 * 	<0: on error (errno)
 */
static int handle_dep_pmcs(struct pfm_context *ctx, struct pfm_event_set *set,
			   u16 cnum)
{
	struct pfarg_pmc req;
	u64 *dep_pmcs;
	int n, p, q, ret = 0;

	dep_pmcs = pfm_pmu_conf->pmd_desc[cnum].dep_pmcs;
	n = bitmap_weight(cast_ulp(dep_pmcs), ctx->regs.max_pmc);

	memset(&req, 0, sizeof(req));

	for(p = 0; n; n--, p = q+1) {
		q = find_next_bit(cast_ulp(dep_pmcs), ctx->regs.max_pmc, p);

		if (test_bit(q, cast_ulp(set->used_pmcs)))
			continue;

		req.reg_num = q;
		req.reg_value = set->pmcs[q]; /* default value */

		ret = __pfm_write_pmcs(ctx, &req, 1);
		if (ret)
			break;
	}
	return ret;
}

/**
 * handle_smpl_bv - checks sampling bitmasks for new PMDs
 * @ctx: context to use
 * @set: set to use
 * @bv: sampling bitmask
 *
 * scans the smpl bitmask looking for new PMDs (not yet used), if found
 * invoke pfm_write_pmds() on them to get them initialized and marked used
 *
 * return:
 * 	0 : success
 * 	<0: error (errno)
 */
static int handle_smpl_bv(struct pfm_context *ctx, struct pfm_event_set *set,
			  unsigned long *bv)
{
	struct pfarg_pmd req;
	int p, q, n, ret = 0;
	u16 max_pmd;

	memset(&req, 0, sizeof(req));

	max_pmd = ctx->regs.max_pmd;

	n = bitmap_weight(cast_ulp(bv), max_pmd);

	for(p = 0; n; n--, p = q+1) {
		q = find_next_bit(cast_ulp(bv), max_pmd, p);

		if (test_bit(q, cast_ulp(set->used_pmds)))
			continue;

		req.reg_num = q;
		req.reg_value = 0;

		ret = __pfm_write_pmds(ctx, &req, 1, 0);
		if (ret)
			break;
	}
	return ret;
}

/**
 * is_invalid -- check if register index is within limits
 * @cnum: register index
 * @impl: bitmask of implemented registers
 * @max: highest implemented registers + 1
 *
 * return:
 *    0 is register index is valid
 *    1 if invalid
 */
static inline int is_invalid(u16 cnum, unsigned long *impl, u16 max)
{
	return cnum >= max || !test_bit(cnum, impl);
}

/**
 * __pfm_write_pmds - modified data registers
 * @ctx: context to operate on
 * @req: pfarg_pmd_t request from user
 * @count: number of element in the pfarg_pmd_t vector
 * @compat: used only on IA-64 to maintain backward compatibility with v2.0
 *
 * The function succeeds whether the context is attached or not.
 * When attached to another thread, that thread must be stopped.
 *
 * The context is locked and interrupts are disabled.
 */
int __pfm_write_pmds(struct pfm_context *ctx, struct pfarg_pmd *req, int count,
		     int compat)
{
	struct pfm_event_set *set, *active_set;
	unsigned long *smpl_pmds, *reset_pmds, *impl_pmds, *impl_rw_pmds;
	u32 req_flags;
	u16 cnum, pmd_type, max_pmd;
	u16 set_id;
	int i, can_access_pmu;
	int ret;
	pfm_pmd_check_t	wr_func;

	active_set = ctx->active_set;
	max_pmd	= ctx->regs.max_pmd;
	impl_pmds = cast_ulp(ctx->regs.pmds);
	impl_rw_pmds = cast_ulp(ctx->regs.rw_pmds);
	wr_func = pfm_pmu_conf->pmd_write_check;
	set = list_first_entry(&ctx->set_list, struct pfm_event_set, list);

	can_access_pmu = 0;

	/*
	 * we cannot access the actual PMD registers when monitoring is masked
	 */
	if (unlikely(ctx->state == PFM_CTX_LOADED))
		can_access_pmu = __get_cpu_var(pmu_owner) == ctx->task
			|| ctx->flags.system;

	ret = -EINVAL;
	for (i = 0; i < count; i++, req++) {

		cnum = req->reg_num;
		set_id = req->reg_set;
		req_flags = req->reg_flags;
		smpl_pmds = cast_ulp(req->reg_smpl_pmds);
		reset_pmds = cast_ulp(req->reg_reset_pmds);

		/*
		 * cannot write to unexisting
		 * writes to read-only register are ignored
		 */
		if (unlikely(is_invalid(cnum, impl_pmds, max_pmd))) {
			PFM_DBG("pmd%u is not available", cnum);
			goto error;
		}

		pmd_type = pfm_pmu_conf->pmd_desc[cnum].type;

		/*
		 * ensure only valid flags are set
		 */
		if (req_flags & ~PFM_REGFL_PMD_ALL) {
			PFM_DBG("pmd%u: invalid flags=0x%x",
				cnum, req_flags);
			goto error;
		}

		/*
		 * verify validity of smpl_pmds
		 */
		if (unlikely(!bitmap_subset(smpl_pmds, impl_pmds, PFM_MAX_PMDS))) {
			PFM_DBG("invalid smpl_pmds=0x%llx for pmd%u",
				(unsigned long long)req->reg_smpl_pmds[0],
				cnum);
			goto error;
		}

		/*
		 * verify validity of reset_pmds
		 * check against impl_rw_pmds because it is not
		 * possible to reset read-only PMDs
		 */
		if (unlikely(!bitmap_subset(reset_pmds, impl_rw_pmds, PFM_MAX_PMDS))) {
			PFM_DBG("invalid reset_pmds=0x%llx for pmd%u",
				(unsigned long long)req->reg_reset_pmds[0],
				cnum);
			goto error;
		}

		/*
		 * locate event set
		 */
		if (set_id != set->id) {
			set = pfm_find_set(ctx, set_id, 0);
			if (set == NULL) {
				PFM_DBG("event set%u does not exist",
						set_id);
				goto error;
			}
		}

		/*
		 * execute write checker, if any
		 */
		if (unlikely(wr_func && (pmd_type & PFM_REG_WC))) {
			ret = (*wr_func)(ctx, set, req);
			if (ret)
				goto error;

		}

		ret = handle_dep_pmcs(ctx, set, cnum);
		if (ret)
			goto error;

		if (unlikely(compat))
			goto skip_set;

		if (bitmap_weight(smpl_pmds, max_pmd)) {
			ret = handle_smpl_bv(ctx, set, smpl_pmds);
			if (ret)
				goto error;
		}

		if (bitmap_weight(reset_pmds, max_pmd)) {
			ret = handle_smpl_bv(ctx, set, reset_pmds);
			if (ret)
				goto error;
		}

		/*
		 * now commit changes to software state
		 */
		bitmap_copy(cast_ulp(set->pmds[cnum].smpl_pmds),
			    smpl_pmds,
			    max_pmd);

		bitmap_copy(cast_ulp(set->pmds[cnum].reset_pmds),
			   reset_pmds,
			   max_pmd);

		set->pmds[cnum].flags = req_flags;

		__set_bit(cnum, cast_ulp(set->used_pmds));

		/*
		 * we reprogram the PMD hence, we clear any pending
		 * ovfl. Does affect ovfl switch on restart but new
		 * value has already been established here
		 */
		if (test_bit(cnum, cast_ulp(set->povfl_pmds))) {
			set->npend_ovfls--;
			__clear_bit(cnum, cast_ulp(set->povfl_pmds));
		}
		__clear_bit(cnum, cast_ulp(set->ovfl_pmds));

		/*
		 * update ovfl_notify
		 */
		if (req_flags & PFM_REGFL_OVFL_NOTIFY)
			__set_bit(cnum, cast_ulp(set->ovfl_notify));
		else
			__clear_bit(cnum, cast_ulp(set->ovfl_notify));

		/*
		 * establish new switch count
		 */
		set->pmds[cnum].ovflsw_thres = req->reg_ovfl_switch_cnt;
		set->pmds[cnum].ovflsw_ref_thres = req->reg_ovfl_switch_cnt;
skip_set:

		/*
		 * set last value to new value for all types of PMD
		 */
		set->pmds[cnum].lval = req->reg_value;
		set->pmds[cnum].value = req->reg_value;

		/*
		 * update reset values (not just for counters)
		 */
		set->pmds[cnum].long_reset = req->reg_long_reset;
		set->pmds[cnum].short_reset = req->reg_short_reset;

		/*
		 * update randomization mask
		 */
		set->pmds[cnum].mask = req->reg_random_mask;

		set->pmds[cnum].eventid = req->reg_smpl_eventid;

		if (set == active_set) {
			set->priv_flags |= PFM_SETFL_PRIV_MOD_PMDS;
			if (can_access_pmu)
				pfm_write_pmd(ctx, cnum, req->reg_value);
		}


		PFM_DBG("set%u pmd%u=0x%llx flags=0x%x a_pmu=%d "
			"ctx_pmd=0x%llx s_reset=0x%llx "
			"l_reset=0x%llx s_pmds=0x%llx "
			"r_pmds=0x%llx o_pmds=0x%llx "
			"o_thres=%llu compat=%d eventid=%llx",
			set->id,
			cnum,
			(unsigned long long)req->reg_value,
			set->pmds[cnum].flags,
			can_access_pmu,
			(unsigned long long)set->pmds[cnum].value,
			(unsigned long long)set->pmds[cnum].short_reset,
			(unsigned long long)set->pmds[cnum].long_reset,
			(unsigned long long)set->pmds[cnum].smpl_pmds[0],
			(unsigned long long)set->pmds[cnum].reset_pmds[0],
			(unsigned long long)set->ovfl_pmds[0],
			(unsigned long long)set->pmds[cnum].ovflsw_thres,
			compat,
			(unsigned long long)set->pmds[cnum].eventid);
	}
	ret = 0;
error:
	/*
	 * make changes visible
	 */
	if (can_access_pmu)
		pfm_arch_serialize();

	return ret;
}

/**
 * __pfm_write_pmcs - modified config registers
 * @ctx: context to operate on
 * @req: pfarg_pmc_t request from user
 * @count: number of element in the pfarg_pmc_t vector
 *
 *
 * The function succeeds whether the context is * attached or not.
 * When attached to another thread, that thread must be stopped.
 *
 * The context is locked and interrupts are disabled.
 */
int __pfm_write_pmcs(struct pfm_context *ctx, struct pfarg_pmc *req, int count)
{
	struct pfm_event_set *set, *active_set;
	u64 value, dfl_val, rsvd_msk;
	unsigned long *impl_pmcs;
	int i, can_access_pmu;
	int ret;
	u16 set_id;
	u16 cnum, pmc_type, max_pmc;
	u32 flags, expert;
	pfm_pmc_check_t	wr_func;

	active_set = ctx->active_set;

	wr_func = pfm_pmu_conf->pmc_write_check;
	max_pmc = ctx->regs.max_pmc;
	impl_pmcs = cast_ulp(ctx->regs.pmcs);
	set = list_first_entry(&ctx->set_list, struct pfm_event_set, list);

	expert = pfm_controls.flags & PFM_CTRL_FL_RW_EXPERT;

	can_access_pmu = 0;

	/*
	 * we cannot access the actual PMC registers when monitoring is masked
	 */
	if (unlikely(ctx->state == PFM_CTX_LOADED))
		can_access_pmu = __get_cpu_var(pmu_owner) == ctx->task
			|| ctx->flags.system;

	ret = -EINVAL;

	for (i = 0; i < count; i++, req++) {

		cnum = req->reg_num;
		set_id = req->reg_set;
		value = req->reg_value;
		flags = req->reg_flags;

		/*
		 * no access to unavailable PMC register
		 */
		if (unlikely(is_invalid(cnum, impl_pmcs, max_pmc))) {
			PFM_DBG("pmc%u is not available", cnum);
			goto error;
		}

		pmc_type = pfm_pmu_conf->pmc_desc[cnum].type;
		dfl_val = pfm_pmu_conf->pmc_desc[cnum].dfl_val;
		rsvd_msk = pfm_pmu_conf->pmc_desc[cnum].rsvd_msk;

		/*
		 * ensure only valid flags are set
		 */
		if (flags & ~PFM_REGFL_PMC_ALL) {
			PFM_DBG("pmc%u: invalid flags=0x%x", cnum, flags);
			goto error;
		}

		/*
		 * locate event set
		 */
		if (set_id != set->id) {
			set = pfm_find_set(ctx, set_id, 0);
			if (set == NULL) {
				PFM_DBG("event set%u does not exist",
					set_id);
				goto error;
			}
		}

		/*
		 * set reserved bits to default values
		 * (reserved bits must be 1 in rsvd_msk)
		 *
		 * bypass via /sys/kernel/perfmon/mode = 1
		 */
		if (likely(!expert))
			value = (value & ~rsvd_msk) | (dfl_val & rsvd_msk);

		if (flags & PFM_REGFL_NO_EMUL64) {
			if (!(pmc_type & PFM_REG_NO64)) {
				PFM_DBG("pmc%u no support for "
					"PFM_REGFL_NO_EMUL64", cnum);
				goto error;
			}
			value &= ~pfm_pmu_conf->pmc_desc[cnum].no_emul64_msk;
		}

		/*
		 * execute write checker, if any
		 */
		if (likely(wr_func && (pmc_type & PFM_REG_WC))) {
			req->reg_value = value;
			ret = (*wr_func)(ctx, set, req);
			if (ret)
				goto error;
			value = req->reg_value;
		}

		/*
		 * Now we commit the changes
		 */

		/*
		 * mark PMC register as used
		 * We do not track associated PMC register based on
		 * the fact that they will likely need to be written
		 * in order to become useful at which point the statement
		 * below will catch that.
		 *
		 * The used_pmcs bitmask is only useful on architectures where
		 * the PMC needs to be modified for particular bits, especially
		 * on overflow or to stop/start.
		 */
		if (!test_bit(cnum, cast_ulp(set->used_pmcs)))
			__set_bit(cnum, cast_ulp(set->used_pmcs));

		set->pmcs[cnum] = value;

		if (set == active_set) {
			set->priv_flags |= PFM_SETFL_PRIV_MOD_PMCS;
			if (can_access_pmu)
				pfm_arch_write_pmc(ctx, cnum, value);
		}

		PFM_DBG("set%u pmc%u=0x%llx a_pmu=%d "
			"u_pmcs=0x%llx",
			set->id,
			cnum,
			(unsigned long long)value,
			can_access_pmu,
			(unsigned long long)set->used_pmcs[0]);
	}
	ret = 0;
error:
	/*
	 * make sure the changes are visible
	 */
	if (can_access_pmu)
		pfm_arch_serialize();

	return ret;
}

/**
 * __pfm_read_pmds - read data registers
 * @ctx: context to operate on
 * @req: pfarg_pmd_t request from user
 * @count: number of element in the pfarg_pmd_t vector
 *
 *
 * The function succeeds whether the context is attached or not.
 * When attached to another thread, that thread must be stopped.
 *
 * The context is locked and interrupts are disabled.
 */
int __pfm_read_pmds(struct pfm_context *ctx, struct pfarg_pmd *req, int count)
{
	u64 val = 0, lval, ovfl_mask, hw_val;
	u64 sw_cnt;
	unsigned long *impl_pmds;
	struct pfm_event_set *set, *active_set;
	int i, ret, can_access_pmu = 0;
	u16 cnum, pmd_type, set_id, max_pmd;

	ovfl_mask = pfm_pmu_conf->ovfl_mask;
	impl_pmds = cast_ulp(ctx->regs.pmds);
	max_pmd   = ctx->regs.max_pmd;
	active_set = ctx->active_set;
	set = list_first_entry(&ctx->set_list, struct pfm_event_set, list);

	if (likely(ctx->state == PFM_CTX_LOADED)) {
		can_access_pmu = __get_cpu_var(pmu_owner) == ctx->task
			|| ctx->flags.system;

		if (can_access_pmu)
			pfm_arch_serialize();
	}

	/*
	 * on both UP and SMP, we can only read the PMD from the hardware
	 * register when the task is the owner of the local PMU.
	 */
	ret = -EINVAL;
	for (i = 0; i < count; i++, req++) {

		cnum = req->reg_num;
		set_id = req->reg_set;

		if (unlikely(is_invalid(cnum, impl_pmds, max_pmd))) {
			PFM_DBG("pmd%u is not implemented/unaccessible", cnum);
			goto error;
		}

		pmd_type = pfm_pmu_conf->pmd_desc[cnum].type;

		/*
		 * locate event set
		 */
		if (set_id != set->id) {
			set = pfm_find_set(ctx, set_id, 0);
			if (set == NULL) {
				PFM_DBG("event set%u does not exist",
					set_id);
				goto error;
			}
		}
		/*
		 * it is not possible to read a PMD which was not requested:
		 * 	- explicitly written via pfm_write_pmds()
		 * 	- provided as a reg_smpl_pmds[] to another PMD during
		 * 	  pfm_write_pmds()
		 *
		 * This is motivated by security and for optimization purposes:
		 * 	- on context switch restore, we can restore only what
		 * 	  we use (except when regs directly readable at user
		 * 	  level, e.g., IA-64 self-monitoring, I386 RDPMC).
		 * 	- do not need to maintain PMC -> PMD dependencies
		 */
		if (unlikely(!test_bit(cnum, cast_ulp(set->used_pmds)))) {
			PFM_DBG("pmd%u cannot read, because not used", cnum);
			goto error;
		}

		val = set->pmds[cnum].value;
		lval = set->pmds[cnum].lval;

		/*
		 * extract remaining ovfl to switch
		 */
		sw_cnt = set->pmds[cnum].ovflsw_thres;

		/*
		 * If the task is not the current one, then we check if the
		 * PMU state is still in the local live register due to lazy
		 * ctxsw. If true, then we read directly from the registers.
		 */
		if (set == active_set && can_access_pmu) {
			hw_val = pfm_read_pmd(ctx, cnum);
			if (pmd_type & PFM_REG_C64)
				val = (val & ~ovfl_mask) | (hw_val & ovfl_mask);
			else
				val = hw_val;
		}

		PFM_DBG("set%u pmd%u=0x%llx sw_thr=%llu lval=0x%llx",
			set->id,
			cnum,
			(unsigned long long)val,
			(unsigned long long)sw_cnt,
			(unsigned long long)lval);

		req->reg_value = val;
		req->reg_last_reset_val = lval;
		req->reg_ovfl_switch_cnt = sw_cnt;
	}
	ret = 0;
error:
	return ret;
}
