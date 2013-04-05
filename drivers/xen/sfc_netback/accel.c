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

#include "accel.h"
#include "accel_msg_iface.h"
#include "accel_solarflare.h"

#include <linux/notifier.h>

#ifdef EFX_GCOV
#include "gcov.h"
#endif

static int netback_accel_netdev_event(struct notifier_block *nb,
				      unsigned long event, void *ptr)
{
	struct net_device *net_dev = (struct net_device *)ptr;
	struct netback_accel *bend;

	if ((event == NETDEV_UP) || 
	    (event == NETDEV_DOWN) ||
	    (event == NETDEV_CHANGE)) {
		mutex_lock(&bend_list_mutex);
		bend = bend_list;
		while (bend != NULL) {
			mutex_lock(&bend->bend_mutex);
			/*
			 * This happens when the shared pages have
			 * been unmapped, but the bend not yet removed
			 * from list
			 */
			if (bend->shared_page == NULL)
				goto next;

			if (bend->net_dev->ifindex == net_dev->ifindex) {
				int ok;
				if (event == NETDEV_CHANGE)
					ok = (netif_carrier_ok(net_dev) && 
					      (net_dev->flags & IFF_UP));
				else
					ok = (netif_carrier_ok(net_dev) && 
					      (event == NETDEV_UP));
				netback_accel_set_interface_state(bend, ok);
			}

		next:
			mutex_unlock(&bend->bend_mutex);
			bend = bend->next_bend;
		}
		mutex_unlock(&bend_list_mutex);
	}

	return NOTIFY_DONE;
}


static struct notifier_block netback_accel_netdev_notifier = {
	.notifier_call = netback_accel_netdev_event,
};


unsigned sfc_netback_max_pages = NETBACK_ACCEL_DEFAULT_MAX_BUF_PAGES;
module_param_named(max_pages, sfc_netback_max_pages, uint, 0644);
MODULE_PARM_DESC(max_pages, 
		 "The number of buffer pages to enforce on each guest");

/* Initialise subsystems need for the accelerated fast path */
static int __init netback_accel_init(void)
{
	int rc = 0;

#ifdef EFX_GCOV
	gcov_provider_init(THIS_MODULE);
#endif

	rc = netback_accel_init_fwd();
	if (rc != 0)
		goto fail0;

	netback_accel_debugfs_init();

	rc = netback_accel_sf_init();
	if (rc != 0)
		goto fail1;

	rc = register_netdevice_notifier
		(&netback_accel_netdev_notifier);
	if (rc != 0)
		goto fail2;

	return 0;

 fail2:
	netback_accel_sf_shutdown();
 fail1:
	netback_accel_debugfs_fini();
	netback_accel_shutdown_fwd();
 fail0:
#ifdef EFX_GCOV
	gcov_provider_fini(THIS_MODULE);
#endif
	return rc;
}

module_init(netback_accel_init);

static void __exit netback_accel_exit(void)
{
	unregister_netdevice_notifier(&netback_accel_netdev_notifier);

	netback_accel_sf_shutdown();

	netback_accel_shutdown_bends();

	netback_accel_debugfs_fini();

	netback_accel_shutdown_fwd();

#ifdef EFX_GCOV
	gcov_provider_fini(THIS_MODULE);
#endif
}

module_exit(netback_accel_exit);

MODULE_LICENSE("GPL");
