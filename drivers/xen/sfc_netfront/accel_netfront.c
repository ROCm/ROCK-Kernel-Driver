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

#include <linux/skbuff.h>
#include <linux/netdevice.h>

/* drivers/xen/netfront/netfront.h */
#include "netfront.h"

#include "accel.h"
#include "accel_bufs.h"
#include "accel_util.h"
#include "accel_msg_iface.h"
#include "accel_ssr.h"
 
#ifdef EFX_GCOV
#include "gcov.h"
#endif

#define NETFRONT_ACCEL_VNIC_FROM_NETDEV(_nd)				\
	((netfront_accel_vnic *)((struct netfront_info *)netdev_priv(net_dev))->accel_priv)

static int netfront_accel_netdev_start_xmit(struct sk_buff *skb,
					    struct net_device *net_dev)
{
	netfront_accel_vnic *vnic = NETFRONT_ACCEL_VNIC_FROM_NETDEV(net_dev);
	struct netfront_info *np = 
		(struct netfront_info *)netdev_priv(net_dev);
	int handled, rc;
	unsigned long flags1, flags2;

	BUG_ON(vnic == NULL);

	/* Take our tx lock and hold for the duration */
	spin_lock_irqsave(&vnic->tx_lock, flags1);

	if (!vnic->tx_enabled) {
		rc = 0;
		goto unlock_out;
	}

	handled = netfront_accel_vi_tx_post(vnic, skb);
	if (handled == NETFRONT_ACCEL_STATUS_BUSY) {
		BUG_ON(vnic->net_dev != net_dev);
		DPRINTK("%s stopping queue\n", __FUNCTION__);

		/* Need netfront's tx_lock and vnic tx_lock to write tx_skb */
		spin_lock_irqsave(&np->tx_lock, flags2);
		BUG_ON(vnic->tx_skb != NULL);
		vnic->tx_skb = skb;
		netif_stop_queue(net_dev);
		spin_unlock_irqrestore(&np->tx_lock, flags2);

		NETFRONT_ACCEL_STATS_OP(vnic->stats.queue_stops++);
	}

	if (handled == NETFRONT_ACCEL_STATUS_CANT)
		rc = 0;
	else
		rc = 1;

unlock_out:
	spin_unlock_irqrestore(&vnic->tx_lock, flags1);

	return rc;
}


static int netfront_accel_netdev_poll(struct net_device *net_dev, int *budget)
{
	netfront_accel_vnic *vnic = NETFRONT_ACCEL_VNIC_FROM_NETDEV(net_dev);
	int rx_allowed = *budget, rx_done;
	
	BUG_ON(vnic == NULL);

	/* Can check this without lock as modifier excludes polls */ 
	if (!vnic->poll_enabled)
		return 0;

	rx_done = netfront_accel_vi_poll(vnic, rx_allowed);
	*budget -= rx_done;
	
	NETFRONT_ACCEL_STATS_OP(vnic->stats.poll_call_count++);

	VPRINTK("%s: done %d allowed %d\n",
		__FUNCTION__, rx_done, rx_allowed);

	netfront_accel_ssr_end_of_burst(vnic, &vnic->ssr_state);

	if (rx_done < rx_allowed) {
		 return 0; /* Done */
	}
	
	NETFRONT_ACCEL_STATS_OP(vnic->stats.poll_reschedule_count++);

	return 1; /* More to do. */
}


/*
 * Process request from netfront to start napi interrupt
 * mode. (i.e. enable interrupts as it's finished polling)
 */
static int netfront_accel_start_napi_interrupts(struct net_device *net_dev) 
{
	netfront_accel_vnic *vnic = NETFRONT_ACCEL_VNIC_FROM_NETDEV(net_dev);
	unsigned long flags;

	BUG_ON(vnic == NULL);
	
	/*
	 * Can check this without lock as writer excludes poll before
	 * modifying
	 */
	if (!vnic->poll_enabled)
		return 0;

	if (!netfront_accel_vi_enable_interrupts(vnic)) {
		/* 
		 * There was something there, tell caller we had
		 * something to do.
		 */
		return 1;
	}

	spin_lock_irqsave(&vnic->irq_enabled_lock, flags);
	vnic->irq_enabled = 1;
	netfront_accel_enable_net_interrupts(vnic);
	spin_unlock_irqrestore(&vnic->irq_enabled_lock, flags);

	return 0;
}


/*
 * Process request from netfront to stop napi interrupt
 * mode. (i.e. disable interrupts as it's starting to poll 
 */
static void netfront_accel_stop_napi_interrupts(struct net_device *net_dev) 
{
	netfront_accel_vnic *vnic = NETFRONT_ACCEL_VNIC_FROM_NETDEV(net_dev);
	unsigned long flags;

	BUG_ON(vnic == NULL);

	spin_lock_irqsave(&vnic->irq_enabled_lock, flags);

	if (!vnic->poll_enabled) {
		spin_unlock_irqrestore(&vnic->irq_enabled_lock, flags);
		return;
	}

	netfront_accel_disable_net_interrupts(vnic);
	vnic->irq_enabled = 0;
	spin_unlock_irqrestore(&vnic->irq_enabled_lock, flags);
}


static int netfront_accel_check_ready(struct net_device *net_dev)
{
	netfront_accel_vnic *vnic = NETFRONT_ACCEL_VNIC_FROM_NETDEV(net_dev);

	BUG_ON(vnic == NULL);

	/* Read of tx_skb is protected by netfront's tx_lock */ 
	return vnic->tx_skb == NULL;
}


static int netfront_accel_get_stats(struct net_device *net_dev,
				    struct net_device_stats *stats)
{
	netfront_accel_vnic *vnic = NETFRONT_ACCEL_VNIC_FROM_NETDEV(net_dev);
	struct netfront_accel_netdev_stats now;

	BUG_ON(vnic == NULL);

	now.fastpath_rx_pkts   = vnic->netdev_stats.fastpath_rx_pkts;
	now.fastpath_rx_bytes  = vnic->netdev_stats.fastpath_rx_bytes;
	now.fastpath_rx_errors = vnic->netdev_stats.fastpath_rx_errors;
	now.fastpath_tx_pkts   = vnic->netdev_stats.fastpath_tx_pkts;
	now.fastpath_tx_bytes  = vnic->netdev_stats.fastpath_tx_bytes;
	now.fastpath_tx_errors = vnic->netdev_stats.fastpath_tx_errors;
	
	stats->rx_packets += (now.fastpath_rx_pkts - 
			      vnic->stats_last_read.fastpath_rx_pkts);
	stats->rx_bytes   += (now.fastpath_rx_bytes -
			      vnic->stats_last_read.fastpath_rx_bytes);
	stats->rx_errors  += (now.fastpath_rx_errors - 
			      vnic->stats_last_read.fastpath_rx_errors);
	stats->tx_packets += (now.fastpath_tx_pkts - 
			      vnic->stats_last_read.fastpath_tx_pkts);
	stats->tx_bytes   += (now.fastpath_tx_bytes - 
			      vnic->stats_last_read.fastpath_tx_bytes);
	stats->tx_errors  += (now.fastpath_tx_errors - 
			      vnic->stats_last_read.fastpath_tx_errors);
	
	vnic->stats_last_read = now;

	return 0;
}


struct netfront_accel_hooks accel_hooks = {
	.new_device	    = &netfront_accel_probe,
	.remove		= &netfront_accel_remove,
	.netdev_poll	   = &netfront_accel_netdev_poll,
	.start_xmit	    = &netfront_accel_netdev_start_xmit,
	.start_napi_irq	= &netfront_accel_start_napi_interrupts,
	.stop_napi_irq	 = &netfront_accel_stop_napi_interrupts,
	.check_ready	   = &netfront_accel_check_ready,
	.get_stats	     = &netfront_accel_get_stats
};


unsigned sfc_netfront_max_pages = NETFRONT_ACCEL_DEFAULT_BUF_PAGES;
module_param_named (max_pages, sfc_netfront_max_pages, uint, 0644);
MODULE_PARM_DESC(max_pages, "Number of buffer pages to request");

unsigned sfc_netfront_buffer_split = 2;
module_param_named (buffer_split, sfc_netfront_buffer_split, uint, 0644);
MODULE_PARM_DESC(buffer_split, 
		 "Fraction of buffers to use for TX, rest for RX");


const char *frontend_name = "sfc_netfront";

struct workqueue_struct *netfront_accel_workqueue;

static int __init netfront_accel_init(void)
{
	int rc;
#ifdef EFX_GCOV	
	gcov_provider_init(THIS_MODULE);
#endif

	/*
	 * If we're running on dom0, netfront hasn't initialised
	 * itself, so we need to keep away
	 */
	if (is_initial_xendomain())
		return 0;

	if (!is_pow2(sizeof(struct net_accel_msg)))
		EPRINTK("%s: bad structure size\n", __FUNCTION__);

	netfront_accel_workqueue = create_workqueue(frontend_name);

	netfront_accel_debugfs_init();

	rc = netfront_accelerator_loaded(NETFRONT_ACCEL_VERSION,
					 frontend_name, &accel_hooks);

	if (rc < 0) {
		EPRINTK("Xen netfront accelerator version mismatch\n");
		goto fail;
	}

	if (rc > 0) {
		/* 
		 * In future may want to add backwards compatibility
		 * and accept certain subsets of previous versions
		 */
		EPRINTK("Xen netfront accelerator version mismatch\n");
		goto fail;
	}

	return 0;

 fail:
	netfront_accel_debugfs_fini();
	flush_workqueue(netfront_accel_workqueue);
	destroy_workqueue(netfront_accel_workqueue);
#ifdef EFX_GCOV
 	gcov_provider_fini(THIS_MODULE);
#endif
	return -EINVAL;
}
module_init(netfront_accel_init);

static void __exit netfront_accel_exit(void)
{
	if (is_initial_xendomain())
		return;

	DPRINTK("%s: unhooking\n", __FUNCTION__);

	/* Unhook from normal netfront */
	netfront_accelerator_stop(frontend_name);

	DPRINTK("%s: done\n", __FUNCTION__);

	netfront_accel_debugfs_fini();

	flush_workqueue(netfront_accel_workqueue);

	destroy_workqueue(netfront_accel_workqueue);

#ifdef EFX_GCOV
 	gcov_provider_fini(THIS_MODULE);
#endif
	return;
}
module_exit(netfront_accel_exit);

MODULE_LICENSE("GPL");

