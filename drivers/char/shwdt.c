/*
 * drivers/char/shwdt.c
 *
 * Watchdog driver for integrated watchdog in the SuperH 3/4 processors.
 *
 * Copyright (C) 2001 Paul Mundt <lethal@chaoticdreams.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/smp_lock.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#if defined(CONFIG_CPU_SH4)
  #define WTCNT		0xffc00008
  #define WTCSR		0xffc0000c
#elif defined(CONFIG_CPU_SH3)
  #define WTCNT		0xffffff84
  #define WTCSR		0xffffff86
#else
  #error "Can't use SH 3/4 watchdog on non-SH 3/4 processor."
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

#define WTCSR_CKS	0x07
#define WTCSR_CKS_1	0x00
#define WTCSR_CKS_4	0x01
#define WTCSR_CKS_16	0x02
#define WTCSR_CKS_32	0x03
#define WTCSR_CKS_64	0x04
#define WTCSR_CKS_256	0x05
#define WTCSR_CKS_1024	0x06
#define WTCSR_CKS_4096	0x07

static int sh_is_open = 0;
static struct watchdog_info sh_wdt_info;

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
	sh_wdt_write_csr((ctrl_inb(WTCSR) & ~WTCSR_IOVF));
	sh_wdt_write_cnt(0);
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
	switch (MINOR(inode->i_rdev)) {
		case WATCHDOG_MINOR:
			if (sh_is_open) {
				return -EBUSY;
			}

			sh_is_open = 1;
			sh_wdt_start();

			return 0;
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
	lock_kernel();
	
	if (MINOR(inode->i_rdev) == WATCHDOG_MINOR) {
#ifndef CONFIG_WATCHDOG_NOWAYOUT
		sh_wdt_stop();
#endif
		sh_is_open = 0;
	}
	
	unlock_kernel();

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
		sh_wdt_ping(0);
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
			sh_wdt_ping(0);
			
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
	owner:		THIS_MODULE,
	read:		sh_wdt_read,
	write:		sh_wdt_write,
	ioctl:		sh_wdt_ioctl,
	open:		sh_wdt_open,
	release:	sh_wdt_close,
};

static struct watchdog_info sh_wdt_info = {
	WDIOF_KEEPALIVEPING,
	1,
	"SH WDT",
};

static struct notifier_block sh_wdt_notifier = {
	sh_wdt_notify_sys,
	NULL,
	0
};

static struct miscdevice sh_wdt_miscdev = {
	WATCHDOG_MINOR,
	"watchdog",
	&sh_wdt_fops,
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

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Paul Mundt <lethal@chaoticdreams.org>");
MODULE_DESCRIPTION("SH 3/4 watchdog driver");
MODULE_LICENSE("GPL");

module_init(sh_wdt_init);
module_exit(sh_wdt_exit);

