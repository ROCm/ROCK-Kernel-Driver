/*
 *  $Id: speedstep.c,v 1.53 2002/09/29 23:43:11 db Exp $
 *
 * (C) 2001  Dave Jones, Arjan van de ven.
 * (C) 2002  Dominik Brodowski <linux@brodo.de>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon reverse engineered information, and on Intel documentation
 *  for chipsets ICH2-M and ICH3-M.
 *
 *  Many thanks to Ducrot Bruno for finding and fixing the last
 *  "missing link" for ICH2-M/ICH3-M support, and to Thomas Winkler 
 *  for extensive testing.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */


/*********************************************************************
 *                        SPEEDSTEP - DEFINITIONS                    *
 *********************************************************************/

#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/msr.h>


static struct cpufreq_driver		*speedstep_driver;

/* speedstep_chipset:
 *   It is necessary to know which chipset is used. As accesses to 
 * this device occur at various places in this module, we need a 
 * static struct pci_dev * pointing to that device.
 */
static unsigned int                     speedstep_chipset;
static struct pci_dev                   *speedstep_chipset_dev;

#define SPEEDSTEP_CHIPSET_ICH2M         0x00000002
#define SPEEDSTEP_CHIPSET_ICH3M         0x00000003


/* speedstep_processor
 */
static unsigned int                     speedstep_processor;

#define SPEEDSTEP_PROCESSOR_PIII_C      0x00000001  /* Coppermine core */
#define SPEEDSTEP_PROCESSOR_PIII_T      0x00000002  /* Tualatin core */
#define SPEEDSTEP_PROCESSOR_P4M         0x00000003  /* P4-M with 100 MHz FSB */


/* speedstep_[low,high]_freq
 *   There are only two frequency states for each processor. Values
 * are in kHz for the time being.
 */
static unsigned int                     speedstep_low_freq;
static unsigned int                     speedstep_high_freq;

#define SPEEDSTEP_HIGH                  0x00000000
#define SPEEDSTEP_LOW                   0x00000001


/* DEBUG
 *   Define it if you want verbose debug output, e.g. for bug reporting
 */
//#define SPEEDSTEP_DEBUG

#ifdef SPEEDSTEP_DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0);
#endif



/*********************************************************************
 *                    LOW LEVEL CHIPSET INTERFACE                    *
 *********************************************************************/

/**
 * speedstep_get_state - read the current SpeedStep state
 * @state: Speedstep state (SPEEDSTEP_LOW or SPEEDSTEP_HIGH)
 *
 *   Tries to read the SpeedStep state. Returns -EIO when there has been
 * trouble to read the status or write to the control register, -EINVAL
 * on an unsupported chipset, and zero on success.
 */
static int speedstep_get_state (unsigned int *state)
{
	unsigned long   flags;
	u32             pmbase;
	u8              value;

	if (!speedstep_chipset_dev || !state)
		return -EINVAL;

	switch (speedstep_chipset) {
	case SPEEDSTEP_CHIPSET_ICH2M:
	case SPEEDSTEP_CHIPSET_ICH3M:
		/* get PMBASE */
		pci_read_config_dword(speedstep_chipset_dev, 0x40, &pmbase);
		if (!(pmbase & 0x01))
			return -EIO;

		pmbase &= 0xFFFFFFFE;
		if (!pmbase) 
			return -EIO;

		/* read state */
		local_irq_save(flags);
		value = inb(pmbase + 0x50);
		local_irq_restore(flags);

		dprintk(KERN_DEBUG "cpufreq: read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

		*state = value & 0x01;
		return 0;

	}

	printk (KERN_ERR "cpufreq: setting CPU frequency on this chipset unsupported.\n");
	return -EINVAL;
}


/**
 * speedstep_set_state - set the SpeedStep state
 * @state: new processor frequency state (SPEEDSTEP_LOW or SPEEDSTEP_HIGH)
 *
 *   Tries to change the SpeedStep state. 
 */
static void speedstep_set_state (unsigned int state, int notify)
{
	u32                     pmbase;
	u8	                pm2_blk;
	u8                      value;
	unsigned long           flags;
	unsigned int            oldstate;
	struct cpufreq_freqs    freqs;

	if (!speedstep_chipset_dev || (state > 0x1))
		return;

	if (speedstep_get_state(&oldstate))
		return;

	if (oldstate == state)
		return;

	freqs.old = (oldstate == SPEEDSTEP_HIGH) ? speedstep_high_freq : speedstep_low_freq;
	freqs.new = (state == SPEEDSTEP_HIGH) ? speedstep_high_freq : speedstep_low_freq;
	freqs.cpu = CPUFREQ_ALL_CPUS; /* speedstep.c is UP only driver */
	
	if (notify)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	switch (speedstep_chipset) {
	case SPEEDSTEP_CHIPSET_ICH2M:
	case SPEEDSTEP_CHIPSET_ICH3M:
		/* get PMBASE */
		pci_read_config_dword(speedstep_chipset_dev, 0x40, &pmbase);
		if (!(pmbase & 0x01))
		{
			printk(KERN_ERR "cpufreq: could not find speedstep register\n");
			return;
		}

		pmbase &= 0xFFFFFFFE;
		if (!pmbase) {
			printk(KERN_ERR "cpufreq: could not find speedstep register\n");
			return;
		}

		/* Disable IRQs */
		local_irq_save(flags);

		/* read state */
		value = inb(pmbase + 0x50);

		dprintk(KERN_DEBUG "cpufreq: read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

		/* write new state */
		value &= 0xFE;
		value |= state;

		dprintk(KERN_DEBUG "cpufreq: writing 0x%x to pmbase 0x%x + 0x50\n", value, pmbase);

		/* Disable bus master arbitration */
		pm2_blk = inb(pmbase + 0x20);
		pm2_blk |= 0x01;
		outb(pm2_blk, (pmbase + 0x20));

		/* Actual transition */
		outb(value, (pmbase + 0x50));

		/* Restore bus master arbitration */
		pm2_blk &= 0xfe;
		outb(pm2_blk, (pmbase + 0x20));

		/* check if transition was sucessful */
		value = inb(pmbase + 0x50);

		/* Enable IRQs */
		local_irq_restore(flags);

		dprintk(KERN_DEBUG "cpufreq: read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

		if (state == (value & 0x1)) {
			dprintk (KERN_INFO "cpufreq: change to %u MHz succeded\n", (freqs.new / 1000));
		} else {
			printk (KERN_ERR "cpufreq: change failed - I/O error\n");
		}
		break;
	default:
		printk (KERN_ERR "cpufreq: setting CPU frequency on this chipset unsupported.\n");
	}

	if (notify)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return;
}


/**
 * speedstep_activate - activate SpeedStep control in the chipset
 *
 *   Tries to activate the SpeedStep status and control registers.
 * Returns -EINVAL on an unsupported chipset, and zero on success.
 */
static int speedstep_activate (void)
{
	if (!speedstep_chipset_dev)
		return -EINVAL;

	switch (speedstep_chipset) {
	case SPEEDSTEP_CHIPSET_ICH2M:
	case SPEEDSTEP_CHIPSET_ICH3M:
	{
		u16             value = 0;

		pci_read_config_word(speedstep_chipset_dev, 
				     0x00A0, &value);
		if (!(value & 0x08)) {
			value |= 0x08;
			dprintk(KERN_DEBUG "cpufreq: activating SpeedStep (TM) registers\n");
			pci_write_config_word(speedstep_chipset_dev, 
					      0x00A0, value);
		}

		return 0;
	}
	}
	
	printk (KERN_ERR "cpufreq: SpeedStep (TM) on this chipset unsupported.\n");
	return -EINVAL;
}


/**
 * speedstep_detect_chipset - detect the Southbridge which contains SpeedStep logic
 *
 *   Detects PIIX4, ICH2-M and ICH3-M so far. The pci_dev points to 
 * the LPC bridge / PM module which contains all power-management 
 * functions. Returns the SPEEDSTEP_CHIPSET_-number for the detected
 * chipset, or zero on failure.
 */
static unsigned int speedstep_detect_chipset (void)
{
	speedstep_chipset_dev = pci_find_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801CA_12, 
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev)
		return SPEEDSTEP_CHIPSET_ICH3M;


	speedstep_chipset_dev = pci_find_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801BA_10,
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev) {
		/* speedstep.c causes lockups on Dell Inspirons 8000 and
		 * 8100 which use a pretty old revision of the 82815 
		 * host brige. Abort on these systems.
		 */
		static struct pci_dev   *hostbridge;
		u8			rev = 0;

		hostbridge  = pci_find_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82815_MC,
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);

		if (!hostbridge)
			return SPEEDSTEP_CHIPSET_ICH2M;
			
		pci_read_config_byte(hostbridge, PCI_REVISION_ID, &rev);
		if (rev < 5) {
			dprintk(KERN_INFO "cpufreq: hostbrige does not support speedstep\n");
			speedstep_chipset_dev = NULL;
			return 0;
		}

		return SPEEDSTEP_CHIPSET_ICH2M;
	}

	return 0;
}



/*********************************************************************
 *                   LOW LEVEL PROCESSOR INTERFACE                   *
 *********************************************************************/


/**
 * pentium3_get_frequency - get the core frequencies for PIIIs
 *
 *   Returns the core frequency of a Pentium III processor (in kHz)
 */
static unsigned int pentium3_get_frequency (void)
{
        /* See table 14 of p3_ds.pdf and table 22 of 29834003.pdf */
	struct {
		unsigned int ratio;	/* Frequency Multiplier (x10) */
		u8 bitmap;	        /* power on configuration bits
					   [27, 25:22] (in MSR 0x2a) */
	} msr_decode_mult [] = {
		{ 30, 0x01 },
		{ 35, 0x05 },
		{ 40, 0x02 },
		{ 45, 0x06 },
		{ 50, 0x00 },
		{ 55, 0x04 },
		{ 60, 0x0b },
		{ 65, 0x0f },
		{ 70, 0x09 },
		{ 75, 0x0d },
		{ 80, 0x0a },
		{ 85, 0x26 },
		{ 90, 0x20 },
		{ 100, 0x2b },
		{ 0, 0xff }     /* error or unknown value */
	};
	/* PIII(-M) FSB settings: see table b1-b of 24547206.pdf */
	struct {
		unsigned int value;     /* Front Side Bus speed in MHz */
		u8 bitmap;              /* power on configuration bits [18: 19]
					   (in MSR 0x2a) */
	} msr_decode_fsb [] = {
		{  66, 0x0 },
		{ 100, 0x2 },
		{ 133, 0x1 },
		{   0, 0xff}
	};
	u32     msr_lo, msr_tmp;
	int     i = 0, j = 0;
	struct  cpuinfo_x86 *c = cpu_data;

	/* read MSR 0x2a - we only need the low 32 bits */
	rdmsr(MSR_IA32_EBL_CR_POWERON, msr_lo, msr_tmp);
	dprintk(KERN_DEBUG "cpufreq: P3 - MSR_IA32_EBL_CR_POWERON: 0x%x 0x%x\n", msr_lo, msr_tmp);
	msr_tmp = msr_lo;

	/* decode the FSB */
	msr_tmp &= 0x00c0000;
	msr_tmp >>= 18;
	while (msr_tmp != msr_decode_fsb[i].bitmap) {
		if (msr_decode_fsb[i].bitmap == 0xff)
			return -EINVAL;
		i++;
	}

	/* decode the multiplier */
	if ((c->x86_model == 0x08) && (c->x86_mask == 0x01)) 
                /* different on early Coppermine PIII */
		msr_lo &= 0x03c00000;
	else
		msr_lo &= 0x0bc00000;
	msr_lo >>= 22;
	while (msr_lo != msr_decode_mult[j].bitmap) {
		if (msr_decode_mult[j].bitmap == 0xff)
			return -EINVAL;
		j++;
	}

	return (msr_decode_mult[j].ratio * msr_decode_fsb[i].value * 100);
}


/**
 * pentium4_get_frequency - get the core frequency for P4-Ms
 *
 *   Should return the core frequency (in kHz) for P4-Ms. 
 */
static unsigned int pentium4_get_frequency(void)
{
	u32 msr_lo, msr_hi;

	rdmsr(0x2c, msr_lo, msr_hi);

	dprintk(KERN_DEBUG "cpufreq: P4 - MSR_EBC_FREQUENCY_ID: 0x%x 0x%x\n", msr_lo, msr_hi);

	/* First 12 bits seem to change a lot (0x511, 0x410 and 0x30f seen 
	 * yet). Next 12 bits always seem to be 0x300. If this is not true 
	 * on this CPU, complain. Last 8 bits are frequency (in 100MHz).
	 */
	if (msr_hi || ((msr_lo & 0x00FFF000) != 0x300000)) {
		printk(KERN_DEBUG "cpufreq: P4 - MSR_EBC_FREQUENCY_ID: 0x%x 0x%x\n", msr_lo, msr_hi);
		printk(KERN_INFO "cpufreq: problem in initialization. Please contact Dominik Brodowski\n");
		printk(KERN_INFO "cpufreq: <linux@brodo.de> and attach this dmesg. Thanks in advance\n");
		return 0;
	}

	msr_lo >>= 24;
	return (msr_lo * 100000);
}


/** 
 * speedstep_detect_processor - detect Intel SpeedStep-capable processors.
 *
 *   Returns the SPEEDSTEP_PROCESSOR_-number for the detected chipset, 
 * or zero on failure.
 */
static unsigned int speedstep_detect_processor (void)
{
	struct cpuinfo_x86 *c = cpu_data;
	u32                     ebx;

	if ((c->x86_vendor != X86_VENDOR_INTEL) || 
	    ((c->x86 != 6) && (c->x86 != 0xF)))
		return 0;

	if (c->x86 == 0xF) {
		/* Intel Pentium 4 Mobile P4-M */
		if (c->x86_model != 2)
			return 0;

		if (c->x86_mask != 4)
			return 0;

		ebx = cpuid_ebx(0x00000001);
		ebx &= 0x000000FF;
		if ((ebx != 0x0e) && (ebx != 0x0f))
			return 0;

		return SPEEDSTEP_PROCESSOR_P4M;
	}

	switch (c->x86_model) {
	case 0x0B: /* Intel PIII [Tualatin] */
		/* cpuid_ebx(1) is 0x04 for desktop PIII, 
		                   0x06 for mobile PIII-M */
		ebx = cpuid_ebx(0x00000001);

		ebx &= 0x000000FF;
		if (ebx != 0x06)
			return 0;

		/* So far all PIII-M processors support SpeedStep. See
		 * Intel's 24540633.pdf of August 2002 
		 */

		return SPEEDSTEP_PROCESSOR_PIII_T;

	case 0x08: /* Intel PIII [Coppermine] */
		/* based on reverse-engineering information and
		 * some guessing. HANDLE WITH CARE! */
 	        {
			u32     msr_lo, msr_hi;

			/* all mobile PIII Coppermines have FSB 100 MHz
			 * ==> sort out a few desktop PIIIs. */
			rdmsr(MSR_IA32_EBL_CR_POWERON, msr_lo, msr_hi);
			dprintk(KERN_DEBUG "cpufreq: Coppermine: MSR_IA32_EBL_Cr_POWERON is 0x%x, 0x%x\n", msr_lo, msr_hi);
			msr_lo &= 0x00c0000;
			if (msr_lo != 0x0080000)
				return 0;

			/* platform ID seems to be 0x00140000 */
			rdmsr(MSR_IA32_PLATFORM_ID, msr_lo, msr_hi);
			dprintk(KERN_DEBUG "cpufreq: Coppermine: MSR_IA32_PLATFORM ID is 0x%x, 0x%x\n", msr_lo, msr_hi);
			msr_hi = msr_lo & 0x001c0000;
			if (msr_hi != 0x00140000)
				return 0;

			/* and these bits seem to be either 00_b, 01_b or
			 * 10_b but never 11_b */
			msr_lo &= 0x00030000;
			if (msr_lo == 0x0030000)
				return 0;

			/* let's hope this is correct... */
			return SPEEDSTEP_PROCESSOR_PIII_C;
		}

	default:
		return 0;
	}
}



/*********************************************************************
 *                        HIGH LEVEL FUNCTIONS                       *
 *********************************************************************/

/**
 * speedstep_detect_speeds - detects low and high CPU frequencies.
 *
 *   Detects the low and high CPU frequencies in kHz. Returns 0 on
 * success or -EINVAL / -EIO on problems. 
 */
static int speedstep_detect_speeds (void)
{
	unsigned long   flags;
	unsigned int    state;
	int             i, result;

	/* Disable irqs for entire detection process */
	local_irq_save(flags);

	for (i=0; i<2; i++) {
		/* read the current state */
		result = speedstep_get_state(&state);
		if (result)
			return result;

		/* save the correct value, and switch to other */
		if (state == SPEEDSTEP_LOW) {
			switch (speedstep_processor) {
			case SPEEDSTEP_PROCESSOR_PIII_C:
			case SPEEDSTEP_PROCESSOR_PIII_T:
				speedstep_low_freq = pentium3_get_frequency();
				break;
			case SPEEDSTEP_PROCESSOR_P4M:
				speedstep_low_freq = pentium4_get_frequency();
			}
			speedstep_set_state(SPEEDSTEP_HIGH, 0);
		} else {
			switch (speedstep_processor) {
			case SPEEDSTEP_PROCESSOR_PIII_C:
			case SPEEDSTEP_PROCESSOR_PIII_T:
				speedstep_high_freq = pentium3_get_frequency();
				break;
			case SPEEDSTEP_PROCESSOR_P4M:
				speedstep_high_freq = pentium4_get_frequency();
			}
			speedstep_set_state(SPEEDSTEP_LOW, 0);
		}
	}

	local_irq_restore(flags);

	if (!speedstep_low_freq || !speedstep_high_freq || 
	    (speedstep_low_freq == speedstep_high_freq))
		return -EIO;

	return 0;
}


/**
 * speedstep_setpolicy - set a new CPUFreq policy
 * @policy: new policy
 *
 * Sets a new CPUFreq policy.
 */
static void speedstep_setpolicy (struct cpufreq_policy *policy)
{
	if (!speedstep_driver || !policy)
		return;

	if (policy->min > speedstep_low_freq) 
		speedstep_set_state(SPEEDSTEP_HIGH, 1);
	else {
		if (policy->max < speedstep_high_freq)
			speedstep_set_state(SPEEDSTEP_LOW, 1);
		else {
			/* both frequency states are allowed */
			if (policy->policy == CPUFREQ_POLICY_POWERSAVE)
				speedstep_set_state(SPEEDSTEP_LOW, 1);
			else
				speedstep_set_state(SPEEDSTEP_HIGH, 1);
		}
	}
}


/**
 * speedstep_verify - verifies a new CPUFreq policy
 * @freq: new policy
 *
 * Limit must be within speedstep_low_freq and speedstep_high_freq, with
 * at least one border included.
 */
static void speedstep_verify (struct cpufreq_policy *policy)
{
	if (!policy || !speedstep_driver || 
	    !speedstep_low_freq || !speedstep_high_freq)
		return;

	policy->cpu = 0; /* UP only */

	cpufreq_verify_within_limits(policy, speedstep_low_freq, speedstep_high_freq);

	if ((policy->min > speedstep_low_freq) && 
	    (policy->max < speedstep_high_freq))
		policy->max = speedstep_high_freq;
	
	return;
}


/**
 * speedstep_init - initializes the SpeedStep CPUFreq driver
 *
 *   Initializes the SpeedStep support. Returns -ENODEV on unsupported
 * devices, -EINVAL on problems during initiatization, and zero on
 * success.
 */
static int __init speedstep_init(void)
{
	int                     result;
	unsigned int            speed;
	struct cpufreq_driver   *driver;


	/* detect chipset */
	speedstep_chipset = speedstep_detect_chipset();

	/* detect chipset */
	if (speedstep_chipset)
		speedstep_processor = speedstep_detect_processor();

	if ((!speedstep_chipset) || (!speedstep_processor)) {
		dprintk(KERN_INFO "cpufreq: Intel(R) SpeedStep(TM) for this %s not (yet) available.\n", speedstep_processor ? "chipset" : "processor");
		return -ENODEV;
	}

	dprintk(KERN_INFO "cpufreq: Intel(R) SpeedStep(TM) support $Revision: 1.53 $\n");
	dprintk(KERN_DEBUG "cpufreq: chipset 0x%x - processor 0x%x\n", 
	       speedstep_chipset, speedstep_processor);

	/* activate speedstep support */
	result = speedstep_activate();
	if (result)
		return result;

	/* detect low and high frequency */
	result = speedstep_detect_speeds();
	if (result)
		return result;

	/* get current speed setting */
	result = speedstep_get_state(&speed);
	if (result)
		return result;

	speed = (speed == SPEEDSTEP_LOW) ? speedstep_low_freq : speedstep_high_freq;

	dprintk(KERN_INFO "cpufreq: currently at %s speed setting - %i MHz\n", 
	       (speed == speedstep_low_freq) ? "low" : "high",
	       (speed / 1000));

	/* initialization of main "cpufreq" code*/
	driver = kmalloc(sizeof(struct cpufreq_driver) + 
			 NR_CPUS * sizeof(struct cpufreq_policy), GFP_KERNEL);
	if (!driver)
		return -ENOMEM;

	driver->policy = (struct cpufreq_policy *) (driver + 1);

#ifdef CONFIG_CPU_FREQ_24_API
	driver->cpu_min_freq    = speedstep_low_freq;
	driver->cpu_cur_freq[0] = speed;
#endif

	driver->verify      = &speedstep_verify;
	driver->setpolicy   = &speedstep_setpolicy;

	driver->policy[0].cpu    = 0;
	driver->policy[0].min    = speedstep_low_freq;
	driver->policy[0].max    = speedstep_high_freq;
	driver->policy[0].max_cpu_freq = speedstep_high_freq;
	driver->policy[0].policy = (speed == speedstep_low_freq) ? 
	    CPUFREQ_POLICY_POWERSAVE : CPUFREQ_POLICY_PERFORMANCE;

	result = cpufreq_register(driver);
	if (result) {
		kfree(driver);
		return result;
	}
	speedstep_driver = driver;

	return 0;
}


/**
 * speedstep_exit - unregisters SpeedStep support
 *
 *   Unregisters SpeedStep support.
 */
static void __exit speedstep_exit(void)
{
	if (speedstep_driver) {
		cpufreq_unregister();
		kfree(speedstep_driver);
	}
}


MODULE_AUTHOR ("Dave Jones <davej@suse.de>, Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION ("Speedstep driver for Intel mobile processors.");
MODULE_LICENSE ("GPL");
module_init(speedstep_init);
module_exit(speedstep_exit);
