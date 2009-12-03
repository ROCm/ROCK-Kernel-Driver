/*
 * perfmon_sysfs.c: perfmon2 sysfs interface
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
#include <linux/module.h> /* for EXPORT_SYMBOL */
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

struct pfm_attribute {
	struct attribute attr;
	ssize_t (*show)(void *, struct pfm_attribute *attr, char *);
	ssize_t (*store)(void *, const char *, size_t);
};
#define to_attr(n) container_of(n, struct pfm_attribute, attr);

#define PFM_RO_ATTR(_name, _show) \
	struct kobj_attribute attr_##_name = __ATTR(_name, 0444, _show, NULL)

#define PFM_RW_ATTR(_name, _show, _store) 			\
	struct kobj_attribute attr_##_name = __ATTR(_name, 0644, _show, _store)

#define PFM_ROS_ATTR(_name, _show) \
	struct pfm_attribute attr_##_name = __ATTR(_name, 0444, _show, NULL)

#define is_attr_name(a, n) (!strcmp((a)->attr.name, n))
int pfm_sysfs_add_pmu(struct pfm_pmu_config *pmu);

static struct kobject *pfm_kernel_kobj, *pfm_fmt_kobj;
static struct kobject *pfm_pmu_kobj;

static ssize_t pfm_regs_attr_show(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct pfm_regmap_desc *reg = to_reg(kobj);
	struct pfm_attribute *attribute = to_attr(attr);
	return attribute->show ? attribute->show(reg, attribute, buf) : -EIO;
}

static ssize_t pfm_fmt_attr_show(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct pfm_smpl_fmt *fmt = to_smpl_fmt(kobj);
	struct pfm_attribute *attribute = to_attr(attr);
	return attribute->show ? attribute->show(fmt, attribute, buf) : -EIO;
}

static struct sysfs_ops pfm_regs_sysfs_ops = {
	.show  = pfm_regs_attr_show
};

static struct sysfs_ops pfm_fmt_sysfs_ops = {
	.show = pfm_fmt_attr_show
};

static struct kobj_type pfm_regs_ktype = {
	.sysfs_ops = &pfm_regs_sysfs_ops,
};

static struct kobj_type pfm_fmt_ktype = {
	.sysfs_ops = &pfm_fmt_sysfs_ops,
};

static ssize_t pfm_controls_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	if (is_attr_name(attr, "version"))
		return snprintf(buf, PAGE_SIZE, "%u.%u\n",  PFM_VERSION_MAJ, PFM_VERSION_MIN);

	if (is_attr_name(attr, "task_sessions_count"))
		return pfm_sysfs_res_show(buf, PAGE_SIZE, 0);

	if (is_attr_name(attr, "debug"))
		return snprintf(buf, PAGE_SIZE, "%d\n", pfm_controls.debug);

	if (is_attr_name(attr, "task_group"))
		return snprintf(buf, PAGE_SIZE, "%d\n", pfm_controls.task_group);

	if (is_attr_name(attr, "mode"))
		return snprintf(buf, PAGE_SIZE, "%d\n", pfm_controls.flags);

	if (is_attr_name(attr, "arg_mem_max"))
		return snprintf(buf, PAGE_SIZE, "%zu\n", pfm_controls.arg_mem_max);
	if (is_attr_name(attr, "sys_sessions_count"))
		return pfm_sysfs_res_show(buf, PAGE_SIZE, 1);

	if (is_attr_name(attr, "smpl_buffer_mem_max"))
		return snprintf(buf, PAGE_SIZE, "%zu\n", pfm_controls.smpl_buffer_mem_max);

	if (is_attr_name(attr, "smpl_buffer_mem_cur"))
		return pfm_sysfs_res_show(buf, PAGE_SIZE, 2);

	if (is_attr_name(attr, "sys_group"))
		return snprintf(buf, PAGE_SIZE, "%d\n", pfm_controls.sys_group);

	/* XXX: could be set to write-only */
	if (is_attr_name(attr, "reset_stats")) {
		buf[0] = '0';
		buf[1] = '\0';
		return strnlen(buf, PAGE_SIZE);
	}
	return 0;
}

static ssize_t pfm_controls_store(struct kobject *kobj, struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	int i;
	size_t d;

	if (sscanf(buf, "%zu", &d) != 1)
		goto skip;

	if (is_attr_name(attr, "debug"))
		pfm_controls.debug = d;

	if (is_attr_name(attr, "task_group"))
		pfm_controls.task_group = d;

	if (is_attr_name(attr, "sys_group"))
		pfm_controls.sys_group = d;

	if (is_attr_name(attr, "mode"))
                pfm_controls.flags = d;

	if (is_attr_name(attr, "arg_mem_max")) {
		/*
		 * we impose a page as the minimum.
		 *
		 * This limit may be smaller than the stack buffer
		 * available and that is fine.
		 */
		if (d >= PAGE_SIZE)
			pfm_controls.arg_mem_max = d;
	}
	if (is_attr_name(attr, "reset_stats")) {
		for_each_online_cpu(i) {
			pfm_reset_stats(i);
		}
	}

	if (is_attr_name(attr, "smpl_buffer_mem_max")) {
		if (d >= PAGE_SIZE)
			pfm_controls.smpl_buffer_mem_max = d;
	}
skip:
	return count;
}

/*
 * /sys/kernel/perfmon attributes
 */
static PFM_RO_ATTR(version, pfm_controls_show);
static PFM_RO_ATTR(task_sessions_count, pfm_controls_show);
static PFM_RO_ATTR(sys_sessions_count, pfm_controls_show);
static PFM_RO_ATTR(smpl_buffer_mem_cur, pfm_controls_show);

static PFM_RW_ATTR(debug, pfm_controls_show, pfm_controls_store);
static PFM_RW_ATTR(task_group, pfm_controls_show, pfm_controls_store);
static PFM_RW_ATTR(mode, pfm_controls_show, pfm_controls_store);
static PFM_RW_ATTR(sys_group, pfm_controls_show, pfm_controls_store);
static PFM_RW_ATTR(arg_mem_max, pfm_controls_show, pfm_controls_store);
static PFM_RW_ATTR(smpl_buffer_mem_max, pfm_controls_show, pfm_controls_store);
static PFM_RW_ATTR(reset_stats, pfm_controls_show, pfm_controls_store);

static struct attribute *pfm_kernel_attrs[] = {
	&attr_version.attr,
	&attr_task_sessions_count.attr,
	&attr_sys_sessions_count.attr,
	&attr_smpl_buffer_mem_cur.attr,
	&attr_debug.attr,
	&attr_reset_stats.attr,
	&attr_sys_group.attr,
	&attr_task_group.attr,
        &attr_mode.attr,
	&attr_smpl_buffer_mem_max.attr,
	&attr_arg_mem_max.attr,
	NULL
};

static struct attribute_group pfm_kernel_attr_group = {
	.attrs = pfm_kernel_attrs,
};

/*
 * per-reg attributes
 */
static ssize_t pfm_reg_show(void *data, struct pfm_attribute *attr, char *buf)
{
	struct pfm_regmap_desc *reg;
	int w;

	reg = data;

	if (is_attr_name(attr, "name"))
		return snprintf(buf, PAGE_SIZE, "%s\n", reg->desc);

	if (is_attr_name(attr, "dfl_val"))
		return snprintf(buf, PAGE_SIZE, "0x%llx\n",
				(unsigned long long)reg->dfl_val);

	if (is_attr_name(attr, "width")) {
		w = (reg->type & PFM_REG_C64) ?
		    pfm_pmu_conf->counter_width : 64;
		return snprintf(buf, PAGE_SIZE, "%d\n", w);
	}

	if (is_attr_name(attr, "rsvd_msk"))
		return snprintf(buf, PAGE_SIZE, "0x%llx\n",
				(unsigned long long)reg->rsvd_msk);

	if (is_attr_name(attr, "addr"))
		return snprintf(buf, PAGE_SIZE, "0x%lx\n", reg->hw_addr);

	return 0;
}

static PFM_ROS_ATTR(name, pfm_reg_show);
static PFM_ROS_ATTR(dfl_val, pfm_reg_show);
static PFM_ROS_ATTR(rsvd_msk, pfm_reg_show);
static PFM_ROS_ATTR(width, pfm_reg_show);
static PFM_ROS_ATTR(addr, pfm_reg_show);

static struct attribute *pfm_reg_attrs[] = {
	&attr_name.attr,
	&attr_dfl_val.attr,
	&attr_rsvd_msk.attr,
	&attr_width.attr,
	&attr_addr.attr,
	NULL
};

static struct attribute_group pfm_reg_attr_group = {
	.attrs = pfm_reg_attrs,
};

static ssize_t pfm_pmu_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	if (is_attr_name(attr, "model"))
		return snprintf(buf, PAGE_SIZE, "%s\n", pfm_pmu_conf->pmu_name);
	return 0;
}
static PFM_RO_ATTR(model, pfm_pmu_show);

static struct attribute *pfm_pmu_desc_attrs[] = {
	&attr_model.attr,
	NULL
};

static struct attribute_group pfm_pmu_desc_attr_group = {
	.attrs = pfm_pmu_desc_attrs,
};

static int pfm_sysfs_add_pmu_regs(struct pfm_pmu_config *pmu)
{
	struct pfm_regmap_desc *reg;
	unsigned int i, k;
	int ret;

	reg = pmu->pmc_desc;
	for (i = 0; i < pmu->num_pmc_entries; i++, reg++) {

		if (!(reg->type & PFM_REG_I))
			continue;

		ret = kobject_init_and_add(&reg->kobj, &pfm_regs_ktype,
					   pfm_pmu_kobj, "pmc%u", i);
		if (ret)
			goto undo_pmcs;

		ret = sysfs_create_group(&reg->kobj, &pfm_reg_attr_group);
		if (ret) {
			kobject_del(&reg->kobj);
			goto undo_pmcs;
		}
	}

	reg = pmu->pmd_desc;
	for (i = 0; i < pmu->num_pmd_entries; i++, reg++) {

		if (!(reg->type & PFM_REG_I))
			continue;

		ret = kobject_init_and_add(&reg->kobj, &pfm_regs_ktype,
					   pfm_pmu_kobj, "pmd%u", i);
		if (ret)
			goto undo_pmds;

		ret = sysfs_create_group(&reg->kobj, &pfm_reg_attr_group);
		if (ret) {
			kobject_del(&reg->kobj);
			goto undo_pmds;
		}
	}
	return 0;
undo_pmds:
	reg = pmu->pmd_desc;
	for (k = 0; k < i; k++, reg++) {
		if (!(reg->type & PFM_REG_I))
			continue;
		sysfs_remove_group(&reg->kobj, &pfm_reg_attr_group);
		kobject_del(&reg->kobj);
	}
	i = pmu->num_pmc_entries;
	/* fall through */
undo_pmcs:
	reg = pmu->pmc_desc;
	for (k = 0; k < i; k++, reg++) {
		if (!(reg->type & PFM_REG_I))
			continue;
		sysfs_remove_group(&reg->kobj, &pfm_reg_attr_group);
		kobject_del(&reg->kobj);
	}
	return ret;
}

static int pfm_sysfs_del_pmu_regs(struct pfm_pmu_config *pmu)
{
	struct pfm_regmap_desc *reg;
	unsigned int i;

	reg = pmu->pmc_desc;
	for (i = 0; i < pmu->num_pmc_entries; i++, reg++) {

		if (!(reg->type & PFM_REG_I))
			continue;

		sysfs_remove_group(&reg->kobj, &pfm_reg_attr_group);
		kobject_del(&reg->kobj);
	}

	reg = pmu->pmd_desc;
	for (i = 0; i < pmu->num_pmd_entries; i++, reg++) {

		if (!(reg->type & PFM_REG_I))
			continue;

		sysfs_remove_group(&reg->kobj, &pfm_reg_attr_group);
		kobject_del(&reg->kobj);
	}
	return 0;
}

/*
 * when a PMU description module is inserted, we create
 * a pmu_desc subdir in sysfs and we populate it with
 * PMU specific information, such as register mappings
 */
int pfm_sysfs_add_pmu(struct pfm_pmu_config *pmu)
{
	int ret;

	pfm_pmu_kobj = kobject_create_and_add("pmu_desc", pfm_kernel_kobj);
	if (!pfm_pmu_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(pfm_pmu_kobj, &pfm_pmu_desc_attr_group);
	if (ret) {
		/* will release pfm_pmu_kobj */
		kobject_put(pfm_pmu_kobj);
		return ret;
	}

	ret = pfm_sysfs_add_pmu_regs(pmu);
	if (ret) {
		sysfs_remove_group(pfm_pmu_kobj, &pfm_pmu_desc_attr_group);
		/* will release pfm_pmu_kobj */
		kobject_put(pfm_pmu_kobj);
	} else
		kobject_uevent(pfm_pmu_kobj, KOBJ_ADD);

	return ret;
}

/*
 * when a PMU description module is removed, we also remove
 * all its information from sysfs, i.e., the pmu_desc subdir
 * disappears
 */
int pfm_sysfs_remove_pmu(struct pfm_pmu_config *pmu)
{
	pfm_sysfs_del_pmu_regs(pmu);
	sysfs_remove_group(pfm_pmu_kobj, &pfm_pmu_desc_attr_group);
	kobject_uevent(pfm_pmu_kobj, KOBJ_REMOVE);
	kobject_put(pfm_pmu_kobj);
	pfm_pmu_kobj = NULL;
	return 0;
}

static ssize_t pfm_fmt_show(void *data, struct pfm_attribute *attr, char *buf)
{
	struct pfm_smpl_fmt *fmt = data;

	if (is_attr_name(attr, "version"))
		return snprintf(buf, PAGE_SIZE, "%u.%u\n",
			fmt->fmt_version >> 16 & 0xffff,
			fmt->fmt_version & 0xffff);
	return 0;
}

/*
 * do not use predefined macros because of name conflict
 * with /sys/kernel/perfmon/version
 */
struct pfm_attribute attr_fmt_version = {
	.attr	= { .name = "version", .mode = 0444 },
	.show	= pfm_fmt_show,
};

static struct attribute *pfm_fmt_attrs[] = {
	&attr_fmt_version.attr,
	NULL
};

static struct attribute_group pfm_fmt_attr_group = {
	.attrs = pfm_fmt_attrs,
};

/*
 * when a sampling format module is inserted, we populate
 * sysfs with some information
 */
int pfm_sysfs_add_fmt(struct pfm_smpl_fmt *fmt)
{
	int ret;

	ret = kobject_init_and_add(&fmt->kobj, &pfm_fmt_ktype,
				   pfm_fmt_kobj, fmt->fmt_name);
	if (ret)
		return ret;

	ret = sysfs_create_group(&fmt->kobj, &pfm_fmt_attr_group);
	if (ret)
		kobject_del(&fmt->kobj);
	else
		kobject_uevent(&fmt->kobj, KOBJ_ADD);

	return ret;
}

/*
 * when a sampling format module is removed, its information
 * must also be removed from sysfs
 */
void pfm_sysfs_remove_fmt(struct pfm_smpl_fmt *fmt)
{
	sysfs_remove_group(&fmt->kobj, &pfm_fmt_attr_group);
	kobject_uevent(&fmt->kobj, KOBJ_REMOVE);
	kobject_del(&fmt->kobj);
}

int __init pfm_init_sysfs(void)
{
	int ret;

	pfm_kernel_kobj = kobject_create_and_add("perfmon", kernel_kobj);
	if (!pfm_kernel_kobj) {
		PFM_ERR("cannot add kernel object: /sys/kernel/perfmon");
		return -ENOMEM;
	}

	ret = sysfs_create_group(pfm_kernel_kobj, &pfm_kernel_attr_group);
	if (ret) {
		kobject_put(pfm_kernel_kobj);
		return ret;
	}

	pfm_fmt_kobj = kobject_create_and_add("formats", pfm_kernel_kobj);
	if (ret) {
		PFM_ERR("cannot add fmt object: %d", ret);
		goto error_fmt;
	}
	if (pfm_pmu_conf)
		pfm_sysfs_add_pmu(pfm_pmu_conf);

	pfm_sysfs_builtin_fmt_add();

	return 0;

error_fmt:
	kobject_del(pfm_kernel_kobj);
	return ret;
}
