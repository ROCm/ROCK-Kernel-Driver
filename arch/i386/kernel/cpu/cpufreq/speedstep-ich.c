/*
 * (C) 2001  Dave Jones, Arjan van de ven.
 * (C) 2002 - 2003  Dominik Brodowski <linux@brodo.de>
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

#include "speedstep-lib.h"


/* speedstep_chipset:
 *   It is necessary to know which chipset is used. As accesses to 
 * this device occur at various places in this module, we need a 
 * static struct pci_dev * pointing to that device.
 */
static struct pci_dev			*speedstep_chipset_dev;


/* speedstep_processor
 */
static unsigned int			speedstep_processor = 0;


/* 
 *   There are only two frequency states for each processor. Values
 * are in kHz for the time being.
 */
static struct cpufreq_frequency_table speedstep_freqs[] = {
	{SPEEDSTEP_HIGH, 	0},
	{SPEEDSTEP_LOW,		0},
	{0,			CPUFREQ_TABLE_END},
};


/* DEBUG
 *   Define it if you want verbose debug output, e.g. for bug reporting
 */
//#define SPEEDSTEP_DEBUG

#ifdef SPEEDSTEP_DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0)
#endif


/**
 * speedstep_set_state - set the SpeedStep state
 * @state: new processor frequency state (SPEEDSTEP_LOW or SPEEDSTEP_HIGH)
 *
 *   Tries to change the SpeedStep state. 
 */
static void speedstep_set_state (unsigned int state, unsigned int notify)
{
	u32			pmbase;
	u8			pm2_blk;
	u8			value;
	unsigned long		flags;
	struct cpufreq_freqs	freqs;

	if (!speedstep_chipset_dev || (state > 0x1))
		return;

	freqs.old = speedstep_get_processor_frequency(speedstep_processor);
	freqs.new = speedstep_freqs[state].frequency;
	freqs.cpu = 0; /* speedstep.c is UP only driver */
	
	if (notify)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

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

	/* check if transition was successful */
	value = inb(pmbase + 0x50);

	/* Enable IRQs */
	local_irq_restore(flags);

	dprintk(KERN_DEBUG "cpufreq: read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

	if (state == (value & 0x1)) {
		dprintk (KERN_INFO "cpufreq: change to %u MHz succeeded\n", (speedstep_get_processor_frequency(speedstep_processor) / 1000));
	} else {
		printk (KERN_ERR "cpufreq: change failed - I/O error\n");
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
	u16		value = 0;

	if (!speedstep_chipset_dev)
		return -EINVAL;

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
			      PCI_DEVICE_ID_INTEL_82801DB_12, 
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev)
		return 4; /* 4-M */

	speedstep_chipset_dev = pci_find_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801CA_12, 
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev)
		return 3; /* 3-M */


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
		static struct pci_dev	*hostbridge;
		u8			rev = 0;

		hostbridge  = pci_find_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82815_MC,
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);

		if (!hostbridge)
			return 2; /* 2-M */
			
		pci_read_config_byte(hostbridge, PCI_REVISION_ID, &rev);
		if (rev < 5) {
			dprintk(KERN_INFO "cpufreq: hostbridge does not support speedstep\n");
			speedstep_chipset_dev = NULL;
			return 0;
		}

		return 2; /* 2-M */
	}

	return 0;
}


/**
 * speedstep_setpolicy - set a new CPUFreq policy
 * @policy: new policy
 *
 * Sets a new CPUFreq policy.
 */
static int speedstep_target (struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	unsigned int	newstate = 0;

	if (cpufreq_frequency_table_target(policy, &speedstep_freqs[0], target_freq, relation, &newstate))
		return -EINVAL;

	speedstep_set_state(newstate, 1);

	return 0;
}


/**
 * speedstep_verify - verifies a new CPUFreq policy
 * @freq: new policy
 *
 * Limit must be within speedstep_low_freq and speedstep_high_freq, with
 * at least one border included.
 */
static int speedstep_verify (struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, &speedstep_freqs[0]);
}


static int speedstep_cpu_init(struct cpufreq_policy *policy)
{
	int		result = 0;
	unsigned int	speed;

	/* capability check */
	if (policy->cpu != 0)
		return -ENODEV;

	/* detect low and high frequency */
	result = speedstep_get_freqs(speedstep_processor,
				     &speedstep_freqs[SPEEDSTEP_LOW].frequency,
				     &speedstep_freqs[SPEEDSTEP_HIGH].frequency,
				     &speedstep_set_state);
	if (result)
		return result;

	/* get current speed setting */
	speed = speedstep_get_processor_frequency(speedstep_processor);
	if (!speed)
		return -EIO;

	dprintk(KERN_INFO "cpufreq: currently at %s speed setting - %i MHz\n", 
		(speed == speedstep_freqs[SPEEDSTEP_LOW].frequency) ? "low" : "high",
		(speed / 1000));

	/* cpuinfo and default policy values */
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	policy->cur = speed;

	return cpufreq_frequency_table_cpuinfo(policy, &speedstep_freqs[0]);
}


static struct cpufreq_driver speedstep_driver = {
	.name		= "speedstep-ich",
	.verify 	= speedstep_verify,
	.target 	= speedstep_target,
	.init		= speedstep_cpu_init,
	.owner		= THIS_MODULE,
};


/**
 * speedstep_init - initializes the SpeedStep CPUFreq driver
 *
 *   Initializes the SpeedStep support. Returns -ENODEV on unsupported
 * devices, -EINVAL on problems during initiatization, and zero on
 * success.
 */
static int __init speedstep_init(void)
{
	/* detect processor */
	speedstep_processor = speedstep_detect_processor();
	if (!speedstep_processor)
		return -ENODEV;

	/* detect chipset */
	if (!speedstep_detect_chipset()) {
		printk(KERN_INFO "cpufreq: Intel(R) SpeedStep(TM) for this chipset not (yet) available.\n");
		return -ENODEV;
	}

	/* activate speedstep support */
	if (speedstep_activate())
		return -EINVAL;

	return cpufreq_register_driver(&speedstep_driver);
}


/**
 * speedstep_exit - unregisters SpeedStep support
 *
 *   Unregisters SpeedStep support.
 */
static void __exit speedstep_exit(void)
{
	cpufreq_unregister_driver(&speedstep_driver);
}


MODULE_AUTHOR ("Dave Jones <davej@codemonkey.org.uk>, Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION ("Speedstep driver for Intel mobile processors on chipsets with ICH-M southbridges.");
MODULE_LICENSE ("GPL");

module_init(speedstep_init);
module_exit(speedstep_exit);
