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
 *      14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *          Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *          Added timeout module option to override default
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
 *	TODO: module parameters to set the I/O port addresses
 */
 
#define WDT_STOP 0x443
#define WDT_START 0x443

#define WD_TIMO 60		/* 1 minute */

static int timeout = WD_TIMO;	/* in seconds */
MODULE_PARM(timeout,"i");
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds (default=60)"); 

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

MODULE_PARM(nowayout,"i");
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

/*
 *	Kernel methods.
 */
 
static void
advwdt_ping(void)
{
	/* Write a watchdog value */
	outb_p(timeout, WDT_START);
}

static ssize_t
advwdt_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (count) {
		if (!nowayout) {
			size_t i;

			adv_expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf+i))
					return -EFAULT;
				if (c == 'V')
					adv_expect_close = 42;
			}
		}
		advwdt_ping();
	}
	return count;
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
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "Advantech WDT"
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
	switch (minor(inode->i_rdev)) {
		case WATCHDOG_MINOR:
			spin_lock(&advwdt_lock);
			if (advwdt_is_open) {
				spin_unlock(&advwdt_lock);
				return -EBUSY;
			}
			if (nowayout) {
				MOD_INC_USE_COUNT;
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
	if (minor(inode->i_rdev) == WATCHDOG_MINOR) {
		spin_lock(&advwdt_lock);
		if (!nowayout) {
			inb_p(WDT_STOP);
		}
		advwdt_is_open = 0;
		spin_unlock(&advwdt_lock);
	}
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
	.owner		= THIS_MODULE,
	.read		= advwdt_read,
	.write		= advwdt_write,
	.ioctl		= advwdt_ioctl,
	.open		= advwdt_open,
	.release	= advwdt_close,
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

static void __init
advwdt_validate_timeout(void)
{
	if (timeout < 1 || timeout > 63) {
		timeout = WD_TIMO;
		printk(KERN_INFO "advantechwdt: timeout value must be 1 <= x <= 63, using %d\n", timeout);
	}
}

static int __init
advwdt_init(void)
{
	printk("WDT driver for Advantech single board computer initialising.\n");

	advwdt_validate_timeout();
	spin_lock_init(&advwdt_lock);
	if (misc_register(&advwdt_miscdev))
		return -ENODEV;
#if WDT_START != WDT_STOP
	if (!request_region(WDT_STOP, 1, "Advantech WDT")) {
		misc_deregister(&advwdt_miscdev);
		return -EIO;
	}
#endif
	if (!request_region(WDT_START, 1, "Advantech WDT")) {
		misc_deregister(&advwdt_miscdev);
#if WDT_START != WDT_STOP
		release_region(WDT_STOP, 1);
#endif
		return -EIO;
	}
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

