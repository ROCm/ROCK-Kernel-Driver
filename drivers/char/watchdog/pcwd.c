/*
 * PC Watchdog Driver
 * by Ken Hollis (khollis@bitgate.com)
 *
 * Permission granted from Simon Machell (73244.1270@compuserve.com)
 * Written for the Linux Kernel, and GPLed by Ken Hollis
 *
 * 960107	Added request_region routines, modulized the whole thing.
 * 960108	Fixed end-of-file pointer (Thanks to Dan Hollis), added
 *		WD_TIMEOUT define.
 * 960216	Added eof marker on the file, and changed verbose messages.
 * 960716	Made functional and cosmetic changes to the source for
 *		inclusion in Linux 2.0.x kernels, thanks to Alan Cox.
 * 960717	Removed read/seek routines, replaced with ioctl.  Also, added
 *		check_region command due to Alan's suggestion.
 * 960821	Made changes to compile in newer 2.0.x kernels.  Added
 *		"cold reboot sense" entry.
 * 960825	Made a few changes to code, deleted some defines and made
 *		typedefs to replace them.  Made heartbeat reset only available
 *		via ioctl, and removed the write routine.
 * 960828	Added new items for PC Watchdog Rev.C card.
 * 960829	Changed around all of the IOCTLs, added new features,
 *		added watchdog disable/re-enable routines.  Added firmware
 *		version reporting.  Added read routine for temperature.
 *		Removed some extra defines, added an autodetect Revision
 *		routine.
 * 961006       Revised some documentation, fixed some cosmetic bugs.  Made
 *              drivers to panic the system if it's overheating at bootup.
 * 961118	Changed some verbiage on some of the output, tidied up
 *		code bits, and added compatibility to 2.1.x.
 * 970912       Enabled board on open and disable on close.
 * 971107	Took account of recent VFS changes (broke read).
 * 971210       Disable board on initialisation in case board already ticking.
 * 971222       Changed open/close for temperature handling
 *              Michael Meskes <meskes@debian.org>.
 * 980112       Used minor numbers from include/linux/miscdevice.h
 * 990403       Clear reset status after reading control status register in
 *              pcwd_showprevstate(). [Marc Boucher <marc@mbsi.ca>]
 * 990605	Made changes to code to support Firmware 1.22a, added
 *		fairly useless proc entry.
 * 990610	removed said useless proc code for the merge <alan>
 * 000403	Removed last traces of proc code. <davej>
 * 011214	Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT <Matt_Domsch@dell.com>
 *              Added timeout module option to override default
 */

/*
 *	A bells and whistles driver is available from http://www.pcwd.de/
 *	More info available at http://www.berkprod.com/ or http://www.pcwatchdog.com/
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/config.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/io.h>

/*
 * These are the auto-probe addresses available.
 *
 * Revision A only uses ports 0x270 and 0x370.  Revision C introduced 0x350.
 * Revision A has an address range of 2 addresses, while Revision C has 3.
 */
static int pcwd_ioports[] = { 0x270, 0x350, 0x370, 0x000 };

#define WD_VER                  "1.14 (03/26/2004)"

/*
 * It should be noted that PCWD_REVISION_B was removed because A and B
 * are essentially the same types of card, with the exception that B
 * has temperature reporting.  Since I didn't receive a Rev.B card,
 * the Rev.B card is not supported.  (It's a good thing too, as they
 * are no longer in production.)
 */
#define	PCWD_REVISION_A		1
#define	PCWD_REVISION_C		2

#define	WD_TIMEOUT		4	/* 2 seconds for a timeout */
static int timeout_val = WD_TIMEOUT;
static int timeout = 2;

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds (default=2)");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");


/*
 * These are the defines that describe the control status bits for the
 * PC Watchdog card, revision A.
 */
#define WD_WDRST                0x01	/* Previously reset state */
#define WD_T110                 0x02	/* Temperature overheat sense */
#define WD_HRTBT                0x04	/* Heartbeat sense */
#define WD_RLY2                 0x08	/* External relay triggered */
#define WD_SRLY2                0x80	/* Software external relay triggered */

/*
 * These are the defines that describe the control status bits for the
 * PC Watchdog card, revision C.
 */
#define WD_REVC_WTRP            0x01	/* Watchdog Trip status */
#define WD_REVC_HRBT            0x02	/* Watchdog Heartbeat */
#define WD_REVC_TTRP            0x04	/* Temperature Trip status */

static int current_readport, revision, temp_panic;
static atomic_t open_allowed = ATOMIC_INIT(1);
static char expect_close;
static int initial_status, supports_temp, mode_debug;
static spinlock_t io_lock;

/*
 * PCWD_CHECKCARD
 *
 * This routine checks the "current_readport" to see if the card lies there.
 * If it does, it returns accordingly.
 */
static int __init pcwd_checkcard(void)
{
	int card_dat, prev_card_dat, found = 0, count = 0, done = 0;

	card_dat = 0x00;
	prev_card_dat = 0x00;

	prev_card_dat = inb(current_readport);
	if (prev_card_dat == 0xFF)
		return 0;

	while(count < timeout_val) {

	/* Read the raw card data from the port, and strip off the
	   first 4 bits */

		card_dat = inb_p(current_readport);
		card_dat &= 0x000F;

	/* Sleep 1/2 second (or 500000 microseconds :) */

		mdelay(500);
		done = 0;

	/* If there's a heart beat in both instances, then this means we
	   found our card.  This also means that either the card was
	   previously reset, or the computer was power-cycled. */

		if ((card_dat & WD_HRTBT) && (prev_card_dat & WD_HRTBT) &&
			(!done)) {
			found = 1;
			done = 1;
			break;
		}

	/* If the card data is exactly the same as the previous card data,
	   it's safe to assume that we should check again.  The manual says
	   that the heart beat will change every second (or the bit will
	   toggle), and this can be used to see if the card is there.  If
	   the card was powered up with a cold boot, then the card will
	   not start blinking until 2.5 minutes after a reboot, so this
	   bit will stay at 1. */

		if ((card_dat == prev_card_dat) && (!done)) {
			count++;
			done = 1;
		}

	/* If the card data is toggling any bits, this means that the heart
	   beat was detected, or something else about the card is set. */

		if ((card_dat != prev_card_dat) && (!done)) {
			done = 1;
			found = 1;
			break;
		}

	/* Otherwise something else strange happened. */

		if (!done)
			count++;
	}

	return((found) ? 1 : 0);
}

static int pcwd_start(void)
{
	int stat_reg;

	/*  Enable the port  */
	if (revision == PCWD_REVISION_C) {
		spin_lock(&io_lock);
		outb_p(0x00, current_readport + 3);
		stat_reg = inb_p(current_readport + 2);
		spin_unlock(&io_lock);
		if (stat_reg & 0x10)
		{
			printk(KERN_INFO "pcwd: Could not start watchdog.\n");
			return -EIO;
		}
	}
	return 0;
}

static int pcwd_stop(void)
{
	int stat_reg;

	/*  Disable the board  */
	if (revision == PCWD_REVISION_C) {
		spin_lock(&io_lock);
		outb_p(0xA5, current_readport + 3);
		outb_p(0xA5, current_readport + 3);
		stat_reg = inb_p(current_readport + 2);
		spin_unlock(&io_lock);
		if ((stat_reg & 0x10) == 0)
		{
			printk(KERN_INFO "pcwd: Could not stop watchdog.\n");
			return -EIO;
		}
	}
	return 0;
}

static void pcwd_send_heartbeat(void)
{
	int wdrst_stat;

	wdrst_stat = inb_p(current_readport);
	wdrst_stat &= 0x0F;

	wdrst_stat |= WD_WDRST;

	if (revision == PCWD_REVISION_A)
		outb_p(wdrst_stat, current_readport + 1);
	else
		outb_p(wdrst_stat, current_readport);
}

static int pcwd_get_status(int *status)
{
	int card_status;

	*status=0;
	spin_lock(&io_lock);
	if (revision == PCWD_REVISION_A)
		/* Rev A cards return status information from
		 * the base register, which is used for the
		 * temperature in other cards. */
		card_status = inb(current_readport);
	else {
		/* Rev C cards return card status in the base
		 * address + 1 register. And use different bits
		 * to indicate a card initiated reset, and an
		 * over-temperature condition. And the reboot
		 * status can be reset. */
		card_status = inb(current_readport + 1);
	}
	spin_unlock(&io_lock);

	if (revision == PCWD_REVISION_A) {
		if (card_status & WD_WDRST)
			*status |= WDIOF_CARDRESET;

		if (card_status & WD_T110) {
			*status |= WDIOF_OVERHEAT;
			if (temp_panic) {
				printk (KERN_INFO "pcwd: Temperature overheat trip!\n");
				machine_power_off();
			}
		}
	} else {
		if (card_status & WD_REVC_WTRP)
			*status |= WDIOF_CARDRESET;

		if (card_status & WD_REVC_TTRP) {
			*status |= WDIOF_OVERHEAT;
			if (temp_panic) {
				printk (KERN_INFO "pcwd: Temperature overheat trip!\n");
				machine_power_off();
			}
		}
	}

	return 0;
}

static int pcwd_clear_status(void)
{
	if (revision == PCWD_REVISION_C) {
		spin_lock(&io_lock);
		outb_p(0x00, current_readport + 1); /* clear reset status */
		spin_unlock(&io_lock);
	}
	return 0;
}

static int pcwd_get_temperature(int *temperature)
{
	/* check that port 0 gives temperature info and no command results */
	if (mode_debug)
		return -1;

	*temperature = 0;
	if (!supports_temp)
		return -ENODEV;

	/*
	 * Convert celsius to fahrenheit, since this was
	 * the decided 'standard' for this return value.
	 */
	spin_lock(&io_lock);
	*temperature = ((inb(current_readport)) * 9 / 5) + 32;
	spin_unlock(&io_lock);

	return 0;
}

/*
 *	/dev/watchdog handling
 */

static int pcwd_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	int rv;
	int status;
	int temperature;
	static struct watchdog_info ident=
	{
		.options =		WDIOF_OVERHEAT |
					WDIOF_CARDRESET |
					WDIOF_MAGICCLOSE,
		.firmware_version =	1,
		.identity =		"PCWD",
	};

	switch(cmd) {
	default:
		return -ENOIOCTLCMD;

	case WDIOC_GETSUPPORT:
		if(copy_to_user((void*)arg, &ident, sizeof(ident)))
			return -EFAULT;
		return 0;

	case WDIOC_GETSTATUS:
		pcwd_get_status(&status);
		return put_user(status, (int *) arg);

	case WDIOC_GETBOOTSTATUS:
		return put_user(initial_status, (int *) arg);

	case WDIOC_GETTEMP:
		if (pcwd_get_temperature(&temperature))
			return -EFAULT;

		return put_user(temperature, (int *) arg);

	case WDIOC_SETOPTIONS:
		if (revision == PCWD_REVISION_C)
		{
			if(copy_from_user(&rv, (int*) arg, sizeof(int)))
				return -EFAULT;

			if (rv & WDIOS_DISABLECARD)
			{
				return pcwd_stop();
			}

			if (rv & WDIOS_ENABLECARD)
			{
				return pcwd_start();
			}

			if (rv & WDIOS_TEMPPANIC)
			{
				temp_panic = 1;
			}
		}
		return -EINVAL;

	case WDIOC_KEEPALIVE:
		pcwd_send_heartbeat();
		return 0;
	}

	return 0;
}

static ssize_t pcwd_write(struct file *file, const char *buf, size_t len,
			  loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (len) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		pcwd_send_heartbeat();
	}
	return len;
}

static int pcwd_open(struct inode *inode, struct file *file)
{
	if (!atomic_dec_and_test(&open_allowed) ) {
		atomic_inc( &open_allowed );
		return -EBUSY;
	}

	if (nowayout)
		__module_get(THIS_MODULE);

	/* Activate */
	pcwd_start();
	return(0);
}

static int pcwd_close(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		pcwd_stop();
		atomic_inc( &open_allowed );
	} else {
		printk(KERN_CRIT "pcwd: Unexpected close, not stopping watchdog!\n");
		pcwd_send_heartbeat();
	}
	expect_close = 0;
	return 0;
}

/*
 *	/dev/temperature handling
 */

static ssize_t pcwd_temp_read(struct file *file, char *buf, size_t count,
			 loff_t *ppos)
{
	int temperature;

	/*  Can't seek (pread) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (pcwd_get_temperature(&temperature))
		return -EFAULT;

	if (copy_to_user(buf, &temperature, 1))
		return -EFAULT;

	return 1;
}

static int pcwd_temp_open(struct inode *inode, struct file *file)
{
	if (!supports_temp)
		return -ENODEV;

	return 0;
}

static int pcwd_temp_close(struct inode *inode, struct file *file)
{
	return 0;
}

static inline void get_support(void)
{
	if (inb(current_readport) != 0xF0)
		supports_temp = 1;
}

static inline int get_revision(void)
{
	int r = PCWD_REVISION_C;

	spin_lock(&io_lock);
	if ((inb(current_readport + 2) == 0xFF) ||
	    (inb(current_readport + 3) == 0xFF))
		r=PCWD_REVISION_A;
	spin_unlock(&io_lock);

	return r;
}

static int __init send_command(int cmd)
{
	int i;

	outb_p(cmd, current_readport + 2);
	mdelay(1);

	i = inb(current_readport);
	i = inb(current_readport);

	return(i);
}

static inline char *get_firmware(void)
{
	int i, found = 0, count = 0, one, ten, hund, minor;
	char *ret;

	ret = kmalloc(6, GFP_KERNEL);
	if(ret == NULL)
		return NULL;

	while((count < 3) && (!found)) {
		outb_p(0x80, current_readport + 2);
		i = inb(current_readport);

		if (i == 0x00)
			found = 1;
		else if (i == 0xF3)
			outb_p(0x00, current_readport + 2);

		udelay(400L);
		count++;
	}

	if (found) {
		mode_debug = 1;

		one = send_command(0x81);
		ten = send_command(0x82);
		hund = send_command(0x83);
		minor = send_command(0x84);
		sprintf(ret, "%c.%c%c%c", one, ten, hund, minor);
	}
	else
		sprintf(ret, "ERROR");

	return(ret);
}

static void debug_off(void)
{
	outb_p(0x00, current_readport + 2);
	mode_debug = 0;
}

static struct file_operations pcwd_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= pcwd_write,
	.ioctl		= pcwd_ioctl,
	.open		= pcwd_open,
	.release	= pcwd_close,
};

static struct miscdevice pcwd_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&pcwd_fops,
};

static struct file_operations pcwd_temp_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= pcwd_temp_read,
	.open		= pcwd_temp_open,
	.release	= pcwd_temp_close,
};

static struct miscdevice temp_miscdev = {
	.minor =	TEMP_MINOR,
	.name =		"temperature",
	.fops =		&pcwd_temp_fops,
};

static void __init pcwd_validate_timeout(void)
{
	timeout_val = timeout * 2;
}

static int __init pcwatchdog_init(void)
{
	char *firmware;
	int i, found = 0;
	pcwd_validate_timeout();
	spin_lock_init(&io_lock);

	revision = PCWD_REVISION_A;

	printk(KERN_INFO "pcwd: v%s Ken Hollis (kenji@bitgate.com)\n", WD_VER);

	/* Initial variables */
	supports_temp = 0;
	mode_debug = 0;
	temp_panic = 0;
	initial_status = 0x0000;

#ifndef	PCWD_BLIND
	for (i = 0; pcwd_ioports[i] != 0; i++) {
		current_readport = pcwd_ioports[i];

		if (pcwd_checkcard()) {
			found = 1;
			break;
		}
	}

	if (!found) {
		printk(KERN_INFO "pcwd: No card detected, or port not available.\n");
		return(-EIO);
	}
#endif

#ifdef	PCWD_BLIND
	current_readport = PCWD_BLIND;
#endif

	get_support();
	revision = get_revision();

	/* get the boot_status */
	pcwd_get_status(&initial_status);

	/* clear the "card caused reboot" flag */
	pcwd_clear_status();

	if (revision == PCWD_REVISION_A)
		printk(KERN_INFO "pcwd: PC Watchdog (REV.A) detected at port 0x%03x\n", current_readport);
	else if (revision == PCWD_REVISION_C) {
		firmware = get_firmware();
		printk(KERN_INFO "pcwd: PC Watchdog (REV.C) detected at port 0x%03x (Firmware version: %s)\n",
			current_readport, firmware);
		kfree(firmware);
	} else {
		/* Should NEVER happen, unless get_revision() fails. */
		printk("pcwd: Unable to get revision.\n");
		return -1;
	}

	debug_off();

	if (supports_temp)
		printk(KERN_INFO "pcwd: Temperature Option Detected.\n");

	if (initial_status & WDIOF_CARDRESET)
		printk(KERN_INFO "pcwd: Previous reboot was caused by the card.\n");

	if (initial_status & WDIOF_OVERHEAT) {
		printk(KERN_EMERG "pcwd: Card senses a CPU Overheat.  Panicking!\n");
		printk(KERN_EMERG "pcwd: CPU Overheat.\n");
	}

	if (initial_status == 0)
		printk(KERN_INFO "pcwd: No previous trip detected - Cold boot or reset\n");

	/*  Disable the board  */
	if (revision == PCWD_REVISION_C) {
		outb_p(0xA5, current_readport + 3);
		outb_p(0xA5, current_readport + 3);
	}

	if (misc_register(&pcwd_miscdev))
		return -ENODEV;

	if (supports_temp)
		if (misc_register(&temp_miscdev)) {
			misc_deregister(&pcwd_miscdev);
			return -ENODEV;
		}


	if (revision == PCWD_REVISION_A) {
		if (!request_region(current_readport, 2, "PCWD Rev.A (Berkshire)")) {
			misc_deregister(&pcwd_miscdev);
			if (supports_temp)
				misc_deregister(&pcwd_miscdev);
			return -EIO;
		}
	}
	else
		if (!request_region(current_readport, 4, "PCWD Rev.C (Berkshire)")) {
			misc_deregister(&pcwd_miscdev);
			if (supports_temp)
				misc_deregister(&pcwd_miscdev);
			return -EIO;
		}

	return 0;
}

static void __exit pcwatchdog_exit(void)
{
	misc_deregister(&pcwd_miscdev);
	/*  Disable the board  */
	if (revision == PCWD_REVISION_C) {
		outb_p(0xA5, current_readport + 3);
		outb_p(0xA5, current_readport + 3);
	}
	if (supports_temp)
		misc_deregister(&temp_miscdev);

	release_region(current_readport, (revision == PCWD_REVISION_A) ? 2 : 4);
}

module_init(pcwatchdog_init);
module_exit(pcwatchdog_exit);

MODULE_AUTHOR("Ken Hollis <kenji@bitgate.com>");
MODULE_DESCRIPTION("Berkshire ISA-PC Watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS_MISCDEV(TEMP_MINOR);
