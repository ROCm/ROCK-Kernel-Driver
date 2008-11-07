/*
 * Copyright(c) 2007 - 2008 Intel Corporation. All rights reserved.
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
MODULE_VERSION("1.0.3");

/*
 * Static functions and variables definations
 */
#ifdef CONFIG_HOTPLUG_CPU
static int fcoe_cpu_callback(struct notifier_block *, ulong, void *);
#endif /* CONFIG_HOTPLUG_CPU */
static int fcoe_device_notification(struct notifier_block *, ulong, void *);
static void fcoe_dev_setup(void);
static void fcoe_dev_cleanup(void);

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

	write_lock_bh(&fcoe_hostlist_lock);
	list_for_each_entry(fc, &fcoe_hostlist, list) {
		lp = fc->lp;
		if (lp->dev_stats[cpu] == NULL)
			lp->dev_stats[cpu] = kzalloc(sizeof(struct fcoe_dev_stats),
						     GFP_KERNEL);
	}
	write_unlock_bh(&fcoe_hostlist_lock);
}

/*
 * destroy percpu stats block
 * called by cpu add/remove notifier
 */
static void fcoe_destroy_percpu_data(int cpu)
{
	struct fc_lport *lp;
	struct fcoe_softc *fc;

	write_lock_bh(&fcoe_hostlist_lock);
	list_for_each_entry(fc, &fcoe_hostlist, list) {
		lp = fc->lp;
		kfree(lp->dev_stats[cpu]);
		lp->dev_stats[cpu] = NULL;
	}
	write_unlock_bh(&fcoe_hostlist_lock);
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
	u16 new_status;
	u32 mfs;
	int rc = NOTIFY_OK;

	read_lock(&fcoe_hostlist_lock);
	list_for_each_entry(fc, &fcoe_hostlist, list) {
		if (fc->real_dev == real_dev) {
			lp = fc->lp;
			break;
		}
	}
	read_unlock(&fcoe_hostlist_lock);
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
			if (stats)
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

static int fcoe_destroy(const char *buffer, struct kernel_param *kp)
{
	struct net_device *netdev;
	char ifname[IFNAMSIZ + 2];
	int rc = -ENODEV;

	strlcpy(ifname, buffer, IFNAMSIZ);
	trimstr(ifname, strlen(ifname));
	netdev = dev_get_by_name(&init_net, ifname);
	if (netdev) {
		rc = fcoe_destroy_interface(netdev);
		dev_put(netdev);
	}
	return rc;
}

static int fcoe_create(const char *buffer, struct kernel_param *kp)
{
	struct net_device *netdev;
	char ifname[IFNAMSIZ + 2];
	int rc = -ENODEV;

	strlcpy(ifname, buffer, IFNAMSIZ);
	trimstr(ifname, strlen(ifname));
	netdev = dev_get_by_name(&init_net, ifname);
	if (netdev) {
		rc = fcoe_create_interface(netdev);
		dev_put(netdev);
	}
	return rc;
}

module_param_call(create, fcoe_create, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(create, "string");
MODULE_PARM_DESC(create, "Create fcoe port using net device passed in.");
module_param_call(destroy, fcoe_destroy, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(destroy, "string");
MODULE_PARM_DESC(destroy, "Destroy fcoe port");

/*
 * Initialization routine
 * 1. Will create fc transport software structure
 * 2. initialize the link list of port information structure
 */
static int __init fcoe_init(void)
{
	int cpu;
	struct fcoe_percpu_s *p;

	rwlock_init(&fcoe_hostlist_lock);

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
				fcoe_percpu[cpu] = p;
				skb_queue_head_init(&p->fcoe_rx_list);
				kthread_bind(p->thread, cpu);
				wake_up_process(p->thread);
			} else {
				fcoe_percpu[cpu] = NULL;
				kfree(p);

			}
		}
	}

	/*
	 * setup link change notification
	 */
	fcoe_dev_setup();

	init_timer(&fcoe_timer);
	fcoe_timer.data = 0;
	fcoe_timer.function = fcoe_watchdog;
	fcoe_timer.expires = (jiffies + (10 * HZ));
	add_timer(&fcoe_timer);

	if (fcoe_sw_init() != 0) {
		FC_DBG("fail to attach fc transport");
		return -1;
	}

	return 0;
}
module_init(fcoe_init);

static void __exit fcoe_exit(void)
{
	u32 idx;
	struct fcoe_softc *fc, *tmp;
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
	del_timer_sync(&fcoe_timer);

	/*
	 * assuming that at this time there will be no
	 * ioctl in prograss, therefore we do not need to lock the
	 * list.
	 */
	list_for_each_entry_safe(fc, tmp, &fcoe_hostlist, list)
		fcoe_destroy_interface(fc->real_dev);

	for (idx = 0; idx < NR_CPUS; idx++) {
		if (fcoe_percpu[idx]) {
			kthread_stop(fcoe_percpu[idx]->thread);
			p = fcoe_percpu[idx];
			spin_lock_bh(&p->fcoe_rx_list.lock);
			while ((skb = __skb_dequeue(&p->fcoe_rx_list)) != NULL)
				kfree_skb(skb);
			spin_unlock_bh(&p->fcoe_rx_list.lock);
			if (fcoe_percpu[idx]->crc_eof_page)
				put_page(fcoe_percpu[idx]->crc_eof_page);
			kfree(fcoe_percpu[idx]);
		}
	}

	fcoe_sw_exit();
}
module_exit(fcoe_exit);
