/*
 *	i810-tco 0.02:	TCO timer driver for i810 chipsets
 *
 *	(c) Copyright 2000 kernel concepts <nils@kernelconcepts.de>, All Rights Reserved.
 *				http://www.kernelconcepts.de
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 *	Neither kernel concepts nor Nils Faerber admit liability nor provide 
 *	warranty for any of this software. This material is provided 
 *	"AS-IS" and at no charge.	
 *
 *	(c) Copyright 2000	kernel concepts <nils@kernelconcepts.de>
 *				developed for
 *                              Jentro AG, Haar/Munich (Germany)
 *
 *	TCO timer driver for i810/i815 chipsets
 *	based on softdog.c by Alan Cox <alan@redhat.com>
 *
 *	The TCO timer is implemented in the 82801AA (82801AB) chip,
 *	see intel documentation from http://developer.intel.com,
 *	order number 290655-003
 *
 *  20000710 Nils Faerber
 *	Initial Version 0.01
 *  20000728 Nils Faerber
 *      0.02 Fix for SMI_EN->TCO_EN bit, some cleanups
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
#include <linux/pci.h>
#include <linux/ioport.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include "i810-tco.h"


/* Just in case that the PCI vendor and device IDs are not yet defined */
#ifndef PCI_DEVICE_ID_INTEL_82801AA_0
#define PCI_DEVICE_ID_INTEL_82801AA_0	0x2410
#endif

/* Default expire timeout */
#define TIMER_MARGIN	50	/* steps of 0.6sec, 2<n<64. Default is 30 seconds */

static unsigned int ACPIBASE;
static spinlock_t tco_lock;	/* Guards the hardware */

static int i810_margin = TIMER_MARGIN;	/* steps of 0.6sec */

MODULE_PARM (i810_margin, "i");

/*
 *	Timer active flag
 */

static int timer_alive;
static int boot_status;

/*
 * Some i810 specific functions
 */


/*
 * Start the timer countdown
 */
static int tco_timer_start (void)
{
	unsigned char val;

	spin_lock(&tco_lock);
	val = inb (TCO1_CNT + 1);
	val &= 0xf7;
	outb (val, TCO1_CNT + 1);
	val = inb (TCO1_CNT + 1);
	spin_unlock(&tco_lock);
	
	if (val & 0x08)
		return -1;
	return 0;
}

/*
 * Stop the timer countdown
 */
static int tco_timer_stop (void)
{
	unsigned char val;

	spin_lock(&tco_lock);
	val = inb (TCO1_CNT + 1);
	val |= 0x08;
	outb (val, TCO1_CNT + 1);
	val = inb (TCO1_CNT + 1);
	spin_unlock(&tco_lock);
	
	if ((val & 0x08) == 0)
		return -1;
	return 0;
}

/*
 * Set the timer reload value
 */
static int tco_timer_settimer (unsigned char tmrval)
{
	unsigned char val;

	/* from the specs: */
	/* "Values of 0h-3h are ignored and should not be attempted" */
	if (tmrval > 0x3f || tmrval < 0x03)
		return -1;
	
	spin_lock(&tco_lock);
	val = inb (TCO1_TMR);
	val &= 0xc0;
	val |= tmrval;
	outb (val, TCO1_TMR);
	val = inb (TCO1_TMR);
	spin_unlock(&tco_lock);
	
	if ((val & 0x3f) != tmrval)
		return -1;

	return 0;
}

/*
 * Reload (trigger) the timer. Lock is needed so we dont reload it during
 * a reprogramming event
 */
 
static void tco_timer_reload (void)
{
	spin_lock(&tco_lock);
	outb (0x01, TCO1_RLD);
	spin_unlock(&tco_lock);
}

/*
 * Read the current timer value
 */
static unsigned char tco_timer_read (void)
{
	return (inb (TCO1_RLD));
}


/*
 *	Allow only one person to hold it open
 */

static int i810tco_open (struct inode *inode, struct file *file)
{
	if (timer_alive)
		return -EBUSY;

	/*
	 *      Reload and activate timer
	 */
	tco_timer_reload ();
	tco_timer_start ();
	timer_alive = 1;
	return 0;
}

static int i810tco_release (struct inode *inode, struct file *file)
{
	/*
	 *      Shut off the timer.
	 */
	tco_timer_stop ();
	timer_alive = 0;
	return 0;
}

static ssize_t i810tco_write (struct file *file, const char *data,
			      size_t len, loff_t * ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	/*
	 *      Refresh the timer.
	 */
	if (len) {
		tco_timer_reload ();
		return 1;
	}
	return 0;
}

static int i810tco_ioctl (struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	static struct watchdog_info ident = {
		0,
		0,
		"i810 TCO timer"
	};
	switch (cmd) {
	default:
		return -ENOIOCTLCMD;
	case WDIOC_GETSUPPORT:
		if (copy_to_user
		    ((struct watchdog_info *) arg, &ident, sizeof (ident)))
			return -EFAULT;
		return 0;
	case WDIOC_GETSTATUS:
		return put_user (tco_timer_read (),
				 (unsigned int *) (int) arg);
	case WDIOC_GETBOOTSTATUS:
		return put_user (boot_status, (int *) arg);
	case WDIOC_KEEPALIVE:
		tco_timer_reload ();
		return 0;
	}
}

static struct pci_dev *i810tco_pci;

static unsigned char i810tco_getdevice (void)
{
	u8 val1, val2;
	u16 badr;
	/*
	 *      Find the PCI device which has vendor id 0x8086
	 *      and device ID 0x2410
	 */
	i810tco_pci = pci_find_device (PCI_VENDOR_ID_INTEL,
				       PCI_DEVICE_ID_INTEL_82801AA_0, NULL);
	if (i810tco_pci) {
		/*
		 *      Find the ACPI base I/O address which is the base
		 *      for the TCO registers (TCOBASE=ACPIBASE + 0x60)
		 *      ACPIBASE is bits [15:7] from 0x40-0x43
		 */
		pci_read_config_byte (i810tco_pci, 0x40, &val1);
		pci_read_config_byte (i810tco_pci, 0x41, &val2);
		badr = ((val2 << 1) | (val1 >> 7)) << 7;
		ACPIBASE = badr;
		/* Something's wrong here, ACPIBASE has to be set */
		if (badr == 0x0001 || badr == 0x0000) {
			printk (KERN_ERR "i810tco init: failed to get TCOBASE address\n");
			return 0;
		}
		/*
		 * Check chipset's NO_REBOOT bit
		 */
		pci_read_config_byte (i810tco_pci, 0xd4, &val1);
		if (val1 & 0x02) {
			val1 &= 0xfd;
			pci_write_config_byte (i810tco_pci, 0xd4, val1);
			pci_read_config_byte (i810tco_pci, 0xd4, &val1);
			if (val1 & 0x02) {
				printk (KERN_ERR "i810tco init: failed to reset NO_REBOOT flag\n");
				return 0;	/* Cannot reset NO_REBOOT bit */
			}
		}
		/* Set the TCO_EN bit in SMI_EN register */
		val1 = inb (SMI_EN + 1);
		val1 &= 0xdf;
		outb (val1, SMI_EN + 1);
		/* Clear out the (probably old) status */
		outb (0, TCO1_STS);
		boot_status = (int) inb (TCO2_STS);
		outb (3, TCO2_STS);
		return 1;
	}
	return 0;
}

static struct file_operations i810tco_fops = {
	owner:		THIS_MODULE,
	write:		i810tco_write,
	ioctl:		i810tco_ioctl,
	open:		i810tco_open,
	release:	i810tco_release,
};

static struct miscdevice i810tco_miscdev = {
	WATCHDOG_MINOR,
	"watchdog",
	&i810tco_fops
};

static int __init watchdog_init (void)
{
	spin_lock_init(&tco_lock);
	if (!i810tco_getdevice () || i810tco_pci == NULL)
		return -ENODEV;
	if (!request_region (TCOBASE, 0x10, "i810 TCO")) {
		printk (KERN_ERR
			"i810 TCO timer: I/O address 0x%04x already in use\n",
			TCOBASE);
		return -EIO;
	}
	if (misc_register (&i810tco_miscdev) != 0) {
		release_region (TCOBASE, 0x10);
		printk (KERN_ERR "i810 TCO timer: cannot register miscdev\n");
		return -EIO;
	}
	tco_timer_settimer ((unsigned char) i810_margin);
	tco_timer_reload ();

	printk (KERN_INFO
		"i810 TCO timer: V0.02, timer margin: %d sec (0x%04x)\n",
		(int) (i810_margin * 6 / 10), TCOBASE);
	return 0;
}

static void __exit watchdog_cleanup (void)
{
	u8 val;

	/* Reset the timer before we leave */
	tco_timer_reload ();
	/* Set the NO_REBOOT bit to prevent later reboots, just for sure */
	pci_read_config_byte (i810tco_pci, 0xd4, &val);
	val |= 0x02;
	pci_write_config_byte (i810tco_pci, 0xd4, val);
	release_region (TCOBASE, 0x10);
	misc_deregister (&i810tco_miscdev);
}

module_init(watchdog_init);
module_exit(watchdog_cleanup);
