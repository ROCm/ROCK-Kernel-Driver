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

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/ctype.h>

#include <scsi/libfc/libfc.h>

#include "fcoe_def.h"

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("FCoE");
MODULE_LICENSE("GPL");

/*
 * Static functions and variables definations
 */
#ifdef CONFIG_HOTPLUG_CPU
static int fcoe_cpu_callback(struct notifier_block *, ulong, void *);
#endif /* CONFIG_HOTPLUG_CPU */
static int fcoe_device_notification(struct notifier_block *, ulong, void *);
static void fcoe_dev_setup(void);
static void fcoe_dev_cleanup(void);

struct scsi_transport_template *fcoe_transport_template;

static int fcoe_reset(struct Scsi_Host *shost)
{
	struct fc_lport *lp = shost_priv(shost);
	fc_lport_enter_reset(lp);
	return 0;
}

struct fc_function_template fcoe_transport_function = {
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_active_fc4s = 1,
	.show_host_maxframe_size = 1,

	.get_host_port_id = fc_get_host_port_id,
	.show_host_port_id = 1,
	.get_host_speed = fc_get_host_speed,
	.show_host_speed = 1,
	.get_host_port_type = fc_get_host_port_type,
	.show_host_port_type = 1,
	.get_host_port_state = fc_get_host_port_state,
	.show_host_port_state = 1,
	.show_host_symbolic_name = 1,

	.dd_fcrport_size = sizeof(struct fc_rport_libfc_priv),
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,

	.get_host_fabric_name = fc_get_host_fabric_name,
	.show_host_fabric_name = 1,
	.show_starget_node_name = 1,
	.show_starget_port_name = 1,
	.show_starget_port_id = 1,
	.set_rport_dev_loss_tmo = fc_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,
	.get_fc_host_stats = fc_get_host_stats,
	.issue_fc_host_lip = fcoe_reset,
};

struct fcoe_percpu_s *fcoe_percpu[NR_CPUS];

#ifdef CONFIG_HOTPLUG_CPU
static struct notifier_block fcoe_cpu_notifier = {
	.notifier_call = fcoe_cpu_callback,
};
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * notification function from net device
 */
static struct notifier_block fcoe_notifier = {
	.notifier_call = fcoe_device_notification,
};

#ifdef CONFIG_HOTPLUG_CPU
/*
 * create percpu stats block
 * called by cpu add/remove notifier
 */
static void fcoe_create_percpu_data(int cpu)
{
	struct fc_lport *lp;
	struct fcoe_softc *fc;
	struct fcoe_dev_stats *p;
	struct fcoe_info *fci = &fcoei;

	write_lock_bh(&fci->fcoe_hostlist_lock);
	list_for_each_entry(fc, &fci->fcoe_hostlist, list) {
		lp = fc->lp;
		if (lp->dev_stats[cpu] == NULL) {
			p = kzalloc(sizeof(struct fcoe_dev_stats), GFP_KERNEL);
			if (p)
				lp->dev_stats[cpu] = p;
		}
	}
	write_unlock_bh(&fci->fcoe_hostlist_lock);
}

/*
 * destroy percpu stats block
 * called by cpu add/remove notifier
 */
static void fcoe_destroy_percpu_data(int cpu)
{
	struct fcoe_dev_stats *p;
	struct fc_lport *lp;
	struct fcoe_softc *fc;
	struct fcoe_info *fci = &fcoei;

	write_lock_bh(&fci->fcoe_hostlist_lock);
	list_for_each_entry(fc, &fci->fcoe_hostlist, list) {
		lp = fc->lp;
		p = lp->dev_stats[cpu];
		if (p != NULL) {
			lp->dev_stats[cpu] = NULL;
			kfree(p);
		}
	}
	write_unlock_bh(&fci->fcoe_hostlist_lock);
}

/*
 * Get notified when a cpu comes on/off. Be hotplug friendly.
 */
static int fcoe_cpu_callback(struct notifier_block *nfb, unsigned long action,
			     void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
		fcoe_create_percpu_data(cpu);
		break;
	case CPU_DEAD:
		fcoe_destroy_percpu_data(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * function to setup link change notification interface
 */
static void fcoe_dev_setup(void)
{
	/*
	 * here setup a interface specific wd time to
	 * monitor the link state
	 */
	register_netdevice_notifier(&fcoe_notifier);
}

/*
 * function to cleanup link change notification interface
 */
static void fcoe_dev_cleanup(void)
{
	unregister_netdevice_notifier(&fcoe_notifier);
}

/*
 * This function is called by the ethernet driver
 * this is called in case of link change event
 */
static int fcoe_device_notification(struct notifier_block *notifier,
				    ulong event, void *ptr)
{
	struct fc_lport *lp = NULL;
	struct net_device *real_dev = ptr;
	struct fcoe_softc *fc;
	struct fcoe_dev_stats *stats;
	struct fcoe_info *fci = &fcoei;
	u16 new_status;
	u32 mfs;
	int rc = NOTIFY_OK;

	read_lock(&fci->fcoe_hostlist_lock);
	list_for_each_entry(fc, &fci->fcoe_hostlist, list) {
		if (fc->real_dev == real_dev) {
			lp = fc->lp;
			break;
		}
	}
	read_unlock(&fci->fcoe_hostlist_lock);
	if (lp == NULL) {
		rc = NOTIFY_DONE;
		goto out;
	}

	new_status = lp->link_status;
	switch (event) {
	case NETDEV_DOWN:
	case NETDEV_GOING_DOWN:
		new_status &= ~FC_LINK_UP;
		break;
	case NETDEV_UP:
	case NETDEV_CHANGE:
		new_status &= ~FC_LINK_UP;
		if (!fcoe_link_ok(lp))
			new_status |= FC_LINK_UP;
		break;
	case NETDEV_CHANGEMTU:
		mfs = fc->real_dev->mtu -
			(sizeof(struct fcoe_hdr) +
			 sizeof(struct fcoe_crc_eof));
		if (fc->user_mfs && fc->user_mfs < mfs)
			mfs = fc->user_mfs;
		if (mfs >= FC_MIN_MAX_FRAME)
			fc_set_mfs(lp, mfs);
		new_status &= ~FC_LINK_UP;
		if (!fcoe_link_ok(lp))
			new_status |= FC_LINK_UP;
		break;
	case NETDEV_REGISTER:
		break;
	default:
		FC_DBG("unknown event %ld call", event);
	}
	if (lp->link_status != new_status) {
		if ((new_status & FC_LINK_UP) == FC_LINK_UP)
			fc_linkup(lp);
		else {
			stats = lp->dev_stats[smp_processor_id()];
			stats->LinkFailureCount++;
			fc_linkdown(lp);
			fcoe_clean_pending_queue(lp);
		}
	}
out:
	return rc;
}

static void trimstr(char *str, int len)
{
	char *cp = str + len;
	while (--cp >= str && *cp == '\n')
		*cp = '\0';
}

static ssize_t fcoe_destroy(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buffer, size_t size)
{
	char ifname[40];
	strcpy(ifname, buffer);
	trimstr(ifname, strlen(ifname));
	fcoe_destroy_interface(ifname);
	return size;
}

static ssize_t fcoe_create(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buffer, size_t size)
{
	char ifname[40];
	strcpy(ifname, buffer);
	trimstr(ifname, strlen(ifname));
	fcoe_create_interface(ifname);
	return size;
}

static const struct kobj_attribute fcoe_destroyattr = \
	__ATTR(destroy, S_IWUSR, NULL, fcoe_destroy);
static const struct kobj_attribute fcoe_createattr = \
	__ATTR(create, S_IWUSR, NULL, fcoe_create);

/*
 * Initialization routine
 * 1. Will create fc transport software structure
 * 2. initialize the link list of port information structure
 */
static int __init fcoeinit(void)
{
	int rc = 0;
	int cpu;
	struct fcoe_percpu_s *p;
	struct fcoe_info *fci = &fcoei;

	rc = sysfs_create_file(&THIS_MODULE->mkobj.kobj,
			       &fcoe_destroyattr.attr);
	if (!rc)
		rc = sysfs_create_file(&THIS_MODULE->mkobj.kobj,
				       &fcoe_createattr.attr);

	if (rc)
		return rc;

	rwlock_init(&fci->fcoe_hostlist_lock);

#ifdef CONFIG_HOTPLUG_CPU
	register_cpu_notifier(&fcoe_cpu_notifier);
#endif /* CONFIG_HOTPLUG_CPU */

	/*
	 * initialize per CPU interrupt thread
	 */
	for_each_online_cpu(cpu) {
		p = kzalloc(sizeof(struct fcoe_percpu_s), GFP_KERNEL);
		if (p) {
			p->thread = kthread_create(fcoe_percpu_receive_thread,
						   (void *)p,
						   "fcoethread/%d", cpu);

			/*
			 * if there is no error then bind the thread to the cpu
			 * initialize the semaphore and skb queue head
			 */
			if (likely(!IS_ERR(p->thread))) {
				p->cpu = cpu;
				fci->fcoe_percpu[cpu] = p;
				skb_queue_head_init(&p->fcoe_rx_list);
				kthread_bind(p->thread, cpu);
				wake_up_process(p->thread);
			} else {
				fci->fcoe_percpu[cpu] = NULL;
				kfree(p);

			}
		}
	}
	if (rc < 0) {
		FC_DBG("failed to initialize proc intrerface\n");
		rc = -ENODEV;
		goto out_chrdev;
	}

	/*
	 * setup link change notification
	 */
	fcoe_dev_setup();

	init_timer(&fci->timer);
	fci->timer.data = (ulong) fci;
	fci->timer.function = fcoe_watchdog;
	fci->timer.expires = (jiffies + (10 * HZ));
	add_timer(&fci->timer);

	fcoe_transport_template =
		fc_attach_transport(&fcoe_transport_function);

	if (fcoe_transport_template == NULL) {
		FC_DBG("fail to attach fc transport");
		return -1;
	}

	return 0;

out_chrdev:
#ifdef CONFIG_HOTPLUG_CPU
	unregister_cpu_notifier(&fcoe_cpu_notifier);
#endif /* CONFIG_HOTPLUG_CPU */
	return rc;
}

static void __exit fcoe_exit(void)
{
	u32 idx;
	struct fcoe_softc *fc, *tmp;
	struct fc_lport *lp;
	struct fcoe_info *fci = &fcoei;
	struct fcoe_percpu_s *p;
	struct sk_buff *skb;

	/*
	 * Stop all call back interfaces
	 */
#ifdef CONFIG_HOTPLUG_CPU
	unregister_cpu_notifier(&fcoe_cpu_notifier);
#endif /* CONFIG_HOTPLUG_CPU */
	fcoe_dev_cleanup();

	/*
	 * stop timer
	 */
	del_timer_sync(&fci->timer);

	/*
	 * assuming that at this time there will be no
	 * ioctl in prograss, therefore we do not need to lock the
	 * list.
	 */
	list_for_each_entry_safe(fc, tmp, &fci->fcoe_hostlist, list) {
		lp = fc->lp;
		fcoe_destroy_interface(lp->ifname);
	}

	for (idx = 0; idx < NR_CPUS; idx++) {
		if (fci->fcoe_percpu[idx]) {
			kthread_stop(fci->fcoe_percpu[idx]->thread);
			p = fci->fcoe_percpu[idx];
			spin_lock_bh(&p->fcoe_rx_list.lock);
			while ((skb = __skb_dequeue(&p->fcoe_rx_list)) != NULL)
				kfree_skb(skb);
			spin_unlock_bh(&p->fcoe_rx_list.lock);
			if (fci->fcoe_percpu[idx]->crc_eof_page)
				put_page(fci->fcoe_percpu[idx]->crc_eof_page);
			kfree(fci->fcoe_percpu[idx]);
		}
	}

	fc_release_transport(fcoe_transport_template);
}

module_init(fcoeinit);
module_exit(fcoe_exit);
