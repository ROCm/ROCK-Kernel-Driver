/*
 *	IB700 Single Board Computer WDT driver for Linux 2.4.x
 *
 *	(c) Copyright 2001 Charles Howes <chowes@vsol.net>
 *
 *      Based on advantechwdt.c which is based on acquirewdt.c which
 *       is based on wdt.c.
 *
 *	(c) Copyright 2000-2001 Marek Michalkiewicz <marekm@linux.org.pl>
 *
 *	Based on acquirewdt.c which is based on wdt.c.
 *	Original copyright messages:
 *
 *	(c) Copyright 1996 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *				http://www.redhat.com
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Alan Cox nor CymruNet Ltd. admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 1995    Alan Cox <alan@redhat.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>

static int ibwdt_is_open;
static spinlock_t ibwdt_lock;

/*
 *
 * Watchdog Timer Configuration
 *
 * The function of the watchdog timer is to reset the system
 * automatically and is defined at I/O port 0443H.  To enable the
 * watchdog timer and allow the system to reset, write I/O port 0443H.
 * To disable the timer, write I/O port 0441H for the system to stop the
 * watchdog function.  The timer has a tolerance of 20% for its
 * intervals.
 *
 * The following describes how the timer should be programmed.
 *
 * Enabling Watchdog:
 * MOV AX,000FH (Choose the values from 0 to F)
 * MOV DX,0443H
 * OUT DX,AX
 *
 * Disabling Watchdog:
 * MOV AX,000FH (Any value is fine.)
 * MOV DX,0441H
 * OUT DX,AX
 *
 * Watchdog timer control table:
 * Level   Value  Time/sec | Level Value Time/sec
 *   1       F       0     |   9     7      16
 *   2       E       2     |   10    6      18
 *   3       D       4     |   11    5      20
 *   4       C       6     |   12    4      22
 *   5       B       8     |   13    3      24
 *   6       A       10    |   14    2      26
 *   7       9       12    |   15    1      28
 *   8       8       14    |   16    0      30
 *
 */

#define WDT_STOP 0x441
#define WDT_START 0x443

#define WD_TIMO 0		/* 30 seconds +/- 20%, from table */

/*
 *	Kernel methods.
 */

static void
ibwdt_ping(void)
{
	/* Write a watchdog value */
	outb_p(WD_TIMO, WDT_START);
}

static ssize_t
ibwdt_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (count) {
		ibwdt_ping();
		return 1;
	}
	return 0;
}

static ssize_t
ibwdt_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int
ibwdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	  unsigned long arg)
{
	static struct watchdog_info ident = {
		WDIOF_KEEPALIVEPING, 1, "IB700 WDT"
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
	  if (copy_to_user((struct watchdog_info *)arg, &ident, sizeof(ident)))
	    return -EFAULT;
	  break;

	case WDIOC_GETSTATUS:
	  if (copy_to_user((int *)arg, &ibwdt_is_open,  sizeof(int)))
	    return -EFAULT;
	  break;

	case WDIOC_KEEPALIVE:
	  ibwdt_ping();
	  break;

	default:
	  return -ENOTTY;
	}
	return 0;
}

static int
ibwdt_open(struct inode *inode, struct file *file)
{
	switch (MINOR(inode->i_rdev)) {
		case WATCHDOG_MINOR:
			spin_lock(&ibwdt_lock);
			if (ibwdt_is_open) {
				spin_unlock(&ibwdt_lock);
				return -EBUSY;
			}
			/*
			 *	Activate
			 */

			ibwdt_is_open = 1;
			ibwdt_ping();
			spin_unlock(&ibwdt_lock);
			return 0;
		default:
			return -ENODEV;
	}
}

static int
ibwdt_close(struct inode *inode, struct file *file)
{
	lock_kernel();
	if (MINOR(inode->i_rdev) == WATCHDOG_MINOR) {
		spin_lock(&ibwdt_lock);
#ifndef CONFIG_WATCHDOG_NOWAYOUT
		outb_p(WD_TIMO, WDT_STOP);
#endif
		ibwdt_is_open = 0;
		spin_unlock(&ibwdt_lock);
	}
	unlock_kernel();
	return 0;
}

/*
 *	Notifier for system down
 */

static int
ibwdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/* Turn the WDT off */
		outb_p(WD_TIMO, WDT_STOP);
	}
	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static struct file_operations ibwdt_fops = {
	owner:		THIS_MODULE,
	read:		ibwdt_read,
	write:		ibwdt_write,
	ioctl:		ibwdt_ioctl,
	open:		ibwdt_open,
	release:	ibwdt_close,
};

static struct miscdevice ibwdt_miscdev = {
	WATCHDOG_MINOR,
	"watchdog",
	&ibwdt_fops
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block ibwdt_notifier = {
	ibwdt_notify_sys,
	NULL,
	0
};

static int __init
ibwdt_init(void)
{
	printk("WDT driver for IB700 single board computer initialising.\n");

	spin_lock_init(&ibwdt_lock);
	misc_register(&ibwdt_miscdev);
#if WDT_START != WDT_STOP
	request_region(WDT_STOP, 1, "IB700 WDT");
#endif
	request_region(WDT_START, 1, "IB700 WDT");
	register_reboot_notifier(&ibwdt_notifier);
	return 0;
}

static void __exit
ibwdt_exit(void)
{
	misc_deregister(&ibwdt_miscdev);
	unregister_reboot_notifier(&ibwdt_notifier);
#if WDT_START != WDT_STOP
	release_region(WDT_STOP,1);
#endif
	release_region(WDT_START,1);
}

module_init(ibwdt_init);
module_exit(ibwdt_exit);

MODULE_AUTHOR("Charles Howes <chowes@vsol.net>");
MODULE_DESCRIPTION("IB700 SBC watchdog driver");
MODULE_LICENSE("GPL");

/* end of ib700wdt.c */
