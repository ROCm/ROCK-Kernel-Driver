/*
 * acpi_processor.c - ACPI Processor Driver ($Revision: 71 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2004       Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *  			- Added processor hotplug support
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  TBD:
 *	1. Make # power states dynamic.
 *	2. Support duty_cycle values that span bit 4.
 *	3. Optimize by having scheduler determine business instead of
 *	   having us try to calculate it here.
 *	4. Need C1 timing -- must modify kernel (IRQ handler) to get this.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/dmi.h>
#include <linux/moduleparam.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/cpu.h>
#include <asm/delay.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/smp.h>
#include <asm/acpi.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/processor.h>


#define ACPI_PROCESSOR_COMPONENT	0x01000000
#define ACPI_PROCESSOR_CLASS		"processor"
#define ACPI_PROCESSOR_DRIVER_NAME	"ACPI Processor Driver"
#define ACPI_PROCESSOR_DEVICE_NAME	"Processor"
#define ACPI_PROCESSOR_FILE_INFO	"info"
#define ACPI_PROCESSOR_FILE_POWER	"power"
#define ACPI_PROCESSOR_FILE_THROTTLING	"throttling"
#define ACPI_PROCESSOR_FILE_LIMIT	"limit"
#define ACPI_PROCESSOR_NOTIFY_PERFORMANCE 0x80
#define ACPI_PROCESSOR_NOTIFY_POWER	0x81

#define US_TO_PM_TIMER_TICKS(t)		((t * (PM_TIMER_FREQUENCY/1000)) / 1000)
#define C2_OVERHEAD			4	/* 1us (3.579 ticks per us) */
#define C3_OVERHEAD			4	/* 1us (3.579 ticks per us) */

#define ACPI_PROCESSOR_LIMIT_USER	0
#define ACPI_PROCESSOR_LIMIT_THERMAL	1

#define ACPI_STA_PRESENT 0x00000001

#define _COMPONENT		ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME		("acpi_processor")

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION(ACPI_PROCESSOR_DRIVER_NAME);
MODULE_LICENSE("GPL");


static int acpi_processor_add (struct acpi_device *device);
static int acpi_processor_start (struct acpi_device *device);
static int acpi_processor_remove (struct acpi_device *device, int type);
static int acpi_processor_info_open_fs(struct inode *inode, struct file *file);
static int acpi_processor_power_open_fs(struct inode *inode, struct file *file);
static int acpi_processor_limit_open_fs(struct inode *inode, struct file *file);
static int acpi_processor_get_limit_info(struct acpi_processor *pr);
static void acpi_processor_notify ( acpi_handle	handle, u32 event, void *data);
static acpi_status acpi_processor_hotadd_init(acpi_handle handle, int *p_cpu);
static int acpi_processor_handle_eject(struct acpi_processor *pr);

static struct acpi_driver acpi_processor_driver = {
	.name =		ACPI_PROCESSOR_DRIVER_NAME,
	.class =	ACPI_PROCESSOR_CLASS,
	.ids =		ACPI_PROCESSOR_HID,
	.ops =		{
				.add =		acpi_processor_add,
				.remove =	acpi_processor_remove,
				.start	= 	acpi_processor_start,
			},
};

#define INSTALL_NOTIFY_HANDLER		1
#define UNINSTALL_NOTIFY_HANDLER	2


static struct file_operations acpi_processor_info_fops = {
	.open 		= acpi_processor_info_open_fs,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct file_operations acpi_processor_power_fops = {
	.open 		= acpi_processor_power_open_fs,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct file_operations acpi_processor_limit_fops = {
	.open 		= acpi_processor_limit_open_fs,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

struct acpi_processor	*processors[NR_CPUS];
struct acpi_processor_errata errata;
static void (*pm_idle_save)(void);


/* --------------------------------------------------------------------------
                                Errata Handling
   -------------------------------------------------------------------------- */

int
acpi_processor_errata_piix4 (
	struct pci_dev		*dev)
{
	u8			rev = 0;
	u8			value1 = 0;
	u8			value2 = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_errata_piix4");

	if (!dev)
		return_VALUE(-EINVAL);

	/*
	 * Note that 'dev' references the PIIX4 ACPI Controller.
	 */

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

	switch (rev) {
	case 0:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found PIIX4 A-step\n"));
		break;
	case 1:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found PIIX4 B-step\n"));
		break;
	case 2:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found PIIX4E\n"));
		break;
	case 3:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found PIIX4M\n"));
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found unknown PIIX4\n"));
		break;
	}

	switch (rev) {

	case 0:		/* PIIX4 A-step */
	case 1:		/* PIIX4 B-step */
		/*
		 * See specification changes #13 ("Manual Throttle Duty Cycle")
		 * and #14 ("Enabling and Disabling Manual Throttle"), plus
		 * erratum #5 ("STPCLK# Deassertion Time") from the January 
		 * 2002 PIIX4 specification update.  Applies to only older 
		 * PIIX4 models.
		 */
		errata.piix4.throttle = 1;

	case 2:		/* PIIX4E */
	case 3:		/* PIIX4M */
		/*
		 * See erratum #18 ("C3 Power State/BMIDE and Type-F DMA 
		 * Livelock") from the January 2002 PIIX4 specification update.
		 * Applies to all PIIX4 models.
		 */

		/* 
		 * BM-IDE
		 * ------
		 * Find the PIIX4 IDE Controller and get the Bus Master IDE 
		 * Status register address.  We'll use this later to read 
		 * each IDE controller's DMA status to make sure we catch all
		 * DMA activity.
		 */
		dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
		           PCI_DEVICE_ID_INTEL_82371AB, 
                           PCI_ANY_ID, PCI_ANY_ID, NULL);
		if (dev) {
			errata.piix4.bmisx = pci_resource_start(dev, 4);
			pci_dev_put(dev);
		}

		/* 
		 * Type-F DMA
		 * ----------
		 * Find the PIIX4 ISA Controller and read the Motherboard
		 * DMA controller's status to see if Type-F (Fast) DMA mode
		 * is enabled (bit 7) on either channel.  Note that we'll 
		 * disable C3 support if this is enabled, as some legacy 
		 * devices won't operate well if fast DMA is disabled.
		 */
		dev = pci_get_subsys(PCI_VENDOR_ID_INTEL, 
			PCI_DEVICE_ID_INTEL_82371AB_0, 
			PCI_ANY_ID, PCI_ANY_ID, NULL);
		if (dev) {
			pci_read_config_byte(dev, 0x76, &value1);
			pci_read_config_byte(dev, 0x77, &value2);
			if ((value1 & 0x80) || (value2 & 0x80))
				errata.piix4.fdma = 1;
			pci_dev_put(dev);
		}

		break;
	}

	if (errata.piix4.bmisx)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
			"Bus master activity detection (BM-IDE) erratum enabled\n"));
	if (errata.piix4.fdma)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
			"Type-F DMA livelock erratum (C3 disabled)\n"));

	return_VALUE(0);
}


int
acpi_processor_errata (
	struct acpi_processor	*pr)
{
	int			result = 0;
	struct pci_dev		*dev = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_errata");

	if (!pr)
		return_VALUE(-EINVAL);

	/*
	 * PIIX4
	 */
	dev = pci_get_subsys(PCI_VENDOR_ID_INTEL, 
		PCI_DEVICE_ID_INTEL_82371AB_3, PCI_ANY_ID, PCI_ANY_ID, NULL);
	if (dev) {
		result = acpi_processor_errata_piix4(dev);
		pci_dev_put(dev);
	}

	return_VALUE(result);
}


/* --------------------------------------------------------------------------
                                Power Management
   -------------------------------------------------------------------------- */

static inline u32
ticks_elapsed (
	u32			t1,
	u32			t2)
{
	if (t2 >= t1)
		return (t2 - t1);
	else if (!acpi_fadt.tmr_val_ext)
		return (((0x00FFFFFF - t1) + t2) & 0x00FFFFFF);
	else
		return ((0xFFFFFFFF - t1) + t2);
}


static void
acpi_processor_power_activate (
	struct acpi_processor	*pr,
	int			state)
{
	if (!pr)
		return;

	pr->power.states[pr->power.state].promotion.count = 0;
	pr->power.states[pr->power.state].demotion.count = 0;

	/* Cleanup from old state. */
	switch (pr->power.state) {
	case ACPI_STATE_C3:
		/* Disable bus master reload */
		acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD, 0, ACPI_MTX_DO_NOT_LOCK);
		break;
	}

	/* Prepare to use new state. */
	switch (state) {
	case ACPI_STATE_C3:
		/* Enable bus master reload */
		acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD, 1, ACPI_MTX_DO_NOT_LOCK);
		break;
	}

	pr->power.state = state;

	return;
}


static void
acpi_processor_idle (void)
{
	struct acpi_processor	*pr = NULL;
	struct acpi_processor_cx *cx = NULL;
	unsigned int		next_state = 0;
	unsigned int		sleep_ticks = 0;
	u32			t1, t2 = 0;

	pr = processors[smp_processor_id()];
	if (!pr)
		return;

	/*
	 * Interrupts must be disabled during bus mastering calculations and
	 * for C2/C3 transitions.
	 */
	local_irq_disable();

	/*
	 * Check whether we truly need to go idle, or should
	 * reschedule:
	 */
	if (unlikely(need_resched())) {
		local_irq_enable();
		return;
	}

	cx = &(pr->power.states[pr->power.state]);

	/*
	 * Check BM Activity
	 * -----------------
	 * Check for bus mastering activity (if required), record, and check
	 * for demotion.
	 */
	if (pr->flags.bm_check) {
		u32		bm_status = 0;

		pr->power.bm_activity <<= 1;

		acpi_get_register(ACPI_BITREG_BUS_MASTER_STATUS, 
			&bm_status, ACPI_MTX_DO_NOT_LOCK);
		if (bm_status) {
			pr->power.bm_activity++;
			acpi_set_register(ACPI_BITREG_BUS_MASTER_STATUS,
				1, ACPI_MTX_DO_NOT_LOCK);
		}
		/*
		 * PIIX4 Erratum #18: Note that BM_STS doesn't always reflect
		 * the true state of bus mastering activity; forcing us to 
		 * manually check the BMIDEA bit of each IDE channel.
		 */
		else if (errata.piix4.bmisx) {
			if ((inb_p(errata.piix4.bmisx + 0x02) & 0x01) 
				|| (inb_p(errata.piix4.bmisx + 0x0A) & 0x01))
				pr->power.bm_activity++;
		}
		/*
		 * Apply bus mastering demotion policy.  Automatically demote
		 * to avoid a faulty transition.  Note that the processor 
		 * won't enter a low-power state during this call (to this 
		 * funciton) but should upon the next.
		 *
		 * TBD: A better policy might be to fallback to the demotion 
		 *      state (use it for this quantum only) istead of 
		 *      demoting -- and rely on duration as our sole demotion
		 *      qualification.  This may, however, introduce DMA 
		 *      issues (e.g. floppy DMA transfer overrun/underrun).
		 */
		if (pr->power.bm_activity & cx->demotion.threshold.bm) {
			local_irq_enable();
			next_state = cx->demotion.state;
			goto end;
		}
	}

	cx->usage++;

	/*
	 * Sleep:
	 * ------
	 * Invoke the current Cx state to put the processor to sleep.
	 */
	switch (pr->power.state) {

	case ACPI_STATE_C1:
		/*
		 * Invoke C1.
		 * Use the appropriate idle routine, the one that would
		 * be used without acpi C-states.
		 */
		if (pm_idle_save)
			pm_idle_save();
		else
			safe_halt();
		/*
                 * TBD: Can't get time duration while in C1, as resumes
		 *      go to an ISR rather than here.  Need to instrument
		 *      base interrupt handler.
		 */
		sleep_ticks = 0xFFFFFFFF;
		break;

	case ACPI_STATE_C2:
		/* Get start time (ticks) */
		t1 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Invoke C2 */
		inb(pr->power.states[ACPI_STATE_C2].address);
		/* Dummy op - must do something useless after P_LVL2 read */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Get end time (ticks) */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Re-enable interrupts */
		local_irq_enable();
		/* Compute time (ticks) that we were actually asleep */
		sleep_ticks = ticks_elapsed(t1, t2) - cx->latency_ticks - C2_OVERHEAD;
		break;

	case ACPI_STATE_C3:
		/* Disable bus master arbitration */
		acpi_set_register(ACPI_BITREG_ARB_DISABLE, 1, ACPI_MTX_DO_NOT_LOCK);
		/* Get start time (ticks) */
		t1 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Invoke C3 */
		inb(pr->power.states[ACPI_STATE_C3].address);
		/* Dummy op - must do something useless after P_LVL3 read */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Get end time (ticks) */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Enable bus master arbitration */
		acpi_set_register(ACPI_BITREG_ARB_DISABLE, 0, ACPI_MTX_DO_NOT_LOCK);
		/* Re-enable interrupts */
		local_irq_enable();
		/* Compute time (ticks) that we were actually asleep */
		sleep_ticks = ticks_elapsed(t1, t2) - cx->latency_ticks - C3_OVERHEAD;
		break;

	default:
		local_irq_enable();
		return;
	}

	next_state = pr->power.state;

	/*
	 * Promotion?
	 * ----------
	 * Track the number of longs (time asleep is greater than threshold)
	 * and promote when the count threshold is reached.  Note that bus
	 * mastering activity may prevent promotions.
	 * Do not promote above max_cstate.
	 */
	if (cx->promotion.state && (cx->promotion.state <= max_cstate)) {
		if (sleep_ticks > cx->promotion.threshold.ticks) {
			cx->promotion.count++;
 			cx->demotion.count = 0;
			if (cx->promotion.count >= cx->promotion.threshold.count) {
				if (pr->flags.bm_check) {
					if (!(pr->power.bm_activity & cx->promotion.threshold.bm)) {
						next_state = cx->promotion.state;
						goto end;
					}
				}
				else {
					next_state = cx->promotion.state;
					goto end;
				}
			}
		}
	}

	/*
	 * Demotion?
	 * ---------
	 * Track the number of shorts (time asleep is less than time threshold)
	 * and demote when the usage threshold is reached.
	 */
	if (cx->demotion.state) {
		if (sleep_ticks < cx->demotion.threshold.ticks) {
			cx->demotion.count++;
			cx->promotion.count = 0;
			if (cx->demotion.count >= cx->demotion.threshold.count) {
				next_state = cx->demotion.state;
				goto end;
			}
		}
	}

end:
	/*
	 * Demote if current state exceeds max_cstate
	 */
	if (pr->power.state > max_cstate) {
		next_state = max_cstate;
	}

	/*
	 * New Cx State?
	 * -------------
	 * If we're going to start using a new Cx state we must clean up
	 * from the previous and prepare to use the new.
	 */
	if (next_state != pr->power.state)
		acpi_processor_power_activate(pr, next_state);

	return;
}


static int
acpi_processor_set_power_policy (
	struct acpi_processor	*pr)
{
	ACPI_FUNCTION_TRACE("acpi_processor_set_power_policy");

	/*
	 * This function sets the default Cx state policy (OS idle handler).
	 * Our scheme is to promote quickly to C2 but more conservatively
	 * to C3.  We're favoring C2  for its characteristics of low latency
	 * (quick response), good power savings, and ability to allow bus
	 * mastering activity.  Note that the Cx state policy is completely
	 * customizable and can be altered dynamically.
	 */

	if (!pr)
		return_VALUE(-EINVAL);

	/*
	 * C0/C1
	 * -----
	 */
	pr->power.state = ACPI_STATE_C1;
	pr->power.default_state = ACPI_STATE_C1;

	/*
	 * C1/C2
	 * -----
	 * Set the default C1 promotion and C2 demotion policies, where we
	 * promote from C1 to C2 after several (10) successive C1 transitions,
	 * as we cannot (currently) measure the time spent in C1. Demote from
	 * C2 to C1 anytime we experience a 'short' (time spent in C2 is less
	 * than the C2 transtion latency).  Note the simplifying assumption 
	 * that the 'cost' of a transition is amortized when we sleep for at
	 * least as long as the transition's latency (thus the total transition
	 * time is two times the latency).
	 *
	 * TBD: Measure C1 sleep times by instrumenting the core IRQ handler.
	 * TBD: Demote to default C-State after long periods of activity.
	 * TBD: Investigate policy's use of CPU utilization -vs- sleep duration.
	 */
	if (pr->power.states[ACPI_STATE_C2].valid) {
		pr->power.states[ACPI_STATE_C1].promotion.threshold.count = 10;
		pr->power.states[ACPI_STATE_C1].promotion.threshold.ticks =
			pr->power.states[ACPI_STATE_C2].latency_ticks;
		pr->power.states[ACPI_STATE_C1].promotion.state = ACPI_STATE_C2;

		pr->power.states[ACPI_STATE_C2].demotion.threshold.count = 1;
		pr->power.states[ACPI_STATE_C2].demotion.threshold.ticks =
			pr->power.states[ACPI_STATE_C2].latency_ticks;
		pr->power.states[ACPI_STATE_C2].demotion.state = ACPI_STATE_C1;
	}

	/*
	 * C2/C3
	 * -----
	 * Set default C2 promotion and C3 demotion policies, where we promote
	 * from C2 to C3 after several (4) cycles of no bus mastering activity
	 * while maintaining sleep time criteria.  Demote immediately on a
	 * short or whenever bus mastering activity occurs.
	 */
	if ((pr->power.states[ACPI_STATE_C2].valid) &&
		(pr->power.states[ACPI_STATE_C3].valid)) {
		pr->power.states[ACPI_STATE_C2].promotion.threshold.count = 4;
		pr->power.states[ACPI_STATE_C2].promotion.threshold.ticks =
			pr->power.states[ACPI_STATE_C3].latency_ticks;
		pr->power.states[ACPI_STATE_C2].promotion.threshold.bm = 0x0F;
		pr->power.states[ACPI_STATE_C2].promotion.state = ACPI_STATE_C3;

		pr->power.states[ACPI_STATE_C3].demotion.threshold.count = 1;
		pr->power.states[ACPI_STATE_C3].demotion.threshold.ticks =
			pr->power.states[ACPI_STATE_C3].latency_ticks;
		pr->power.states[ACPI_STATE_C3].demotion.threshold.bm = 0x0F;
		pr->power.states[ACPI_STATE_C3].demotion.state = ACPI_STATE_C2;
	}

	return_VALUE(0);
}


int
acpi_processor_get_power_info (
	struct acpi_processor	*pr)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_get_power_info");

	if (!pr)
		return_VALUE(-EINVAL);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"lvl2[0x%08x] lvl3[0x%08x]\n",
		pr->power.states[ACPI_STATE_C2].address,
		pr->power.states[ACPI_STATE_C3].address));

	/* TBD: Support ACPI 2.0 objects */

	/*
	 * C0
	 * --
	 * This state exists only as filler in our array.
	 */
	pr->power.states[ACPI_STATE_C0].valid = 1;

	/*
	 * C1
	 * --
	 * ACPI requires C1 support for all processors.
	 *
	 * TBD: What about PROC_C1?
	 */
	pr->power.states[ACPI_STATE_C1].valid = 1;

	/*
	 * C2
	 * --
	 * We're (currently) only supporting C2 on UP systems.
	 *
	 * TBD: Support for C2 on MP (P_LVL2_UP).
	 */
	if (pr->power.states[ACPI_STATE_C2].address) {

		pr->power.states[ACPI_STATE_C2].latency = acpi_fadt.plvl2_lat;

		/*
		 * C2 latency must be less than or equal to 100 microseconds.
		 */
		if (acpi_fadt.plvl2_lat > ACPI_PROCESSOR_MAX_C2_LATENCY)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"C2 latency too large [%d]\n",
				acpi_fadt.plvl2_lat));
		/*
		 * Only support C2 on UP systems (see TBD above).
		 */
		else if (errata.smp)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"C2 not supported in SMP mode\n"));
		/*
		 * Otherwise we've met all of our C2 requirements.
		 * Normalize the C2 latency to expidite policy.
		 */
		else {
			pr->power.states[ACPI_STATE_C2].valid = 1;
			pr->power.states[ACPI_STATE_C2].latency_ticks = 
				US_TO_PM_TIMER_TICKS(acpi_fadt.plvl2_lat);
		}
	}

	/*
	 * C3
	 * --
	 * TBD: Investigate use of WBINVD on UP/SMP system in absence of
	 *	bm_control.
	 */
	if (pr->power.states[ACPI_STATE_C3].address) {

		pr->power.states[ACPI_STATE_C3].latency = acpi_fadt.plvl3_lat;

		/*
		 * C3 latency must be less than or equal to 1000 microseconds.
		 */
		if (acpi_fadt.plvl3_lat > ACPI_PROCESSOR_MAX_C3_LATENCY)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"C3 latency too large [%d]\n", 
				acpi_fadt.plvl3_lat));
		/*
		 * Only support C3 when bus mastering arbitration control
		 * is present (able to disable bus mastering to maintain
		 * cache coherency while in C3).
		 */
		else if (!pr->flags.bm_control)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"C3 support requires bus mastering control\n"));
		/*
		 * Only support C3 on UP systems, as bm_control is only viable
		 * on a UP system and flushing caches (e.g. WBINVD) is simply 
		 * too costly (at this time).
		 */
		else if (errata.smp)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"C3 not supported in SMP mode\n"));
		/*
		 * PIIX4 Erratum #18: We don't support C3 when Type-F (fast) 
		 * DMA transfers are used by any ISA device to avoid livelock.
		 * Note that we could disable Type-F DMA (as recommended by
		 * the erratum), but this is known to disrupt certain ISA 
		 * devices thus we take the conservative approach.
		 */
		else if (errata.piix4.fdma) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"C3 not supported on PIIX4 with Type-F DMA\n"));
		}
		/*
		 * Otherwise we've met all of our C3 requirements.  
		 * Normalize the C2 latency to expidite policy.  Enable
		 * checking of bus mastering status (bm_check) so we can 
		 * use this in our C3 policy.
		 */
		else {
			pr->power.states[ACPI_STATE_C3].valid = 1;
			pr->power.states[ACPI_STATE_C3].latency_ticks = 
				US_TO_PM_TIMER_TICKS(acpi_fadt.plvl3_lat);
			pr->flags.bm_check = 1;
		}
	}

	/*
	 * Set Default Policy
	 * ------------------
	 * Now that we know which state are supported, set the default
	 * policy.  Note that this policy can be changed dynamically
	 * (e.g. encourage deeper sleeps to conserve battery life when
	 * not on AC).
	 */
	result = acpi_processor_set_power_policy(pr);
	if (result)
		return_VALUE(result);

	/*
	 * If this processor supports C2 or C3 we denote it as being 'power
	 * manageable'.  Note that there's really no policy involved for
	 * when only C1 is supported.
	 */
	if (pr->power.states[ACPI_STATE_C2].valid 
		|| pr->power.states[ACPI_STATE_C3].valid)
		pr->flags.power = 1;

	return_VALUE(0);
}




/* --------------------------------------------------------------------------
                                 Limit Interface
   -------------------------------------------------------------------------- */

static int
acpi_processor_apply_limit (
	struct acpi_processor* 	pr)
{
	int			result = 0;
	u16			px = 0;
	u16			tx = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_apply_limit");

	if (!pr)
		return_VALUE(-EINVAL);

	if (!pr->flags.limit)
		return_VALUE(-ENODEV);

	if (pr->flags.throttling) {
		if (pr->limit.user.tx > tx)
			tx = pr->limit.user.tx;
		if (pr->limit.thermal.tx > tx)
			tx = pr->limit.thermal.tx;

		result = acpi_processor_set_throttling(pr, tx);
		if (result)
			goto end;
	}

	pr->limit.state.px = px;
	pr->limit.state.tx = tx;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Processor [%d] limit set to (P%d:T%d)\n",
		pr->id,
		pr->limit.state.px,
		pr->limit.state.tx));

end:
	if (result)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unable to set limit\n"));

	return_VALUE(result);
}


#ifdef CONFIG_CPU_FREQ

/* If a passive cooling situation is detected, primarily CPUfreq is used, as it
 * offers (in most cases) voltage scaling in addition to frequency scaling, and
 * thus a cubic (instead of linear) reduction of energy. Also, we allow for
 * _any_ cpufreq driver and not only the acpi-cpufreq driver.
 */

static unsigned int cpufreq_thermal_reduction_pctg[NR_CPUS];
static unsigned int acpi_thermal_cpufreq_is_init = 0;


static int cpu_has_cpufreq(unsigned int cpu)
{
	struct cpufreq_policy policy;
	if (!acpi_thermal_cpufreq_is_init)
		return -ENODEV;
	if (!cpufreq_get_policy(&policy, cpu))
		return -ENODEV;
	return 0;
}


static int acpi_thermal_cpufreq_increase(unsigned int cpu)
{
	if (!cpu_has_cpufreq(cpu))
		return -ENODEV;

	if (cpufreq_thermal_reduction_pctg[cpu] < 60) {
		cpufreq_thermal_reduction_pctg[cpu] += 20;
		cpufreq_update_policy(cpu);
		return 0;
	}

	return -ERANGE;
}


static int acpi_thermal_cpufreq_decrease(unsigned int cpu)
{
	if (!cpu_has_cpufreq(cpu))
		return -ENODEV;

	if (cpufreq_thermal_reduction_pctg[cpu] >= 20) {
		cpufreq_thermal_reduction_pctg[cpu] -= 20;
		cpufreq_update_policy(cpu);
		return 0;
	}

	return -ERANGE;
}


static int acpi_thermal_cpufreq_notifier(
	struct notifier_block *nb,
	unsigned long event,
	void *data)
{
	struct cpufreq_policy *policy = data;
	unsigned long max_freq = 0;

	if (event != CPUFREQ_ADJUST)
		goto out;

	max_freq = (policy->cpuinfo.max_freq * (100 - cpufreq_thermal_reduction_pctg[policy->cpu])) / 100;

	cpufreq_verify_within_limits(policy, 0, max_freq);

 out:
	return 0;
}


static struct notifier_block acpi_thermal_cpufreq_notifier_block = {
	.notifier_call = acpi_thermal_cpufreq_notifier,
};


static void acpi_thermal_cpufreq_init(void) {
	int i;

	for (i=0; i<NR_CPUS; i++)
		cpufreq_thermal_reduction_pctg[i] = 0;

	i = cpufreq_register_notifier(&acpi_thermal_cpufreq_notifier_block, CPUFREQ_POLICY_NOTIFIER);
	if (!i)
		acpi_thermal_cpufreq_is_init = 1;
}

static void acpi_thermal_cpufreq_exit(void) {
	if (acpi_thermal_cpufreq_is_init)
		cpufreq_unregister_notifier(&acpi_thermal_cpufreq_notifier_block, CPUFREQ_POLICY_NOTIFIER);

	acpi_thermal_cpufreq_is_init = 0;
}

#else /* ! CONFIG_CPU_FREQ */

static void acpi_thermal_cpufreq_init(void) { return; }
static void acpi_thermal_cpufreq_exit(void) { return; }
static int acpi_thermal_cpufreq_increase(unsigned int cpu) { return -ENODEV; }
static int acpi_thermal_cpufreq_decrease(unsigned int cpu) { return -ENODEV; }


#endif


int
acpi_processor_set_thermal_limit (
	acpi_handle		handle,
	int			type)
{
	int			result = 0;
	struct acpi_processor	*pr = NULL;
	struct acpi_device	*device = NULL;
	int			tx = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_set_thermal_limit");

	if ((type < ACPI_PROCESSOR_LIMIT_NONE) 
		|| (type > ACPI_PROCESSOR_LIMIT_DECREMENT))
		return_VALUE(-EINVAL);

	result = acpi_bus_get_device(handle, &device);
	if (result)
		return_VALUE(result);

	pr = (struct acpi_processor *) acpi_driver_data(device);
	if (!pr)
		return_VALUE(-ENODEV);

	/* Thermal limits are always relative to the current Px/Tx state. */
	if (pr->flags.throttling)
		pr->limit.thermal.tx = pr->throttling.state;

	/*
	 * Our default policy is to only use throttling at the lowest
	 * performance state.
	 */

	tx = pr->limit.thermal.tx;

	switch (type) {

	case ACPI_PROCESSOR_LIMIT_NONE:
		do {
			result = acpi_thermal_cpufreq_decrease(pr->id);
		} while (!result);
		tx = 0;
		break;

	case ACPI_PROCESSOR_LIMIT_INCREMENT:
		/* if going up: P-states first, T-states later */

		result = acpi_thermal_cpufreq_increase(pr->id);
		if (!result)
			goto end;
		else if (result == -ERANGE)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
					"At maximum performance state\n"));

		if (pr->flags.throttling) {
			if (tx == (pr->throttling.state_count - 1))
				ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
					"At maximum throttling state\n"));
			else
				tx++;
		}
		break;

	case ACPI_PROCESSOR_LIMIT_DECREMENT:
		/* if going down: T-states first, P-states later */

		if (pr->flags.throttling) {
			if (tx == 0)
				ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
					"At minimum throttling state\n"));
			else {
				tx--;
				goto end;
			}
		}

		result = acpi_thermal_cpufreq_decrease(pr->id);
		if (result == -ERANGE)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
					"At minimum performance state\n"));

		break;
	}

end:
	if (pr->flags.throttling) {
		pr->limit.thermal.px = 0;
		pr->limit.thermal.tx = tx;

		result = acpi_processor_apply_limit(pr);
		if (result)
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
					  "Unable to set thermal limit\n"));

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Thermal limit now (P%d:T%d)\n",
				  pr->limit.thermal.px,
				  pr->limit.thermal.tx));
	} else
		result = 0;

	return_VALUE(result);
}


static int
acpi_processor_get_limit_info (
	struct acpi_processor	*pr)
{
	ACPI_FUNCTION_TRACE("acpi_processor_get_limit_info");

	if (!pr)
		return_VALUE(-EINVAL);

	if (pr->flags.throttling)
		pr->flags.limit = 1;

	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

struct proc_dir_entry		*acpi_processor_dir = NULL;

static int acpi_processor_info_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor	*pr = (struct acpi_processor *)seq->private;

	ACPI_FUNCTION_TRACE("acpi_processor_info_seq_show");

	if (!pr)
		goto end;

	seq_printf(seq, "processor id:            %d\n"
			"acpi id:                 %d\n"
			"bus mastering control:   %s\n"
			"power management:        %s\n"
			"throttling control:      %s\n"
			"limit interface:         %s\n",
			pr->id,
			pr->acpi_id,
			pr->flags.bm_control ? "yes" : "no",
			pr->flags.power ? "yes" : "no",
			pr->flags.throttling ? "yes" : "no",
			pr->flags.limit ? "yes" : "no");

end:
	return_VALUE(0);
}

static int acpi_processor_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_processor_info_seq_show,
						PDE(inode)->data);
}

static int acpi_processor_power_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor	*pr = (struct acpi_processor *)seq->private;
	unsigned int		i;

	ACPI_FUNCTION_TRACE("acpi_processor_power_seq_show");

	if (!pr)
		goto end;

	seq_printf(seq, "active state:            C%d\n"
			"default state:           C%d\n"
			"max_cstate:              C%d\n"
			"bus master activity:     %08x\n",
			pr->power.state,
			pr->power.default_state,
			max_cstate,
			pr->power.bm_activity);

	seq_puts(seq, "states:\n");

	for (i = 1; i < ACPI_C_STATE_COUNT; i++) {
		seq_printf(seq, "   %cC%d:                  ", 
			(i == pr->power.state?'*':' '), i);

		if (!pr->power.states[i].valid) {
			seq_puts(seq, "<not supported>\n");
			continue;
		}

		if (pr->power.states[i].promotion.state)
			seq_printf(seq, "promotion[C%d] ",
				pr->power.states[i].promotion.state);
		else
			seq_puts(seq, "promotion[--] ");

		if (pr->power.states[i].demotion.state)
			seq_printf(seq, "demotion[C%d] ",
				pr->power.states[i].demotion.state);
		else
			seq_puts(seq, "demotion[--] ");

		seq_printf(seq, "latency[%03d] usage[%08d]\n",
			pr->power.states[i].latency,
			pr->power.states[i].usage);
	}

end:
	return_VALUE(0);
}

static int acpi_processor_power_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_processor_power_seq_show,
						PDE(inode)->data);
}


static int acpi_processor_limit_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor	*pr = (struct acpi_processor *)seq->private;

	ACPI_FUNCTION_TRACE("acpi_processor_limit_seq_show");

	if (!pr)
		goto end;

	if (!pr->flags.limit) {
		seq_puts(seq, "<not supported>\n");
		goto end;
	}

	seq_printf(seq, "active limit:            P%d:T%d\n"
			"user limit:              P%d:T%d\n"
			"thermal limit:           P%d:T%d\n",
			pr->limit.state.px, pr->limit.state.tx,
			pr->limit.user.px, pr->limit.user.tx,
			pr->limit.thermal.px, pr->limit.thermal.tx);

end:
	return_VALUE(0);
}

static int acpi_processor_limit_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_processor_limit_seq_show,
						PDE(inode)->data);
}

static ssize_t
acpi_processor_write_limit (
	struct file		*file,
	const char		__user *buffer,
	size_t			count,
	loff_t			*data)
{
	int			result = 0;
        struct seq_file 	*m = (struct seq_file *)file->private_data;
	struct acpi_processor	*pr = (struct acpi_processor *)m->private;
	char			limit_string[25] = {'\0'};
	int			px = 0;
	int			tx = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_write_limit");

	if (!pr || (count > sizeof(limit_string) - 1)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid argument\n"));
		return_VALUE(-EINVAL);
	}
	
	if (copy_from_user(limit_string, buffer, count)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid data\n"));
		return_VALUE(-EFAULT);
	}
	
	limit_string[count] = '\0';

	if (sscanf(limit_string, "%d:%d", &px, &tx) != 2) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid data format\n"));
		return_VALUE(-EINVAL);
	}

	if (pr->flags.throttling) {
		if ((tx < 0) || (tx > (pr->throttling.state_count - 1))) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid tx\n"));
			return_VALUE(-EINVAL);
		}
		pr->limit.user.tx = tx;
	}

	result = acpi_processor_apply_limit(pr);

	return_VALUE(count);
}


static int
acpi_processor_add_fs (
	struct acpi_device	*device)
{
	struct proc_dir_entry	*entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_add_fs");

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
			acpi_processor_dir);
		if (!acpi_device_dir(device))
			return_VALUE(-ENODEV);
	}
	acpi_device_dir(device)->owner = THIS_MODULE;

	/* 'info' [R] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_INFO,
		S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_INFO));
	else {
		entry->proc_fops = &acpi_processor_info_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'power' [R] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_POWER,
		S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_POWER));
	else {
		entry->proc_fops = &acpi_processor_power_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'throttling' [R/W] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_THROTTLING,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_THROTTLING));
	else {
		entry->proc_fops = &acpi_processor_throttling_fops;
		entry->proc_fops->write = acpi_processor_write_throttling;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'limit' [R/W] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_LIMIT,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_LIMIT));
	else {
		entry->proc_fops = &acpi_processor_limit_fops;
		entry->proc_fops->write = acpi_processor_write_limit;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	return_VALUE(0);
}


static int
acpi_processor_remove_fs (
	struct acpi_device	*device)
{
	ACPI_FUNCTION_TRACE("acpi_processor_remove_fs");

	if (acpi_device_dir(device)) {
		remove_proc_entry(ACPI_PROCESSOR_FILE_INFO,acpi_device_dir(device));
		remove_proc_entry(ACPI_PROCESSOR_FILE_POWER,acpi_device_dir(device));
		remove_proc_entry(ACPI_PROCESSOR_FILE_THROTTLING,
			acpi_device_dir(device));
		remove_proc_entry(ACPI_PROCESSOR_FILE_LIMIT,acpi_device_dir(device));
		remove_proc_entry(acpi_device_bid(device), acpi_processor_dir);
		acpi_device_dir(device) = NULL;
	}

	return_VALUE(0);
}

/* Use the acpiid in MADT to map cpus in case of SMP */
#ifndef CONFIG_SMP
#define convert_acpiid_to_cpu(acpi_id) (0xff)
#else

#ifdef CONFIG_IA64
#define arch_acpiid_to_apicid 	ia64_acpiid_to_sapicid
#define arch_cpu_to_apicid 	ia64_cpu_to_sapicid
#define ARCH_BAD_APICID		(0xffff)
#else
#define arch_acpiid_to_apicid 	x86_acpiid_to_apicid
#define arch_cpu_to_apicid 	x86_cpu_to_apicid
#define ARCH_BAD_APICID		(0xff)
#endif

static u8 convert_acpiid_to_cpu(u8 acpi_id)
{
	u16 apic_id;
	int i;
	
	apic_id = arch_acpiid_to_apicid[acpi_id];
	if (apic_id == ARCH_BAD_APICID)
		return -1;

	for (i = 0; i < NR_CPUS; i++) {
		if (arch_cpu_to_apicid[i] == apic_id)
			return i;
	}
	return -1;
}
#endif

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static int
acpi_processor_get_info (
	struct acpi_processor	*pr)
{
	acpi_status		status = 0;
	union acpi_object	object = {0};
	struct acpi_buffer	buffer = {sizeof(union acpi_object), &object};
	u8			cpu_index;
	static int		cpu0_initialized;

	ACPI_FUNCTION_TRACE("acpi_processor_get_info");

	if (!pr)
		return_VALUE(-EINVAL);

	if (num_online_cpus() > 1)
		errata.smp = TRUE;

	acpi_processor_errata(pr);

	/*
	 * Check to see if we have bus mastering arbitration control.  This
	 * is required for proper C3 usage (to maintain cache coherency).
	 */
	if (acpi_fadt.V1_pm2_cnt_blk && acpi_fadt.pm2_cnt_len) {
		pr->flags.bm_control = 1;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"Bus mastering arbitration control present\n"));
	}
	else
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"No bus mastering arbitration control\n"));

	/*
	 * Evalute the processor object.  Note that it is common on SMP to
	 * have the first (boot) processor with a valid PBLK address while
	 * all others have a NULL address.
	 */
	status = acpi_evaluate_object(pr->handle, NULL, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Error evaluating processor object\n"));
		return_VALUE(-ENODEV);
	}

	/*
	 * TBD: Synch processor ID (via LAPIC/LSAPIC structures) on SMP.
	 *	>>> 'acpi_get_processor_id(acpi_id, &id)' in arch/xxx/acpi.c
	 */
	pr->acpi_id = object.processor.proc_id;

	cpu_index = convert_acpiid_to_cpu(pr->acpi_id);

  	/* Handle UP system running SMP kernel, with no LAPIC in MADT */
  	if ( !cpu0_initialized && (cpu_index == 0xff) &&
  		       	(num_online_cpus() == 1)) {
   		cpu_index = 0;
   	}

   	cpu0_initialized = 1;

   	pr->id = cpu_index;

  	/*
  	 *  Extra Processor objects may be enumerated on MP systems with
  	 *  less than the max # of CPUs. They should be ignored _iff
  	 *  they are physically not present.
  	 */
   	if (cpu_index >=  NR_CPUS) {
   		if (ACPI_FAILURE(acpi_processor_hotadd_init(pr->handle, &pr->id))) {
   			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
   				"Error getting cpuindex for acpiid 0x%x\n",
   				pr->acpi_id));
   			return_VALUE(-ENODEV);
   		}
    	}
 
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Processor [%d:%d]\n", pr->id, 
		pr->acpi_id));

	if (!object.processor.pblk_address)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No PBLK (NULL address)\n"));
	else if (object.processor.pblk_length != 6)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid PBLK length [%d]\n",
			object.processor.pblk_length));
	else {
		pr->throttling.address = object.processor.pblk_address;
		pr->throttling.duty_offset = acpi_fadt.duty_offset;
		pr->throttling.duty_width = acpi_fadt.duty_width;
		pr->power.states[ACPI_STATE_C2].address =
			object.processor.pblk_address + 4;
		pr->power.states[ACPI_STATE_C3].address =
			object.processor.pblk_address + 5;

		/*
		 * We don't care about error returns - we just try to mark
		 * these reserved so that nobody else is confused into thinking
		 * that this region might be unused..
		 *
		 * (In particular, allocating the IO range for Cardbus)
		 */
		request_region(pr->throttling.address, 6, "ACPI CPU throttle");
	}

	acpi_processor_get_power_info(pr);
#ifdef CONFIG_CPU_FREQ
	acpi_processor_ppc_has_changed(pr);
#endif
	acpi_processor_get_throttling_info(pr);
	acpi_processor_get_limit_info(pr);

	return_VALUE(0);
}

static int
acpi_processor_start(
	struct acpi_device	*device)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	u32			i = 0;
	struct acpi_processor	*pr;

	ACPI_FUNCTION_TRACE("acpi_processor_start");

	pr = acpi_driver_data(device);

	result = acpi_processor_get_info(pr);
	if (result) {
		/* Processor is physically not present */
		return_VALUE(0);
	}

	BUG_ON((pr->id >= NR_CPUS) || (pr->id < 0));

	processors[pr->id] = pr;

	result = acpi_processor_add_fs(device);
	if (result)
		goto end;

	status = acpi_install_notify_handler(pr->handle, ACPI_DEVICE_NOTIFY,
		acpi_processor_notify, pr);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Error installing device notify handler\n"));
	}

	/*
	 * Install the idle handler if processor power management is supported.
	 * Note that we use previously set idle handler will be used on 
	 * platforms that only support C1.
	 */
	if ((pr->flags.power) && (!boot_option_idle_override)) {
		printk(KERN_INFO PREFIX "%s [%s] (supports",
			acpi_device_name(device), acpi_device_bid(device));
		for (i = 1; i < ACPI_C_STATE_COUNT; i++)
			if (pr->power.states[i].valid)
				printk(" C%d", i);
		printk(")\n");
		if (pr->id == 0) {
			pm_idle_save = pm_idle;
			pm_idle = acpi_processor_idle;
		}
	}
	
	if (pr->flags.throttling) {
		printk(KERN_INFO PREFIX "%s [%s] (supports",
			acpi_device_name(device), acpi_device_bid(device));
		printk(" %d throttling states", pr->throttling.state_count);
		printk(")\n");
	}

end:

	return_VALUE(result);
}



static void
acpi_processor_notify (
	acpi_handle		handle,
	u32			event,
	void			*data)
{
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	struct acpi_device	*device = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_notify");

	if (!pr)
		return_VOID;

	if (acpi_bus_get_device(pr->handle, &device))
		return_VOID;

	switch (event) {
	case ACPI_PROCESSOR_NOTIFY_PERFORMANCE:
		acpi_processor_ppc_has_changed(pr);
		acpi_bus_generate_event(device, event, 
			pr->performance_platform_limit);
		break;
	case ACPI_PROCESSOR_NOTIFY_POWER:
		/* TBD */
		acpi_bus_generate_event(device, event, 0);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"Unsupported event [0x%x]\n", event));
		break;
	}

	return_VOID;
}


static int
acpi_processor_add (
	struct acpi_device	*device)
{
	struct acpi_processor	*pr = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_add");

	if (!device)
		return_VALUE(-EINVAL);

	pr = kmalloc(sizeof(struct acpi_processor), GFP_KERNEL);
	if (!pr)
		return_VALUE(-ENOMEM);
	memset(pr, 0, sizeof(struct acpi_processor));

	pr->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_PROCESSOR_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PROCESSOR_CLASS);
	acpi_driver_data(device) = pr;

	return_VALUE(0);
}


static int
acpi_processor_remove (
	struct acpi_device	*device,
	int			type)
{
	acpi_status		status = AE_OK;
	struct acpi_processor	*pr = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_remove");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

	pr = (struct acpi_processor *) acpi_driver_data(device);

	if (pr->id >= NR_CPUS) {
		kfree(pr);
		return_VALUE(0);
	}

	if (type == ACPI_BUS_REMOVAL_EJECT) {
		if (acpi_processor_handle_eject(pr))
			return_VALUE(-EINVAL);
	}

	/* Unregister the idle handler when processor #0 is removed. */
	if (pr->id == 0) {
		pm_idle = pm_idle_save;
		/*
		 * We are about to unload the current idle thread pm callback
		 * (pm_idle), Wait for all processors to update cached/local
		 * copies of pm_idle before proceeding.
		 */
		synchronize_kernel();
	}

	status = acpi_remove_notify_handler(pr->handle, ACPI_DEVICE_NOTIFY, 
		acpi_processor_notify);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Error removing notify handler\n"));
	}

	acpi_processor_remove_fs(device);

	processors[pr->id] = NULL;

	kfree(pr);

	return_VALUE(0);
}

/*
 * IBM ThinkPad R40e crashes mysteriously when going into C2 or C3. 
 * For now disable this. Probably a bug somewhere else.
 *
 * To skip this limit, boot/load with a large max_cstate limit.
 */
static int no_c2c3(struct dmi_system_id *id)
{
	if (max_cstate > ACPI_C_STATES_MAX)
		return 0;

	printk(KERN_NOTICE PREFIX "%s detected - C2,C3 disabled."
		" Override with \"processor.max_cstate=9\"\n", id->ident);

	max_cstate = 1;

	return 0;
}

static struct dmi_system_id __initdata processor_dmi_table[] = { 
	{ no_c2c3, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET60WW") }},
	{ no_c2c3, "Medion 41700", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"Phoenix Technologies LTD"),
	  DMI_MATCH(DMI_BIOS_VERSION,"R01-A1J") }},
	{},
};

#ifdef CONFIG_ACPI_HOTPLUG_CPU
/****************************************************************************
 * 	Acpi processor hotplug support 				       	    *
 ****************************************************************************/

static int is_processor_present(acpi_handle handle);

static int
is_processor_present(
	acpi_handle handle)
{
	acpi_status 		status;
	unsigned long		sta = 0;

	ACPI_FUNCTION_TRACE("is_processor_present");

	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status) || !(sta & ACPI_STA_PRESENT)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Processor Device is not present\n"));
		return_VALUE(0);
	}
	return_VALUE(1);
}


static
int acpi_processor_device_add(
	acpi_handle	handle,
	struct acpi_device **device)
{
	acpi_handle		phandle;
	struct acpi_device 	*pdev;
	struct acpi_processor	*pr;

	ACPI_FUNCTION_TRACE("acpi_processor_device_add");

	if (acpi_get_parent(handle, &phandle)) {
		return_VALUE(-ENODEV);
	}

	if (acpi_bus_get_device(phandle, &pdev)) {
		return_VALUE(-ENODEV);
	}

	if (acpi_bus_add(device, pdev, handle, ACPI_BUS_TYPE_PROCESSOR)) {
		return_VALUE(-ENODEV);
	}

	acpi_bus_scan(*device);

	pr = acpi_driver_data(*device);
	if (!pr)
		return_VALUE(-ENODEV);

	if ((pr->id >=0) && (pr->id < NR_CPUS)) {
		kobject_hotplug(&(*device)->kobj, KOBJ_ONLINE);
	}
	return_VALUE(0);
}


static void
acpi_processor_hotplug_notify (
	acpi_handle		handle,
	u32			event,
	void			*data)
{
	struct acpi_processor	*pr;
	struct acpi_device	*device = NULL;
	int result;

	ACPI_FUNCTION_TRACE("acpi_processor_hotplug_notify");

	switch (event) {
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		printk("Processor driver received %s event\n",
			(event==ACPI_NOTIFY_BUS_CHECK)?
			"ACPI_NOTIFY_BUS_CHECK":"ACPI_NOTIFY_DEVICE_CHECK");

		if (!is_processor_present(handle))
			break;

		if (acpi_bus_get_device(handle, &device)) {
			result = acpi_processor_device_add(handle, &device);
			if (result)
				ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					"Unable to add the device\n"));
			break;
		}

		pr = acpi_driver_data(device);
		if (!pr) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				"Driver data is NULL\n"));
			break;
		}
		
		if (pr->id >= 0 && (pr->id < NR_CPUS)) {
			kobject_hotplug(&device->kobj, KOBJ_OFFLINE);
			break;
		}

		result = acpi_processor_start(device);
		if ((!result) && ((pr->id >=0) && (pr->id < NR_CPUS))) {
			kobject_hotplug(&device->kobj, KOBJ_ONLINE);
		} else {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				"Device [%s] failed to start\n",
				acpi_device_bid(device)));
		}
	break;
	case ACPI_NOTIFY_EJECT_REQUEST:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,"received ACPI_NOTIFY_EJECT_REQUEST\n"));

		if (acpi_bus_get_device(handle, &device)) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,"Device don't exist, dropping EJECT\n"));
			break;
		}
		pr = acpi_driver_data(device);
		if (!pr) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,"Driver data is NULL, dropping EJECT\n"));
			return_VOID;
		}

		if ((pr->id < NR_CPUS) && (cpu_present(pr->id)))
			kobject_hotplug(&device->kobj, KOBJ_OFFLINE);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"Unsupported event [0x%x]\n", event));
		break;
	}

	return_VOID;
}

static acpi_status
processor_walk_namespace_cb(acpi_handle handle,
	u32 lvl,
	void *context,
	void **rv)
{
	acpi_status 			status;
	int *action = context;
	acpi_object_type	type = 0;

	status = acpi_get_type(handle, &type);
	if (ACPI_FAILURE(status))
		return(AE_OK);

	if (type != ACPI_TYPE_PROCESSOR)
		return(AE_OK);

	switch(*action) {
	case INSTALL_NOTIFY_HANDLER:
		acpi_install_notify_handler(handle,
			ACPI_SYSTEM_NOTIFY,
			acpi_processor_hotplug_notify,
			NULL);
		break;
	case UNINSTALL_NOTIFY_HANDLER:
		acpi_remove_notify_handler(handle,
			ACPI_SYSTEM_NOTIFY,
			acpi_processor_hotplug_notify);
		break;
	default:
		break;
	}

	return(AE_OK);
}


static acpi_status
acpi_processor_hotadd_init(
	acpi_handle		handle,
	int			*p_cpu)
{
	ACPI_FUNCTION_TRACE("acpi_processor_hotadd_init");
	
	if (!is_processor_present(handle)) {
		return_VALUE(AE_ERROR);
	}

	if (acpi_map_lsapic(handle, p_cpu))
		return_VALUE(AE_ERROR);

	if (arch_register_cpu(*p_cpu)) {
		acpi_unmap_lsapic(*p_cpu);
		return_VALUE(AE_ERROR);
	}

	return_VALUE(AE_OK);
}


static int
acpi_processor_handle_eject(struct acpi_processor *pr)
{
	if (cpu_online(pr->id)) {
		return(-EINVAL);
	}
	arch_unregister_cpu(pr->id);
	acpi_unmap_lsapic(pr->id);
	return(0);
}
#else
static acpi_status
acpi_processor_hotadd_init(
	acpi_handle		handle,
	int			*p_cpu)
{
	return AE_ERROR;
}
static int
acpi_processor_handle_eject(struct acpi_processor *pr)
{
	return(-EINVAL);
}
#endif


static
void acpi_processor_install_hotplug_notify(void)
{
#ifdef CONFIG_ACPI_HOTPLUG_CPU
	int action = INSTALL_NOTIFY_HANDLER;
	acpi_walk_namespace(ACPI_TYPE_PROCESSOR,
				     ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX,
				     processor_walk_namespace_cb,
				     &action, NULL);
#endif
}


static
void acpi_processor_uninstall_hotplug_notify(void)
{
#ifdef CONFIG_ACPI_HOTPLUG_CPU
	int action = UNINSTALL_NOTIFY_HANDLER;
	acpi_walk_namespace(ACPI_TYPE_PROCESSOR,
				     ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX,
				     processor_walk_namespace_cb,
				     &action, NULL);
#endif
}

/*
 * We keep the driver loaded even when ACPI is not running. 
 * This is needed for the powernow-k8 driver, that works even without
 * ACPI, but needs symbols from this driver
 */

static int __init
acpi_processor_init (void)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_init");

	memset(&processors, 0, sizeof(processors));
	memset(&errata, 0, sizeof(errata));

	acpi_processor_dir = proc_mkdir(ACPI_PROCESSOR_CLASS, acpi_root_dir);
	if (!acpi_processor_dir)
		return_VALUE(0);
	acpi_processor_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&acpi_processor_driver);
	if (result < 0) {
		remove_proc_entry(ACPI_PROCESSOR_CLASS, acpi_root_dir);
		return_VALUE(0);
	}

	acpi_processor_install_hotplug_notify();

	acpi_thermal_cpufreq_init();

	acpi_processor_ppc_init();

	dmi_check_system(processor_dmi_table); 

	if (max_cstate < ACPI_C_STATES_MAX)
		printk(KERN_NOTICE "ACPI: processor limited to max C-state %d\n", max_cstate);

	return_VALUE(0);
}


static void __exit
acpi_processor_exit (void)
{
	ACPI_FUNCTION_TRACE("acpi_processor_exit");

	acpi_processor_ppc_exit();

	acpi_thermal_cpufreq_exit();

	acpi_processor_uninstall_hotplug_notify();

	acpi_bus_unregister_driver(&acpi_processor_driver);

	remove_proc_entry(ACPI_PROCESSOR_CLASS, acpi_root_dir);

	return_VOID;
}


module_init(acpi_processor_init);
module_exit(acpi_processor_exit);
module_param_named(max_cstate, max_cstate, uint, 0);

EXPORT_SYMBOL(acpi_processor_set_thermal_limit);

MODULE_ALIAS("processor");
