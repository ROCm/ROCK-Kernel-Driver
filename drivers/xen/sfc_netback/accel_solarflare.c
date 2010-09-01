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

#include "common.h"

#include "accel.h"
#include "accel_solarflare.h"
#include "accel_msg_iface.h"
#include "accel_util.h"

#include "accel_cuckoo_hash.h"

#include "ci/driver/resource/efx_vi.h"

#include "ci/efrm/nic_table.h" 
#include "ci/efhw/public.h"

#include <xen/evtchn.h>
#include <xen/driver_util.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "driverlink_api.h"

#define SF_XEN_RX_USR_BUF_SIZE 2048

struct falcon_bend_accel_priv {
	struct efx_vi_state *efx_vih;

	/*! Array of pointers to dma_map state, used so VNIC can
	 *  request their removal in a single message
	 */
	struct efx_vi_dma_map_state **dma_maps;
	/*! Index into dma_maps */
	int dma_maps_index; 

	/*! Serialises access to filters */
	spinlock_t filter_lock;	     
	/*! Bitmap of which filters are free */
	unsigned long free_filters;	 
	/*! Used for index normalisation */
	u32 filter_idx_mask;		
	struct netback_accel_filter_spec *fspecs; 
	cuckoo_hash_table filter_hash_table;

	u32 txdmaq_gnt;
	u32 rxdmaq_gnt;
	u32 doorbell_gnt;
	u32 evq_rptr_gnt;
	u32 evq_mem_gnts[EF_HW_FALCON_EVQ_PAGES];
	u32 evq_npages;
};

/* Forward declaration */
static int netback_accel_filter_init(struct netback_accel *);
static void netback_accel_filter_shutdown(struct netback_accel *);

/**************************************************************************
 * 
 * Driverlink stuff
 *
 **************************************************************************/

struct driverlink_port {
	struct list_head link;
	enum net_accel_hw_type type;
	struct net_device *net_dev;
	struct efx_dl_device *efx_dl_dev;
	void *fwd_priv;
};

static struct list_head dl_ports;

/* This mutex protects global state, such as the dl_ports list */
DEFINE_MUTEX(accel_mutex);

static int init_done = 0;

/* The DL callbacks */


#if defined(EFX_USE_FASTCALL)
static enum efx_veto fastcall
#else
static enum efx_veto
#endif
bend_dl_tx_packet(struct efx_dl_device *efx_dl_dev,
		  struct sk_buff *skb)
{
	struct driverlink_port *port = efx_dl_dev->priv;

	BUG_ON(port == NULL);

	NETBACK_ACCEL_STATS_OP(global_stats.dl_tx_packets++);
	if (skb_mac_header_was_set(skb))
		netback_accel_tx_packet(skb, port->fwd_priv);
	else {
		DPRINTK("Ignoring packet with missing mac address\n");
		NETBACK_ACCEL_STATS_OP(global_stats.dl_tx_bad_packets++);
	}
	return EFX_ALLOW_PACKET;
}

/* EFX_USE_FASTCALL */
#if defined(EFX_USE_FASTCALL)
static enum efx_veto fastcall
#else
static enum efx_veto
#endif
bend_dl_rx_packet(struct efx_dl_device *efx_dl_dev,
		  const char *pkt_buf, int pkt_len)
{
	struct driverlink_port *port = efx_dl_dev->priv;
	struct netback_pkt_buf pkt;
	struct ethhdr *eh;

	BUG_ON(port == NULL);

	pkt.mac.raw = (char *)pkt_buf;
	pkt.nh.raw = (char *)pkt_buf + ETH_HLEN;
	eh = (struct ethhdr *)pkt_buf;
	pkt.protocol = eh->h_proto;

	NETBACK_ACCEL_STATS_OP(global_stats.dl_rx_packets++);
	netback_accel_rx_packet(&pkt, port->fwd_priv);
	return EFX_ALLOW_PACKET;
}


/* Callbacks we'd like to get from the netdriver through driverlink */
struct efx_dl_callbacks bend_dl_callbacks =
	{
		.tx_packet = bend_dl_tx_packet,
		.rx_packet = bend_dl_rx_packet,
	};


static struct netback_accel_hooks accel_hooks = {
	THIS_MODULE,
	&netback_accel_probe,
	&netback_accel_remove
};


/* Driver link probe - register our callbacks */
static int bend_dl_probe(struct efx_dl_device *efx_dl_dev,
			 const struct net_device *net_dev,
			 const struct efx_dl_device_info *dev_info,
			 const char* silicon_rev)
{
	int rc;
	enum net_accel_hw_type type;
	struct driverlink_port *port;

	DPRINTK("%s: %s\n", __FUNCTION__, silicon_rev);

	if (strcmp(silicon_rev, "falcon/a1") == 0)
		type = NET_ACCEL_MSG_HWTYPE_FALCON_A;
	else if (strcmp(silicon_rev, "falcon/b0") == 0)
		type = NET_ACCEL_MSG_HWTYPE_FALCON_B;
	else if (strcmp(silicon_rev, "siena/a0") == 0)
		type = NET_ACCEL_MSG_HWTYPE_SIENA_A;
	else {
		EPRINTK("%s: unsupported silicon %s\n", __FUNCTION__,
			silicon_rev);
		rc = -EINVAL;
		goto fail1;
	}
	
	port = kmalloc(sizeof(struct driverlink_port), GFP_KERNEL);
	if (port == NULL) {
		EPRINTK("%s: no memory for dl probe\n", __FUNCTION__);
		rc = -ENOMEM;
		goto fail1;
	}

	port->efx_dl_dev = efx_dl_dev;
	efx_dl_dev->priv = port;

	port->fwd_priv = netback_accel_init_fwd_port();
	if (port->fwd_priv == NULL) {
		EPRINTK("%s: failed to set up forwarding for port\n",
			__FUNCTION__);
		rc = -ENOMEM;
		goto fail2;
	}

	rc = efx_dl_register_callbacks(efx_dl_dev, &bend_dl_callbacks);
	if (rc != 0) {
		EPRINTK("%s: register_callbacks failed\n", __FUNCTION__);
		goto fail3;
	}

	port->type = type;
	port->net_dev = (struct net_device *)net_dev;

	mutex_lock(&accel_mutex);
	list_add(&port->link, &dl_ports);
	mutex_unlock(&accel_mutex);

	rc = netback_connect_accelerator(NETBACK_ACCEL_VERSION, 0,
					 port->net_dev->name, &accel_hooks);

	if (rc < 0) {
		EPRINTK("Xen netback accelerator version mismatch\n");
		goto fail4;
	} else if (rc > 0) {
		/*
		 * In future may want to add backwards compatibility
		 * and accept certain subsets of previous versions
		 */
		EPRINTK("Xen netback accelerator version mismatch\n");
		goto fail4;
	} 

	return 0;

 fail4:
	mutex_lock(&accel_mutex);
	list_del(&port->link);
	mutex_unlock(&accel_mutex);

	efx_dl_unregister_callbacks(efx_dl_dev, &bend_dl_callbacks);
 fail3: 
	netback_accel_shutdown_fwd_port(port->fwd_priv);
 fail2:
	efx_dl_dev->priv = NULL;
	kfree(port);
 fail1:
	return rc;
}


static void bend_dl_remove(struct efx_dl_device *efx_dl_dev)
{
	struct driverlink_port *port;

	DPRINTK("Unregistering driverlink callbacks.\n");

	mutex_lock(&accel_mutex);

	port = (struct driverlink_port *)efx_dl_dev->priv;

	BUG_ON(list_empty(&dl_ports));
	BUG_ON(port == NULL);
	BUG_ON(port->efx_dl_dev != efx_dl_dev);

	netback_disconnect_accelerator(0, port->net_dev->name);

	list_del(&port->link);

	mutex_unlock(&accel_mutex);

	efx_dl_unregister_callbacks(efx_dl_dev, &bend_dl_callbacks);
	netback_accel_shutdown_fwd_port(port->fwd_priv);

	efx_dl_dev->priv = NULL;
	kfree(port);

	return;
}


static void bend_dl_reset_suspend(struct efx_dl_device *efx_dl_dev)
{
	struct driverlink_port *port;

	DPRINTK("Driverlink reset suspend.\n");

	mutex_lock(&accel_mutex);

	port = (struct driverlink_port *)efx_dl_dev->priv;
	BUG_ON(list_empty(&dl_ports));
	BUG_ON(port == NULL);
	BUG_ON(port->efx_dl_dev != efx_dl_dev);

	netback_disconnect_accelerator(0, port->net_dev->name);
	mutex_unlock(&accel_mutex);
}


static void bend_dl_reset_resume(struct efx_dl_device *efx_dl_dev, int ok)
{
	int rc;
	struct driverlink_port *port;

	DPRINTK("Driverlink reset resume.\n");
	
	if (!ok)
		return;

	port = (struct driverlink_port *)efx_dl_dev->priv;
	BUG_ON(list_empty(&dl_ports));
	BUG_ON(port == NULL);
	BUG_ON(port->efx_dl_dev != efx_dl_dev);

	rc = netback_connect_accelerator(NETBACK_ACCEL_VERSION, 0,
					 port->net_dev->name, &accel_hooks);
	if (rc != 0) {
		EPRINTK("Xen netback accelerator version mismatch\n");

		mutex_lock(&accel_mutex);
		list_del(&port->link);
		mutex_unlock(&accel_mutex);

		efx_dl_unregister_callbacks(efx_dl_dev, &bend_dl_callbacks);

		netback_accel_shutdown_fwd_port(port->fwd_priv);

		efx_dl_dev->priv = NULL;
		kfree(port);
	}
}


static struct efx_dl_driver bend_dl_driver = 
	{
		.name = "SFC Xen backend",
		.probe = bend_dl_probe,
		.remove = bend_dl_remove,
		.reset_suspend = bend_dl_reset_suspend,
		.reset_resume = bend_dl_reset_resume
	};


int netback_accel_sf_init(void)
{
	int rc, nic_i;
	struct efhw_nic *nic;

	INIT_LIST_HEAD(&dl_ports);

	rc = efx_dl_register_driver(&bend_dl_driver);
	/* If we couldn't find the NET driver, give up */
	if (rc == -ENOENT)
		return rc;
	
	if (rc == 0) {
		EFRM_FOR_EACH_NIC(nic_i, nic)
			falcon_nic_set_rx_usr_buf_size(nic, 
						       SF_XEN_RX_USR_BUF_SIZE);
	}

	init_done = (rc == 0);
	return rc;
}


void netback_accel_sf_shutdown(void)
{
	if (!init_done)
		return;
	DPRINTK("Unregistering driverlink driver\n");

	/*
	 * This will trigger removal callbacks for all the devices, which
	 * will unregister their callbacks, disconnect from netfront, etc.
	 */
	efx_dl_unregister_driver(&bend_dl_driver);
}


int netback_accel_sf_hwtype(struct netback_accel *bend)
{
	struct driverlink_port *port;

	mutex_lock(&accel_mutex);

	list_for_each_entry(port, &dl_ports, link) {
		if (strcmp(bend->nicname, port->net_dev->name) == 0) {
			bend->hw_type = port->type;
			bend->accel_setup = netback_accel_setup_vnic_hw;
			bend->accel_shutdown = netback_accel_shutdown_vnic_hw;
			bend->fwd_priv = port->fwd_priv;
			bend->net_dev = port->net_dev;
			mutex_unlock(&accel_mutex);
			return 0;
		}
	}

	mutex_unlock(&accel_mutex);

	EPRINTK("Failed to identify backend device '%s' with a NIC\n",
		bend->nicname);

	return -ENOENT;
}


/****************************************************************************
 * Resource management code
 ***************************************************************************/

static int alloc_page_state(struct netback_accel *bend, int max_pages)
{
	struct falcon_bend_accel_priv *accel_hw_priv;

	if (max_pages < 0 || max_pages > bend->quotas.max_buf_pages) {
		EPRINTK("%s: invalid max_pages: %d\n", __FUNCTION__, max_pages);
		return -EINVAL;
	}

	accel_hw_priv = kzalloc(sizeof(struct falcon_bend_accel_priv),
				GFP_KERNEL);
	if (accel_hw_priv == NULL) {
		EPRINTK("%s: no memory for accel_hw_priv\n", __FUNCTION__);
		return -ENOMEM;
	}

	accel_hw_priv->dma_maps = kzalloc
		(sizeof(struct efx_vi_dma_map_state **) * 
		 (max_pages / NET_ACCEL_MSG_MAX_PAGE_REQ), GFP_KERNEL);
	if (accel_hw_priv->dma_maps == NULL) {
		EPRINTK("%s: no memory for dma_maps\n", __FUNCTION__);
		kfree(accel_hw_priv);
		return -ENOMEM;
	}

	bend->buffer_maps = kzalloc(sizeof(struct vm_struct *) * max_pages, 
				    GFP_KERNEL);
	if (bend->buffer_maps == NULL) {
		EPRINTK("%s: no memory for buffer_maps\n", __FUNCTION__);
		kfree(accel_hw_priv->dma_maps);
		kfree(accel_hw_priv);
		return -ENOMEM;
	}

	bend->buffer_addrs = kzalloc(sizeof(u64) * max_pages, GFP_KERNEL);
	if (bend->buffer_addrs == NULL) {
		kfree(bend->buffer_maps);
		kfree(accel_hw_priv->dma_maps);
		kfree(accel_hw_priv);
		return -ENOMEM;
	}

	bend->accel_hw_priv = accel_hw_priv;

	return 0;
}


static int free_page_state(struct netback_accel *bend)
{
	struct falcon_bend_accel_priv *accel_hw_priv;

	DPRINTK("%s: %p\n", __FUNCTION__, bend);

	accel_hw_priv = bend->accel_hw_priv;

	if (accel_hw_priv) {
		kfree(accel_hw_priv->dma_maps);
		kfree(bend->buffer_maps);
		kfree(bend->buffer_addrs);
		kfree(accel_hw_priv);
		bend->accel_hw_priv = NULL;
		bend->max_pages = 0;
	}

	return 0;
}


/* The timeout event callback for the event q */
static void bend_evq_timeout(void *context, int is_timeout)
{
	struct netback_accel *bend = (struct netback_accel *)context;
	if (is_timeout) {
		/* Pass event to vnic front end driver */
		VPRINTK("timeout event to %d\n", bend->net_channel);
		NETBACK_ACCEL_STATS_OP(bend->stats.evq_timeouts++);
		notify_remote_via_irq(bend->net_channel_irq);
	} else {
		/* It's a wakeup event, used by Falcon */
		VPRINTK("wakeup to %d\n", bend->net_channel);
		NETBACK_ACCEL_STATS_OP(bend->stats.evq_wakeups++);
		notify_remote_via_irq(bend->net_channel_irq);
	}
}


/*
 * Create the eventq and associated gubbins for communication with the
 * front end vnic driver
 */
static int ef_get_vnic(struct netback_accel *bend)
{
	struct falcon_bend_accel_priv *accel_hw_priv;
	int rc = 0;

	BUG_ON(bend->hw_state != NETBACK_ACCEL_RES_NONE);

	/* Allocate page related state and accel_hw_priv */
	rc = alloc_page_state(bend, bend->max_pages);
	if (rc != 0) {
		EPRINTK("Failed to allocate page state: %d\n", rc);
		return rc;
	}

	accel_hw_priv = bend->accel_hw_priv;

	rc = efx_vi_alloc(&accel_hw_priv->efx_vih, bend->net_dev->ifindex);
	if (rc != 0) {
		EPRINTK("%s: efx_vi_alloc failed %d\n", __FUNCTION__, rc);
		free_page_state(bend);
		return rc;
	}

	rc = efx_vi_eventq_register_callback(accel_hw_priv->efx_vih,
					     bend_evq_timeout,
					     bend);
	if (rc != 0) {
		EPRINTK("%s: register_callback failed %d\n", __FUNCTION__, rc);
		efx_vi_free(accel_hw_priv->efx_vih);
		free_page_state(bend);
		return rc;
	}

	bend->hw_state = NETBACK_ACCEL_RES_ALLOC;
	
	return 0;
}


static void ef_free_vnic(struct netback_accel *bend)
{
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;

	BUG_ON(bend->hw_state != NETBACK_ACCEL_RES_ALLOC);

	efx_vi_eventq_kill_callback(accel_hw_priv->efx_vih);

	DPRINTK("Hardware is freeable. Will proceed.\n");

	efx_vi_free(accel_hw_priv->efx_vih);
	accel_hw_priv->efx_vih = NULL;

	VPRINTK("Free page state...\n");
	free_page_state(bend);

	bend->hw_state = NETBACK_ACCEL_RES_NONE;
}


static inline void ungrant_or_crash(grant_ref_t gntref, int domain) {
	if (net_accel_ungrant_page(gntref) == -EBUSY)
		net_accel_shutdown_remote(domain);
}


static void netback_accel_release_hwinfo(struct netback_accel *bend)
{
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;
	int i;

	DPRINTK("Remove dma q grants %d %d\n", accel_hw_priv->txdmaq_gnt,
		accel_hw_priv->rxdmaq_gnt);
	ungrant_or_crash(accel_hw_priv->txdmaq_gnt, bend->far_end);
	ungrant_or_crash(accel_hw_priv->rxdmaq_gnt, bend->far_end);

	DPRINTK("Remove doorbell grant %d\n", accel_hw_priv->doorbell_gnt);
	ungrant_or_crash(accel_hw_priv->doorbell_gnt, bend->far_end);

	if (bend->hw_type == NET_ACCEL_MSG_HWTYPE_FALCON_A) {
		DPRINTK("Remove rptr grant %d\n", accel_hw_priv->evq_rptr_gnt);
		ungrant_or_crash(accel_hw_priv->evq_rptr_gnt, bend->far_end);
	}

	for (i = 0; i < accel_hw_priv->evq_npages; i++) {
		DPRINTK("Remove evq grant %d\n", accel_hw_priv->evq_mem_gnts[i]);
		ungrant_or_crash(accel_hw_priv->evq_mem_gnts[i], bend->far_end);
	}

	bend->hw_state = NETBACK_ACCEL_RES_FILTER;

	return;
}


static int ef_bend_hwinfo_falcon_common(struct netback_accel *bend, 
					struct net_accel_hw_falcon_b *hwinfo)
{
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;
	struct efx_vi_hw_resource_metadata res_mdata;
	struct efx_vi_hw_resource res_array[EFX_VI_HW_RESOURCE_MAXSIZE];
	int rc, len = EFX_VI_HW_RESOURCE_MAXSIZE, i, pfn = 0;
	unsigned long txdmaq_pfn = 0, rxdmaq_pfn = 0;

	rc = efx_vi_hw_resource_get_phys(accel_hw_priv->efx_vih, &res_mdata,
					 res_array, &len);
	if (rc != 0) {
		DPRINTK("%s: resource_get_phys returned %d\n",
			__FUNCTION__, rc);
		return rc;
	}

	hwinfo->nic_arch = res_mdata.nic_arch;
	hwinfo->nic_variant = res_mdata.nic_variant;
	hwinfo->nic_revision = res_mdata.nic_revision;

	hwinfo->evq_order = res_mdata.evq_order;
	hwinfo->evq_offs = res_mdata.evq_offs;
	hwinfo->evq_capacity = res_mdata.evq_capacity;
	hwinfo->instance = res_mdata.instance;
	hwinfo->rx_capacity = res_mdata.rx_capacity;
	hwinfo->tx_capacity = res_mdata.tx_capacity;

	VPRINTK("evq_order %d evq_offs %d evq_cap %d inst %d rx_cap %d tx_cap %d\n",
		hwinfo->evq_order, hwinfo->evq_offs, hwinfo->evq_capacity,
		hwinfo->instance, hwinfo->rx_capacity, hwinfo->tx_capacity);

	for (i = 0; i < len; i++) {
		struct efx_vi_hw_resource *res = &(res_array[i]);
		switch (res->type) {
		case EFX_VI_HW_RESOURCE_TXDMAQ:
			txdmaq_pfn = page_to_pfn(virt_to_page(res->address));
			break;
		case EFX_VI_HW_RESOURCE_RXDMAQ: 
			rxdmaq_pfn = page_to_pfn(virt_to_page(res->address));
			break;
		case EFX_VI_HW_RESOURCE_EVQTIMER:
			break;
		case EFX_VI_HW_RESOURCE_EVQRPTR:
		case EFX_VI_HW_RESOURCE_EVQRPTR_OFFSET:
			hwinfo->evq_rptr = res->address;
			break;
		case EFX_VI_HW_RESOURCE_EVQMEMKVA: 
			accel_hw_priv->evq_npages =  1 << res_mdata.evq_order;
			pfn = page_to_pfn(virt_to_page(res->address));
			break;
		case EFX_VI_HW_RESOURCE_BELLPAGE:
			hwinfo->doorbell_mfn  = res->address;
			break;
		default:
			EPRINTK("%s: Unknown hardware resource type %d\n",
				__FUNCTION__, res->type);
			break;
		}
	}

	VPRINTK("Passing txdmaq page pfn %lx\n", txdmaq_pfn);
	rc = net_accel_grant_page(bend->hdev_data, pfn_to_mfn(txdmaq_pfn), 0);
	if (rc < 0)
		goto fail0;
	accel_hw_priv->txdmaq_gnt = hwinfo->txdmaq_gnt = rc;

	VPRINTK("Passing rxdmaq page pfn %lx\n", rxdmaq_pfn);
	rc = net_accel_grant_page(bend->hdev_data, pfn_to_mfn(rxdmaq_pfn), 0);
	if (rc < 0)
		goto fail1;
	accel_hw_priv->rxdmaq_gnt = hwinfo->rxdmaq_gnt = rc;

	VPRINTK("Passing doorbell page mfn %x\n", hwinfo->doorbell_mfn);
	/* Make the relevant H/W pages mappable by the far end */
	rc = net_accel_grant_page(bend->hdev_data, hwinfo->doorbell_mfn, 1);
	if (rc < 0)
		goto fail2;
	accel_hw_priv->doorbell_gnt = hwinfo->doorbell_gnt = rc;
	
	/* Now do the same for the memory pages */
	/* Convert the page + length we got back for the evq to grants. */
	for (i = 0; i < accel_hw_priv->evq_npages; i++) {
		rc = net_accel_grant_page(bend->hdev_data, pfn_to_mfn(pfn), 0);
		if (rc < 0)
			goto fail3;
		accel_hw_priv->evq_mem_gnts[i] = hwinfo->evq_mem_gnts[i] = rc;

		VPRINTK("Got grant %u for evq pfn %x\n", hwinfo->evq_mem_gnts[i], 
			pfn);
		pfn++;
	}

	return 0;

 fail3:
	for (i = i - 1; i >= 0; i--) {
		ungrant_or_crash(accel_hw_priv->evq_mem_gnts[i], bend->far_end);
	}
	ungrant_or_crash(accel_hw_priv->doorbell_gnt, bend->far_end);
 fail2:
	ungrant_or_crash(accel_hw_priv->rxdmaq_gnt, bend->far_end);
 fail1:
	ungrant_or_crash(accel_hw_priv->txdmaq_gnt, bend->far_end);	
 fail0:
	return rc;
}


static int ef_bend_hwinfo_falcon_a(struct netback_accel *bend, 
				   struct net_accel_hw_falcon_a *hwinfo)
{
	int rc, i;
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;

	if ((rc = ef_bend_hwinfo_falcon_common(bend, &hwinfo->common)) != 0)
		return rc;

	/*
	 * Note that unlike the above, where the message field is the
	 * page number, here evq_rptr is the entire address because
	 * it is currently a pointer into the densely mapped timer page.
	 */
	VPRINTK("Passing evq_rptr pfn %x for rptr %x\n", 
		hwinfo->common.evq_rptr >> PAGE_SHIFT,
		hwinfo->common.evq_rptr);
	rc = net_accel_grant_page(bend->hdev_data, 
				  hwinfo->common.evq_rptr >> PAGE_SHIFT, 0);
	if (rc < 0) {
		/* Undo ef_bend_hwinfo_falcon_common() */
		ungrant_or_crash(accel_hw_priv->txdmaq_gnt, bend->far_end);
		ungrant_or_crash(accel_hw_priv->rxdmaq_gnt, bend->far_end);
		ungrant_or_crash(accel_hw_priv->doorbell_gnt, bend->far_end);
		for (i = 0; i < accel_hw_priv->evq_npages; i++) {
			ungrant_or_crash(accel_hw_priv->evq_mem_gnts[i],
					 bend->far_end);
		}
		return rc;
	}

	accel_hw_priv->evq_rptr_gnt = hwinfo->evq_rptr_gnt = rc;
	VPRINTK("evq_rptr_gnt got %d\n", hwinfo->evq_rptr_gnt);
	
	return 0;
}


static int ef_bend_hwinfo_falcon_b(struct netback_accel *bend, 
				   struct net_accel_hw_falcon_b *hwinfo)
{
	return ef_bend_hwinfo_falcon_common(bend, hwinfo);
}


/*
 * Fill in the message with a description of the hardware resources, based on
 * the H/W type
 */
static int netback_accel_hwinfo(struct netback_accel *bend, 
				struct net_accel_msg_hw *msgvi)
{
	int rc = 0;
	
	BUG_ON(bend->hw_state != NETBACK_ACCEL_RES_FILTER);

	msgvi->type = bend->hw_type;
	switch (bend->hw_type) {
	case NET_ACCEL_MSG_HWTYPE_FALCON_A:
		rc = ef_bend_hwinfo_falcon_a(bend, &msgvi->resources.falcon_a);
		break;
	case NET_ACCEL_MSG_HWTYPE_FALCON_B:
	case NET_ACCEL_MSG_HWTYPE_SIENA_A:
		rc = ef_bend_hwinfo_falcon_b(bend, &msgvi->resources.falcon_b);
		break;
	case NET_ACCEL_MSG_HWTYPE_NONE:
		/* Nothing to do. The slow path should just work. */
		break;
	}

	if (rc == 0)
		bend->hw_state = NETBACK_ACCEL_RES_HWINFO;
		
	return rc;
}


/* Allocate hardware resources and make them available to the client domain */
int netback_accel_setup_vnic_hw(struct netback_accel *bend)
{
	struct net_accel_msg msg;
	int err;

	/* Allocate the event queue, VI and so on. */
	err = ef_get_vnic(bend);
	if (err) {
		EPRINTK("Failed to allocate hardware resource for bend:"
			"error %d\n", err);
		return err;
	}

	/* Set up the filter management */
	err = netback_accel_filter_init(bend);
	if (err) {
		EPRINTK("Filter setup failed, error %d", err);
		ef_free_vnic(bend);
		return err;
	}

	net_accel_msg_init(&msg, NET_ACCEL_MSG_SETHW);

	/*
	 * Extract the low-level hardware info we will actually pass to the
	 * other end, and set up the grants/ioremap permissions needed
	 */
	err = netback_accel_hwinfo(bend, &msg.u.hw);

	if (err != 0) {
		netback_accel_filter_shutdown(bend);
		ef_free_vnic(bend);
		return err;
	}

	/* Send the message, this is a reply to a hello-reply */
	err = net_accel_msg_reply_notify(bend->shared_page, 
					 bend->msg_channel_irq, 
					 &bend->to_domU, &msg);

	/*
	 * The message should succeed as it's logically a reply and we
	 * guarantee space for replies, but a misbehaving frontend
	 * could result in that behaviour, so be tolerant
	 */
	if (err != 0) {
		netback_accel_release_hwinfo(bend);
		netback_accel_filter_shutdown(bend);
		ef_free_vnic(bend);
	}

	return err;
}


/* Free hardware resources  */
void netback_accel_shutdown_vnic_hw(struct netback_accel *bend)
{
	/*
	 * Only try and release resources if accel_hw_priv was setup,
	 * otherwise there is nothing to do as we're on "null-op"
	 * acceleration
	 */
	switch (bend->hw_state) {
	case NETBACK_ACCEL_RES_HWINFO:
		VPRINTK("Release hardware resources\n");
		netback_accel_release_hwinfo(bend);
		/* deliberate drop through */
	case NETBACK_ACCEL_RES_FILTER:		
		VPRINTK("Free filters...\n");
		netback_accel_filter_shutdown(bend);
		/* deliberate drop through */
	case NETBACK_ACCEL_RES_ALLOC:
		VPRINTK("Free vnic...\n");
		ef_free_vnic(bend);
		/* deliberate drop through */
	case NETBACK_ACCEL_RES_NONE:
		break;
	default:
		BUG();
	}
}

/**************************************************************************
 * 
 * Buffer table stuff
 *
 **************************************************************************/

/*
 * Undo any allocation that netback_accel_msg_rx_buffer_map() has made
 * if it fails half way through
 */
static inline void buffer_map_cleanup(struct netback_accel *bend, int i)
{
	while (i > 0) {
		i--;
		bend->buffer_maps_index--;
		net_accel_unmap_device_page(bend->hdev_data, 
					    bend->buffer_maps[bend->buffer_maps_index],
					    bend->buffer_addrs[bend->buffer_maps_index]);
	}
}


int netback_accel_add_buffers(struct netback_accel *bend, int pages, int log2_pages,
			      u32 *grants, u32 *buf_addr_out)
{
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;
	unsigned long long addr_array[NET_ACCEL_MSG_MAX_PAGE_REQ];
	int rc, i, index;
	u64 dev_bus_addr;

	/* Make sure we can't overflow the dma_maps array */
	if (accel_hw_priv->dma_maps_index >= 
	    bend->max_pages / NET_ACCEL_MSG_MAX_PAGE_REQ) {
		EPRINTK("%s: too many buffer table allocations: %d %d\n",
			__FUNCTION__, accel_hw_priv->dma_maps_index, 
			bend->max_pages / NET_ACCEL_MSG_MAX_PAGE_REQ);
		return -EINVAL;
	}

	/* Make sure we can't overflow the buffer_maps array */
	if (bend->buffer_maps_index + pages > bend->max_pages) {
		EPRINTK("%s: too many pages mapped: %d + %d > %d\n", 
			__FUNCTION__, bend->buffer_maps_index,
			pages, bend->max_pages);
		return -EINVAL;
	}

	for (i = 0; i < pages; i++) {
		VPRINTK("%s: mapping page %d\n", __FUNCTION__, i);
		rc = net_accel_map_device_page
			(bend->hdev_data, grants[i],
			 &bend->buffer_maps[bend->buffer_maps_index],
			 &dev_bus_addr);
    
		if (rc != 0) {
			EPRINTK("error in net_accel_map_device_page\n");
			buffer_map_cleanup(bend, i);
			return rc;
		}
		
		bend->buffer_addrs[bend->buffer_maps_index] = dev_bus_addr;

		bend->buffer_maps_index++;

		addr_array[i] = dev_bus_addr;
	}

	VPRINTK("%s: mapping dma addresses to vih %p\n", __FUNCTION__, 
		accel_hw_priv->efx_vih);

	index = accel_hw_priv->dma_maps_index;
	if ((rc = efx_vi_dma_map_addrs(accel_hw_priv->efx_vih, addr_array, pages,
				       &(accel_hw_priv->dma_maps[index]))) < 0) {
		EPRINTK("error in dma_map_pages\n");
		buffer_map_cleanup(bend, i);
		return rc;
	}

	accel_hw_priv->dma_maps_index++;
	NETBACK_ACCEL_STATS_OP(bend->stats.num_buffer_pages += pages);

	//DPRINTK("%s: getting map address\n", __FUNCTION__);

	*buf_addr_out = efx_vi_dma_get_map_addr(accel_hw_priv->efx_vih, 
						accel_hw_priv->dma_maps[index]);

	//DPRINTK("%s: done\n", __FUNCTION__);

	return 0;
}


int netback_accel_remove_buffers(struct netback_accel *bend)
{
	/* Only try to free buffers if accel_hw_priv was setup */
	if (bend->hw_state != NETBACK_ACCEL_RES_NONE) {
		struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;
		int i;

		efx_vi_reset(accel_hw_priv->efx_vih);

		while (accel_hw_priv->dma_maps_index > 0) {
			accel_hw_priv->dma_maps_index--;
			i = accel_hw_priv->dma_maps_index;
			efx_vi_dma_unmap_addrs(accel_hw_priv->efx_vih, 
					       accel_hw_priv->dma_maps[i]);
		}
		
		while (bend->buffer_maps_index > 0) {
			VPRINTK("Unmapping granted buffer %d\n", 
				bend->buffer_maps_index);
			bend->buffer_maps_index--;
			i = bend->buffer_maps_index;
			net_accel_unmap_device_page(bend->hdev_data, 
						    bend->buffer_maps[i],
						    bend->buffer_addrs[i]);
		}

		NETBACK_ACCEL_STATS_OP(bend->stats.num_buffer_pages = 0);
	}

	return 0;
}

/**************************************************************************
 * 
 * Filter stuff
 *
 **************************************************************************/

static int netback_accel_filter_init(struct netback_accel *bend)
{
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;
	int i, rc;

	BUG_ON(bend->hw_state != NETBACK_ACCEL_RES_ALLOC);

	spin_lock_init(&accel_hw_priv->filter_lock);

	if ((rc = cuckoo_hash_init(&accel_hw_priv->filter_hash_table, 
				   5 /* space for 32 filters */, 8)) != 0) {
		EPRINTK("Failed to initialise filter hash table\n");
		return rc;
	}

	accel_hw_priv->fspecs = kzalloc(sizeof(struct netback_accel_filter_spec) *
					bend->quotas.max_filters,
					GFP_KERNEL);

	if (accel_hw_priv->fspecs == NULL) {
		EPRINTK("No memory for filter specs.\n");
		cuckoo_hash_destroy(&accel_hw_priv->filter_hash_table);
		return -ENOMEM;
	}

	for (i = 0; i < bend->quotas.max_filters; i++) {
		accel_hw_priv->free_filters |= (1 << i);
	}

	/* Base mask on highest set bit in max_filters  */
	accel_hw_priv->filter_idx_mask = (1 << fls(bend->quotas.max_filters)) - 1;
	VPRINTK("filter setup: max is %x mask is %x\n",
		bend->quotas.max_filters, accel_hw_priv->filter_idx_mask);

	bend->hw_state = NETBACK_ACCEL_RES_FILTER;

	return 0;
}


static inline void make_filter_key(cuckoo_hash_ip_key *key,  
				   struct netback_accel_filter_spec *filt)

{
	key->local_ip = filt->destip_be;
	key->local_port = filt->destport_be;
	key->proto = filt->proto;
}


static inline 
void netback_accel_free_filter(struct falcon_bend_accel_priv *accel_hw_priv,
			       int filter)
{
	cuckoo_hash_ip_key filter_key;

	if (!(accel_hw_priv->free_filters & (1 << filter))) {
		efx_vi_filter_stop(accel_hw_priv->efx_vih, 
				   accel_hw_priv->fspecs[filter].filter_handle);
		make_filter_key(&filter_key, &(accel_hw_priv->fspecs[filter]));
		if (cuckoo_hash_remove(&accel_hw_priv->filter_hash_table,
				       (cuckoo_hash_key *)&filter_key)) {
			EPRINTK("%s: Couldn't find filter to remove from table\n",
				__FUNCTION__);
			BUG();
		}
	}
}


static void netback_accel_filter_shutdown(struct netback_accel *bend)
{
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;
	int i;
	unsigned long flags;

	BUG_ON(bend->hw_state != NETBACK_ACCEL_RES_FILTER);

	spin_lock_irqsave(&accel_hw_priv->filter_lock, flags);

	BUG_ON(accel_hw_priv->fspecs == NULL);

	for (i = 0; i < bend->quotas.max_filters; i++) {
		netback_accel_free_filter(accel_hw_priv, i);
	}
	
	kfree(accel_hw_priv->fspecs);
	accel_hw_priv->fspecs = NULL;
	accel_hw_priv->free_filters = 0;
	
	cuckoo_hash_destroy(&accel_hw_priv->filter_hash_table);

	spin_unlock_irqrestore(&accel_hw_priv->filter_lock, flags);

	bend->hw_state = NETBACK_ACCEL_RES_ALLOC;
}


/*! Suggest a filter to replace when we want to insert a new one and have
 *  none free.
 */
static unsigned get_victim_filter(struct netback_accel *bend)
{
	/*
	 * We could attempt to get really clever, and may do at some
	 * point, but random replacement is v. cheap and low on
	 * pathological worst cases.
	 */
	unsigned index, cycles;

	rdtscl(cycles);

	/*
	 * Some doubt about the quality of the bottom few bits, so
	 * throw 'em * away
	 */
	index = (cycles >> 4) & ((struct falcon_bend_accel_priv *)
				 bend->accel_hw_priv)->filter_idx_mask;
	/*
	 * We don't enforce that the number of filters is a power of
	 * two, but the masking gets us to within one subtraction of a
	 * valid index
	 */
	if (index >= bend->quotas.max_filters)
		index -= bend->quotas.max_filters;
	DPRINTK("backend %s->%d has no free filters. Filter %d will be evicted\n",
		bend->nicname, bend->far_end, index);
	return index;
}


/* Add a filter for the specified IP/port to the backend */
int 
netback_accel_filter_check_add(struct netback_accel *bend, 
			       struct netback_accel_filter_spec *filt)
{
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;
	struct netback_accel_filter_spec *fs;
	unsigned filter_index;
	unsigned long flags;
	int rc, recycling = 0;
	cuckoo_hash_ip_key filter_key, evict_key;

	BUG_ON(filt->proto != IPPROTO_TCP && filt->proto != IPPROTO_UDP);

	DPRINTK("Will add %s filter for dst ip %08x and dst port %d\n", 
		(filt->proto == IPPROTO_TCP) ? "TCP" : "UDP",
		be32_to_cpu(filt->destip_be), be16_to_cpu(filt->destport_be));

	spin_lock_irqsave(&accel_hw_priv->filter_lock, flags);
	/*
	 * Check to see if we're already filtering this IP address and
	 * port. Happens if you insert a filter mid-stream as there
	 * are many packets backed up to be delivered to dom0 already
	 */
	make_filter_key(&filter_key, filt);
	if (cuckoo_hash_lookup(&accel_hw_priv->filter_hash_table, 
			       (cuckoo_hash_key *)(&filter_key), 
			       &filter_index)) {
		DPRINTK("Found matching filter %d already in table\n", 
			filter_index);
		rc = -1;
		goto out;
	}

	if (accel_hw_priv->free_filters == 0) {
		filter_index = get_victim_filter(bend);
		recycling = 1;
	} else {
		filter_index = __ffs(accel_hw_priv->free_filters);
		clear_bit(filter_index, &accel_hw_priv->free_filters);
	}

	fs = &accel_hw_priv->fspecs[filter_index];

	if (recycling) {
		DPRINTK("Removing filter index %d handle %p\n", filter_index,
			fs->filter_handle);

		if ((rc = efx_vi_filter_stop(accel_hw_priv->efx_vih, 
					     fs->filter_handle)) != 0) {
			EPRINTK("Couldn't clear NIC filter table entry %d\n", rc);
		}

		make_filter_key(&evict_key, fs);
		if (cuckoo_hash_remove(&accel_hw_priv->filter_hash_table,
				       (cuckoo_hash_key *)&evict_key)) {
			EPRINTK("Couldn't find filter to remove from table\n");
			BUG();
		}
		NETBACK_ACCEL_STATS_OP(bend->stats.num_filters--);
	}

	/* Update the filter spec with new details */
	*fs = *filt;

	if ((rc = cuckoo_hash_add(&accel_hw_priv->filter_hash_table, 
				  (cuckoo_hash_key *)&filter_key, filter_index,
				  1)) != 0) {
		EPRINTK("Error (%d) adding filter to table\n", rc);
		accel_hw_priv->free_filters |= (1 << filter_index);
		goto out;
	}

	rc = efx_vi_filter(accel_hw_priv->efx_vih, filt->proto, filt->destip_be,
			   filt->destport_be, 
			   (struct filter_resource_t **)&fs->filter_handle);

	if (rc != 0) {
		EPRINTK("Hardware filter insertion failed. Error %d\n", rc);
		accel_hw_priv->free_filters |= (1 << filter_index);
		cuckoo_hash_remove(&accel_hw_priv->filter_hash_table, 
				   (cuckoo_hash_key *)&filter_key);
		rc = -1;
		goto out;
	}

	NETBACK_ACCEL_STATS_OP(bend->stats.num_filters++);

	VPRINTK("%s: success index %d handle %p\n", __FUNCTION__, filter_index, 
		fs->filter_handle);

	rc = filter_index;
 out:
	spin_unlock_irqrestore(&accel_hw_priv->filter_lock, flags);
	return rc;
}


/* Remove a filter entry for the specific device and IP/port */
static void netback_accel_filter_remove(struct netback_accel *bend, 
					int filter_index)
{
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;

	BUG_ON(accel_hw_priv->free_filters & (1 << filter_index));
	netback_accel_free_filter(accel_hw_priv, filter_index);
	accel_hw_priv->free_filters |= (1 << filter_index);
}


/* Remove a filter entry for the specific device and IP/port */
void netback_accel_filter_remove_spec(struct netback_accel *bend, 
				      struct netback_accel_filter_spec *filt)
{
	struct falcon_bend_accel_priv *accel_hw_priv = bend->accel_hw_priv;
	unsigned filter_found;
	unsigned long flags;
	cuckoo_hash_ip_key filter_key;
	struct netback_accel_filter_spec *fs;

	if (filt->proto == IPPROTO_TCP) {
		DPRINTK("Remove TCP filter for dst ip %08x and dst port %d\n",
			be32_to_cpu(filt->destip_be),
			be16_to_cpu(filt->destport_be));
	} else if (filt->proto == IPPROTO_UDP) {
		DPRINTK("Remove UDP filter for dst ip %08x and dst port %d\n",
			be32_to_cpu(filt->destip_be),
			be16_to_cpu(filt->destport_be));
	} else {
		/*
		 * This could be provoked by an evil frontend, so can't
		 * BUG(), but harmless as it should fail tests below 
		 */
		DPRINTK("Non-TCP/UDP filter dst ip %08x and dst port %d\n",
			be32_to_cpu(filt->destip_be),
			be16_to_cpu(filt->destport_be));
	}

	spin_lock_irqsave(&accel_hw_priv->filter_lock, flags);

	make_filter_key(&filter_key, filt);
	if (!cuckoo_hash_lookup(&accel_hw_priv->filter_hash_table, 
			       (cuckoo_hash_key *)(&filter_key), 
			       &filter_found)) {
		EPRINTK("Couldn't find matching filter already in table\n");
		goto out;
	}
	
	/* Do a full check to make sure we've not had a hash collision */
	fs = &accel_hw_priv->fspecs[filter_found];
	if (fs->destip_be == filt->destip_be &&
	    fs->destport_be == filt->destport_be &&
	    fs->proto == filt->proto &&
	    !memcmp(fs->mac, filt->mac, ETH_ALEN)) {
		netback_accel_filter_remove(bend, filter_found);
	} else {
		EPRINTK("Entry in hash table does not match filter spec\n");
		goto out;
	}

 out:
	spin_unlock_irqrestore(&accel_hw_priv->filter_lock, flags);
}
