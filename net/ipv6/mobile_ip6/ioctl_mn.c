/*
 *	Mobile Node IOCTL Control device
 *
 *	Authors:
 *	Henrik Petander		<lpetande@tml.hut.fi>
 *
 *	$Id: s.ioctl_mn.c 1.6 03/04/10 13:02:40+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/ioctl.h> 
#include <net/ipv6.h>
#include <asm/uaccess.h>

#include "debug.h"
#include "mdetect.h"
#include "multiaccess_ctl.h"

/* Reserved for local / experimental use */
#define MAJOR_NUM 0xf9

/* Get Care-of address information for Mobile Node */
#define IOCTL_GET_CAREOFADDR _IOWR(MAJOR_NUM, 9, void *)

#define MA_IOCTL_SET_IFACE_PREFERENCE _IOR (MAJOR_NUM, 13, void *)

/* The name of the device file */
#define CTLFILE "mipv6_dev"

static int inuse = 0;

static int mipv6_open(struct inode *inode, struct file *file)
{
	DEBUG(DBG_INFO, "(%p)\n", file);

	if (inuse)
		return -EBUSY;

	inuse++;

	return 0;
}

static int mipv6_close(struct inode *inode, struct file *file)
{
	DEBUG(DBG_INFO, "(%p,%p)\n", inode, file);
	inuse--;

	return 0;
}

int mipv6_ioctl(struct inode *inode, struct file *file, 
		unsigned int ioctl_num,	/* The number of the ioctl */
		unsigned long arg)	/* The parameter to it */
{
	struct in6_addr careofaddr;

	/* Switch according to the ioctl called */
	switch (ioctl_num) {
	case IOCTL_GET_CAREOFADDR:
		DEBUG(DBG_DATADUMP, "IOCTL_GET_CAREOFADDR");
		/* First get home address from user and then look up 
		 * the care-of address and return it
		 */
		if (copy_from_user(&careofaddr, (struct in6_addr *)arg, 
				   sizeof(struct in6_addr)) < 0) {
			DEBUG(DBG_WARNING, "Copy from user failed");
			return -EFAULT;
		}
		mipv6_get_care_of_address(&careofaddr, &careofaddr);
		if (copy_to_user((struct in6_addr *)arg, &careofaddr,
				 sizeof(struct in6_addr)) < 0) {
			DEBUG(DBG_WARNING, "copy_to_user failed");
			return -EFAULT;
		}
		break;
	case MA_IOCTL_SET_IFACE_PREFERENCE:
		DEBUG(DBG_INFO, "MA_IOCTL_SET_IFACE_PREFERENCE");
		ma_ctl_set_preference(arg);
		break;

	default:
		DEBUG(DBG_WARNING, "Unknown ioctl cmd (%d)", ioctl_num);
		return -ENOENT;
	}
	return 0;
}

struct file_operations Fops = {
	owner: THIS_MODULE,
	read: NULL,
	write: NULL,
	poll: NULL,
	ioctl: mipv6_ioctl,
	open: mipv6_open,
	release: mipv6_close
};


/* Initialize the module - Register the character device */
int mipv6_ioctl_mn_init(void)
{
	int ret_val;

	/* Register the character device (atleast try) */
	ret_val = register_chrdev(MAJOR_NUM, CTLFILE, &Fops);

	/* Negative values signify an error */
	if (ret_val < 0) {
		DEBUG(DBG_ERROR, "failed registering char device (err=%d)",
		      ret_val);
		return ret_val;
	}

	DEBUG(DBG_INFO, "Device number %x, success", MAJOR_NUM);
	return 0;
}


/* Cleanup - unregister the appropriate file from /proc */
void mipv6_ioctl_mn_exit(void)
{
	int ret;
	/* Unregister the device */
	ret = unregister_chrdev(MAJOR_NUM, CTLFILE);

	/* If there's an error, report it */
	if (ret < 0)
		DEBUG(DBG_ERROR, "errorcode: %d\n", ret);
}
