/*
 *	Advantech Single Board Computer WDT driver for Linux 2.4.x
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

static int advwdt_is_open;
static spinlock_t advwdt_lock;

/*
 *	You must set these - there is no sane way to probe for this board.
 *
 *	To enable or restart, write the timeout value in seconds (1 to 63)
 *	to I/O port WDT_START.  To disable, read I/O port WDT_STOP.
 *	Both are 0x443 for most boards (tested on a PCA-6276VE-00B1), but
 *	check your manual (at least the PCA-6159 seems to be different -
 *	the manual says WDT_STOP is 0x43, not 0x443).
 *	(0x43 is also a write-only control register for the 8254 timer!)
 *
 *	TODO: module parameters to set the I/O port addresses and NOWAYOUT
 *	option at load time.
 */
 
#define WDT_STOP 0x443
#define WDT_START 0x443

#define WD_TIMO 60		/* 1 minute */

/*
 *	Kernel methods.
 */
 
static void
advwdt_ping(void)
{
	/* Write a watchdog value */
	outb_p(WD_TIMO, WDT_START);
}

static ssize_t
advwdt_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (count) {
		advwdt_ping();
		return 1;
	}
	return 0;
}

static ssize_t
advwdt_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int
advwdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	  unsigned long arg)
{
	static struct watchdog_info ident = {
		WDIOF_KEEPALIVEPING, 1, "Advantech WDT"
	};
	
	switch (cmd) {
	case WDIOC_GETSUPPORT:
	  if (copy_to_user((struct watchdog_info *)arg, &ident, sizeof(ident)))
	    return -EFAULT;
	  break;
	  
	case WDIOC_GETSTATUS:
	  if (copy_to_user((int *)arg, &advwdt_is_open,  sizeof(int)))
	    return -EFAULT;
	  break;

	case WDIOC_KEEPALIVE:
	  advwdt_ping();
	  break;

	default:
	  return -ENOTTY;
	}
	return 0;
}

static int
advwdt_open(struct inode *inode, struct file *file)
{
	switch (MINOR(inode->i_rdev)) {
		case WATCHDOG_MINOR:
			spin_lock(&advwdt_lock);
			if (advwdt_is_open) {
				spin_unlock(&advwdt_lock);
				return -EBUSY;
			}
			/*
			 *	Activate 
			 */
	 
			advwdt_is_open = 1;
			advwdt_ping();
			spin_unlock(&advwdt_lock);
			return 0;
		default:
			return -ENODEV;
	}
}

static int
advwdt_close(struct inode *inode, struct file *file)
{
	lock_kernel();
	if (MINOR(inode->i_rdev) == WATCHDOG_MINOR) {
		spin_lock(&advwdt_lock);
#ifndef CONFIG_WATCHDOG_NOWAYOUT	
		inb_p(WDT_STOP);
#endif		
		advwdt_is_open = 0;
		spin_unlock(&advwdt_lock);
	}
	unlock_kernel();
	return 0;
}

/*
 *	Notifier for system down
 */

static int
advwdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/* Turn the WDT off */
		inb_p(WDT_STOP);
	}
	return NOTIFY_DONE;
}
 
/*
 *	Kernel Interfaces
 */
 
static struct file_operations advwdt_fops = {
	owner:		THIS_MODULE,
	read:		advwdt_read,
	write:		advwdt_write,
	ioctl:		advwdt_ioctl,
	open:		advwdt_open,
	release:	advwdt_close,
};

static struct miscdevice advwdt_miscdev = {
	WATCHDOG_MINOR,
	"watchdog",
	&advwdt_fops
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off. 
 */
 
static struct notifier_block advwdt_notifier = {
	advwdt_notify_sys,
	NULL,
	0
};

static int __init
advwdt_init(void)
{
	printk("WDT driver for Advantech single board computer initialising.\n");

	spin_lock_init(&advwdt_lock);
	misc_register(&advwdt_miscdev);
#if WDT_START != WDT_STOP
	request_region(WDT_STOP, 1, "Advantech WDT");
#endif
	request_region(WDT_START, 1, "Advantech WDT");
	register_reboot_notifier(&advwdt_notifier);
	return 0;
}

static void __exit
advwdt_exit(void)
{
	misc_deregister(&advwdt_miscdev);
	unregister_reboot_notifier(&advwdt_notifier);
#if WDT_START != WDT_STOP
	release_region(WDT_STOP,1);
#endif
	release_region(WDT_START,1);
}

module_init(advwdt_init);
module_exit(advwdt_exit);

MODULE_LICENSE("GPL");

/* end of advantechwdt.c */

