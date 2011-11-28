/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include <linux/fs.h>
#include <linux/debugfs.h>

#include "accel.h"

#if defined(CONFIG_DEBUG_FS)
static struct dentry *sfc_debugfs_root = NULL;
#endif

#if NETBACK_ACCEL_STATS
struct netback_accel_global_stats global_stats;
#if defined(CONFIG_DEBUG_FS)
static struct netback_accel_global_dbfs  global_dbfs;
#endif
#endif

void netback_accel_debugfs_init(void) 
{
#if defined(CONFIG_DEBUG_FS)
	sfc_debugfs_root = debugfs_create_dir("sfc_netback", NULL);
	if (sfc_debugfs_root == NULL)
		return;

	global_dbfs.num_fwds = debugfs_create_u32
		("num_fwds", S_IRUSR | S_IRGRP | S_IROTH,
		 sfc_debugfs_root, &global_stats.num_fwds);
	global_dbfs.dl_tx_packets = debugfs_create_u64
		("dl_tx_packets", S_IRUSR | S_IRGRP | S_IROTH,
		 sfc_debugfs_root, &global_stats.dl_tx_packets);
	global_dbfs.dl_rx_packets = debugfs_create_u64
		("dl_rx_packets", S_IRUSR | S_IRGRP | S_IROTH,
		 sfc_debugfs_root, &global_stats.dl_rx_packets);
	global_dbfs.dl_tx_bad_packets = debugfs_create_u64
		("dl_tx_bad_packets", S_IRUSR | S_IRGRP | S_IROTH,
		 sfc_debugfs_root, &global_stats.dl_tx_bad_packets);
#endif
}


void netback_accel_debugfs_fini(void)
{
#if defined(CONFIG_DEBUG_FS)
	debugfs_remove(global_dbfs.num_fwds);
	debugfs_remove(global_dbfs.dl_tx_packets);
	debugfs_remove(global_dbfs.dl_rx_packets);
	debugfs_remove(global_dbfs.dl_tx_bad_packets);

	debugfs_remove(sfc_debugfs_root);
#endif
}


int netback_accel_debugfs_create(struct netback_accel *bend)
{
#if defined(CONFIG_DEBUG_FS)
	/* Smallest length is 7 (vif0.0\n) */
	int length = 7, temp;

	if (sfc_debugfs_root == NULL)
		return -ENOENT;

	/* Work out length of string representation of far_end and vif_num */
	temp = bend->far_end;
	while (temp > 9) {
		length++;
		temp = temp / 10;
	}
	temp = bend->vif_num;
	while (temp > 9) {
		length++;
		temp = temp / 10;
	}

	bend->dbfs_dir_name = kmalloc(length, GFP_KERNEL);
	if (bend->dbfs_dir_name == NULL)
		return -ENOMEM;
	sprintf(bend->dbfs_dir_name, "vif%d.%d", bend->far_end, bend->vif_num);

	bend->dbfs_dir = debugfs_create_dir(bend->dbfs_dir_name, 
					    sfc_debugfs_root);
	if (bend->dbfs_dir == NULL) {
		kfree(bend->dbfs_dir_name);
		return -ENOMEM;
	}

#if NETBACK_ACCEL_STATS
	bend->dbfs.evq_wakeups = debugfs_create_u64
		("evq_wakeups", S_IRUSR | S_IRGRP | S_IROTH,
		 bend->dbfs_dir, &bend->stats.evq_wakeups);
	bend->dbfs.evq_timeouts = debugfs_create_u64
		("evq_timeouts", S_IRUSR | S_IRGRP | S_IROTH,
		 bend->dbfs_dir, &bend->stats.evq_timeouts);
	bend->dbfs.num_filters = debugfs_create_u32
		("num_filters", S_IRUSR | S_IRGRP | S_IROTH,
		 bend->dbfs_dir, &bend->stats.num_filters);
	bend->dbfs.num_buffer_pages = debugfs_create_u32
		("num_buffer_pages", S_IRUSR | S_IRGRP | S_IROTH,
		 bend->dbfs_dir, &bend->stats.num_buffer_pages);
#endif
#endif
        return 0;
}


int netback_accel_debugfs_remove(struct netback_accel *bend)
{
#if defined(CONFIG_DEBUG_FS)
	if (bend->dbfs_dir != NULL) {
#if NETBACK_ACCEL_STATS
		debugfs_remove(bend->dbfs.evq_wakeups);
		debugfs_remove(bend->dbfs.evq_timeouts);
		debugfs_remove(bend->dbfs.num_filters);
		debugfs_remove(bend->dbfs.num_buffer_pages);
#endif
		debugfs_remove(bend->dbfs_dir);
	}

	if (bend->dbfs_dir_name)
		kfree(bend->dbfs_dir_name);
#endif
        return 0;
}


