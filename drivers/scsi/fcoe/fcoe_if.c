/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

/*
 * FCOE protocol file
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <net/rtnetlink.h>

#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_encaps.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

#include <scsi/libfc/libfc.h>

#include <scsi/fc/fc_fcoe.h>
#include "fcoe_def.h"

#define FCOE_VERSION "0.1"

#define FCOE_MAX_LUN        255
#define FCOE_MAX_FCP_TARGET 256

#define FCOE_MIN_XID		    0x0004
#define FCOE_MAX_XID		    0x07ef

int debug_fcoe;

struct fcoe_info fcoei = {
	.fcoe_hostlist = LIST_HEAD_INIT(fcoei.fcoe_hostlist),
};

static struct fcoe_softc *fcoe_find_fc_lport(const char *name)
{
	struct fcoe_softc *fc;
	struct fc_lport *lp;
	struct fcoe_info *fci = &fcoei;

	read_lock(&fci->fcoe_hostlist_lock);
	list_for_each_entry(fc, &fci->fcoe_hostlist, list) {
		lp = fc->lp;
		if (!strncmp(name, lp->ifname, IFNAMSIZ)) {
			read_unlock(&fci->fcoe_hostlist_lock);
			return fc;
		}
	}
	read_unlock(&fci->fcoe_hostlist_lock);
	return NULL;
}

/*
 * Convert 48-bit IEEE MAC address to 64-bit FC WWN.
 */
static u64 fcoe_wwn_from_mac(unsigned char mac[MAX_ADDR_LEN],
			     unsigned int scheme, unsigned int port)
{
	u64 wwn;
	u64 host_mac;

	/* The MAC is in NO, so flip only the low 48 bits */
	host_mac = ((u64) mac[0] << 40) |
		((u64) mac[1] << 32) |
		((u64) mac[2] << 24) |
		((u64) mac[3] << 16) |
		((u64) mac[4] << 8) |
		(u64) mac[5];

	WARN_ON(host_mac >= (1ULL << 48));
	wwn = host_mac | ((u64) scheme << 60);
	switch (scheme) {
	case 1:
		WARN_ON(port != 0);
		break;
	case 2:
		WARN_ON(port >= 0xfff);
		wwn |= (u64) port << 48;
		break;
	default:
		WARN_ON(1);
		break;
	}

	return wwn;
}

static struct scsi_host_template fcoe_driver_template = {
	.module = THIS_MODULE,
	.name = "FCoE Driver",
	.proc_name = FCOE_DRIVER_NAME,
	.queuecommand = fc_queuecommand,
	.eh_abort_handler = fc_eh_abort,
	.eh_device_reset_handler = fc_eh_device_reset,
	.eh_host_reset_handler = fc_eh_host_reset,
	.slave_alloc = fc_slave_alloc,
	.change_queue_depth = fc_change_queue_depth,
	.change_queue_type = fc_change_queue_type,
	.this_id = -1,
	.cmd_per_lun = 32,
	.can_queue = FC_MAX_OUTSTANDING_COMMANDS,
	.use_clustering = ENABLE_CLUSTERING,
	.sg_tablesize = 4,
	.max_sectors = 0xffff,
};

int fcoe_destroy_interface(const char *ifname)
{
	int cpu, idx;
	struct fcoe_dev_stats *p;
	struct fcoe_percpu_s *pp;
	struct fcoe_softc *fc;
	struct fcoe_rcv_info *fr;
	struct fcoe_info *fci = &fcoei;
	struct sk_buff_head *list;
	struct sk_buff *skb, *next;
	struct sk_buff *head;
	struct fc_lport *lp;
	u8 flogi_maddr[ETH_ALEN];

	fc = fcoe_find_fc_lport(ifname);
	if (!fc)
		return -ENODEV;

	lp = fc->lp;

	/* Remove the instance from fcoe's list */
	write_lock_bh(&fci->fcoe_hostlist_lock);
	list_del(&fc->list);
	write_unlock_bh(&fci->fcoe_hostlist_lock);

	/* Cleanup the fc_lport */
	fc_lport_destroy(lp);
	fc_fcp_destroy(lp);
	if (lp->emp)
		fc_exch_mgr_free(lp->emp);

	/* Detach from the scsi-ml */
	fc_remove_host(lp->host);
	scsi_remove_host(lp->host);

	/* Don't listen for Ethernet packets anymore */
	dev_remove_pack(&fc->fcoe_packet_type);

	/* Delete secondary MAC addresses */
	rtnl_lock();
	memcpy(flogi_maddr, (u8[6]) FC_FCOE_FLOGI_MAC, ETH_ALEN);
	dev_unicast_delete(fc->real_dev, flogi_maddr, ETH_ALEN);
	if (compare_ether_addr(fc->data_src_addr, (u8[6]) { 0 }))
		dev_unicast_delete(fc->real_dev, fc->data_src_addr, ETH_ALEN);
	rtnl_unlock();

	/* Free the per-CPU revieve threads */
	for (idx = 0; idx < NR_CPUS; idx++) {
		if (fci->fcoe_percpu[idx]) {
			pp = fci->fcoe_percpu[idx];
			spin_lock_bh(&pp->fcoe_rx_list.lock);
			list = &pp->fcoe_rx_list;
			head = list->next;
			for (skb = head; skb != (struct sk_buff *)list;
			     skb = next) {
				next = skb->next;
				fr = fcoe_dev_from_skb(skb);
				if (fr->fr_dev == fc->lp) {
					__skb_unlink(skb, list);
					kfree_skb(skb);
				}
			}
			spin_unlock_bh(&pp->fcoe_rx_list.lock);
		}
	}

	/* Free existing skbs */
	fcoe_clean_pending_queue(lp);

	/* Free memory used by statistical counters */
	for_each_online_cpu(cpu) {
		p = lp->dev_stats[cpu];
		if (p) {
			lp->dev_stats[cpu] = NULL;
			kfree(p);
		}
	}

	/* Release the net_device and Scsi_Host */
	dev_put(fc->real_dev);
	scsi_host_put(lp->host);
	return 0;
}

/*
 * Return zero if link is OK for use by FCoE.
 * Any permanently-disqualifying conditions have been previously checked.
 * This also updates the speed setting, which may change with link for 100/1000.
 *
 * This function should probably be checking for PAUSE support at some point
 * in the future. Currently Per-priority-pause is not determinable using
 * ethtool, so we shouldn't be restrictive until that problem is resolved.
 */
int fcoe_link_ok(struct fc_lport *lp)
{
	struct fcoe_softc *fc = (struct fcoe_softc *)lp->drv_priv;
	struct net_device *dev = fc->real_dev;
	struct ethtool_cmd ecmd = { ETHTOOL_GSET };
	int rc = 0;

	if ((dev->flags & IFF_UP) && netif_carrier_ok(dev)) {
		dev = fc->phys_dev;
		if (dev->ethtool_ops->get_settings) {
			dev->ethtool_ops->get_settings(dev, &ecmd);
			lp->link_supported_speeds &=
				~(FC_PORTSPEED_1GBIT | FC_PORTSPEED_10GBIT);
			if (ecmd.supported & (SUPPORTED_1000baseT_Half |
					      SUPPORTED_1000baseT_Full))
				lp->link_supported_speeds |= FC_PORTSPEED_1GBIT;
			if (ecmd.supported & SUPPORTED_10000baseT_Full)
				lp->link_supported_speeds |=
					FC_PORTSPEED_10GBIT;
			if (ecmd.speed == SPEED_1000)
				lp->link_speed = FC_PORTSPEED_1GBIT;
			if (ecmd.speed == SPEED_10000)
				lp->link_speed = FC_PORTSPEED_10GBIT;
		}
	} else
		rc = -1;

	return rc;
}

static struct libfc_function_template fcoe_libfc_fcn_templ = {
	.frame_send = fcoe_xmit,
};

static int lport_config(struct fc_lport *lp, struct Scsi_Host *shost)
{
	int i = 0;
	struct fcoe_dev_stats *p;

	lp->host = shost;
	lp->drv_priv = (void *)(lp + 1);

	lp->emp = fc_exch_mgr_alloc(lp, FC_CLASS_3,
				    FCOE_MIN_XID, FCOE_MAX_XID);
	if (!lp->emp)
		return -ENOMEM;

	lp->link_status = 0;
	lp->max_retry_count = 3;
	lp->e_d_tov = 2 * 1000;	/* FC-FS default */
	lp->r_a_tov = 2 * 2 * 1000;
	lp->service_params = (FCP_SPPF_INIT_FCN | FCP_SPPF_RD_XRDY_DIS |
			      FCP_SPPF_RETRY | FCP_SPPF_CONF_COMPL);

	/*
	 * allocate per cpu stats block
	 */
	for_each_online_cpu(i) {
		p = kzalloc(sizeof(struct fcoe_dev_stats), GFP_KERNEL);
		if (p)
			lp->dev_stats[i] = p;
	}

	/* Finish fc_lport configuration */
	fc_lport_config(lp);

	return 0;
}

static int net_config(struct fc_lport *lp)
{
	u32 mfs;
	u64 wwnn, wwpn;
	struct net_device *net_dev;
	struct fcoe_softc *fc = (struct fcoe_softc *)lp->drv_priv;
	u8 flogi_maddr[ETH_ALEN];

	/* Require support for get_pauseparam ethtool op. */
	net_dev = fc->real_dev;
	if (!net_dev->ethtool_ops && (net_dev->priv_flags & IFF_802_1Q_VLAN))
		net_dev = vlan_dev_real_dev(net_dev);
	if (!net_dev->ethtool_ops || !net_dev->ethtool_ops->get_pauseparam)
		return -EOPNOTSUPP;

	fc->phys_dev = net_dev;

	/* Do not support for bonding device */
	if ((fc->real_dev->priv_flags & IFF_MASTER_ALB) ||
	    (fc->real_dev->priv_flags & IFF_SLAVE_INACTIVE) ||
	    (fc->real_dev->priv_flags & IFF_MASTER_8023AD)) {
		return -EOPNOTSUPP;
	}

	/*
	 * Determine max frame size based on underlying device and optional
	 * user-configured limit.  If the MFS is too low, fcoe_link_ok()
	 * will return 0, so do this first.
	 */
	mfs = fc->real_dev->mtu - (sizeof(struct fcoe_hdr) +
				   sizeof(struct fcoe_crc_eof));
	fc_set_mfs(lp, mfs);

	lp->link_status = ~FC_PAUSE & ~FC_LINK_UP;
	if (!fcoe_link_ok(lp))
		lp->link_status |= FC_LINK_UP;

	if (fc->real_dev->features & NETIF_F_SG)
		lp->capabilities = TRANS_C_SG;


	skb_queue_head_init(&fc->fcoe_pending_queue);

	memcpy(lp->ifname, fc->real_dev->name, IFNAMSIZ);

	/* setup Source Mac Address */
	memcpy(fc->ctl_src_addr, fc->real_dev->dev_addr,
	       fc->real_dev->addr_len);

	wwnn = fcoe_wwn_from_mac(fc->real_dev->dev_addr, 1, 0);
	fc_set_wwnn(lp, wwnn);
	/* XXX - 3rd arg needs to be vlan id */
	wwpn = fcoe_wwn_from_mac(fc->real_dev->dev_addr, 2, 0);
	fc_set_wwpn(lp, wwpn);

	/*
	 * Add FCoE MAC address as second unicast MAC address
	 * or enter promiscuous mode if not capable of listening
	 * for multiple unicast MACs.
	 */
	rtnl_lock();
	memcpy(flogi_maddr, (u8[6]) FC_FCOE_FLOGI_MAC, ETH_ALEN);
	dev_unicast_add(fc->real_dev, flogi_maddr, ETH_ALEN);
	rtnl_unlock();

	/*
	 * setup the receive function from ethernet driver
	 * on the ethertype for the given device
	 */
	fc->fcoe_packet_type.func = fcoe_rcv;
	fc->fcoe_packet_type.type = __constant_htons(ETH_P_FCOE);
	fc->fcoe_packet_type.dev = fc->real_dev;
	dev_add_pack(&fc->fcoe_packet_type);

	return 0;
}

static void shost_config(struct fc_lport *lp)
{
	lp->host->max_lun = FCOE_MAX_LUN;
	lp->host->max_id = FCOE_MAX_FCP_TARGET;
	lp->host->max_channel = 0;
	lp->host->transportt = fcoe_transport_template;
}

static int libfc_config(struct fc_lport *lp)
{
	/* Set the function pointers set by the LLDD */
	memcpy(&lp->tt, &fcoe_libfc_fcn_templ,
	       sizeof(struct libfc_function_template));

	if (fc_fcp_init(lp))
		return -ENOMEM;
	fc_exch_init(lp);
	fc_lport_init(lp);
	fc_rport_init(lp);
	fc_ns_init(lp);
	fc_attr_init(lp);

	return 0;
}

/*
 * This function creates the fcoe interface
 * create struct fcdev which is a shared structure between opefc
 * and transport level protocol.
 */
int fcoe_create_interface(const char *ifname)
{
	struct fc_lport *lp = NULL;
	struct fcoe_softc *fc;
	struct net_device *net_dev;
	struct Scsi_Host *shost;
	struct fcoe_info *fci = &fcoei;
	int rc = 0;

	net_dev = dev_get_by_name(&init_net, ifname);
	if (net_dev == NULL) {
		FC_DBG("could not get network device for %s",
		       ifname);
		return -ENODEV;
	}

	if (fcoe_find_fc_lport(net_dev->name) != NULL) {
		rc = -EEXIST;
		goto out_put_dev;
	}

	shost = scsi_host_alloc(&fcoe_driver_template,
				sizeof(struct fc_lport) +
				sizeof(struct fcoe_softc));

	if (!shost) {
		FC_DBG("Could not allocate host structure\n");
		rc = -ENOMEM;
		goto out_put_dev;
	}

	lp = shost_priv(shost);
	rc = lport_config(lp, shost);
	if (rc)
		goto out_host_put;

	/* Configure the fcoe_softc */
	fc = (struct fcoe_softc *)lp->drv_priv;
	fc->lp = lp;
	fc->real_dev = net_dev;
	shost_config(lp);


	/* Add the new host to the SCSI-ml */
	rc = scsi_add_host(lp->host, NULL);
	if (rc) {
		FC_DBG("error on scsi_add_host\n");
		goto out_lp_destroy;
	}

	sprintf(fc_host_symbolic_name(lp->host), "%s v%s over %s",
		FCOE_DRIVER_NAME, FCOE_VERSION,
		ifname);

	/* Configure netdev and networking properties of the lp */
	rc = net_config(lp);
	if (rc)
		goto out_lp_destroy;

	/* Initialize the library */
	rc = libfc_config(lp);
	if (rc)
		goto out_lp_destroy;

	write_lock_bh(&fci->fcoe_hostlist_lock);
	list_add_tail(&fc->list, &fci->fcoe_hostlist);
	write_unlock_bh(&fci->fcoe_hostlist_lock);

	lp->boot_time = jiffies;

	fc_fabric_login(lp);

	return rc;

out_lp_destroy:
	fc_exch_mgr_free(lp->emp); /* Free the EM */
out_host_put:
	scsi_host_put(lp->host);
out_put_dev:
	dev_put(net_dev);
	return rc;
}

void fcoe_clean_pending_queue(struct fc_lport *lp)
{
	struct fcoe_softc  *fc = lp->drv_priv;
	struct sk_buff *skb;

	spin_lock_bh(&fc->fcoe_pending_queue.lock);
	while ((skb = __skb_dequeue(&fc->fcoe_pending_queue)) != NULL) {
		spin_unlock_bh(&fc->fcoe_pending_queue.lock);
		kfree_skb(skb);
		spin_lock_bh(&fc->fcoe_pending_queue.lock);
	}
	spin_unlock_bh(&fc->fcoe_pending_queue.lock);
}
