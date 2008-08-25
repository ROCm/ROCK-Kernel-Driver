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

void netfront_accel_debugfs_init(void) 
{
#if defined(CONFIG_DEBUG_FS)
	sfc_debugfs_root = debugfs_create_dir(frontend_name, NULL);
#endif
}


void netfront_accel_debugfs_fini(void)
{
#if defined(CONFIG_DEBUG_FS)
	if (sfc_debugfs_root)
		debugfs_remove(sfc_debugfs_root);
#endif
}


int netfront_accel_debugfs_create(netfront_accel_vnic *vnic)
{
#if defined(CONFIG_DEBUG_FS)
	if (sfc_debugfs_root == NULL)
		return -ENOENT;

	vnic->dbfs_dir = debugfs_create_dir(vnic->net_dev->name, 
					    sfc_debugfs_root);
	if (vnic->dbfs_dir == NULL)
		return -ENOMEM;

	vnic->netdev_dbfs.fastpath_rx_pkts = debugfs_create_u32
		("fastpath_rx_pkts", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->netdev_stats.fastpath_rx_pkts);
	vnic->netdev_dbfs.fastpath_rx_bytes = debugfs_create_u32
		("fastpath_rx_bytes", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->netdev_stats.fastpath_rx_bytes);
	vnic->netdev_dbfs.fastpath_rx_errors = debugfs_create_u32
		("fastpath_rx_errors", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->netdev_stats.fastpath_rx_errors);
	vnic->netdev_dbfs.fastpath_tx_pkts = debugfs_create_u32
		("fastpath_tx_pkts", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->netdev_stats.fastpath_tx_pkts);
	vnic->netdev_dbfs.fastpath_tx_bytes = debugfs_create_u32
		("fastpath_tx_bytes", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->netdev_stats.fastpath_tx_bytes);
	vnic->netdev_dbfs.fastpath_tx_errors = debugfs_create_u32
		("fastpath_tx_errors", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->netdev_stats.fastpath_tx_errors);

#if NETFRONT_ACCEL_STATS
	vnic->dbfs.irq_count = debugfs_create_u64
		("irq_count", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.irq_count);
	vnic->dbfs.useless_irq_count = debugfs_create_u64
		("useless_irq_count", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.useless_irq_count);
	vnic->dbfs.poll_schedule_count = debugfs_create_u64
		("poll_schedule_count", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.poll_schedule_count);
	vnic->dbfs.poll_call_count = debugfs_create_u64
		("poll_call_count", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.poll_call_count);
	vnic->dbfs.poll_reschedule_count = debugfs_create_u64
		("poll_reschedule_count", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.poll_reschedule_count);
	vnic->dbfs.queue_stops = debugfs_create_u64
		("queue_stops", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.queue_stops);
	vnic->dbfs.queue_wakes = debugfs_create_u64
		("queue_wakes", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.queue_wakes);
	vnic->dbfs.ssr_bursts = debugfs_create_u64
		("ssr_bursts", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.ssr_bursts);
	vnic->dbfs.ssr_drop_stream = debugfs_create_u64
		("ssr_drop_stream", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.ssr_drop_stream);
	vnic->dbfs.ssr_misorder = debugfs_create_u64
		("ssr_misorder", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.ssr_misorder);
	vnic->dbfs.ssr_slow_start = debugfs_create_u64
		("ssr_slow_start", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.ssr_slow_start);
	vnic->dbfs.ssr_merges = debugfs_create_u64
		("ssr_merges", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.ssr_merges);
	vnic->dbfs.ssr_too_many = debugfs_create_u64
		("ssr_too_many", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.ssr_too_many);
	vnic->dbfs.ssr_new_stream = debugfs_create_u64
		("ssr_new_stream", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.ssr_new_stream);

	vnic->dbfs.fastpath_tx_busy = debugfs_create_u64
		("fastpath_tx_busy", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.fastpath_tx_busy);
	vnic->dbfs.fastpath_tx_completions = debugfs_create_u64
		("fastpath_tx_completions", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.fastpath_tx_completions);
	vnic->dbfs.fastpath_tx_pending_max = debugfs_create_u32
		("fastpath_tx_pending_max", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.fastpath_tx_pending_max);
	vnic->dbfs.event_count = debugfs_create_u64
		("event_count", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.event_count);
	vnic->dbfs.bad_event_count = debugfs_create_u64
		("bad_event_count", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.bad_event_count);
	vnic->dbfs.event_count_since_irq = debugfs_create_u32
		("event_count_since_irq", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.event_count_since_irq);
	vnic->dbfs.events_per_irq_max = debugfs_create_u32
		("events_per_irq_max", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.events_per_irq_max);
	vnic->dbfs.fastpath_frm_trunc = debugfs_create_u64
		("fastpath_frm_trunc", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.fastpath_frm_trunc);
	vnic->dbfs.rx_no_desc_trunc = debugfs_create_u64
		("rx_no_desc_trunc", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.rx_no_desc_trunc);
	vnic->dbfs.events_per_poll_max = debugfs_create_u32
		("events_per_poll_max", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.events_per_poll_max);
	vnic->dbfs.events_per_poll_rx_max = debugfs_create_u32
		("events_per_poll_rx_max", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.events_per_poll_rx_max);
	vnic->dbfs.events_per_poll_tx_max = debugfs_create_u32
		("events_per_poll_tx_max", S_IRUSR | S_IRGRP | S_IROTH,
		 vnic->dbfs_dir, &vnic->stats.events_per_poll_tx_max);
#endif
#endif
	return 0;
}


int netfront_accel_debugfs_remove(netfront_accel_vnic *vnic)
{
#if defined(CONFIG_DEBUG_FS)
	if (vnic->dbfs_dir != NULL) {
		debugfs_remove(vnic->netdev_dbfs.fastpath_rx_pkts);
		debugfs_remove(vnic->netdev_dbfs.fastpath_rx_bytes);
		debugfs_remove(vnic->netdev_dbfs.fastpath_rx_errors);
		debugfs_remove(vnic->netdev_dbfs.fastpath_tx_pkts);
		debugfs_remove(vnic->netdev_dbfs.fastpath_tx_bytes);
		debugfs_remove(vnic->netdev_dbfs.fastpath_tx_errors);
		
#if NETFRONT_ACCEL_STATS
		debugfs_remove(vnic->dbfs.irq_count);
		debugfs_remove(vnic->dbfs.useless_irq_count);
		debugfs_remove(vnic->dbfs.poll_schedule_count);
		debugfs_remove(vnic->dbfs.poll_call_count);
		debugfs_remove(vnic->dbfs.poll_reschedule_count);
		debugfs_remove(vnic->dbfs.queue_stops);
		debugfs_remove(vnic->dbfs.queue_wakes);
		debugfs_remove(vnic->dbfs.ssr_bursts);
		debugfs_remove(vnic->dbfs.ssr_drop_stream);
		debugfs_remove(vnic->dbfs.ssr_misorder);
		debugfs_remove(vnic->dbfs.ssr_slow_start);
		debugfs_remove(vnic->dbfs.ssr_merges);
		debugfs_remove(vnic->dbfs.ssr_too_many);
		debugfs_remove(vnic->dbfs.ssr_new_stream);
		
		debugfs_remove(vnic->dbfs.fastpath_tx_busy);
		debugfs_remove(vnic->dbfs.fastpath_tx_completions);
		debugfs_remove(vnic->dbfs.fastpath_tx_pending_max);
		debugfs_remove(vnic->dbfs.event_count);
		debugfs_remove(vnic->dbfs.bad_event_count);
		debugfs_remove(vnic->dbfs.event_count_since_irq);
		debugfs_remove(vnic->dbfs.events_per_irq_max);
		debugfs_remove(vnic->dbfs.fastpath_frm_trunc);
		debugfs_remove(vnic->dbfs.rx_no_desc_trunc);
		debugfs_remove(vnic->dbfs.events_per_poll_max);
		debugfs_remove(vnic->dbfs.events_per_poll_rx_max);
		debugfs_remove(vnic->dbfs.events_per_poll_tx_max);
#endif
		debugfs_remove(vnic->dbfs_dir);
	}
#endif
	return 0;
}
