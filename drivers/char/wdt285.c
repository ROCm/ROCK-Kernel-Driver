/*
 *	Intel 21285 watchdog driver
 *	Copyright (c) Phil Blundell <pb@nexus.co.uk>, 1998
 *
 *	based on
 *
 *	SoftDog	0.05:	A Software Watchdog Device
 *
 *	(c) Copyright 1996 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 */
 
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>

#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/hardware/dec21285.h>

/*
 * Define this to stop the watchdog actually rebooting the machine.
 */
#undef ONLY_TESTING

#define TIMER_MARGIN	60		/* (secs) Default is 1 minute */

#define FCLK	(50*1000*1000)		/* 50MHz */

static int soft_margin = TIMER_MARGIN;	/* in seconds */
static int timer_alive;

#ifdef ONLY_TESTING
/*
 *	If the timer expires..
 */

static void watchdog_fire(int irq, void *dev_id, struct pt_regs *regs)
{
	printk(KERN_CRIT "Watchdog: Would Reboot.\n");
	*CSR_TIMER4_CNTL = 0;
	*CSR_TIMER4_CLR = 0;
}
#endif

static void watchdog_ping(void)
{
	/*
	 *	Refresh the timer.
	 */
	*CSR_TIMER4_LOAD = soft_margin * (FCLK / 256);
}

/*
 *	Allow only one person to hold it open
 */
 
static int watchdog_open(struct inode *inode, struct file *file)
{
	if(timer_alive)
		return -EBUSY;
	/*
	 *	Ahead watchdog factor ten, Mr Sulu
	 */
	*CSR_TIMER4_CLR = 0;
	watchdog_ping();
	*CSR_TIMER4_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_AUTORELOAD 
		| TIMER_CNTL_DIV256;
#ifdef ONLY_TESTING
	request_irq(IRQ_TIMER4, watchdog_fire, 0, "watchdog", NULL);
#else
	*CSR_SA110_CNTL |= 1 << 13;
	MOD_INC_USE_COUNT;
#endif
	timer_alive = 1;
	return 0;
}

static int watchdog_release(struct inode *inode, struct file *file)
{
#ifdef ONLY_TESTING
	lock_kernel();
	free_irq(IRQ_TIMER4, NULL);
	timer_alive = 0;
	unlock_kernel();
#else
	/*
	 *	It's irreversible!
	 */
#endif
	return 0;
}

static ssize_t watchdog_write(struct file *file, const char *data, size_t len, loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	/*
	 *	Refresh the timer.
	 */
	if(len)
	{
		watchdog_ping();
		return 1;
	}
	return 0;
}

static int watchdog_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int i;
	static struct watchdog_info ident=
	{
		0,
		0,
		"Footbridge Watchdog"
	};
	switch(cmd)
	{
		default:
			return -ENOIOCTLCMD;
		case WDIOC_GETSUPPORT:
			i = verify_area(VERIFY_WRITE, (void*) arg, sizeof(struct watchdog_info));
			if (i)
				return i;
			else
				return copy_to_user((struct watchdog_info *)arg, &ident, sizeof(ident));
		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			return put_user(0,(int *)arg);
		case WDIOC_KEEPALIVE:
			watchdog_ping();
			return 0;
	}
}

static struct file_operations watchdog_fops=
{
	owner:		THIS_MODULE,
	write:		watchdog_write,
	ioctl:		watchdog_ioctl,
	open:		watchdog_open,
	release:	watchdog_release,
};

static struct miscdevice watchdog_miscdev=
{
	WATCHDOG_MINOR,
	"watchdog",
	&watchdog_fops
};

static int __init footbridge_watchdog_init(void)
{
	if (machine_is_netwinder())
		return -ENODEV;

	misc_register(&watchdog_miscdev);
	printk("Footbridge Watchdog Timer: 0.01, timer margin: %d sec\n", 
	       soft_margin);
	if (machine_is_cats())
		printk("Warning: Watchdog reset may not work on this machine.\n");
	return 0;
}

static void __exit footbridge_watchdog_exit(void)
{
	misc_deregister(&watchdog_miscdev);
}

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Phil Blundell <pb@nexus.co.uk>");
MODULE_DESCRIPTION("21285 watchdog driver");

MODULE_PARM(soft_margin,"i");
MODULE_PARM_DESC(soft_margin,"Watchdog timeout in seconds");

module_init(footbridge_watchdog_init);
module_exit(footbridge_watchdog_exit);
