/*
 *	AMD Elan SC520 processor Watchdog Timer driver
 *
 *      Based on acquirewdt.c by Alan Cox,
 *           and sbc60xxwdt.c by Jakob Oestergaard <jakob@unthought.net>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	The authors do NOT admit liability nor provide warranty for
 *	any of this software. This material is provided "AS-IS" in
 *      the hope that it may be useful for others.
 *
 *	(c) Copyright 2001    Scott Jennings <linuxdrivers@oro.net>
 *           9/27 - 2001      [Initial release]
 *
 *	Additional fixes Alan Cox
 *	-	Fixed formatting
 *	-	Removed debug printks
 *	-	Fixed SMP built kernel deadlock
 *	-	Switched to private locks not lock_kernel
 *	-	Used ioremap/writew/readw
 *	-	Added NOWAYOUT support
 *
 *     4/12 - 2002 Changes by Rob Radez <rob@osinvestor.com>
 *     -       Change comments
 *     -       Eliminate fop_llseek
 *     -       Change CONFIG_WATCHDOG_NOWAYOUT semantics
 *     -       Add KERN_* tags to printks
 *     -       fix possible wdt_is_open race
 *     -       Report proper capabilities in watchdog_info
 *     -       Add WDIOC_{GETSTATUS, GETBOOTSTATUS, SETTIMEOUT,
 *             GETTIMEOUT, SETOPTIONS} ioctls
 *     09/8 - 2003 Changes by Wim Van Sebroeck <wim@iguana.be>
 *     -       cleanup of trailing spaces
 *     -       added extra printk's for startup problems
 *     -       use module_param
 *     -       made timeout (the emulated heartbeat) a module_param
 *     -       made the keepalive ping an internal subroutine
 *
 *  This WDT driver is different from most other Linux WDT
 *  drivers in that the driver will ping the watchdog by itself,
 *  because this particular WDT has a very short timeout (1.6
 *  seconds) and it would be insane to count on any userspace
 *  daemon always getting scheduled within that time frame.
 *
 *  This driver uses memory mapped IO, and spinlock.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

/*
 * The SC520 can timeout anywhere from 492us to 32.21s.
 * If we reset the watchdog every ~250ms we should be safe.
 */

#define WDT_INTERVAL (HZ/4+1)

/*
 * We must not require too good response from the userspace daemon.
 * Here we require the userspace daemon to send us a heartbeat
 * char to /dev/watchdog every 30 seconds.
 */

#define WATCHDOG_TIMEOUT 30		/* 30 sec default timeout */
static int timeout = WATCHDOG_TIMEOUT;	/* in seconds, will be multiplied by HZ to get seconds to wait for a ping */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds. (1<=timeout<=3600, default=" __MODULE_STRING(WATCHDOG_TIMEOUT) ")");

/*
 * AMD Elan SC520 timeout value is 492us times a power of 2 (0-7)
 *
 *   0: 492us    2: 1.01s    4: 4.03s   6: 16.22s
 *   1: 503ms    3: 2.01s    5: 8.05s   7: 32.21s
 */

#define TIMEOUT_EXPONENT ( 1 << 3 )  /* 0x08 = 2.01s */

/* #define MMCR_BASE_DEFAULT 0xfffef000 */
#define MMCR_BASE_DEFAULT ((__u16 *)0xffffe)
#define OFFS_WDTMRCTL ((unsigned int)0xcb0)
#define WDT_ENB 0x8000		/* [15] Watchdog Timer Enable */
#define WDT_WRST_ENB 0x4000	/* [14] Watchdog Timer Reset Enable */

#define OUR_NAME "sc520_wdt"
#define PFX OUR_NAME ": "

#define WRT_DOG(data) *wdtmrctl=data

static __u16 *wdtmrctl;

static void wdt_timer_ping(unsigned long);
static struct timer_list timer;
static unsigned long next_heartbeat;
static unsigned long wdt_is_open;
static char wdt_expect_close;
static spinlock_t wdt_spinlock;

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

/*
 *	Whack the dog
 */

static void wdt_timer_ping(unsigned long data)
{
	/* If we got a heartbeat pulse within the WDT_US_INTERVAL
	 * we agree to ping the WDT
	 */
	if(time_before(jiffies, next_heartbeat))
	{
		/* Ping the WDT */
		spin_lock(&wdt_spinlock);
		writew(0xAAAA, wdtmrctl);
		writew(0x5555, wdtmrctl);
		spin_unlock(&wdt_spinlock);

		/* Re-set the timer interval */
		timer.expires = jiffies + WDT_INTERVAL;
		add_timer(&timer);
	} else {
		printk(KERN_WARNING PFX "Heartbeat lost! Will not ping the watchdog\n");
	}
}

/*
 * Utility routines
 */

static void wdt_config(int writeval)
{
	__u16 dummy;
	unsigned long flags;

	/* buy some time (ping) */
	spin_lock_irqsave(&wdt_spinlock, flags);
	dummy=readw(wdtmrctl);  /* ensure write synchronization */
	writew(0xAAAA, wdtmrctl);
	writew(0x5555, wdtmrctl);
	/* make WDT configuration register writable one time */
	writew(0x3333, wdtmrctl);
	writew(0xCCCC, wdtmrctl);
	/* write WDT configuration register */
	writew(writeval, wdtmrctl);
	spin_unlock_irqrestore(&wdt_spinlock, flags);
}

static void wdt_startup(void)
{
	next_heartbeat = jiffies + (timeout * HZ);

	/* Start the timer */
	timer.expires = jiffies + WDT_INTERVAL;
	add_timer(&timer);

	wdt_config(WDT_ENB | WDT_WRST_ENB | TIMEOUT_EXPONENT);
	printk(KERN_INFO PFX "Watchdog timer is now enabled.\n");
}

static void wdt_turnoff(void)
{
	if (!nowayout) {
		/* Stop the timer */
		del_timer(&timer);
		wdt_config(0);
		printk(KERN_INFO PFX "Watchdog timer is now disabled...\n");
	}
}

static void wdt_keepalive(void)
{
	/* user land ping */
	next_heartbeat = jiffies + (timeout * HZ);
}

/*
 * /dev/watchdog handling
 */

static ssize_t fop_write(struct file * file, const char * buf, size_t count, loff_t * ppos)
{
	/* We can't seek */
	if(ppos != &file->f_pos)
		return -ESPIPE;

	/* See if we got the magic character 'V' and reload the timer */
	if(count)
	{
		if (!nowayout)
		{
			size_t ofs;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
			wdt_expect_close = 0;

			/* now scan */
			for(ofs = 0; ofs != count; ofs++) {
				char c;
				if (get_user(c, buf + ofs))
					return -EFAULT;
				if(c == 'V')
					wdt_expect_close = 42;
			}
		}

		/* Well, anyhow someone wrote to us, we should return that favour */
		wdt_keepalive();
	}
	return count;
}

static int fop_open(struct inode * inode, struct file * file)
{
	/* Just in case we're already talking to someone... */
	if(test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;
	if (nowayout)
		__module_get(THIS_MODULE);

	/* Good, fire up the show */
	wdt_startup();
	return 0;
}

static int fop_close(struct inode * inode, struct file * file)
{
	if(wdt_expect_close == 42)
		wdt_turnoff();
	else {
		del_timer(&timer);
		printk(KERN_CRIT PFX "device file closed unexpectedly. Will not stop the WDT!\n");
	}
	clear_bit(0, &wdt_is_open);
	wdt_expect_close = 0;
	return 0;
}

static int fop_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	static struct watchdog_info ident=
	{
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "SC520",
	};

	switch(cmd)
	{
		default:
			return -ENOIOCTLCMD;
		case WDIOC_GETSUPPORT:
			return copy_to_user((struct watchdog_info *)arg, &ident, sizeof(ident))?-EFAULT:0;
		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			return put_user(0, (int *)arg);
		case WDIOC_KEEPALIVE:
			wdt_keepalive();
			return 0;
		case WDIOC_SETOPTIONS:
		{
			int new_options, retval = -EINVAL;

			if(get_user(new_options, (int *)arg))
				return -EFAULT;

			if(new_options & WDIOS_DISABLECARD) {
				wdt_turnoff();
				retval = 0;
			}

			if(new_options & WDIOS_ENABLECARD) {
				wdt_startup();
				retval = 0;
			}

			return retval;
		}
		case WDIOC_SETTIMEOUT:
		{
			int new_timeout;

			if(get_user(new_timeout, (int *)arg))
				return -EFAULT;

			if(new_timeout < 1 || new_timeout > 3600) /* arbitrary upper limit */
				return -EINVAL;

			timeout = new_timeout;
			wdt_keepalive();
			/* Fall through */
		}
		case WDIOC_GETTIMEOUT:
			return put_user(timeout, (int *)arg);
	}
}

static struct file_operations wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= fop_write,
	.open		= fop_open,
	.release	= fop_close,
	.ioctl		= fop_ioctl,
};

static struct miscdevice wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &wdt_fops,
};

/*
 *	Notifier for system down
 */

static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if(code==SYS_DOWN || code==SYS_HALT)
		wdt_turnoff();
	return NOTIFY_DONE;
}

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block wdt_notifier=
{
	.notifier_call = wdt_notify_sys,
	.next = NULL,
	.priority = 0,
};

static void __exit sc520_wdt_unload(void)
{
	wdt_turnoff();

	/* Deregister */
	misc_deregister(&wdt_miscdev);
	iounmap(wdtmrctl);
	unregister_reboot_notifier(&wdt_notifier);
}

static int __init sc520_wdt_init(void)
{
	int rc = -EBUSY;
	unsigned long cbar;

	spin_lock_init(&wdt_spinlock);

	if(timeout < 1 || timeout > 3600) /* arbitrary upper limit */
	{
		timeout = WATCHDOG_TIMEOUT;
		printk(KERN_INFO PFX "timeout value must be 1<=x<=3600, using %d\n",
			timeout);
	}

	init_timer(&timer);
	timer.function = wdt_timer_ping;
	timer.data = 0;

	rc = misc_register(&wdt_miscdev);
	if (rc)
	{
		printk(KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			wdt_miscdev.minor, rc);
		goto err_out_region2;
	}

	rc = register_reboot_notifier(&wdt_notifier);
	if (rc)
	{
		printk(KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			rc);
		goto err_out_miscdev;
	}

	/* get the Base Address Register */
	cbar = inl_p(0xfffc);
	printk(KERN_INFO PFX "CBAR: 0x%08lx\n", cbar);
	/* check if MMCR aliasing bit is set */
	if (cbar & 0x80000000) {
		printk(KERN_INFO PFX "MMCR Aliasing enabled.\n");
		wdtmrctl = (__u16 *)(cbar & 0x3fffffff);
	} else {
		printk(KERN_INFO PFX "!!! WARNING !!!\n"
		  "\t MMCR Aliasing found NOT enabled!\n"
		  "\t Using default value of: %p\n"
		  "\t This has not been tested!\n"
		  "\t Please email Scott Jennings <smj@oro.net>\n"
		  "\t  and Bill Jennings <bj@oro.net> if it works!\n"
		  , MMCR_BASE_DEFAULT
		  );
	  wdtmrctl = MMCR_BASE_DEFAULT;
	}

	wdtmrctl = (__u16 *)((char *)wdtmrctl + OFFS_WDTMRCTL);
	wdtmrctl = ioremap((unsigned long)wdtmrctl, 2);
	if (!wdtmrctl) {
		printk (KERN_ERR PFX "Unable to remap memory.\n");
		rc = -ENOMEM;
		goto err_out_notifier;
	}
	printk(KERN_INFO PFX "WDT driver for SC520 initialised. timeout=%d sec (nowayout=%d)\n",
		timeout,nowayout);

	return 0;

err_out_notifier:
	unregister_reboot_notifier(&wdt_notifier);
err_out_miscdev:
	misc_deregister(&wdt_miscdev);
err_out_region2:
	return rc;
}

module_init(sc520_wdt_init);
module_exit(sc520_wdt_unload);

MODULE_AUTHOR("Scott and Bill Jennings");
MODULE_DESCRIPTION("Driver for watchdog timer in AMD \"Elan\" SC520 uProcessor");
MODULE_LICENSE("GPL");
