/*
 * perfmon_pmu.c: perfmon2 PMU configuration management
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
#include <linux/module.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

#ifndef CONFIG_MODULE_UNLOAD
#define module_refcount(n)	1
#endif

static __cacheline_aligned_in_smp int request_mod_in_progress;
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(pfm_pmu_conf_lock);

static __cacheline_aligned_in_smp DEFINE_SPINLOCK(pfm_pmu_acq_lock);
static u32 pfm_pmu_acquired;

/*
 * perfmon core must acces PMU information ONLY through pfm_pmu_conf
 * if pfm_pmu_conf is NULL, then no description is registered
 */
struct pfm_pmu_config	*pfm_pmu_conf;
EXPORT_SYMBOL(pfm_pmu_conf);

static inline int pmu_is_module(struct pfm_pmu_config *c)
{
	return !(c->flags & PFM_PMUFL_IS_BUILTIN);
}
/**
 * pfm_pmu_regdesc_init -- initialize regdesc structure from PMU table
 * @regs: the regdesc structure to initialize
 * @excl_type: the register type(s) to exclude from this regdesc
 * @unvail_pmcs: unavailable PMC registers
 * @unavail_pmds: unavailable PMD registers
 *
 * Return:
 * 	0 success
 * 	errno in case of error
 */
static int pfm_pmu_regdesc_init(struct pfm_regdesc *regs, int excl_type,
				u64 *unavail_pmcs, u64 *unavail_pmds)
{
	struct pfm_regmap_desc *d;
	u16 n, n2, n_counters, i;
	int first_intr_pmd = -1, max1, max2, max3;

	/*
	 * compute the number of implemented PMC from the
	 * description table
	 */
	n = 0;
	max1 = max2 = -1;
	d = pfm_pmu_conf->pmc_desc;
	for (i = 0; i < pfm_pmu_conf->num_pmc_entries;  i++, d++) {
		if (!(d->type & PFM_REG_I))
			continue;

		if (test_bit(i, cast_ulp(unavail_pmcs)))
			continue;

		if (d->type & excl_type)
			continue;

		__set_bit(i, cast_ulp(regs->pmcs));

		max1 = i;
		n++;
	}

	if (!n) {
		PFM_INFO("%s PMU description has no PMC registers",
			 pfm_pmu_conf->pmu_name);
		return -EINVAL;
	}

	regs->max_pmc = max1 + 1;
	regs->num_pmcs = n;

	n = n_counters = n2 = 0;
	max1 = max2 = max3 = -1;
	d = pfm_pmu_conf->pmd_desc;
	for (i = 0; i < pfm_pmu_conf->num_pmd_entries;  i++, d++) {
		if (!(d->type & PFM_REG_I))
			continue;

		if (test_bit(i, cast_ulp(unavail_pmds)))
			continue;

		if (d->type & excl_type)
			continue;

		__set_bit(i, cast_ulp(regs->pmds));
		max1 = i;
		n++;

		/*
		 * read-write registers
		 */
		if (!(d->type & PFM_REG_RO)) {
			__set_bit(i, cast_ulp(regs->rw_pmds));
			max3 = i;
			n2++;
		}

		/*
		 * counter registers
		 */
		if (d->type & PFM_REG_C64) {
			__set_bit(i, cast_ulp(regs->cnt_pmds));
			n_counters++;
		}

		/*
		 * PMD with intr capabilities
		 */
		if (d->type & PFM_REG_INTR) {
			__set_bit(i, cast_ulp(regs->intr_pmds));
			if (first_intr_pmd == -1)
				first_intr_pmd = i;
			max2 = i;
		}
	}

	if (!n) {
		PFM_INFO("%s PMU description has no PMD registers",
			 pfm_pmu_conf->pmu_name);
		return -EINVAL;
	}

	regs->max_pmd = max1 + 1;
	regs->first_intr_pmd = first_intr_pmd;
	regs->max_intr_pmd  = max2 + 1;

	regs->num_counters = n_counters;
	regs->num_pmds = n;
	regs->max_rw_pmd = max3 + 1;
	regs->num_rw_pmd = n2;

	PFM_DBG("intr_pmds=0x%llx cnt_pmds=0x%llx rw_pmds=0x%llx",
		(unsigned long long)regs->intr_pmds[0],
		(unsigned long long)regs->cnt_pmds[0],
		(unsigned long long)regs->rw_pmds[0]);

	return 0;
}

/**
 * pfm_pmu_regdesc_init_all -- initialize all regdesc structures
 * @una_pmcs : unavailable PMC registers
 * @una_pmds : unavailable PMD registers
 *
 * Return:
 * 	0 sucess
 * 	errno if error
 *
 * We maintain 3 regdesc:
 * 	regs_all: all available registers
 * 	regs_sys: registers available to system-wide contexts only
 * 	regs_thr: registers available to per-thread contexts only
 */
static int pfm_pmu_regdesc_init_all(u64 *una_pmcs, u64 *una_pmds)
{
	int ret;

	memset(&pfm_pmu_conf->regs_all, 0, sizeof(struct pfm_regdesc));
	memset(&pfm_pmu_conf->regs_thr, 0, sizeof(struct pfm_regdesc));
	memset(&pfm_pmu_conf->regs_sys, 0, sizeof(struct pfm_regdesc));

	ret = pfm_pmu_regdesc_init(&pfm_pmu_conf->regs_all,
				   0,
				   una_pmcs, una_pmds);
	if (ret)
		return ret;

	PFM_DBG("regs_all.pmcs=0x%llx",
		(unsigned long long)pfm_pmu_conf->regs_all.pmcs[0]);

	ret = pfm_pmu_regdesc_init(&pfm_pmu_conf->regs_thr,
				   PFM_REG_SYS,
				   una_pmcs, una_pmds);
	if (ret)
		return ret;
	PFM_DBG("regs.thr.pmcs=0x%llx",
		(unsigned long long)pfm_pmu_conf->regs_thr.pmcs[0]);

	ret = pfm_pmu_regdesc_init(&pfm_pmu_conf->regs_sys,
				    PFM_REG_THR,
				    una_pmcs, una_pmds);

	PFM_DBG("regs_sys.pmcs=0x%llx",
		(unsigned long long)pfm_pmu_conf->regs_sys.pmcs[0]);

	return ret;
}

int pfm_pmu_register(struct pfm_pmu_config *cfg)
{
	u16 i, nspec, nspec_ro, num_pmcs, num_pmds, num_wc = 0;
	int type, ret = -EBUSY;

	if (perfmon_disabled) {
		PFM_INFO("perfmon disabled, cannot add PMU description");
		return -ENOSYS;
	}

	nspec = nspec_ro = num_pmds = num_pmcs = 0;

	/* some sanity checks */
	if (cfg == NULL || cfg->pmu_name == NULL) {
		PFM_INFO("PMU config descriptor is invalid");
		return -EINVAL;
	}

	/* must have a probe */
	if (cfg->probe_pmu == NULL) {
		PFM_INFO("PMU config has no probe routine");
		return -EINVAL;
	}

	/*
	 * execute probe routine before anything else as it
	 * may update configuration tables
	 */
	if ((*cfg->probe_pmu)() == -1) {
		PFM_INFO("%s PMU detection failed", cfg->pmu_name);
		return -EINVAL;
	}

	if (!(cfg->flags & PFM_PMUFL_IS_BUILTIN) && cfg->owner == NULL) {
		PFM_INFO("PMU config %s is missing owner", cfg->pmu_name);
		return -EINVAL;
	}

	if (!cfg->num_pmd_entries) {
		PFM_INFO("%s needs to define num_pmd_entries", cfg->pmu_name);
		return -EINVAL;
	}

	if (!cfg->num_pmc_entries) {
		PFM_INFO("%s needs to define num_pmc_entries", cfg->pmu_name);
		return -EINVAL;
	}

	if (!cfg->counter_width) {
		PFM_INFO("PMU config %s, zero width counters", cfg->pmu_name);
		return -EINVAL;
	}

	/*
	 * REG_RO, REG_V not supported on PMC registers
	 */
	for (i = 0; i < cfg->num_pmc_entries;  i++) {

		type = cfg->pmc_desc[i].type;

		if (type & PFM_REG_I)
			num_pmcs++;

		if (type & PFM_REG_WC)
			num_wc++;

		if (type & PFM_REG_V) {
			PFM_INFO("PFM_REG_V is not supported on "
				 "PMCs (PMC%d)", i);
			return -EINVAL;
		}
		if (type & PFM_REG_RO) {
			PFM_INFO("PFM_REG_RO meaningless on "
				 "PMCs (PMC%u)", i);
			return -EINVAL;
		}
	}

	if (num_wc && cfg->pmc_write_check == NULL) {
		PFM_INFO("some PMCs have write-checker but no callback provided\n");
		return -EINVAL;
	}

	/*
	 * check virtual PMD registers
	 */
	num_wc = 0;
	for (i = 0; i < cfg->num_pmd_entries;  i++) {

		type = cfg->pmd_desc[i].type;

		if (type & PFM_REG_I)
			num_pmds++;

		if (type & PFM_REG_V) {
			nspec++;
			if (type & PFM_REG_RO)
				nspec_ro++;
		}

		if (type & PFM_REG_WC)
			num_wc++;
	}

	if (num_wc && cfg->pmd_write_check == NULL) {
		PFM_INFO("PMD have write-checker but no callback provided\n");
		return -EINVAL;
	}

	if (nspec && cfg->pmd_sread == NULL) {
		PFM_INFO("PMU config is missing pmd_sread()");
		return -EINVAL;
	}

	nspec = nspec - nspec_ro;
	if (nspec && cfg->pmd_swrite == NULL) {
		PFM_INFO("PMU config is missing pmd_swrite()");
		return -EINVAL;
	}

	if (num_pmcs >= PFM_MAX_PMCS) {
		PFM_INFO("%s PMCS registers exceed name space [0-%u]",
			 cfg->pmu_name,
			 PFM_MAX_PMCS);
		return -EINVAL;
	}
	if (num_pmds >= PFM_MAX_PMDS) {
		PFM_INFO("%s PMDS registers exceed name space [0-%u]",
			 cfg->pmu_name,
			 PFM_MAX_PMDS);
		return -EINVAL;
	}
	spin_lock(&pfm_pmu_conf_lock);

	if (pfm_pmu_conf)
		goto unlock;

	if (!cfg->version)
		cfg->version = "0.0";

	pfm_pmu_conf = cfg;
	pfm_pmu_conf->ovfl_mask = (1ULL << cfg->counter_width) - 1;

	ret = pfm_arch_pmu_config_init(cfg);
	if (ret)
		goto unlock;

	ret = pfm_sysfs_add_pmu(pfm_pmu_conf);
	if (ret)
		pfm_pmu_conf = NULL;

unlock:
	spin_unlock(&pfm_pmu_conf_lock);

	if (ret) {
		PFM_INFO("register %s PMU error %d", cfg->pmu_name, ret);
	} else {
		PFM_INFO("%s PMU installed", cfg->pmu_name);
		/*
		 * (re)initialize PMU on each PMU now that we have a description
		 */
		on_each_cpu(__pfm_init_percpu, cfg, 1);
	}
	return ret;
}
EXPORT_SYMBOL(pfm_pmu_register);

/*
 * remove PMU description. Caller must pass address of current
 * configuration. This is mostly for sanity checking as only
 * one config can exist at any time.
 *
 * We are using the module refcount mechanism to protect against
 * removal while the configuration is being used. As long as there is
 * one context, a PMU configuration cannot be removed. The protection is
 * managed in module logic.
 */
void pfm_pmu_unregister(struct pfm_pmu_config *cfg)
{
	if (!(cfg || pfm_pmu_conf))
		return;

	spin_lock(&pfm_pmu_conf_lock);

	BUG_ON(module_refcount(pfm_pmu_conf->owner));

	if (cfg->owner == pfm_pmu_conf->owner) {
		pfm_sysfs_remove_pmu(pfm_pmu_conf);
		pfm_pmu_conf = NULL;
	}

	spin_unlock(&pfm_pmu_conf_lock);
}
EXPORT_SYMBOL(pfm_pmu_unregister);

static int pfm_pmu_request_module(void)
{
	char *mod_name;
	int ret;

	mod_name = pfm_arch_get_pmu_module_name();
	if (!mod_name)
		return -ENOSYS;

	ret = request_module("%s", mod_name);

	PFM_DBG("mod=%s ret=%d", mod_name, ret);
	return ret;
}

/*
 * autoload:
 * 	0     : do not try to autoload the PMU description module
 * 	not 0 : try to autoload the PMU description module
 */
int pfm_pmu_conf_get(int autoload)
{
	int ret;

	spin_lock(&pfm_pmu_conf_lock);

	if (request_mod_in_progress) {
		ret = -ENOSYS;
		goto skip;
	}

	if (autoload && pfm_pmu_conf == NULL) {

		request_mod_in_progress = 1;

		spin_unlock(&pfm_pmu_conf_lock);

		pfm_pmu_request_module();

		spin_lock(&pfm_pmu_conf_lock);

		request_mod_in_progress = 0;

		/*
		 * request_module() may succeed but the module
		 * may not have registered properly so we need
		 * to check
		 */
	}

	ret = pfm_pmu_conf == NULL ? -ENOSYS : 0;
	if (!ret && pmu_is_module(pfm_pmu_conf)
	    && !try_module_get(pfm_pmu_conf->owner))
		ret = -ENOSYS;

skip:
	spin_unlock(&pfm_pmu_conf_lock);

	return ret;
}

void pfm_pmu_conf_put(void)
{
	if (pfm_pmu_conf == NULL || !pmu_is_module(pfm_pmu_conf))
		return;

	spin_lock(&pfm_pmu_conf_lock);
	module_put(pfm_pmu_conf->owner);
	spin_unlock(&pfm_pmu_conf_lock);
}

/*
 * quiesce the PMU on one CPU
 */
static void __pfm_pmu_quiesce_percpu(void *dummy)
{
	u64 *mask, val;
	u16 num, i;

	mask = pfm_pmu_conf->regs_all.pmcs;
	num = pfm_pmu_conf->regs_all.num_pmcs;

	for (i = 0; num; i++) {
		if (test_bit(i, cast_ulp(mask))) {
			val = pfm_pmu_conf->pmc_desc[i].dfl_val;
			pfm_arch_write_pmc(NULL, i, val);
			num--;
		}
	}
}

/*
 * Quiesce the PMU on all CPUs
 * This is necessary as we have no guarantee the PMU
 * is actually stopped when perfmon gets control
 */
static void pfm_pmu_quiesce(void)
{
	on_each_cpu(__pfm_pmu_quiesce_percpu, NULL, 1);
}

/*
 * acquire PMU resource from lower-level PMU register allocator
 * (currently perfctr-watchdog.c)
 *
 * acquisition is done when the first context is created (and not
 * when it is loaded). We grab all that is defined in the description
 * module and then we make adjustments at the arch-specific level.
 *
 * The PMU resource is released when the last perfmon context is
 * destroyed.
 *
 * interrupts are not masked
 */
int pfm_pmu_acquire(struct pfm_context *ctx)
{
	u64 unavail_pmcs[PFM_PMC_BV];
	u64 unavail_pmds[PFM_PMD_BV];
	int ret = 0;

	spin_lock(&pfm_pmu_acq_lock);

	PFM_DBG("pmu_acquired=%u", pfm_pmu_acquired);

	pfm_pmu_acquired++;

	/*
	 * we need to initialize regdesc each  time we re-acquire
	 * the PMU for the first time as there may have been changes
	 * in the list of available registers, e.g., NMI may have
	 * been disabled. Checking on PMU module insert is not
	 * enough
	 */
	if (pfm_pmu_acquired == 1) {

		memset(unavail_pmcs, 0, sizeof(unavail_pmcs));
		memset(unavail_pmds, 0, sizeof(unavail_pmds));

		/*
		 * gather unavailable registers
		 *
		 * cannot use pfm_pmu_conf->regs_all as it
		 * is not yet initialized
		 */
		ret = pfm_arch_reserve_regs(unavail_pmcs, unavail_pmds);
		if (ret) {
			pfm_pmu_acquired = 0;
		} else {
			pfm_pmu_regdesc_init_all(unavail_pmcs, unavail_pmds);

			/* available PMU ressources */
			PFM_DBG("PMU acquired: %u PMCs, %u PMDs, %u counters",
				pfm_pmu_conf->regs_all.num_pmcs,
				pfm_pmu_conf->regs_all.num_pmds,
				pfm_pmu_conf->regs_all.num_counters);

			ret = pfm_arch_acquire_pmu();
			if (ret) {
				pfm_arch_release_regs();
				pfm_pmu_acquired = 0;
			} else
				pfm_pmu_quiesce();
		}
	}
	spin_unlock(&pfm_pmu_acq_lock);

	/*
	 * copy the regdesc that corresponds to the context
	 * we copy and not just point because it helps with
	 * memory locality. the regdesc structure is accessed
	 * very frequently in performance critical code such
	 * as context switch and interrupt handling. By using
	 * a local copy, we increase memory footprint, but
	 * increase chance to have local memory access,
	 * especially for system-wide contexts.
	 */
	if (!ret) {
		if (ctx->flags.system)
			ctx->regs = pfm_pmu_conf->regs_sys;
		else
			ctx->regs = pfm_pmu_conf->regs_thr;
	}
	return ret;
}

/*
 * release the PMU resource
 *
 * actual release happens when last context is destroyed
 *
 * interrupts are not masked
 */
void pfm_pmu_release(void)
{
	BUG_ON(irqs_disabled());

	/*
	 * we need to use a spinlock because release takes some time
	 * and we may have a race with pfm_pmu_acquire()
	 */
	spin_lock(&pfm_pmu_acq_lock);

	PFM_DBG("pmu_acquired=%d", pfm_pmu_acquired);

	/*
	 * we decouple test and decrement because if we had errors
	 * in pfm_pmu_acquire(), we still come here on pfm_context_free()
	 * but with pfm_pmu_acquire=0
	 */
	if (pfm_pmu_acquired > 0 && --pfm_pmu_acquired == 0) {
		pfm_arch_release_regs();
		pfm_arch_release_pmu();
		PFM_DBG("PMU released");
	}
	spin_unlock(&pfm_pmu_acq_lock);
}
