/*  
 * 2001 (c) Oy L M Ericsson Ab
 *
 * Author: NomadicLab / Ericsson Research <ipv6@nomadiclab.com>
 *
 * $Id: s.multiaccess_ctl.c 1.14 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 */

/*
 * Vertical hand-off information manager
 */

#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include "multiaccess_ctl.h"
#include "debug.h"

/*
 * Local variables
 */
static LIST_HEAD(if_list);

/* Internal interface information list */
struct ma_if_info {
	struct list_head list;
	int        interface_id;
	int        preference;
	__u8       status;
};

/**
 * ma_ctl_get_preference - get preference value for interface
 * @ifi: interface index
 * 
 * Returns integer value preference for given interface.
 **/
int ma_ctl_get_preference(int ifi)
{
	struct list_head *lh;
	struct ma_if_info *info;
	int pref = 0;

	list_for_each(lh, &if_list) {
		info = list_entry(lh, struct ma_if_info, list);
		if (info->interface_id == ifi) {
			pref = info->preference;
			return pref;
		}
	}
	return -1;
}
/**
 * ma_ctl_get_preference - get preference value for interface
 * @ifi: interface index
 * 
 * Returns integer value interface index for interface with highest preference.
 **/
int ma_ctl_get_preferred_if(void)
{
	struct list_head *lh;
	struct ma_if_info *info, *pref_if = NULL;
	
	list_for_each(lh, &if_list) {
		info = list_entry(lh, struct ma_if_info, list);
		if (!pref_if || (info->preference > pref_if->preference)) {
			pref_if = info;
		}
	}
	if (pref_if) return pref_if->interface_id;
	return 0;
}
/**
 * ma_ctl_set_preference - set preference for interface
 * @arg: ioctl args
 *
 * Sets preference of an existing interface (called by ioctl).
 **/
void ma_ctl_set_preference(unsigned long arg)
{
	struct list_head *lh;
	struct ma_if_info *info;
	struct ma_if_uinfo uinfo;
	
	memset(&uinfo, 0, sizeof(struct ma_if_uinfo));
	if (copy_from_user(&uinfo, (struct ma_if_uinfo *)arg, 
			   sizeof(struct ma_if_uinfo)) < 0) {
		DEBUG(DBG_WARNING, "copy_from_user failed");
		return;
	}

	/* check if the interface exists */
	list_for_each(lh, &if_list) {
		info = list_entry(lh, struct ma_if_info, list);
		if (info->interface_id == uinfo.interface_id) {
			info->preference = uinfo.preference;
			return;
		}
	}
}

/**
 * ma_ctl_add_iface - add new interface to list
 * @if_index: interface index
 *
 * Adds new interface entry to preference list.  Preference is set to
 * the same value as @if_index.  Entry @status is set to
 * %MA_IFACE_NOT_USED.
 **/
void ma_ctl_add_iface(int if_index)
{
	struct list_head *lh;
	struct ma_if_info *info;

	DEBUG_FUNC();
	
	/* check if the interface already exists */
	list_for_each(lh, &if_list) {
		info = list_entry(lh, struct ma_if_info, list);
		if (info->interface_id == if_index) {
			info->status = MA_IFACE_NOT_USED;
			info->preference = if_index;
			return;
		}
	}

	info = kmalloc(sizeof(struct ma_if_info), GFP_ATOMIC);
	if (info == NULL) {
		DEBUG(DBG_ERROR, "Out of memory");
		return;
	}
	memset(info, 0, sizeof(struct ma_if_info));
	info->interface_id = if_index;
	info->preference = if_index;
	info->status = MA_IFACE_NOT_USED;
	list_add(&info->list, &if_list);
}

/**
 * ma_ctl_del_iface - remove entry from the list
 * @if_index: interface index
 *
 * Removes entry for interface @if_index from preference list.
 **/
int ma_ctl_del_iface(int if_index)
{
	struct list_head *lh, *next;
	struct ma_if_info *info;

	DEBUG_FUNC();

	/* if the iface exists, change availability to 0 */
	list_for_each_safe(lh, next, &if_list) {
		info = list_entry(lh, struct ma_if_info, list);
		if (info->interface_id == if_index) {
			list_del(&info->list);
			kfree(info);
			return 0;
		}
	}

	return -1;
}

/**
 * ma_ctl_upd_iface - update entry (and list)
 * @if_index: interface to update
 * @status: new status for interface
 * @change_if_index: new interface
 *
 * Updates @if_index entry on preference list.  Entry status is set to
 * @status.  If new @status is %MA_IFACE_CURRENT, updates list to have
 * only one current device.  If @status is %MA_IFACE_NOT_PRESENT,
 * entry is deleted and further if entry had %MA_IFACE_CURRENT set,
 * new current device is looked up and returned in @change_if_index.
 * New preferred interface is also returned if current device changes
 * to %MA_IFACE_NOT_USED.  Returns 0 on success, otherwise negative.
 **/
int ma_ctl_upd_iface(int if_index, int status, int *change_if_index)
{
	struct list_head *lh, *tmp;
	struct ma_if_info *info, *pref = NULL;
	int found = 0;

	DEBUG_FUNC();

	*change_if_index = 0;

	/* check if the interface exists */
	list_for_each_safe(lh, tmp, &if_list) {
		info = list_entry(lh, struct ma_if_info, list);
		if (status == MA_IFACE_NOT_PRESENT) {
			if (info->interface_id == if_index) {
				list_del_init(&info->list);
				kfree(info);
				found = 1;
				break;
			}
		} else if (status == MA_IFACE_CURRENT) {
			if (info->interface_id == if_index) {
				info->status |= MA_IFACE_CURRENT;
				found = 1;
			} else {
				info->status |= MA_IFACE_NOT_USED;
			}
		} else if (status == MA_IFACE_NOT_USED) {
			if (info->interface_id == if_index) {
				if (info->status | MA_IFACE_CURRENT) {
					found = 1;
				}
				info->status &= !MA_IFACE_CURRENT;
				info->status |= MA_IFACE_NOT_USED;
				info->status &= !MA_IFACE_HAS_ROUTER;
			}
			break;
		} else if (status == MA_IFACE_HAS_ROUTER) {
			if (info->interface_id == if_index) {
				info->status |= MA_IFACE_HAS_ROUTER;
			}
			return 0;
		}
	}

	if (status & (MA_IFACE_NOT_USED|MA_IFACE_NOT_PRESENT) && found) {
		/* select new interface */
		list_for_each(lh, &if_list) {
			info = list_entry(lh, struct ma_if_info, list);
			if (pref == NULL || ((info->preference > pref->preference) && 
					     info->status & MA_IFACE_HAS_ROUTER))
				pref = info;
		}
		if (pref) {
			*change_if_index = pref->interface_id;
			pref->status |= MA_IFACE_CURRENT;
		} else {
			*change_if_index = -1;
		}
		return 0;
	}

	if (found) return 0;

	return -1;
}

static int if_proc_info(char *buffer, char **start, off_t offset,
			int length)
{
	struct list_head *lh;
	struct ma_if_info *info;
	int len = 0;

	list_for_each(lh, &if_list) {
		info = list_entry(lh, struct ma_if_info, list);
		len += sprintf(buffer + len, "%02d %010d %1d %1d\n",
			       info->interface_id, info->preference,
			       !!(info->status & MA_IFACE_HAS_ROUTER),
			       !!(info->status & MA_IFACE_CURRENT));
	}

	*start = buffer + offset;

	len -= offset;

	if (len > length) len = length;

	return len;

}

void ma_ctl_init(void)
{
	proc_net_create("mip6_iface", 0, if_proc_info);
}

void ma_ctl_clean(void)
{
	proc_net_remove("mip6_iface");
}
