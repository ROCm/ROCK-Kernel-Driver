/*
 *	AMD 766/768 TCO Timer Driver
 *	(c) Copyright 2002 Zwane Mwaikambo <zwane@commfireservices.com>
 *	All Rights Reserved.
 *
 *	Parts from;
 *	Hardware driver for the AMD 768 Random Number Generator (RNG)
 *	(c) Copyright 2001 Red Hat Inc <alan@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version 2
 *	as published by the Free Software Foundation.
 *
 *	The author(s) of this software shall not be held liable for damages
 *	of any nature resulting due to the use of this software. This
 *	software is provided AS-IS with no warranties.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <asm/semaphore.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/pci.h>

#define AMDTCO_MODULE_VER	"build 20020601"
#define AMDTCO_MODULE_NAME	"amd7xx_tco"
#define PFX			AMDTCO_MODULE_NAME ": "

#define	MAX_TIMEOUT	38	/* max of 38 seconds */

/* pmbase registers */
#define GLOBAL_SMI_REG	0x2a
#define TCO_EN		(1 << 1)	/* bit 1 in global SMI register */
#define TCO_RELOAD_REG	0x40		/* bits 0-5 are current count, 6-7 are reserved */
#define TCO_INITVAL_REG	0x41		/* bits 0-5 are value to load, 6-7 are reserved */
#define TCO_TIMEOUT_MASK	0x3f
#define TCO_STATUS2_REG	0x46
#define NDTO_STS2	(1 << 1)	/* we're interested in the second timeout */ 
#define BOOT_STS	(1 << 2)	/* will be set if NDTO_STS2 was set before reboot */
#define TCO_CTRL1_REG	0x48
#define TCO_HALT	(1 << 11)

static char banner[] __initdata = KERN_INFO PFX AMDTCO_MODULE_VER;
static int timeout = 38;
static u32 pmbase;		/* PMxx I/O base */
static struct pci_dev *dev;
static struct semaphore open_sem;
spinlock_t amdtco_lock;	/* only for device access */
static int expect_close = 0;

MODULE_PARM(timeout, "i");
MODULE_PARM_DESC(timeout, "range is 0-38 seconds, default is 38");

static inline int amdtco_status(void)
{
	u16 reg;
	int status = 0;

	reg = inb(pmbase+TCO_CTRL1_REG);
	if ((reg & TCO_HALT) == 0)
		status |= WDIOF_KEEPALIVEPING;

	reg = inb(pmbase+TCO_STATUS2_REG);
	if (reg & BOOT_STS)
		status |= WDIOF_CARDRESET;

	return status;
}

static inline void amdtco_ping(void)
{
	u8 reg;

	spin_lock(&amdtco_lock);
	reg = inb(pmbase+TCO_RELOAD_REG);
	outb(1 | reg, pmbase+TCO_RELOAD_REG);
	spin_unlock(&amdtco_lock);
}

static inline int amdtco_gettimeout(void)
{
	return inb(TCO_RELOAD_REG) & TCO_TIMEOUT_MASK;
}

static inline void amdtco_settimeout(unsigned int timeout)
{
	u8 reg;

	spin_lock(&amdtco_lock);
	reg = inb(pmbase+TCO_INITVAL_REG);
	reg |= timeout & TCO_TIMEOUT_MASK;
	outb(reg, pmbase+TCO_INITVAL_REG);
	spin_unlock(&amdtco_lock);
}

static inline void amdtco_global_enable(void)
{
	u16 reg;

	spin_lock(&amdtco_lock);
	reg = inw(pmbase+GLOBAL_SMI_REG);
	reg |= TCO_EN;
	outw(reg, pmbase+GLOBAL_SMI_REG);
	spin_unlock(&amdtco_lock);
}

static inline void amdtco_enable(void)
{
	u16 reg;
	
	spin_lock(&amdtco_lock);
	reg = inw(pmbase+TCO_CTRL1_REG);
	reg &= ~TCO_HALT;
	outw(reg, pmbase+TCO_CTRL1_REG);
	spin_unlock(&amdtco_lock);
}

static inline void amdtco_disable(void)
{
	u16 reg;

	spin_lock(&amdtco_lock);
	reg = inw(pmbase+TCO_CTRL1_REG);
	reg |= TCO_HALT;
	outw(reg, pmbase+TCO_CTRL1_REG);
	spin_unlock(&amdtco_lock);
}

static int amdtco_fop_open(struct inode *inode, struct file *file)
{
	if (down_trylock(&open_sem))
		return -EBUSY;

#ifdef CONFIG_WATCHDOG_NOWAYOUT	
	MOD_INC_USE_COUNT;
#endif

	if (timeout > MAX_TIMEOUT)
		timeout = MAX_TIMEOUT;

	amdtco_settimeout(timeout);	
	amdtco_global_enable();
	amdtco_ping();
	printk(KERN_INFO PFX "Watchdog enabled, timeout = %d/%d seconds",
		amdtco_gettimeout(), timeout);
	
	return 0;
}


static int amdtco_fop_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int new_timeout;
	int tmp;

	static struct watchdog_info ident = {
		.options	= WDIOF_SETTIMEOUT | WDIOF_CARDRESET,
		.identity	= "AMD 766/768"
	};

	switch (cmd) {
		default:
			return -ENOTTY;	

		case WDIOC_GETSUPPORT:
			if (copy_to_user((struct watchdog_info *)arg, &ident, sizeof ident))
				return -EFAULT;
			return 0;

		case WDIOC_GETSTATUS:
			return put_user(amdtco_status(), (int *)arg);
	
		case WDIOC_KEEPALIVE:
			amdtco_ping();
			return 0;

		case WDIOC_SETTIMEOUT:
			if (get_user(new_timeout, (int *)arg))
				return -EFAULT;
			
			if (new_timeout < 0)
				return -EINVAL;
		
			if (new_timeout > MAX_TIMEOUT)
				new_timeout = MAX_TIMEOUT;

			timeout = new_timeout;
			amdtco_settimeout(timeout);
			/* fall through and return the new timeout */

		case WDIOC_GETTIMEOUT:
			return put_user(amdtco_gettimeout(), (int *)arg);
	
		case WDIOC_SETOPTIONS:
			if (copy_from_user(&tmp, (int *)arg, sizeof tmp))
                                return -EFAULT;

			if (tmp & WDIOS_DISABLECARD)
				amdtco_disable();

			if (tmp & WDIOS_ENABLECARD)
				amdtco_enable();
			
			return 0;
	}
}


static int amdtco_fop_release(struct inode *inode, struct file *file)
{
	if (expect_close) {
		amdtco_disable();	
		printk(KERN_INFO PFX "Watchdog disabled\n");
	} else {
		amdtco_ping();
		printk(KERN_CRIT PFX "Unexpected close!, timeout in %d seconds)\n", timeout);
	}	
	
	up(&open_sem);
	return 0;
}


static ssize_t amdtco_fop_write(struct file *file, const char *data, size_t len, loff_t *ppos)
{
	if (ppos != &file->f_pos)
		return -ESPIPE;
	
	if (len) {
#ifndef CONFIG_WATCHDOG_NOWAYOUT
		size_t i;
		char c;
		expect_close = 0;
		
		for (i = 0; i != len; i++) {
			if (get_user(c, data + i))
				return -EFAULT;

			if (c == 'V')
				expect_close = 1;
		}
#endif
		amdtco_ping();
		return len;
	}

	return 0;
}


static int amdtco_notify_sys(struct notifier_block *this, unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		amdtco_disable();

	return NOTIFY_DONE;
}


static struct notifier_block amdtco_notifier =
{
	notifier_call:	amdtco_notify_sys
};

static struct file_operations amdtco_fops =
{
	.owner		= THIS_MODULE,
	.write		= amdtco_fop_write,
	.ioctl		= amdtco_fop_ioctl,
	.open		= amdtco_fop_open,
	.release	= amdtco_fop_release
};

static struct miscdevice amdtco_miscdev =
{
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &amdtco_fops
};

static struct pci_device_id amdtco_pci_tbl[] __initdata = {
	/* AMD 766 PCI_IDs here */
	{ 0x1022, 0x7443, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, }
};

MODULE_DEVICE_TABLE (pci, amdtco_pci_tbl);

static int __init amdtco_init(void)
{
	int ret;

	sema_init(&open_sem, 1);
	spin_lock_init(&amdtco_lock);

	pci_for_each_dev(dev) {
		if (pci_match_device (amdtco_pci_tbl, dev) != NULL)
			goto found_one;
	}

	return -ENODEV;

found_one:
	
	if ((ret = register_reboot_notifier(&amdtco_notifier))) {
		printk(KERN_ERR PFX "Unable to register reboot notifier err = %d\n", ret);
		goto out_clean;
	}

	if ((ret = misc_register(&amdtco_miscdev))) {
		printk(KERN_ERR PFX "Unable to register miscdev on minor %d\n", WATCHDOG_MINOR);
		goto out_unreg_reboot;
	}

	pci_read_config_dword(dev, 0x58, &pmbase);
	pmbase &= 0x0000FF00;

	if (pmbase == 0) {
		printk (KERN_ERR PFX "power management base not set\n");
		ret = -EIO;
		goto out_unreg_misc;
	}

	/* ret = 0; */
	printk(banner);
	goto out_clean;

out_unreg_misc:
	misc_deregister(&amdtco_miscdev);
out_unreg_reboot:
	unregister_reboot_notifier(&amdtco_notifier);
out_clean:
	return ret;
}

static void __exit amdtco_exit(void)
{
	misc_deregister(&amdtco_miscdev);
	unregister_reboot_notifier(&amdtco_notifier);
}


#ifndef MODULE
static int __init amdtco_setup(char *str)
{
	int ints[4];

	str = get_options (str, ARRAY_SIZE(ints), ints);
	if (ints[0] > 0)
		timeout = ints[1];

	return 1;
}

__setup("amd7xx_tco=", amdtco_setup);
#endif

module_init(amdtco_init);
module_exit(amdtco_exit);

MODULE_AUTHOR("Zwane Mwaikambo <zwane@commfireservices.com>");
MODULE_DESCRIPTION("AMD 766/768 TCO Timer Driver");
MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;

