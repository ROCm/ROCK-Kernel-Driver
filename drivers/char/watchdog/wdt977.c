/*
 *	Wdt977	0.02:	A Watchdog Device for Netwinder W83977AF chip
 *
 *	(c) Copyright 1998 Rebel.com (Woody Suwalski <woody@netwinder.org>)
 *
 *			-----------------------
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *			-----------------------
 *      14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *           Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *	19-Dec-2001 Woody Suwalski: Netwinder fixes, ioctl interface
 *	06-Jan-2002 Woody Suwalski: For compatibility, convert all timeouts
 *				    from minutes to seconds.
 *      07-Jul-2003 Daniele Bellucci: Audit return code of misc_register in
 *                                    nwwatchdog_init.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/watchdog.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/mach-types.h>
#include <asm/uaccess.h>

#define WATCHDOG_MINOR	130

#define	DEFAULT_TIMEOUT	1	/* default timeout = 1 minute */

static	int timeout = DEFAULT_TIMEOUT*60;	/* TO in seconds from user */
static	int timeoutM = DEFAULT_TIMEOUT;		/* timeout in minutes */
static	unsigned long timer_alive;
static	int testmode;
static int expect_close = 0;

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,"Watchdog timeout in seconds (60..15300), default=60");
module_param(testmode, int, 0);
MODULE_PARM_DESC(testmode,"Watchdog testmode (1 = no reboot), default=0");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");


/* This is kicking the watchdog by simply re-writing the timeout to reg. 0xF2 */
static int kick_wdog(void)
{
	/*
	 *	Refresh the timer.
	 */

	/* unlock the SuperIO chip */
	outb(0x87,0x370);
	outb(0x87,0x370);

	/* select device Aux2 (device=8) and kicks watchdog reg F2 */
	/* F2 has the timeout in minutes */

	outb(0x07,0x370);
	outb(0x08,0x371);
	outb(0xF2,0x370);
	outb(timeoutM,0x371);

	/* lock the SuperIO chip */
	outb(0xAA,0x370);

	return 0;
}


/*
 *	Allow only one person to hold it open
 */

static int wdt977_open(struct inode *inode, struct file *file)
{

	if( test_and_set_bit(0,&timer_alive) )
		return -EBUSY;

	/* convert seconds to minutes, rounding up */
	timeoutM = timeout + 59;
	timeoutM /= 60;

	if (nowayout)
	{
		__module_get(THIS_MODULE);

		/* do not permit disabling the watchdog by writing 0 to reg. 0xF2 */
		if (!timeoutM) timeoutM = DEFAULT_TIMEOUT;
	}

	if (machine_is_netwinder())
	{
		/* we have a hw bug somewhere, so each 977 minute is actually only 30sec
		 *  this limits the max timeout to half of device max of 255 minutes...
		 */
		timeoutM += timeoutM;
	}

	/* max timeout value = 255 minutes (0xFF). Write 0 to disable WatchDog. */
	if (timeoutM > 255) timeoutM = 255;

	/* convert seconds to minutes */
	printk(KERN_INFO "Wdt977 Watchdog activated: timeout = %d sec, nowayout = %i, testmode = %i.\n",
		machine_is_netwinder() ? (timeoutM>>1)*60 : timeoutM*60,
		nowayout, testmode);

	/* unlock the SuperIO chip */
	outb(0x87,0x370);
	outb(0x87,0x370);

	/* select device Aux2 (device=8) and set watchdog regs F2, F3 and F4
	 * F2 has the timeout in minutes
	 * F3 could be set to the POWER LED blink (with GP17 set to PowerLed)
	 *   at timeout, and to reset timer on kbd/mouse activity (not impl.)
	 * F4 is used to just clear the TIMEOUT'ed state (bit 0)
	 */
	outb(0x07,0x370);
	outb(0x08,0x371);
	outb(0xF2,0x370);
	outb(timeoutM,0x371);
	outb(0xF3,0x370);
	outb(0x00,0x371);	/* another setting is 0E for kbd/mouse/LED */
	outb(0xF4,0x370);
	outb(0x00,0x371);

	/* at last select device Aux1 (dev=7) and set GP16 as a watchdog output */
	/* in test mode watch the bit 1 on F4 to indicate "triggered" */
	if (!testmode)
	{
		outb(0x07,0x370);
		outb(0x07,0x371);
		outb(0xE6,0x370);
		outb(0x08,0x371);
	}

	/* lock the SuperIO chip */
	outb(0xAA,0x370);

	return 0;
}

static int wdt977_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 * 	Lock it in if it's a module and we set nowayout
	 */
	if (!nowayout)
	{
		/* unlock the SuperIO chip */
		outb(0x87,0x370);
		outb(0x87,0x370);

		/* select device Aux2 (device=8) and set watchdog regs F2,F3 and F4
		* F3 is reset to its default state
		* F4 can clear the TIMEOUT'ed state (bit 0) - back to default
		* We can not use GP17 as a PowerLed, as we use its usage as a RedLed
		*/
		outb(0x07,0x370);
		outb(0x08,0x371);
		outb(0xF2,0x370);
		outb(0xFF,0x371);
		outb(0xF3,0x370);
		outb(0x00,0x371);
		outb(0xF4,0x370);
		outb(0x00,0x371);
		outb(0xF2,0x370);
		outb(0x00,0x371);

		/* at last select device Aux1 (dev=7) and set GP16 as a watchdog output */
		outb(0x07,0x370);
		outb(0x07,0x371);
		outb(0xE6,0x370);
		outb(0x08,0x371);

		/* lock the SuperIO chip */
		outb(0xAA,0x370);

		clear_bit(0,&timer_alive);

		printk(KERN_INFO "Wdt977 Watchdog: shutdown\n");
	} else {
		printk(KERN_CRIT "WDT device closed unexpectedly.  WDT will not stop!\n");
	}
	return 0;
}


/*
 *      wdt977_write:
 *      @file: file handle to the watchdog
 *      @buf: buffer to write (unused as data does not matter here
 *      @count: count of bytes
 *      @ppos: pointer to the position to write. No seeks allowed
 *
 *      A write to a watchdog device is defined as a keepalive signal. Any
 *      write of data will do, as we we don't define content meaning.
 */

static ssize_t wdt977_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	/* Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (count) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 1;
			}
		}

		kick_wdog();
	}
	return count;
}

/*
 *      wdt977_ioctl:
 *      @inode: inode of the device
 *      @file: file handle to the device
 *      @cmd: watchdog command
 *      @arg: argument pointer
 *
 *      The watchdog API defines a common set of functions for all watchdogs
 *      according to their available features.
 */

static struct watchdog_info ident = {
	.options	= WDIOF_SETTIMEOUT,
	.identity	= "Winbond 83977"
};

static int wdt977_ioctl(struct inode *inode, struct file *file,
         unsigned int cmd, unsigned long arg)
{
	int temp;

	switch(cmd)
	{
	default:
		return -ENOTTY;

	case WDIOC_GETSUPPORT:
	    return copy_to_user((struct watchdog_info *)arg, &ident,
			sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETBOOTSTATUS:
		return put_user(0, (int *) arg);

	case WDIOC_GETSTATUS:
		/* unlock the SuperIO chip */
		outb(0x87,0x370);
		outb(0x87,0x370);

		/* select device Aux2 (device=8) and read watchdog reg F4 */
		outb(0x07,0x370);
		outb(0x08,0x371);
		outb(0xF4,0x370);
		temp = inb(0x371);

		/* lock the SuperIO chip */
		outb(0xAA,0x370);

		/* return info if "expired" in test mode */
		return put_user(temp & 1, (int *) arg);

	case WDIOC_KEEPALIVE:
		kick_wdog();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (copy_from_user(&temp, (int *) arg, sizeof(int)))
			return -EFAULT;

		/* convert seconds to minutes, rounding up */
		temp += 59;
		temp /= 60;

		/* we have a hw bug somewhere, so each 977 minute is actually only 30sec
		*  this limits the max timeout to half of device max of 255 minutes...
		*/
		if (machine_is_netwinder())
		{
		    temp += temp;
		}

		/* Sanity check */
		if (temp < 0 || temp > 255)
			return -EINVAL;

		if (!temp && nowayout)
			return -EINVAL;

		timeoutM = temp;
		kick_wdog();
		return 0;
	}
}


static struct file_operations wdt977_fops=
{
	.owner		= THIS_MODULE,
	.write		= wdt977_write,
	.ioctl		= wdt977_ioctl,
	.open		= wdt977_open,
	.release	= wdt977_release,
};

static struct miscdevice wdt977_miscdev=
{
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &wdt977_fops
};

static int __init nwwatchdog_init(void)
{
	int retval;
	if (!machine_is_netwinder())
		return -ENODEV;

	retval = misc_register(&wdt977_miscdev);
	if (!retval)
		printk(KERN_INFO "Wdt977 Watchdog sleeping.\n");
	return retval;
}

static void __exit nwwatchdog_exit(void)
{
	misc_deregister(&wdt977_miscdev);
}

module_init(nwwatchdog_init);
module_exit(nwwatchdog_exit);

MODULE_DESCRIPTION("W83977AF Watchdog driver");
MODULE_LICENSE("GPL");
