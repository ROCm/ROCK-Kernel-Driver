/*
 * perfmon_debugfs.c: perfmon2 statistics interface to debugfs
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
 * Copyright (c) 2007 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
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
#include <linux/debugfs.h>
#include <linux/perfmon_kern.h>

/*
 * to make the statistics visible to user space:
 * $ mount -t debugfs none /mnt
 * $ cd /mnt/perfmon
 * then choose a CPU subdir
 */
DECLARE_PER_CPU(struct pfm_stats, pfm_stats);

static struct dentry *pfm_debugfs_dir;

void pfm_reset_stats(int cpu)
{
	struct pfm_stats *st;
	unsigned long flags;

	st = &per_cpu(pfm_stats, cpu);

	local_irq_save(flags);
	memset(st->v, 0, sizeof(st->v));
	local_irq_restore(flags);
}

static const char *pfm_stats_strs[] = {
	"ovfl_intr_all_count",
	"ovfl_intr_ns",
	"ovfl_intr_spurious_count",
	"ovfl_intr_replay_count",
	"ovfl_intr_regular_count",
	"handle_work_count",
	"ovfl_notify_count",
	"reset_pmds_count",
	"pfm_restart_count",
	"fmt_handler_calls",
	"fmt_handler_ns",
	"set_switch_count",
	"set_switch_ns",
	"set_switch_exp",
	"ctxswin_count",
	"ctxswin_ns",
	"handle_timeout_count",
	"ovfl_intr_nmi_count",
	"ctxswout_count",
	"ctxswout_ns",
};
#define PFM_NUM_STRS ARRAY_SIZE(pfm_stats_strs)

void pfm_debugfs_del_cpu(int cpu)
{
	struct pfm_stats *st;
	int i;

	st = &per_cpu(pfm_stats, cpu);

	for (i = 0; i < PFM_NUM_STATS; i++) {
		if (st->dirs[i])
			debugfs_remove(st->dirs[i]);
		st->dirs[i] = NULL;
	}
	if (st->cpu_dir)
		debugfs_remove(st->cpu_dir);
	st->cpu_dir = NULL;
}

int pfm_debugfs_add_cpu(int cpu)
{
	struct pfm_stats *st;
	int i;

	/*
	 * sanity check between stats names and the number
	 * of entries in the pfm_stats value array.
	 */
	if (PFM_NUM_STRS != PFM_NUM_STATS) {
		PFM_ERR("PFM_NUM_STRS != PFM_NUM_STATS error");
		return -1;
	}

	st = &per_cpu(pfm_stats, cpu);
	sprintf(st->cpu_name, "cpu%d", cpu);

	st->cpu_dir = debugfs_create_dir(st->cpu_name, pfm_debugfs_dir);
	if (!st->cpu_dir)
		return -1;

	for (i = 0; i < PFM_NUM_STATS; i++) {
		st->dirs[i] = debugfs_create_u64(pfm_stats_strs[i],
						 S_IRUGO,
						 st->cpu_dir,
						 &st->v[i]);
		if (!st->dirs[i])
			goto error;
	}
	pfm_reset_stats(cpu);
	return 0;
error:
	while (i >= 0) {
		debugfs_remove(st->dirs[i]);
		i--;
	}
	debugfs_remove(st->cpu_dir);
	return -1;
}

/*
 * called once from pfm_init()
 */
int __init pfm_init_debugfs(void)
{
	int cpu1, cpu2, ret;

	pfm_debugfs_dir = debugfs_create_dir("perfmon", NULL);
	if (!pfm_debugfs_dir)
		return -1;

	for_each_online_cpu(cpu1) {
		ret = pfm_debugfs_add_cpu(cpu1);
		if (ret)
			goto error;
	}
	return 0;
error:
	for_each_online_cpu(cpu2) {
		if (cpu2 == cpu1)
			break;
		pfm_debugfs_del_cpu(cpu2);
	}
	return -1;
}
