/*

	Hardware driver for Intel i810 Random Number Generator (RNG)
	Copyright 2000 Jeff Garzik <jgarzik@mandrakesoft.com>
	Copyright 2000 Philipp Rumpf <prumpf@tux.org>

	Driver Web site:  http://gtf.org/garzik/drivers/i810_rng/



	Based on:
	Intel 82802AB/82802AC Firmware Hub (FWH) Datasheet
		May 1999 Order Number: 290658-002 R

	Intel 82802 Firmware Hub: Random Number Generator
	Programmer's Reference Manual
		December 1999 Order Number: 298029-001 R

	Intel 82802 Firmware HUB Random Number Generator Driver
	Copyright (c) 2000 Matt Sottek <msottek@quiknet.com>

	Special thanks to Matt Sottek.  I did the "guts", he
	did the "brains" and all the testing.  (Anybody wanna send
	me an i810 or i820?)

	----------------------------------------------------------

	This software may be used and distributed according to the terms
        of the GNU Public License, incorporated herein by reference.

	----------------------------------------------------------

	From the firmware hub datasheet:

	The Firmware Hub integrates a Random Number Generator (RNG)
	using thermal noise generated from inherently random quantum
	mechanical properties of silicon. When not generating new random
	bits the RNG circuitry will enter a low power state. Intel will
	provide a binary software driver to give third party software
	access to our RNG for use as a security feature. At this time,
	the RNG is only to be used with a system in an OS-present state.

	----------------------------------------------------------

	Theory of operation:

	This driver has TWO modes of operation:

	Mode 1
	------
	Character driver.  Using the standard open()
	and read() system calls, you can read random data from
	the i810 RNG device.  This data is NOT CHECKED by any
	fitness tests, and could potentially be bogus (if the
	hardware is faulty or has been tampered with).

	/dev/intel_rng is char device major 10, minor 183.


	Mode 2
	------
	Injection of entropy into the kernel entropy pool via a
	timer function.

	A timer is run at rng_timer_len intervals, reading 8 bits
	of data from the RNG.  If the RNG has previously passed a
	FIPS test, then the data will be added to the /dev/random
	entropy pool.  Then, those 8 bits are added to an internal
	test data pool.  When that pool is full, a FIPS test is
	run to verify that the last N bytes read are decently random.

	Thus, the RNG will never be enabled until it passes a
	FIPS test.  And, data will stop flowing into the system
	entropy pool if the data is determined to be non-random.

	Finally, note that the timer defaults to OFF.  This ensures
	that the system entropy pool will not be polluted with
	RNG-originated data unless a conscious decision is made
	by the user.

	HOWEVER NOTE THAT UP TO 2499 BYTES OF DATA CAN BE BOGUS
	BEFORE THE SYSTEM WILL NOTICE VIA THE FIPS TEST.

	----------------------------------------------------------

	Driver notes:

	* You may enable and disable the RNG timer via sysctl:

		# disable RNG
		echo 0 > /proc/sys/dev/i810_rng_timer

		# enable RNG
		echo 1 > /proc/sys/dev/i810_rng_timer

	* The default number of entropy bits added by default is
	the full 8 bits.  If you wish to reduce this value for
	paranoia's sake, you can do so via sysctl as well:

		# Add only 4 bits of entropy to /dev/random
		echo 4 > /proc/sys/dev/i810_rng_entropy

	* The default number of entropy bits can also be set via
	a module parameter "rng_entropy" at module load time.

	* When the RNG timer is enabled, the driver reads 1 byte
	from the hardware RNG every N jiffies.  By default, every
	half-second.  If you would like to change the timer interval,
	do so via another sysctl:

		echo 200 > /proc/sys/dev/i810_rng_interval

	NOTE THIS VALUE IS IN JIFFIES, NOT SECONDS OR MILLISECONDS.
	Minimum interval is 1 jiffy, maximum interval is 24 hours.

	* In order to unload the i810_rng module, you must first
	disable the hardware via sysctl i810_hw_enabled, as shown above,
	and make sure all users of the character device have closed

	* The timer and the character device may be used simultaneously,
	if desired.

	* FIXME: support poll()

	* FIXME: should we be crazy and support mmap()?

	* FIXME: It is possible for the timer function to read,
	and shove into the kernel entropy pool, 2499 bytes of data
	before the internal FIPS test notices that the data is bad.
	The kernel should handle this (I think???), but we should use a
	2500-byte array, and re-run the FIPS test for every byte read.
	This will slow things down but guarantee that bad data is
	never passed upstream.

	* FIXME: module unload is racy.  To fix this, struct ctl_table
	needs an owner member a la struct file_operations.

	* Since the RNG is accessed from a timer as well as normal
	kernel code, but not from interrupts, we use spin_lock_bh
	in regular code, and spin_lock in the timer function, to
	serialize access to the RNG hardware area.

	----------------------------------------------------------

	Change history:

	Version 0.6.2:
	* Clean up spinlocks.  Since we don't have any interrupts
	  to worry about, but we do have a timer to worry about,
	  we use spin_lock_bh everywhere except the timer function
	  itself.
	* Fix module load/unload.
	* Fix timer function and h/w enable/disable logic
	* New timer interval sysctl
	* Clean up sysctl names

	Version 0.9.0:
	* Don't register a pci_driver, because we are really
	  using PCI bridge vendor/device ids, and someone
	  may want to register a driver for the bridge. (bug fix)
	* Don't let the usage count go negative (bug fix)
	* Clean up spinlocks (bug fix)
	* Enable PCI device, if necessary (bug fix)
	* iounmap on module unload (bug fix)
	* If RNG chrdev is already in use when open(2) is called,
	  sleep until it is available.
	* Remove redundant globals rng_allocated, rng_use_count
	* Convert numeric globals to unsigned
	* Module unload cleanup

	Version 0.9.1:
	* Support i815 chipsets too (Matt Sottek)
	* Fix reference counting when statically compiled (prumpf)
	* Rewrite rng_dev_read (prumpf)
	* Make module races less likely (prumpf)
	* Small miscellaneous bug fixes (prumpf)
	* Use pci table for PCI id list

	Version 0.9.2:
	* Simplify open blocking logic

 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/sysctl.h>
#include <linux/miscdevice.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/uaccess.h>


/*
 * core module and version information
 */
#define RNG_VERSION "0.9.2"
#define RNG_MODULE_NAME "i810_rng"
#define RNG_DRIVER_NAME   RNG_MODULE_NAME " hardware driver " RNG_VERSION
#define PFX RNG_MODULE_NAME ": "


/*
 * debugging macros
 */
#undef RNG_DEBUG /* define to 1 to enable copious debugging info */

#ifdef RNG_DEBUG
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define RNG_NDEBUG 0        /* define to 1 to disable lightweight runtime checks */
#if RNG_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(!(expr)) {                                   \
        printk( "Assertion failed! %s,%s,%s,line=%d\n", \
        #expr,__FILE__,__FUNCTION__,__LINE__);          \
        }
#endif


/*
 * prototypes
 */
static void rng_fips_test_store (int rng_data);
static void rng_run_fips_test (void);


/*
 * RNG registers (offsets from rng_mem)
 */
#define RNG_HW_STATUS			0
#define		RNG_PRESENT		0x40
#define		RNG_ENABLED		0x01
#define RNG_STATUS			1
#define		RNG_DATA_PRESENT	0x01
#define RNG_DATA			2

#define RNG_ADDR			0xFFBC015F
#define RNG_ADDR_LEN			3

#define RNG_MAX_ENTROPY			8 /* max entropy h/w is capable of */

#define RNG_MISCDEV_MINOR		183 /* official */


/*
 * Frequency that data is added to kernel entropy pool
 * HZ>>1 == every half-second
 */
#define RNG_DEF_TIMER_LEN		(HZ >> 1)


/*
 * number of bytes required for a FIPS test.
 * do not alter unless you really, I mean
 * REALLY know what you are doing.
 */
#define RNG_FIPS_TEST_THRESHOLD	2500


/*
 * various RNG status variables.  they are globals
 * as we only support a single RNG device
 */
static int rng_hw_enabled;		/* is the RNG h/w enabled? */
static int rng_timer_enabled;		/* is the RNG timer enabled? */
static int rng_trusted;			/* does FIPS trust out data? */
static int rng_enabled_sysctl;		/* sysctl for enabling/disabling RNG */
static unsigned int rng_entropy = 8;	/* number of entropy bits we submit to /dev/random */
static unsigned int rng_entropy_sysctl;	/* sysctl for changing entropy bits */
static unsigned int rng_interval_sysctl; /* sysctl for changing timer interval */
static int rng_have_mem_region;		/* did we grab RNG region via request_mem_region? */
static unsigned int rng_fips_counter;	/* size of internal FIPS test data pool */
static unsigned int rng_timer_len = RNG_DEF_TIMER_LEN; /* timer interval, in jiffies */
static void *rng_mem;			/* token to our ioremap'd RNG register area */
static spinlock_t rng_lock = SPIN_LOCK_UNLOCKED; /* hardware lock */
static struct timer_list rng_timer;	/* kernel timer for RNG hardware reads and tests */
static struct pci_dev *rng_pdev;	/* Firmware Hub PCI device found during PCI probe */
static struct semaphore rng_open_sem;	/* Semaphore for serializing rng_open/release */


/*
 * inlined helper functions for accessing RNG registers
 */
static inline u8 rng_hwstatus (void)
{
	assert (rng_mem != NULL);
	return readb (rng_mem + RNG_HW_STATUS);
}


static inline void rng_hwstatus_set (u8 hw_status)
{
	assert (rng_mem != NULL);
	writeb (hw_status, rng_mem + RNG_HW_STATUS);
}


static inline int rng_data_present (void)
{
	assert (rng_mem != NULL);
	assert (rng_hw_enabled == 1);

	return (readb (rng_mem + RNG_STATUS) & RNG_DATA_PRESENT) ? 1 : 0;
}


static inline int rng_data_read (void)
{
	assert (rng_mem != NULL);
	assert (rng_hw_enabled == 1);

	return readb (rng_mem + RNG_DATA);
}


/*
 * rng_timer_ticker - executes every rng_timer_len jiffies,
 *		      adds a single byte to system entropy
 *		      and internal FIPS test pools
 */
static void rng_timer_tick (unsigned long data)
{
	int rng_data;

	spin_lock (&rng_lock);

	if (rng_data_present ()) {
		/* gimme some thermal noise, baby */
		rng_data = rng_data_read ();

		spin_unlock (&rng_lock);

		/*
		 * if RNG has been verified in the past, add
		 * data just read to the /dev/random pool,
		 * with the entropy specified by the user
		 * via sysctl (defaults to 8 bits)
		 */
		if (rng_trusted)
			batch_entropy_store (rng_data, jiffies, rng_entropy);

		/* fitness testing via FIPS, if we have enough data */
		rng_fips_test_store (rng_data);
		if (rng_fips_counter > RNG_FIPS_TEST_THRESHOLD)
			rng_run_fips_test ();
	} else {
		spin_unlock (&rng_lock);
	}

	/* run the timer again, if enabled */
	if (rng_timer_enabled) {
		rng_timer.expires = jiffies + rng_timer_len;
		add_timer (&rng_timer);
	}
}


/*
 * rng_enable - enable or disable the RNG hardware
 */
static int rng_enable (int enable)
{
	int rc = 0, action = 0;
	u8 hw_status, new_status;

	DPRINTK ("ENTER\n");

	spin_lock_bh (&rng_lock);

	hw_status = rng_hwstatus ();

	if (enable) {
		rng_hw_enabled++;
		MOD_INC_USE_COUNT;
	} else {
		if (rng_hw_enabled) {
			rng_hw_enabled--;
			MOD_DEC_USE_COUNT;
		}
	}

	if (rng_hw_enabled && ((hw_status & RNG_ENABLED) == 0)) {
		rng_hwstatus_set (hw_status | RNG_ENABLED);
		action = 1;
	}

	else if (!rng_hw_enabled && (hw_status & RNG_ENABLED)) {
		rng_hwstatus_set (hw_status & ~RNG_ENABLED);
		action = 2;
	}

	new_status = rng_hwstatus ();

	spin_unlock_bh (&rng_lock);

	if (action == 1)
		printk (KERN_INFO PFX "RNG h/w enabled\n");
	else if (action == 2)
		printk (KERN_INFO PFX "RNG h/w disabled\n");

	/* too bad C doesn't have ^^ */
	if ((!enable) != (!(new_status & RNG_ENABLED))) {
		printk (KERN_ERR PFX "Unable to %sable the RNG\n",
			enable ? "en" : "dis");
		rc = -EIO;
	}

	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}


/*
 * rng_handle_sysctl_enable - handle a read or write of our enable/disable sysctl
 */

static int rng_handle_sysctl_enable (ctl_table * table, int write, struct file *filp,
				     void *buffer, size_t * lenp)
{
	int enabled_save, rc;

	DPRINTK ("ENTER\n");

	MOD_INC_USE_COUNT;
	spin_lock_bh (&rng_lock);
	rng_enabled_sysctl = enabled_save = rng_timer_enabled;
	spin_unlock_bh (&rng_lock);

	rc = proc_dointvec (table, write, filp, buffer, lenp);
	if (rc)
		return rc;

	spin_lock_bh (&rng_lock);
	if (enabled_save != rng_enabled_sysctl) {
		rng_timer_enabled = rng_enabled_sysctl;
		spin_unlock_bh (&rng_lock);

		/* enable/disable hardware */
		rng_enable (rng_enabled_sysctl);

		/* enable/disable timer */
		if (rng_enabled_sysctl) {
			rng_timer.expires = jiffies + rng_timer_len;
			add_timer (&rng_timer);
		} else {
			del_timer_sync (&rng_timer);
		}
	} else {
		spin_unlock_bh (&rng_lock);
	}

	/* This needs to be in a higher layer */
	MOD_DEC_USE_COUNT;

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/*
 * rng_handle_sysctl_entropy - handle a read or write of our entropy bits sysctl
 */

static int rng_handle_sysctl_entropy (ctl_table * table, int write, struct file *filp,
				      void *buffer, size_t * lenp)
{
	int entropy_bits_save, rc;

	DPRINTK ("ENTER\n");

	spin_lock_bh (&rng_lock);
	rng_entropy_sysctl = entropy_bits_save = rng_entropy;
	spin_unlock_bh (&rng_lock);

	rc = proc_dointvec (table, write, filp, buffer, lenp);
	if (rc)
		return rc;

	if (entropy_bits_save == rng_entropy_sysctl)
		goto out;

	if ((rng_entropy_sysctl >= 0) &&
    	    (rng_entropy_sysctl <= 8)) {
		spin_lock_bh (&rng_lock);
		rng_entropy = rng_entropy_sysctl;
		spin_unlock_bh (&rng_lock);

		printk (KERN_INFO PFX "entropy bits now %d\n", rng_entropy_sysctl);
	} else {
		printk (KERN_INFO PFX "ignoring invalid entropy setting (%d)\n",
			rng_entropy_sysctl);
	}

out:
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}

/*
 * rng_handle_sysctl_interval - handle a read or write of our timer interval len sysctl
 */

static int rng_handle_sysctl_interval (ctl_table * table, int write, struct file *filp,
				      void *buffer, size_t * lenp)
{
	int timer_len_save, rc;

	DPRINTK ("ENTER\n");

	spin_lock_bh (&rng_lock);
	rng_interval_sysctl = timer_len_save = rng_timer_len;
	spin_unlock_bh (&rng_lock);

	rc = proc_dointvec (table, write, filp, buffer, lenp);
	if (rc)
		return rc;

	if (timer_len_save == rng_interval_sysctl)
		goto out;

	if ((rng_interval_sysctl > 0) &&
    	    (rng_interval_sysctl < (HZ*86400))) {
		spin_lock_bh (&rng_lock);
		rng_timer_len = rng_interval_sysctl;
		spin_unlock_bh (&rng_lock);

		printk (KERN_INFO PFX "timer interval now %d\n", rng_interval_sysctl);
	} else {
		printk (KERN_INFO PFX "ignoring invalid timer interval (%d)\n",
			rng_interval_sysctl);
	}

out:
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/*
 * rng_sysctl - add or remove the rng sysctl
 */
static void rng_sysctl (int add)
{
#define DEV_I810_TIMER		1
#define DEV_I810_ENTROPY	2
#define DEV_I810_INTERVAL	3

	/* Definition of the sysctl */
	/* FIXME: use new field:value style of struct initialization */
	static ctl_table rng_sysctls[] = {
		{DEV_I810_TIMER,		/* ID */
		 RNG_MODULE_NAME "_timer",	/* name in /proc */
		 &rng_enabled_sysctl,
		 sizeof (rng_enabled_sysctl),	/* data ptr, data size */
		 0644,				/* mode */
		 0,				/* child */
		 rng_handle_sysctl_enable,	/* proc handler */
		 0,				/* strategy */
		 0,				/* proc control block */
		 0, 0}
		,
		{DEV_I810_ENTROPY,		/* ID */
		 RNG_MODULE_NAME "_entropy",	/* name in /proc */
		 &rng_entropy_sysctl,
		 sizeof (rng_entropy_sysctl),	/* data ptr, data size */
		 0644,				/* mode */
		 0,				/* child */
		 rng_handle_sysctl_entropy,	/* proc handler */
		 0,				/* strategy */
		 0,				/* proc control block */
		 0, 0}
		,
		{DEV_I810_INTERVAL,		/* ID */
		 RNG_MODULE_NAME "_interval",	/* name in /proc */
		 &rng_interval_sysctl,
		 sizeof (rng_interval_sysctl),	/* data ptr, data size */
		 0644,				/* mode */
		 0,				/* child */
		 rng_handle_sysctl_interval,	/* proc handler */
		 0,				/* strategy */
		 0,				/* proc control block */
		 0, 0}
		,
		{0}
	};

	/* Define the parent file : /proc/sys/dev */
	static ctl_table sysctls_root[] = {
		{CTL_DEV,
		 "dev",
		 NULL, 0,
		 0555,
		 rng_sysctls},
		{0}
	};
	static struct ctl_table_header *sysctls_root_header = NULL;

	if (add) {
		if (!sysctls_root_header)
			sysctls_root_header = register_sysctl_table (sysctls_root, 0);
	} else if (sysctls_root_header) {
		unregister_sysctl_table (sysctls_root_header);
		sysctls_root_header = NULL;
	}
}


static int rng_dev_open (struct inode *inode, struct file *filp)
{
	int rc = -EINVAL;

	if ((filp->f_mode & FMODE_READ) == 0)
		return rc;
	if (filp->f_mode & FMODE_WRITE)
		return rc;

	/* wait for device to become free */
	if (filp->f_flags & O_NONBLOCK) {
		if (down_trylock (&rng_open_sem))
			return -EAGAIN;
	} else {
		if (down_interruptible (&rng_open_sem))
			return -ERESTARTSYS;
	}

	if (rng_enable (1)) {
		rc = -EIO;
		goto err_out;
	}

	return 0;

err_out:
	up (&rng_open_sem);
	return rc;
}


static int rng_dev_release (struct inode *inode, struct file *filp)
{
	rng_enable(0);
	up (&rng_open_sem);
	return 0;
}


static ssize_t rng_dev_read (struct file *filp, char *buf, size_t size,
			     loff_t * offp)
{
	int have_data;
	u8 data = 0;
	ssize_t ret = 0;

	while (size) {
		spin_lock_bh (&rng_lock);

		have_data = 0;
		if (rng_data_present ()) {
			data = rng_data_read ();
			have_data = 1;
		}

		spin_unlock_bh (&rng_lock);

		if (have_data) {
			if (put_user (data, buf++)) {
				ret = ret ? : -EFAULT;
				break;
			}
			size--;
			ret++;
		}

		if (current->need_resched)
			schedule ();

		if (signal_pending (current))
			return ret ? : -ERESTARTSYS;

		if (filp->f_flags & O_NONBLOCK)
			return ret ? : -EAGAIN;
	}

	return ret;
}


/*
 * rng_init_one - look for and attempt to init a single RNG
 */
static int __init rng_init_one (struct pci_dev *dev)
{
	int rc;
	u8 hw_status;

	DPRINTK ("ENTER\n");

	if (pci_enable_device (dev))
		return -EIO;

	/* XXX currently fails, investigate who has our mem region */
	if (request_mem_region (RNG_ADDR, RNG_ADDR_LEN, RNG_MODULE_NAME))
		rng_have_mem_region = 1;

	rng_mem = ioremap (RNG_ADDR, RNG_ADDR_LEN);
	if (rng_mem == NULL) {
		printk (KERN_ERR PFX "cannot ioremap RNG Memory\n");
		DPRINTK ("EXIT, returning -EBUSY\n");
		rc = -EBUSY;
		goto err_out_free_res;
	}

	/* Check for Intel 82802 */
	hw_status = rng_hwstatus ();
	if ((hw_status & RNG_PRESENT) == 0) {
		printk (KERN_ERR PFX "RNG not detected\n");
		DPRINTK ("EXIT, returning -ENODEV\n");
		rc = -ENODEV;
		goto err_out_free_map;
	}

	if (rng_entropy < 0 || rng_entropy > RNG_MAX_ENTROPY)
		rng_entropy = RNG_MAX_ENTROPY;

	/* init core RNG timer, but do not add it */
	init_timer (&rng_timer);
	rng_timer.function = rng_timer_tick;

	/* turn RNG h/w off, if it's on */
	rc = rng_enable (0);
	if (rc) {
		printk (KERN_ERR PFX "cannot disable RNG, aborting\n");
		goto err_out_free_map;
	}

	/* add sysctls */
	rng_sysctl (1);

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out_free_map:
	iounmap (rng_mem);
err_out_free_res:
	if (rng_have_mem_region)
		release_mem_region (RNG_ADDR, RNG_ADDR_LEN);
	return rc;
}


/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might one day
 * want to register another driver on the same PCI id.
 */
const static struct pci_device_id rng_pci_tbl[] __initdata = {
	{ 0x8086, 0x2418, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x8086, 0x2428, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x8086, 0x1130, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, },
};
MODULE_DEVICE_TABLE (pci, rng_pci_tbl);


MODULE_AUTHOR("Jeff Garzik, Matt Sottek");
MODULE_DESCRIPTION("Intel i8xx chipset Random Number Generator (RNG) driver");
MODULE_PARM(rng_entropy, "1i");
MODULE_PARM_DESC(rng_entropy, "Bits of entropy to add to random pool per RNG byte (range: 0-8, default 8)");


static struct file_operations rng_chrdev_ops = {
	owner:		THIS_MODULE,
	open:		rng_dev_open,
	release:	rng_dev_release,
	read:		rng_dev_read,
};


static struct miscdevice rng_miscdev = {
	RNG_MISCDEV_MINOR,
	RNG_MODULE_NAME,
	&rng_chrdev_ops,
};


/*
 * rng_init - initialize RNG module
 */
static int __init rng_init (void)
{
	int rc;
	struct pci_dev *pdev;

	DPRINTK ("ENTER\n");

	init_MUTEX (&rng_open_sem);

	pci_for_each_dev(pdev) {
		if (pci_match_device (rng_pci_tbl, pdev) != NULL)
			goto match;
	}

	DPRINTK ("EXIT, returning -ENODEV\n");
	return -ENODEV;

match:
	rc = rng_init_one (pdev);
	if (rc)
		return rc;

	rc = misc_register (&rng_miscdev);
	if (rc) {
		iounmap (rng_mem);
		if (rng_have_mem_region)
			release_mem_region (RNG_ADDR, RNG_ADDR_LEN);
		DPRINTK ("EXIT, returning %d\n", rc);
		return rc;
	}

	printk (KERN_INFO RNG_DRIVER_NAME " loaded\n");

	rng_pdev = pdev;

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/*
 * rng_init - shutdown RNG module
 */
static void __exit rng_cleanup (void)
{
	DPRINTK ("ENTER\n");

	assert (rng_timer_enabled == 0);
	assert (rng_hw_enabled == 0);

	misc_deregister (&rng_miscdev);

	rng_sysctl (0);

	iounmap (rng_mem);
	if (rng_have_mem_region)
		release_mem_region (RNG_ADDR, RNG_ADDR_LEN);

	DPRINTK ("EXIT\n");
}


module_init (rng_init);
module_exit (rng_cleanup);




/* These are the startup tests suggested by the FIPS 140-1 spec section
*  4.11.1 (http://csrc.nist.gov/fips/fips1401.htm)
*  The Monobit, Poker, Runs, and Long Runs tests are implemented below.
*  This test is run at periodic intervals to verify
*  data is sufficiently random. If the tests are failed the RNG module
*  will no longer submit data to the entropy pool, but the tests will
*  continue to run at the given interval. If at a later time the RNG
*  passes all tests it will be re-enabled for the next period.
*   The reason for this is that it is not unlikely that at some time
*  during normal operation one of the tests will fail. This does not
*  necessarily mean the RNG is not operating properly, it is just a
*  statistically rare event. In that case we don't want to forever
*  disable the RNG, we will just leave it disabled for the period of
*  time until the tests are rerun and passed.
*
*  For argument sake I tested /dev/urandom with these tests and it
*  took 142,095 tries before I got a failure, and urandom isn't as
*  random as random :)
*/

static int poker[16] = { 0, }, runs[12] = { 0, };
static int ones = 0, rlength = -1, current_bit = 0, rng_test = 0;


/*
 * rng_fips_test_store - store 8 bits of entropy in FIPS
 * 			 internal test data pool
 */
static void rng_fips_test_store (int rng_data)
{
	int j;
	static int last_bit = 0;

	DPRINTK ("ENTER, rng_data = %d\n", rng_data);

	poker[rng_data >> 4]++;
	poker[rng_data & 15]++;

	/* Note in the loop below rlength is always one less than the actual
	   run length. This makes things easier. */
	last_bit = (rng_data & 128) >> 7;
	for (j = 7; j >= 0; j--) {
		ones += current_bit = (rng_data & 1 << j) >> j;
		if (current_bit != last_bit) {
			/* If runlength is 1-6 count it in correct bucket. 0's go in
			   runs[0-5] 1's go in runs[6-11] hence the 6*current_bit below */
			if (rlength < 5) {
				runs[rlength +
				     (6 * current_bit)]++;
			} else {
				runs[5 + (6 * current_bit)]++;
			}

			/* Check if we just failed longrun test */
			if (rlength >= 33)
				rng_test &= 8;
			rlength = 0;
			/* flip the current run type */
			last_bit = current_bit;
		} else {
			rlength++;
		}
	}

	DPRINTK ("EXIT\n");
}


/*
 * now that we have some data, run a FIPS test
 */
static void rng_run_fips_test (void)
{
	int j, i;

	DPRINTK ("ENTER\n");

	/* add in the last (possibly incomplete) run */
	if (rlength < 5)
		runs[rlength + (6 * current_bit)]++;
	else {
		runs[5 + (6 * current_bit)]++;
		if (rlength >= 33)
			rng_test &= 8;
	}
	/* Ones test */
	if ((ones >= 10346) || (ones <= 9654))
		rng_test &= 1;
	/* Poker calcs */
	for (i = 0, j = 0; i < 16; i++)
		j += poker[i] * poker[i];
	if ((j >= 1580457) || (j <= 1562821))
		rng_test &= 2;
	if ((runs[0] < 2267) || (runs[0] > 2733) ||
	    (runs[1] < 1079) || (runs[1] > 1421) ||
	    (runs[2] < 502) || (runs[2] > 748) ||
	    (runs[3] < 223) || (runs[3] > 402) ||
	    (runs[4] < 90) || (runs[4] > 223) ||
	    (runs[5] < 90) || (runs[5] > 223) ||
	    (runs[6] < 2267) || (runs[6] > 2733) ||
	    (runs[7] < 1079) || (runs[7] > 1421) ||
	    (runs[8] < 502) || (runs[8] > 748) ||
	    (runs[9] < 223) || (runs[9] > 402) ||
	    (runs[10] < 90) || (runs[10] > 223) ||
	    (runs[11] < 90) || (runs[11] > 223)) {
		rng_test &= 4;
	}

	rng_test = !rng_test;
	DPRINTK ("FIPS test %sed\n", rng_test ? "pass" : "fail");

	/* enable/disable RNG with results of the tests */
	if (rng_test && !rng_trusted)
		printk (KERN_WARNING PFX "FIPS test passed, enabling RNG\n");
	else if (!rng_test && rng_trusted)
		printk (KERN_WARNING PFX "FIPS test failed, disabling RNG\n");

	rng_trusted = rng_test;

	/* finally, clear out FIPS variables for start of next run */
	memset (poker, 0, sizeof (poker));
	memset (runs, 0, sizeof (runs));
	ones = 0;
	rlength = -1;
	current_bit = 0;
	rng_test = 0;

	DPRINTK ("EXIT\n");
}
