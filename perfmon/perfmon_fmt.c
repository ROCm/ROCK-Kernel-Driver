/*
 * perfmon_fmt.c: perfmon2 sampling buffer format management
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

static __cacheline_aligned_in_smp DEFINE_SPINLOCK(pfm_smpl_fmt_lock);
static LIST_HEAD(pfm_smpl_fmt_list);

static inline int fmt_is_mod(struct pfm_smpl_fmt *f)
{
	return !(f->fmt_flags & PFM_FMTFL_IS_BUILTIN);
}

static struct pfm_smpl_fmt *pfm_find_fmt(char *name)
{
	struct pfm_smpl_fmt *entry;

	list_for_each_entry(entry, &pfm_smpl_fmt_list, fmt_list) {
		if (!strcmp(entry->fmt_name, name))
			return entry;
	}
	return NULL;
}
/*
 * find a buffer format based on its name
 */
struct pfm_smpl_fmt *pfm_smpl_fmt_get(char *name)
{
	struct pfm_smpl_fmt *fmt;

	spin_lock(&pfm_smpl_fmt_lock);

	fmt = pfm_find_fmt(name);

	/*
	 * increase module refcount
	 */
	if (fmt && fmt_is_mod(fmt) && !try_module_get(fmt->owner))
		fmt = NULL;

	spin_unlock(&pfm_smpl_fmt_lock);

	return fmt;
}

void pfm_smpl_fmt_put(struct pfm_smpl_fmt *fmt)
{
	if (fmt == NULL || !fmt_is_mod(fmt))
		return;
	BUG_ON(fmt->owner == NULL);

	spin_lock(&pfm_smpl_fmt_lock);
	module_put(fmt->owner);
	spin_unlock(&pfm_smpl_fmt_lock);
}

int pfm_fmt_register(struct pfm_smpl_fmt *fmt)
{
	int ret = 0;

	if (perfmon_disabled) {
		PFM_INFO("perfmon disabled, cannot add sampling format");
		return -ENOSYS;
	}

	/* some sanity checks */
	if (fmt == NULL) {
		PFM_INFO("perfmon: NULL format for register");
		return -EINVAL;
	}

	if (fmt->fmt_name == NULL) {
		PFM_INFO("perfmon: format has no name");
		return -EINVAL;
	}

	if (fmt->fmt_qdepth > PFM_MSGS_COUNT) {
		PFM_INFO("perfmon: format %s requires %u msg queue depth (max %d)",
		       fmt->fmt_name,
		       fmt->fmt_qdepth,
		       PFM_MSGS_COUNT);
		return -EINVAL;
	}

	/*
	 * fmt is missing the initialization of .owner = THIS_MODULE
	 * this is only valid when format is compiled as a module
	 */
	if (fmt->owner == NULL && fmt_is_mod(fmt)) {
		PFM_INFO("format %s has no module owner", fmt->fmt_name);
		return -EINVAL;
	}
	/*
	 * we need at least a handler
	 */
	if (fmt->fmt_handler == NULL) {
		PFM_INFO("format %s has no handler", fmt->fmt_name);
		return -EINVAL;
	}

	/*
	 * format argument size cannot be bigger than PAGE_SIZE
	 */
	if (fmt->fmt_arg_size > PAGE_SIZE) {
		PFM_INFO("format %s arguments too big", fmt->fmt_name);
		return -EINVAL;
	}

	spin_lock(&pfm_smpl_fmt_lock);

	/*
	 * because of sysfs, we cannot have two formats with the same name
	 */
	if (pfm_find_fmt(fmt->fmt_name)) {
		PFM_INFO("format %s already registered", fmt->fmt_name);
		ret = -EBUSY;
		goto out;
	}

	ret = pfm_sysfs_add_fmt(fmt);
	if (ret) {
		PFM_INFO("sysfs cannot add format entry for %s", fmt->fmt_name);
		goto out;
	}

	list_add(&fmt->fmt_list, &pfm_smpl_fmt_list);

	PFM_INFO("added sampling format %s", fmt->fmt_name);
out:
	spin_unlock(&pfm_smpl_fmt_lock);

	return ret;
}
EXPORT_SYMBOL(pfm_fmt_register);

int pfm_fmt_unregister(struct pfm_smpl_fmt *fmt)
{
	struct pfm_smpl_fmt *fmt2;
	int ret = 0;

	if (!fmt || !fmt->fmt_name) {
		PFM_DBG("invalid fmt");
		return -EINVAL;
	}

	spin_lock(&pfm_smpl_fmt_lock);

	fmt2 = pfm_find_fmt(fmt->fmt_name);
	if (!fmt) {
		PFM_INFO("unregister failed, format not registered");
		ret = -EINVAL;
		goto out;
	}
	list_del_init(&fmt->fmt_list);

	pfm_sysfs_remove_fmt(fmt);

	PFM_INFO("removed sampling format: %s", fmt->fmt_name);

out:
	spin_unlock(&pfm_smpl_fmt_lock);
	return ret;

}
EXPORT_SYMBOL(pfm_fmt_unregister);

/*
 * we defer adding the builtin formats to /sys/kernel/perfmon/formats
 * until after the pfm sysfs subsystem is initialized. This function
 * is called from pfm_init_sysfs()
 */
void __init pfm_sysfs_builtin_fmt_add(void)
{
	struct pfm_smpl_fmt *entry;

	/*
	 * locking not needed, kernel not fully booted
	 * when called
	 */
	list_for_each_entry(entry, &pfm_smpl_fmt_list, fmt_list) {
		pfm_sysfs_add_fmt(entry);
	}
}
