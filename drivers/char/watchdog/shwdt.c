/*
 * drivers/char/shwdt.c
 *
 * Watchdog driver for integrated watchdog in the SuperH processors.
 *
 * Copyright (C) 2001, 2002 Paul Mundt <lethal@0xd6.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * 14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *     Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#if defined(CONFIG_CPU_SH5)
  #define WTCNT		CPRC_BASE + 0x10
  #define WTCSR		CPRC_BASE + 0x18
#elif defined(CONFIG_CPU_SH4)
  #define WTCNT		0xffc00008
  #define WTCSR		0xffc0000c
#elif defined(CONFIG_CPU_SH3)
  #define WTCNT		0xffffff84
  #define WTCSR		0xffffff86
#else
  #error "Can't use SuperH watchdog on this platform"
#endif

#define WTCNT_HIGH	0x5a00
#define WTCSR_HIGH	0xa500

#define WTCSR_TME	0x80
#define WTCSR_WT	0x40
#define WTCSR_RSTS	0x20
#define WTCSR_WOVF	0x10
#define WTCSR_IOVF	0x08
#define WTCSR_CKS2	0x04
#define WTCSR_CKS1	0x02
#define WTCSR_CKS0	0x01

/*
 * CKS0-2 supports a number of clock division ratios. At the time the watchdog
 * is enabled, it defaults to a 41 usec overflow period .. we overload this to
 * something a little more reasonable, and really can't deal with anything
 * lower than WTCSR_CKS_1024, else we drop back into the usec range.
 *
 * Clock Division Ratio         Overflow Period
 * --------------------------------------------
 *     1/32 (initial value)       41 usecs
 *     1/64                       82 usecs
 *     1/128                     164 usecs
 *     1/256                     328 usecs
 *     1/512                     656 usecs
 *     1/1024                   1.31 msecs
 *     1/2048                   2.62 msecs
 *     1/4096                   5.25 msecs
 */
#define WTCSR_CKS_32	0x00
#define WTCSR_CKS_64	0x01
#define WTCSR_CKS_128	0x02
#define WTCSR_CKS_256	0x03
#define WTCSR_CKS_512	0x04
#define WTCSR_CKS_1024	0x05
#define WTCSR_CKS_2048	0x06
#define WTCSR_CKS_4096	0x07

/*
 * Default clock division ratio is 5.25 msecs. Overload this at module load
 * time. Any value not in the msec range will default to a timeout of one
 * jiffy, which exceeds the usec overflow periods.
 */
static int clock_division_ratio = WTCSR_CKS_4096;

#define msecs_to_jiffies(msecs)	(jiffies + ((HZ * msecs + 999) / 1000))
#define next_ping_period(cks)	msecs_to_jiffies(cks - 4)
#define user_ping_period(cks)	(next_ping_period(cks) * 10)

static unsigned long sh_is_open = 0;
static struct watchdog_info sh_wdt_info;
static struct timer_list timer;
static unsigned long next_heartbeat;

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

MODULE_PARM(nowayout,"i");
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

/**
 *	sh_wdt_write_cnt - Write to Counter
 *
 *	@val: Value to write
 *
 *	Writes the given value @val to the lower byte of the timer counter.
 *	The upper byte is set manually on each write.
 */
static void sh_wdt_write_cnt(__u8 val)
{
	ctrl_outw(WTCNT_HIGH | (__u16)val, WTCNT);
}

/**
 * 	sh_wdt_write_csr - Write to Control/Status Register
 *
 * 	@val: Value to write
 *
 * 	Writes the given value @val to the lower byte of the control/status
 * 	register. The upper byte is set manually on each write.
 */
static void sh_wdt_write_csr(__u8 val)
{
	ctrl_outw(WTCSR_HIGH | (__u16)val, WTCSR);
}

/**
 * 	sh_wdt_start - Start the Watchdog
 *
 * 	Starts the watchdog.
 */
static void sh_wdt_start(void)
{
	timer.expires = next_ping_period(clock_division_ratio);
	next_heartbeat = user_ping_period(clock_division_ratio);
	add_timer(&timer);

	sh_wdt_write_csr(WTCSR_WT | WTCSR_CKS_4096);
	sh_wdt_write_cnt(0);
	sh_wdt_write_csr((ctrl_inb(WTCSR) | WTCSR_TME));
}

/**
 * 	sh_wdt_stop - Stop the Watchdog
 *
 * 	Stops the watchdog.
 */
static void sh_wdt_stop(void)
{
	del_timer(&timer);

	sh_wdt_write_csr((ctrl_inb(WTCSR) & ~WTCSR_TME));
}

/**
 * 	sh_wdt_ping - Ping the Watchdog
 *
 *	@data: Unused
 *
 * 	Clears overflow bit, resets timer counter.
 */
static void sh_wdt_ping(unsigned long data)
{
	if (time_before(jiffies, next_heartbeat)) {
		sh_wdt_write_csr((ctrl_inb(WTCSR) & ~WTCSR_IOVF));
		sh_wdt_write_cnt(0);

		timer.expires = next_ping_period(clock_division_ratio);
		add_timer(&timer);
	}
}

/**
 * 	sh_wdt_open - Open the Device
 *
 * 	@inode: inode of device
 * 	@file: file handle of device
 *
 * 	Watchdog device is opened and started.
 */
static int sh_wdt_open(struct inode *inode, struct file *file)
{
	switch (minor(inode->i_rdev)) {
		case WATCHDOG_MINOR:
			if (test_and_set_bit(0, &sh_is_open))
				return -EBUSY;

			if (nowayout) {
				MOD_INC_USE_COUNT;
			}

			sh_wdt_start();

			break;
		default:
			return -ENODEV;
	}

	return 0;
}

/**
 * 	sh_wdt_close - Close the Device
 *
 * 	@inode: inode of device
 * 	@file: file handle of device
 *
 * 	Watchdog device is closed and stopped.
 */
static int sh_wdt_close(struct inode *inode, struct file *file)
{
	if (minor(inode->i_rdev) == WATCHDOG_MINOR) {
		if (!nowayout) {
			sh_wdt_stop();
		}
		clear_bit(0, &sh_is_open);
	}
	
	return 0;
}

/**
 * 	sh_wdt_read - Read from Device
 *
 * 	@file: file handle of device
 * 	@buf: buffer to write to
 * 	@count: length of buffer
 * 	@ppos: offset
 *
 * 	Unsupported.
 */
static ssize_t sh_wdt_read(struct file *file, char *buf,
			   size_t count, loff_t *ppos)
{
	return -EINVAL;
}

/**
 * 	sh_wdt_write - Write to Device
 *
 * 	@file: file handle of device
 * 	@buf: buffer to write
 * 	@count: length of buffer
 * 	@ppos: offset
 *
 * 	Pings the watchdog on write.
 */
static ssize_t sh_wdt_write(struct file *file, const char *buf,
			    size_t count, loff_t *ppos)
{
	/* Can't seek (pwrite) on this device */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (count) {
		next_heartbeat = user_ping_period(clock_division_ratio);
		return 1;
	}

	return 0;
}

/**
 * 	sh_wdt_ioctl - Query Device
 *
 * 	@inode: inode of device
 * 	@file: file handle of device
 * 	@cmd: watchdog command
 * 	@arg: argument
 *
 * 	Query basic information from the device or ping it, as outlined by the
 * 	watchdog API.
 */
static int sh_wdt_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		case WDIOC_GETSUPPORT:
			if (copy_to_user((struct watchdog_info *)arg,
					  &sh_wdt_info,
					  sizeof(sh_wdt_info))) {
				return -EFAULT;
			}
			
			break;
		case WDIOC_GETSTATUS:
			if (copy_to_user((int *)arg,
					 &sh_is_open,
					 sizeof(int))) {
				return -EFAULT;
			}

			break;
		case WDIOC_KEEPALIVE:
			next_heartbeat = user_ping_period(clock_division_ratio);
			
			break;
		default:
			return -ENOTTY;
	}

	return 0;
}

/**
 * 	sh_wdt_notify_sys - Notifier Handler
 * 	
 * 	@this: notifier block
 * 	@code: notifier event
 * 	@unused: unused
 *
 * 	Handles specific events, such as turning off the watchdog during a
 * 	shutdown event.
 */
static int sh_wdt_notify_sys(struct notifier_block *this,
			     unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		sh_wdt_stop();
	}

	return NOTIFY_DONE;
}

static struct file_operations sh_wdt_fops = {
	.owner		= THIS_MODULE,
	.read		= sh_wdt_read,
	.write		= sh_wdt_write,
	.ioctl		= sh_wdt_ioctl,
	.open		= sh_wdt_open,
	.release	= sh_wdt_close,
};

static struct watchdog_info sh_wdt_info = {
	.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
	.firmware_version = 1,
	.identity = "SH WDT",
};

static struct notifier_block sh_wdt_notifier = {
	.notifier_call = sh_wdt_notify_sys,
	.next = NULL,
	.priority = 0
};

static struct miscdevice sh_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops &sh_wdt_fops,
};

/**
 * 	sh_wdt_init - Initialize module
 *
 * 	Registers the device and notifier handler. Actual device
 * 	initialization is handled by sh_wdt_open().
 */
static int __init sh_wdt_init(void)
{
	if (misc_register(&sh_wdt_miscdev)) {
		printk(KERN_ERR "shwdt: Can't register misc device\n");
		return -EINVAL;
	}

	if (!request_region(WTCNT, 1, "shwdt")) {
		printk(KERN_ERR "shwdt: Can't request WTCNT region\n");
		misc_deregister(&sh_wdt_miscdev);
		return -ENXIO;
	}

	if (!request_region(WTCSR, 1, "shwdt")) {
		printk(KERN_ERR "shwdt: Can't request WTCSR region\n");
		release_region(WTCNT, 1);
		misc_deregister(&sh_wdt_miscdev);
		return -ENXIO;
	}

	if (register_reboot_notifier(&sh_wdt_notifier)) {
		printk(KERN_ERR "shwdt: Can't register reboot notifier\n");
		release_region(WTCSR, 1);
		release_region(WTCNT, 1);
		misc_deregister(&sh_wdt_miscdev);
		return -EINVAL;
	}

	init_timer(&timer);
	timer.function = sh_wdt_ping;
	timer.data = 0;

	return 0;
}

/**
 * 	sh_wdt_exit - Deinitialize module
 *
 * 	Unregisters the device and notifier handler. Actual device
 * 	deinitialization is handled by sh_wdt_close().
 */
static void __exit sh_wdt_exit(void)
{
	unregister_reboot_notifier(&sh_wdt_notifier);
	release_region(WTCSR, 1);
	release_region(WTCNT, 1);
	misc_deregister(&sh_wdt_miscdev);
}

MODULE_AUTHOR("Paul Mundt <lethal@0xd6.org>");
MODULE_DESCRIPTION("SuperH watchdog driver");
MODULE_LICENSE("GPL");
MODULE_PARM(clock_division_ratio, "i");
MODULE_PARM_DESC(clock_division_ratio, "Clock division ratio. Valid ranges are from 0x5 (1.31ms) to 0x7 (5.25ms). Defaults to 0x7.");

module_init(sh_wdt_init);
module_exit(sh_wdt_exit);

