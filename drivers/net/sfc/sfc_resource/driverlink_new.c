/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains driverlink code which interacts with the sfc network
 * driver.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
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

#include "linux_resource_internal.h"
#include "driverlink_api.h"
#include "kernel_compat.h"
#include <ci/efhw/falcon.h>

#include <linux/rtnetlink.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>

/* The DL driver and associated calls */
static int efrm_dl_probe(struct efx_dl_device *efrm_dev,
			 const struct net_device *net_dev,
			 const struct efx_dl_device_info *dev_info,
			 const char *silicon_rev);

static void efrm_dl_remove(struct efx_dl_device *efrm_dev);

static void efrm_dl_reset_suspend(struct efx_dl_device *efrm_dev);

static void efrm_dl_reset_resume(struct efx_dl_device *efrm_dev, int ok);

static void efrm_dl_mtu_changed(struct efx_dl_device *, int);
static void efrm_dl_event_falcon(struct efx_dl_device *efx_dev, void *p_event);

static struct efx_dl_driver efrm_dl_driver = {
	.name = "resource",
	.probe = efrm_dl_probe,
	.remove = efrm_dl_remove,
	.reset_suspend = efrm_dl_reset_suspend,
	.reset_resume = efrm_dl_reset_resume
};

static void
init_vi_resource_dimensions(struct vi_resource_dimensions *rd,
			    const struct efx_dl_falcon_resources *res)
{
	rd->evq_timer_min = res->evq_timer_min;
	rd->evq_timer_lim = res->evq_timer_lim;
	rd->evq_int_min = res->evq_int_min;
	rd->evq_int_lim = res->evq_int_lim;
	rd->rxq_min = res->rxq_min;
	rd->rxq_lim = res->rxq_lim;
	rd->txq_min = res->txq_min;
	rd->txq_lim = res->txq_lim;
	EFRM_TRACE
	    ("Using evq_int(%d-%d) evq_timer(%d-%d) RXQ(%d-%d) TXQ(%d-%d)",
	     res->evq_int_min, res->evq_int_lim, res->evq_timer_min,
	     res->evq_timer_lim, res->rxq_min, res->rxq_lim, res->txq_min,
	     res->txq_lim);
}

static int
efrm_dl_probe(struct efx_dl_device *efrm_dev,
	      const struct net_device *net_dev,
	      const struct efx_dl_device_info *dev_info,
	      const char *silicon_rev)
{
	struct vi_resource_dimensions res_dim;
	struct efx_dl_falcon_resources *res;
	struct linux_efhw_nic *lnic;
	struct pci_dev *dev;
	struct efhw_nic *nic;
	unsigned probe_flags = 0;
	int non_irq_evq;
	int rc;

	efrm_dev->priv = NULL;

	efx_dl_search_device_info(dev_info, EFX_DL_FALCON_RESOURCES,
				  struct efx_dl_falcon_resources,
				  hdr, res);

	if (res == NULL) {
		EFRM_ERR("%s: Unable to find falcon driverlink resources",
			 __func__);
		return -EINVAL;
	}

	if (res->flags & EFX_DL_FALCON_USE_MSI)
		probe_flags |= NIC_FLAG_TRY_MSI;

	dev = efrm_dev->pci_dev;
	if (res->flags & EFX_DL_FALCON_DUAL_FUNC) {
		unsigned vendor = dev->vendor;
		EFRM_ASSERT(dev->bus != NULL);
		dev = NULL;

		while ((dev = pci_get_device(vendor, FALCON_S_DEVID, dev))
		       != NULL) {
			EFRM_ASSERT(dev->bus != NULL);
			/* With PCIe (since it's point to point)
			 * the slot ID is usually 0 and
			 * the bus ID changes NIC to NIC, so we really
			 * need to check both. */
			if (PCI_SLOT(dev->devfn) ==
			    PCI_SLOT(efrm_dev->pci_dev->devfn)
			    && dev->bus->number ==
			    efrm_dev->pci_dev->bus->number)
				break;
		}
		if (dev == NULL) {
			EFRM_ERR("%s: Unable to find falcon secondary "
				 "PCI device.", __func__);
			return -ENODEV;
		}
		pci_dev_put(dev);
	}

	init_vi_resource_dimensions(&res_dim, res);

	EFRM_ASSERT(res_dim.evq_timer_lim > res_dim.evq_timer_min);
	res_dim.evq_timer_lim--;
	non_irq_evq = res_dim.evq_timer_lim;

	rc = efrm_nic_add(dev, probe_flags, net_dev->dev_addr, &lnic,
			  res->biu_lock,
			  res->buffer_table_min, res->buffer_table_lim,
			  non_irq_evq, &res_dim);
	if (rc != 0)
		return rc;

	nic = &lnic->efrm_nic.efhw_nic;
	nic->mtu = net_dev->mtu + ETH_HLEN;
	nic->net_driver_dev = efrm_dev;
	nic->ifindex = net_dev->ifindex;
#ifdef CONFIG_NET_NS
	nic->nd_net = net_dev->nd_net;
#endif
	efrm_dev->priv = nic;

	/* Register a callback so we're told when MTU changes.
	 * We dynamically allocate efx_dl_callbacks, because
	 * the callbacks that we want depends on the NIC type.
	 */
	lnic->dl_callbacks =
	    kmalloc(sizeof(struct efx_dl_callbacks), GFP_KERNEL);
	if (!lnic->dl_callbacks) {
		EFRM_ERR("Out of memory (%s)", __func__);
		efrm_nic_del(lnic);
		return -ENOMEM;
	}
	memset(lnic->dl_callbacks, 0, sizeof(*lnic->dl_callbacks));
	lnic->dl_callbacks->mtu_changed = efrm_dl_mtu_changed;

	if ((res->flags & EFX_DL_FALCON_DUAL_FUNC) == 0) {
		/* Net driver receives all management events.
		 * Register a callback to receive the ones
		 * we're interested in. */
		lnic->dl_callbacks->event = efrm_dl_event_falcon;
	}

	rc = efx_dl_register_callbacks(efrm_dev, lnic->dl_callbacks);
	if (rc < 0) {
		EFRM_ERR("%s: efx_dl_register_callbacks failed (%d)",
			 __func__, rc);
		kfree(lnic->dl_callbacks);
		efrm_nic_del(lnic);
		return rc;
	}

	return 0;
}

/* When we unregister ourselves on module removal, this function will be
 * called for all the devices we claimed */
static void efrm_dl_remove(struct efx_dl_device *efrm_dev)
{
	struct efhw_nic *nic = efrm_dev->priv;
	struct linux_efhw_nic *lnic = linux_efhw_nic(nic);
	EFRM_TRACE("%s called", __func__);
	if (lnic->dl_callbacks) {
		efx_dl_unregister_callbacks(efrm_dev, lnic->dl_callbacks);
		kfree(lnic->dl_callbacks);
	}
	if (efrm_dev->priv)
		efrm_nic_del(lnic);
	EFRM_TRACE("%s OK", __func__);
}

static void efrm_dl_reset_suspend(struct efx_dl_device *efrm_dev)
{
	EFRM_NOTICE("%s:", __func__);
}

static void efrm_dl_reset_resume(struct efx_dl_device *efrm_dev, int ok)
{
	EFRM_NOTICE("%s: ok=%d", __func__, ok);
}

int efrm_driverlink_register(void)
{
	EFRM_TRACE("%s:", __func__);
	return efx_dl_register_driver(&efrm_dl_driver);
}

void efrm_driverlink_unregister(void)
{
	EFRM_TRACE("%s:", __func__);
	efx_dl_unregister_driver(&efrm_dl_driver);
}

static void efrm_dl_mtu_changed(struct efx_dl_device *efx_dev, int mtu)
{
	struct efhw_nic *nic = efx_dev->priv;

	ASSERT_RTNL();	/* Since we're looking at efx_dl_device::port_net_dev */

	EFRM_TRACE("%s: old=%d new=%d", __func__, nic->mtu, mtu + ETH_HLEN);
	/* If this happened we must have agreed to it above */
	nic->mtu = mtu + ETH_HLEN;
}

static void efrm_dl_event_falcon(struct efx_dl_device *efx_dev, void *p_event)
{
	struct efhw_nic *nic = efx_dev->priv;
	struct linux_efhw_nic *lnic = linux_efhw_nic(nic);
	efhw_event_t *ev = p_event;

	switch (FALCON_EVENT_CODE(ev)) {
	case FALCON_EVENT_CODE_CHAR:
		falcon_handle_char_event(nic, lnic->ev_handlers, ev);
		break;
	default:
		EFRM_WARN("%s: unknown event type=%x", __func__,
			  (unsigned)FALCON_EVENT_CODE(ev));
		break;
	}
}
