/*
 * acpi_processor.c - ACPI Processor Driver ($Revision: 50 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
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
 *	1. Make # power/performance states dynamic.
 *	2. Includes support for _real_ performance states (not just throttle).
 *	3. Support duty_cycle values that span bit 4.
 *	4. Optimize by having scheduler determine business instead of
 *         having us try to calculate it here.
 *      5. Need C1 timing -- must modify kernel (IRQ handler) to get this.
 *	6. Convert time values to ticks (initially) to avoid having to do
 *         the math (acpi_get_timer_duration).
 *      7. What is a good default value for the OS busy_metric?
 *      8. Support both thermal and power limits.
 *      9. Resolve PIIX4 BMISX errata issue (getting an I/O port value of 0).
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <asm/io.h>
#include <asm/system.h>
#include "acpi_bus.h"
#include "acpi_drivers.h"


#define _COMPONENT		ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME		("acpi_processor")

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION(ACPI_PROCESSOR_DRIVER_NAME);
MODULE_LICENSE("GPL");

#define PREFIX				"ACPI: "


#define ACPI_PROCESSOR_MAX_POWER	ACPI_C_STATE_COUNT
#define ACPI_PROCESSOR_MAX_C2_LATENCY	100
#define ACPI_PROCESSOR_MAX_C3_LATENCY	1000

#define ACPI_PROCESSOR_MAX_PERFORMANCE	4

#define ACPI_PROCESSOR_MAX_THROTTLING	16
#define ACPI_PROCESSOR_MAX_THROTTLE	500	/* 50% */
#define ACPI_PROCESSOR_MAX_DUTY_WIDTH	4

const u32 POWER_OF_2[] = {1,2,4,8,16,32,64};

#define ACPI_PROCESSOR_MAX_LIMIT	20

static int acpi_processor_add (struct acpi_device *device);
static int acpi_processor_remove (struct acpi_device *device, int type);

static struct acpi_driver acpi_processor_driver = {
	name:			ACPI_PROCESSOR_DRIVER_NAME,
	class:			ACPI_PROCESSOR_CLASS,
	ids:			ACPI_PROCESSOR_HID,
	ops:			{
					add:	acpi_processor_add,
					remove:	acpi_processor_remove,
				},
};

/* Power Management */

struct acpi_processor_cx_policy {
	u32			count;
	int			state;
	struct {
		u32			time;
		u32			count;
		u32			bm;
	}			threshold;
};

struct acpi_processor_cx {
	u8			valid;
	u32			address;
	u32			latency;
	u32			power;
	u32			usage;
	struct acpi_processor_cx_policy promotion;
	struct acpi_processor_cx_policy demotion;
};

struct acpi_processor_power {
	int			state;
	int			default_state;
	u32			bm_activity;
	u32			busy_metric;
	struct acpi_processor_cx states[ACPI_PROCESSOR_MAX_POWER];
};

/* Performance Management */

struct acpi_processor_px {
	u8			valid;
	u32			core_frequency;
	u32			power;
	u32			transition_latency;
	u32			bus_master_latency;
	u32			control;
	u32			status;
};

struct acpi_processor_performance {
	int			state;
	int			state_count;
	struct acpi_processor_px states[ACPI_PROCESSOR_MAX_PERFORMANCE];
};


/* Throttling Control */

struct acpi_processor_tx {
	u8			valid;
	u16			power;
	u16			performance;
};

struct acpi_processor_throttling {
	int			state;
	u32			address;
	u8			duty_offset;
	u8			duty_width;
	int			state_count;
	struct acpi_processor_tx states[ACPI_PROCESSOR_MAX_THROTTLING];
};

/* Limit Interface */

struct acpi_processor_lx {
	u8			valid;
	u16			performance;
	int			px;
	int			tx;
};

struct acpi_processor_limit {
	int			state;
	int			state_count;
	struct {
		u8			valid;
		u16			performance;
		int			px;
		int			tx;
	}			states[ACPI_PROCESSOR_MAX_LIMIT];
};

struct acpi_processor_flags {
	u8			bm_control:1;
	u8			power:1;
	u8			performance:1;
	u8			throttling:1;
	u8			limit:1;
	u8			reserved:3;
};

struct acpi_processor_errata {
	struct {
		u8			reverse_throttle;
		u32			bmisx;
	}			piix4;
};

struct acpi_processor {
	acpi_handle		handle;
	u32			acpi_id;
	u32			id;
	struct acpi_processor_flags flags;
	struct acpi_processor_errata errata;
	struct acpi_processor_power power;
	struct acpi_processor_performance performance;
	struct acpi_processor_throttling throttling;
	struct acpi_processor_limit limit;
};


static u8			acpi_processor_smp = 0;


/* --------------------------------------------------------------------------
                                Errata Handling
   -------------------------------------------------------------------------- */

int
acpi_processor_get_errata (
	struct acpi_processor	*pr)
{
	struct pci_dev		*dev = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_get_errata");

	if (!pr)
		return_VALUE(-EINVAL);

	/*
	 * PIIX4
	 * -----
	 */
	dev = pci_find_subsys(PCI_VENDOR_ID_INTEL,
		PCI_DEVICE_ID_INTEL_82371AB_3, PCI_ANY_ID, PCI_ANY_ID, dev);
	if (dev) {
		u8		rev = 0;

		pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "PIIX4 ACPI rev %d\n", rev));

		switch (rev) {

		case 0:		/* PIIX4 A-step */
		case 1:		/* PIIX4 B-step */
			/*
			 * Workaround for reverse-notation on throttling states
			 * used by early PIIX4 models.
			 */
			pr->errata.piix4.reverse_throttle = 1;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"Reverse-throttle errata enabled\n"));

		case 2:		/* PIIX4E */
		case 3:		/* PIIX4M */
			/*
			 * Workaround for errata #18 "C3 Power State/BMIDE and
			 * Type-F DMA Livelock" from the July 2001 PIIX4
			 * specification update.  Applies to all PIIX4 models.
			 */
			/* TBD: Why is the bmisx value always ZERO? */
			pr->errata.piix4.bmisx = pci_resource_start(dev, 4);
			if (pr->errata.piix4.bmisx)
				ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
					"BM-IDE errata enabled\n"));
			break;
		}
	}

	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                                Power Management
   -------------------------------------------------------------------------- */

static struct acpi_processor *acpi_processor_list[NR_CPUS];
static void (*pm_idle_save)(void) = NULL;

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
		acpi_hw_bit_register_write(ACPI_BITREG_BUS_MASTER_RLD, 0, ACPI_MTX_DO_NOT_LOCK);
		break;
	}

	/* Prepare to use new state. */
	switch (state) {
	case ACPI_STATE_C3:
		/* Enable bus master reload */
		acpi_hw_bit_register_write(ACPI_BITREG_BUS_MASTER_RLD, 1, ACPI_MTX_DO_NOT_LOCK);
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
	int			next_state = 0;
	u32			start_ticks = 0;
	u32			end_ticks = 0;
	u32			time_elapsed = 0;
	static unsigned long	last_idle_jiffies = 0;

	pr = acpi_processor_list[smp_processor_id()];
	if (!pr)
		return;

	/*
	 * Interrupts must be disabled during bus mastering calculations and
	 * for C2/C3 transitions.
	 */
	__cli();

	next_state = pr->power.state;

	/*
	 * Check OS Idleness:
	 * ------------------
	 * If the OS has been busy (hasn't called the idle handler in a while)
	 * then automatically demote to the default power state (e.g. C1).
	 *
	 * TBD: Optimize by having scheduler determine business instead
	 *      of having us try to calculate it here.
	 */
	if (pr->power.state != pr->power.default_state) {
		if ((jiffies - last_idle_jiffies) >= pr->power.busy_metric) {
			next_state = pr->power.default_state;
			if (next_state != pr->power.state)
				acpi_processor_power_activate(pr, next_state);
		}
	}

	/*
	 * Log BM Activity:
	 * ----------------
	 * Read BM_STS and record its value for later use by C3 policy.
	 * (Note that we save the BM_STS values for the last 32 cycles).
	 */
	if (pr->flags.bm_control) {
		pr->power.bm_activity <<= 1;
		if (acpi_hw_bit_register_read(ACPI_BITREG_BUS_MASTER_STATUS, ACPI_MTX_DO_NOT_LOCK)) {
			pr->power.bm_activity |= 1;
			acpi_hw_bit_register_write(ACPI_BITREG_BUS_MASTER_STATUS,
				1, ACPI_MTX_DO_NOT_LOCK);
		}
		/*
		 * PIIX4 Errata:
		 * -------------
		 * This code is a workaround for errata #18 "C3 Power State/
		 * BMIDE and Type-F DMA Livelock" from the July '01 PIIX4
		 * specification update.  Note that BM_STS doesn't always
		 * reflect the true state of bus mastering activity; forcing
		 * us to manually check the BMIDEA bit of each IDE channel.
		 */
		else if (pr->errata.piix4.bmisx) {
			if ((inb_p(pr->errata.piix4.bmisx + 0x02) & 0x01) ||
				(inb_p(pr->errata.piix4.bmisx + 0x0A) & 0x01))
				pr->power.bm_activity |= 1;
		}
	}

	cx = &(pr->power.states[pr->power.state]);
	cx->usage++;

	/*
	 * Sleep:
	 * ------
	 * Invoke the current Cx state to put the processor to sleep.
	 */
	switch (pr->power.state) {

	case ACPI_STATE_C1:
		/* Invoke C1. */
		safe_halt();
		/*
                 * TBD: Can't get time duration while in C1, as resumes
		 *      go to an ISR rather than here.  Need to instrument
		 *      base interrupt handler.
		 */
		time_elapsed = 0xFFFFFFFF;
		break;

	case ACPI_STATE_C2:
		/* See how long we're asleep for */
		start_ticks = inl(acpi_fadt.Xpm_tmr_blk.address);
		/* Invoke C2 */
		inb(pr->power.states[ACPI_STATE_C2].address);
		/* Dummy op - must do something useless after P_LVL2 read */
		end_ticks = inl(acpi_fadt.Xpm_tmr_blk.address);
		/* Compute time elapsed */
		end_ticks = inl(acpi_fadt.Xpm_tmr_blk.address);
		/* Re-enable interrupts */
		__sti();
		/*
		 * Compute the amount of time asleep (in the Cx state).
		 * TBD: Convert to PM timer ticks initially to avoid having
		 *      to do the math (acpi_get_timer_duration).
		 */
		acpi_get_timer_duration(start_ticks, end_ticks, &time_elapsed);
		break;

	case ACPI_STATE_C3:
		/* Disable bus master arbitration */
		acpi_hw_bit_register_write(ACPI_BITREG_ARB_DISABLE, 1, ACPI_MTX_DO_NOT_LOCK);
		/* See how long we're asleep for */
		start_ticks = inl(acpi_fadt.Xpm_tmr_blk.address);
		/* Invoke C2 */
		inb(pr->power.states[ACPI_STATE_C3].address);
		/* Dummy op - must do something useless after P_LVL3 read */
		end_ticks = inl(acpi_fadt.Xpm_tmr_blk.address);
		/* Compute time elapsed */
		end_ticks = inl(acpi_fadt.Xpm_tmr_blk.address);
		/* Enable bus master arbitration */
		acpi_hw_bit_register_write(ACPI_BITREG_ARB_DISABLE, 0, ACPI_MTX_DO_NOT_LOCK);
		/* Re-enable interrupts */
		__sti();
		/*
		 * Compute the amount of time asleep (in the Cx state).
		 * TBD: Convert to PM timer ticks initially to avoid having
		 *      to do the math (acpi_get_timer_duration).
		 */
		acpi_get_timer_duration(start_ticks, end_ticks, &time_elapsed);
		break;

	default:
		__sti();
		return;
	}

	/*
	 * Promotion?
	 * ----------
	 * Track the number of longs (time asleep is greater than threshold)
	 * and promote when the count threshold is reached.  Note that bus
	 * mastering activity may prevent promotions.
	 */
	if (cx->promotion.state) {
		if (time_elapsed >= cx->promotion.threshold.time) {
			cx->promotion.count++;
 			cx->demotion.count = 0;
			if (cx->promotion.count >= cx->promotion.threshold.count) {
				if (pr->flags.bm_control) {
					if (!(pr->power.bm_activity & cx->promotion.threshold.bm))
						next_state = cx->promotion.state;
				}
				else
					next_state = cx->promotion.state;
			}
		}
	}

	/*
	 * Demotion?
	 * ---------
	 * Track the number of shorts (time asleep is less than time threshold)
	 * and demote when the usage threshold is reached.  Note that bus
	 * mastering activity may cause immediate demotions.
	 */
	if (cx->demotion.state) {
		if (time_elapsed < cx->demotion.threshold.time) {
			cx->demotion.count++;
			cx->promotion.count = 0;
			if (cx->demotion.count >= cx->demotion.threshold.count)
				next_state = cx->demotion.state;
		}
		if (pr->flags.bm_control) {
			if (pr->power.bm_activity & cx->demotion.threshold.bm)
				next_state = cx->demotion.state;
		}
	}

	/*
	 * New Cx State?
	 * -------------
	 * If we're going to start using a new Cx state we must clean up
	 * from the previous and prepare to use the new.
	 */
	if (next_state != pr->power.state)
		acpi_processor_power_activate(pr, next_state);

	/*
	 * Track OS Idleness:
	 * ------------------
	 * Record a jiffies timestamp to compute time elapsed between calls
	 * to the idle handler.
	 */
	last_idle_jiffies = jiffies;

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
	 * The Busy Metric is used to determine when the OS has been busy
	 * and thus when policy should return to using the default Cx state
	 * (e.g. C1).  On Linux we use the number of jiffies (scheduler
	 * quantums) that transpire between calls to the idle handler.
	 *
	 * TBD: What is a good value for the OS busy_metric?
	 */
	pr->power.busy_metric = 2;

	/*
	 * C0/C1
	 * -----
	 */
	if (pr->power.states[ACPI_STATE_C1].valid) {
		pr->power.state = ACPI_STATE_C1;
		pr->power.default_state = ACPI_STATE_C1;
	}
	else {
		pr->power.state = ACPI_STATE_C0;
		pr->power.default_state = ACPI_STATE_C0;
		return_VALUE(0);
	}

	/*
	 * C1/C2
	 * -----
	 * Set the default C1 promotion and C2 demotion policies, where we
	 * promote from C1 to C2 anytime we're asleep in C1 for longer than
	 * two times the C2 latency (to amortize cost of transitions). Demote
	 * from C2 to C1 anytime we're asleep in C2 for less than this time.
	 */
	if (pr->power.states[ACPI_STATE_C2].valid) {
		pr->power.states[ACPI_STATE_C1].promotion.threshold.count = 10;
		pr->power.states[ACPI_STATE_C1].promotion.threshold.time =
			(2 * pr->power.states[ACPI_STATE_C2].latency);
		pr->power.states[ACPI_STATE_C1].promotion.state = ACPI_STATE_C2;

		pr->power.states[ACPI_STATE_C2].demotion.threshold.count = 1;
		pr->power.states[ACPI_STATE_C2].demotion.threshold.time =
			(2 * pr->power.states[ACPI_STATE_C2].latency);
		pr->power.states[ACPI_STATE_C2].demotion.state = ACPI_STATE_C1;
	}

	/*
	 * C2/C3
	 * -----
	 * Set default C2 promotion and C3 demotion policies, where we promote
	 * from C2 to C3 after 4 cycles (0x0F) of no bus mastering activity
	 * (while maintaining sleep time criteria).  Demote immediately on a
	 * short or whenever bus mastering activity occurs.
	 */
	if ((pr->power.states[ACPI_STATE_C2].valid) &&
		(pr->power.states[ACPI_STATE_C3].valid)) {
		pr->power.states[ACPI_STATE_C2].promotion.threshold.count = 1;
		pr->power.states[ACPI_STATE_C2].promotion.threshold.time =
			(2 * pr->power.states[ACPI_STATE_C3].latency);
		pr->power.states[ACPI_STATE_C2].promotion.threshold.bm = 0x0F;
		pr->power.states[ACPI_STATE_C2].promotion.state = ACPI_STATE_C3;

		pr->power.states[ACPI_STATE_C3].demotion.threshold.count = 1;
		pr->power.states[ACPI_STATE_C3].demotion.threshold.time =
			(2 * pr->power.states[ACPI_STATE_C3].latency);
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
	pr->power.states[ACPI_STATE_C0].valid = TRUE;

	/*
	 * C1
	 * --
	 * ACPI requires C1 support for all processors.
	 *
	 * TBD: What about PROC_C1?
	 */
	pr->power.states[ACPI_STATE_C1].valid = TRUE;

	/*
	 * C2
	 * --
	 * We're (currently) only supporting C2 on UP systems.
	 *
	 * TBD: Support for C2 on MP (P_LVL2_UP).
	 */
	if (pr->power.states[ACPI_STATE_C2].address) {
		pr->power.states[ACPI_STATE_C2].latency = acpi_fadt.plvl2_lat;
		if (acpi_fadt.plvl2_lat > ACPI_PROCESSOR_MAX_C2_LATENCY)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"C2 latency too large [%d]\n",
				acpi_fadt.plvl2_lat));
		else if (!acpi_processor_smp)
			pr->power.states[ACPI_STATE_C2].valid = TRUE;
	}

	/*
	 * C3
	 * --
	 * We're (currently) only supporting C3 on UP systems that include
	 * bus mastering arbitration control.  Note that this method of
	 * maintaining cache coherency (disabling of bus mastering) cannot be
	 * used on SMP systems, and flushing caches (e.g. WBINVD) is simply
	 * too costly (at this time).
	 */
	if (pr->power.states[ACPI_STATE_C3].address) {
		pr->power.states[ACPI_STATE_C3].latency = acpi_fadt.plvl3_lat;
		if (acpi_fadt.plvl3_lat > ACPI_PROCESSOR_MAX_C3_LATENCY)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"C3 latency too large [%d]\n", 
				acpi_fadt.plvl3_lat));
		else if (!acpi_processor_smp && pr->flags.bm_control)
			pr->power.states[ACPI_STATE_C3].valid = 1;
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
	if (0 != result)
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
                              Performance Management
   -------------------------------------------------------------------------- */

static int
acpi_processor_get_performance (
	struct acpi_processor	*pr)
{
	ACPI_FUNCTION_TRACE("acpi_processor_get_performance_state");

	if (!pr)
		return_VALUE(-EINVAL);

	if (!pr->flags.performance)
		return_VALUE(0);

	/* TBD */

	return_VALUE(0);
}


static int
acpi_processor_set_performance (
	struct acpi_processor	*pr,
	int			state)
{
	ACPI_FUNCTION_TRACE("acpi_processor_set_performance_state");

	if (!pr)
		return_VALUE(-EINVAL);

	if (!pr->flags.performance)
		return_VALUE(-ENODEV);

	if (state >= pr->performance.state_count)
		return_VALUE(-ENODEV);

	if (state == pr->performance.state)
		return_VALUE(0);

	/* TBD */

	return_VALUE(0);
}


static int
acpi_processor_get_performance_info (
	struct acpi_processor	*pr)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_get_performance_info");

	if (!pr)
		return_VALUE(-EINVAL);

	/* TBD: Support ACPI 2.0 objects */

	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                              Throttling Control
   -------------------------------------------------------------------------- */

static int
acpi_processor_get_throttling (
	struct acpi_processor	*pr)
{
	int			state = 0;
	u32			value = 0;
	u32			duty_mask = 0;
	u32			duty_value = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_get_throttling");

	if (!pr)
		return_VALUE(-EINVAL);

	if (!pr->flags.throttling)
		return_VALUE(-ENODEV);

	pr->throttling.state = 0;

	__cli();

	duty_mask = pr->throttling.state_count - 1;
	duty_mask <<= pr->throttling.duty_offset;

	value = inb(pr->throttling.address);

	/*
	 * Compute the current throttling state when throttling is enabled
	 * (bit 4 is on).  Note that the reverse_throttling flag indicates
	 * that the duty_value is opposite of that specified by ACPI.
	 */
	if (value & 0x10) {
		duty_value = value & duty_mask;
		duty_value >>= pr->throttling.duty_offset;

		if (duty_value) {
			if (pr->errata.piix4.reverse_throttle)
				state = duty_value;
			else
				state = pr->throttling.state_count-duty_value;
		}
	}

	pr->throttling.state = state;

	__sti();

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Throttling state is T%d (%d%% throttling applied)\n",
		state, pr->throttling.states[state].performance));

	return_VALUE(0);
}


static int
acpi_processor_set_throttling (
	struct acpi_processor	*pr,
	int			state)
{
	u32                     value = 0;
	u32                     duty_mask = 0;
	u32                     duty_value = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_set_throttling");

	if (!pr)
		return_VALUE(-EINVAL);

	if ((state < 0) || (state > (pr->throttling.state_count - 1)))
		return_VALUE(-EINVAL);

	if (!pr->flags.throttling || !pr->throttling.states[state].valid)
		return_VALUE(-ENODEV);

	if (state == pr->throttling.state)
		return_VALUE(0);

	__cli();

	/*
	 * Calculate the duty_value and duty_mask.  Note that the
	 * reverse_throttling flag indicates that the duty_value is
	 * opposite of that specified by ACPI.
	 */
	if (state) {
		if (pr->errata.piix4.reverse_throttle)
			duty_value = state;
		else
			duty_value = pr->throttling.state_count - state;

		duty_value <<= pr->throttling.duty_offset;

		/* Used to clear all duty_value bits */
		duty_mask = pr->performance.state_count - 1;
		duty_mask <<= acpi_fadt.duty_offset;
		duty_mask = ~duty_mask;
	}

	/*
	 * Disable throttling by writing a 0 to bit 4.  Note that we must
	 * turn it off before you can change the duty_value.
	 */
	value = inb(pr->throttling.address);
	if (value & 0x10) {
		value &= 0xFFFFFFEF;
		outl(value, pr->throttling.address);
	}

	/*
	 * Write the new duty_value and then enable throttling.  Note
	 * that a state value of 0 leaves throttling disabled.
	 */
	if (state) {
		value &= duty_mask;
		value |= duty_value;
		outl(value, pr->throttling.address);

		value |= 0x00000010;
		outl(value, pr->throttling.address);
	}

	pr->throttling.state = state;

	__sti();

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Throttling state set to T%d (%d%%)\n",
		state, (pr->throttling.states[state].performance?pr->throttling.states[state].performance/10:0)));

	return_VALUE(0);
}


static int
acpi_processor_get_throttling_info (
	struct acpi_processor	*pr)
{
	int			result = 0;
	int			step = 0;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_get_throttling_info");

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"pblk_address[0x%08x] duty_offset[%d] duty_width[%d]\n",
		pr->throttling.address,
		pr->throttling.duty_offset,
		pr->throttling.duty_width));

	if (!pr)
		return_VALUE(-EINVAL);

	/* TBD: Support ACPI 2.0 objects */

	if (!pr->throttling.address) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No throttling register\n"));
		return_VALUE(0);
	}
	else if (!pr->throttling.duty_width) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid duty_width\n"));
		return_VALUE(-EFAULT);
	}
	/* TBD: Support duty_cycle values that span bit 4. */
	else if ((pr->throttling.duty_offset
		+ pr->throttling.duty_width) > 4) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "duty_cycle spans bit 4\n"));
		return_VALUE(0);
	}

	pr->throttling.state_count = POWER_OF_2[acpi_fadt.duty_width];

	/*
	 * Compute state values. Note that throttling displays a linear power/
	 * performance relationship (at 50% performance the CPU will consume
	 * 50% power).  Values are in 1/10th of a percent to preserve accuracy.
	 */

	step = (1000 / pr->throttling.state_count);

	for (i=0; i<pr->throttling.state_count; i++) {
		pr->throttling.states[i].performance = step * i;
		pr->throttling.states[i].power = step * i;
		pr->throttling.states[i].valid = 1;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %d throttling states\n", 
		pr->throttling.state_count));

	pr->flags.throttling = 1;

	/*
	 * Disable throttling (if enabled).  We'll let subsequent policy (e.g. 
	 * thermal) decide to lower performance if it so chooses, but for now 
	 * we'll crank up the speed.
	 */

	result = acpi_processor_get_throttling(pr);
	if (0 != result)
		goto end;

	if (pr->throttling.state) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Disabling throttling (was T%d)\n", 
			pr->throttling.state));
		result = acpi_processor_set_throttling(pr, 0);
		if (0 != result)
			goto end;
	}

end:
	if (0 != result)
		pr->flags.throttling = 0;

	return_VALUE(result);
}


/* --------------------------------------------------------------------------
                                 Limit Interface
   -------------------------------------------------------------------------- */

int
acpi_processor_set_limit (
	acpi_handle		handle,
	int			type,
	int			*state)
{
	int			result = 0;
	struct acpi_processor	*pr = NULL;
	struct acpi_device	*device = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_set_limit");

	if (!state)
		return_VALUE(-EINVAL);

	result = acpi_bus_get_device(handle, &device);
	if (0 != result)
		return_VALUE(result);

	pr = (struct acpi_processor *) acpi_driver_data(device);
	if (!pr)
		return_VALUE(-ENODEV);

	if (!pr->flags.limit)
		return_VALUE(-ENODEV);

	switch (type) {
	case ACPI_PROCESSOR_LIMIT_NONE:
		*state = 0;
		pr->limit.state = 0;
		break;
	case ACPI_PROCESSOR_LIMIT_INCREMENT:
		if (*state == (pr->limit.state_count - 1)) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Already at maximum limit state\n"));
			return_VALUE(1);
		}
		*state = ++pr->limit.state;
		break;
	case ACPI_PROCESSOR_LIMIT_DECREMENT:
		if (*state == 0) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Already at minimum limit state\n"));
			return_VALUE(1);
		}
		*state = --pr->limit.state;
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid limit type [%d]\n",
			type));
		*state = pr->limit.state;
		return_VALUE(-EINVAL);
		break;
	}

	if (pr->flags.performance) {
		result = acpi_processor_set_performance(pr, 
			pr->limit.states[*state].px);
		if (0 != result)
			goto end;
	}

	if (pr->flags.throttling) {
		result = acpi_processor_set_throttling(pr, 
			pr->limit.states[*state].tx);
		if (0 != result)
			goto end;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Processor [%d] limit now %d%% (P%d:T%d)\n",
		pr->id,
		pr->limit.states[*state].performance / 10,
		pr->limit.states[*state].px,
		pr->limit.states[*state].tx));

end:
	if (0 != result)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unable to set limit\n"));

	return_VALUE(result);
}


static int
acpi_processor_get_limit_info (
	struct acpi_processor	*pr)
{
	int			i = 0;
	int			px = 0;
	int			tx = 0;
	int			base_perf = 1000;
	int			throttle = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_get_limit_info");

	if (!pr)
		return_VALUE(-EINVAL);

	/*
	 * Limit
	 * -----
	 * Our default policy is to only use throttling at the lowest
	 * performance state.  This is enforced by adding throttling states 
	 * after perormance states.  We also only expose throttling states 
	 * less than the maximum throttle value (e.g. 50%).
	 */

	if (pr->flags.performance) {
		for (px=0; px<pr->performance.state_count; px++) {
			if (!pr->performance.states[px].valid)
				continue;
			i = pr->limit.state_count++;
			pr->limit.states[i].px = px;
			pr->limit.states[i].performance = (pr->performance.states[px].core_frequency / pr->performance.states[0].core_frequency) * 1000;
			pr->limit.states[i].valid = 1;
		}
		px--;
		base_perf = pr->limit.states[i].performance;
	}

	if (pr->flags.throttling) {
		for (tx=0; tx<pr->throttling.state_count; tx++) {
			if (!pr->throttling.states[tx].valid)
				continue;
			if (pr->throttling.states[tx].performance > ACPI_PROCESSOR_MAX_THROTTLE)
				continue;
			i = pr->limit.state_count++;
			pr->limit.states[i].px = px;
			pr->limit.states[i].tx = tx;
			throttle = (base_perf * pr->throttling.states[tx].performance) / 1000;
			pr->limit.states[i].performance = base_perf - throttle;
			pr->limit.states[i].valid = 1;
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %d limit states\n", 
		pr->limit.state_count));

	if (pr->limit.state_count)
		pr->flags.limit = 1;

	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

#include <linux/compatmac.h>
#include <linux/proc_fs.h>

struct proc_dir_entry		*acpi_processor_dir = NULL;

static int
acpi_processor_read_info (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	char			*p = page;
	int			len = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_read_info");

	if (!pr || (off != 0))
		goto end;

	p += sprintf(p, "processor id:            %d\n",
		pr->id);

	p += sprintf(p, "acpi id:                 %d\n",
		pr->acpi_id);

	p += sprintf(p, "bus mastering control:   %s\n",
		pr->flags.bm_control ? "yes" : "no");

	p += sprintf(p, "power management:        %s\n",
		pr->flags.power ? "yes" : "no");

	p += sprintf(p, "throttling control:      %s\n",
		pr->flags.throttling ? "yes" : "no");

	p += sprintf(p, "performance management:  %s\n",
		pr->flags.performance ? "yes" : "no");

	p += sprintf(p, "limit interface:         %s\n",
		pr->flags.limit ? "yes" : "no");

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_processor_read_power (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	char			*p = page;
	int			len = 0;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_read_power");

	if (!pr || (off != 0))
		goto end;

	p += sprintf(p, "active state:            C%d\n",
		pr->power.state);

	p += sprintf(p, "default state:           C%d\n",
		pr->power.default_state);

	p += sprintf(p, "bus master activity:     %08x\n",
		pr->power.bm_activity);

	p += sprintf(p, "states:\n");

	for (i=1; i<ACPI_C_STATE_COUNT; i++) {

		p += sprintf(p, "   %cC%d:                  ", 
			(i == pr->power.state?'*':' '), i);

		if (!pr->power.states[i].valid) {
			p += sprintf(p, "<not supported>\n");
			continue;
		}

		if (pr->power.states[i].promotion.state)
			p += sprintf(p, "promotion[C%d] ",
				pr->power.states[i].promotion.state);
		else
			p += sprintf(p, "promotion[--] ");

		if (pr->power.states[i].demotion.state)
			p += sprintf(p, "demotion[C%d] ",
				pr->power.states[i].demotion.state);
		else
			p += sprintf(p, "demotion[--] ");

		p += sprintf(p, "latency[%03d] usage[%08d]\n",
			pr->power.states[i].latency,
			pr->power.states[i].usage);
	}

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_processor_read_performance (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	char			*p = page;
	int			len = 0;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_read_performance");

	if (!pr || (off != 0))
		goto end;

	if (!pr->flags.performance) {
		p += sprintf(p, "<not supported>\n");
		goto end;
	}

	p += sprintf(p, "state count:             %d\n",
		pr->performance.state_count);

	p += sprintf(p, "active state:            P%d\n",
		pr->performance.state);

	p += sprintf(p, "states:\n");

	for (i=0; i<pr->performance.state_count; i++)
		p += sprintf(p, "   %cP%d:                %d Mhz, %d mW %s\n",
			(i == pr->performance.state?'*':' '), i,
			pr->performance.states[i].core_frequency,
			pr->performance.states[i].power,
			(pr->performance.states[i].valid?"":"(disabled)"));

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_processor_write_performance (
        struct file		*file,
        const char		*buffer,
        unsigned long		count,
        void			*data)
{
	int			result = 0;
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	char			state_string[12] = {'\0'};

	ACPI_FUNCTION_TRACE("acpi_processor_write_performance");

	if (!pr || (count > sizeof(state_string) - 1))
		return_VALUE(-EINVAL);
	
	if (copy_from_user(state_string, buffer, count))
		return_VALUE(-EFAULT);
	
	state_string[count] = '\0';
	
	result = acpi_processor_set_throttling(pr, 
		simple_strtoul(state_string, NULL, 0));
	if (0 != result)
		return_VALUE(result);

	return_VALUE(count);
}


static int
acpi_processor_read_throttling (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	char			*p = page;
	int			len = 0;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_read_throttling");

	if (!pr || (off != 0))
		goto end;

	if (!(pr->throttling.state_count > 0)) {
		p += sprintf(p, "<not supported>\n");
		goto end;
	}

	p += sprintf(p, "state count:             %d\n",
		pr->throttling.state_count);

	p += sprintf(p, "active state:            T%d\n",
		pr->throttling.state);

	p += sprintf(p, "states:\n");

	for (i=0; i<pr->throttling.state_count; i++)
		p += sprintf(p, "   %cT%d:                  %02d%% %s\n",
			(i == pr->throttling.state?'*':' '), i,
			(pr->throttling.states[i].performance?pr->throttling.states[i].performance/10:0),
			(pr->throttling.states[i].valid?"":"(disabled)"));

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_processor_write_throttling (
        struct file		*file,
        const char		*buffer,
        unsigned long		count,
        void			*data)
{
	int			result = 0;
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	char			state_string[12] = {'\0'};

	ACPI_FUNCTION_TRACE("acpi_processor_write_throttling");

	if (!pr || (count > sizeof(state_string) - 1))
		return_VALUE(-EINVAL);
	
	if (copy_from_user(state_string, buffer, count))
		return_VALUE(-EFAULT);
	
	state_string[count] = '\0';
	
	result = acpi_processor_set_throttling(pr, 
		simple_strtoul(state_string, NULL, 0));
	if (0 != result)
		return_VALUE(result);

	return_VALUE(count);
}


static int
acpi_processor_read_limit (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	char			*p = page;
	int			len = 0;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_read_limit");

	if (!pr || (off != 0))
		goto end;

	if (!pr->flags.limit) {
		p += sprintf(p, "<not supported>\n");
		goto end;
	}

	p += sprintf(p, "state count:             %d\n",
		pr->limit.state_count);

	p += sprintf(p, "active state:            L%d\n",
		pr->limit.state);

	p += sprintf(p, "states:\n");

	for (i=0; i<pr->limit.state_count; i++)
		p += sprintf(p, "   %cL%d:                  %02d%% [P%d:T%d] %s\n",
			(i == pr->limit.state?'*':' '),
			i,
			pr->limit.states[i].performance / 10,
			pr->limit.states[i].px,
			pr->limit.states[i].tx,
			pr->limit.states[i].valid?"":"(disabled)");

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_processor_write_limit (
        struct file		*file,
        const char		*buffer,
        unsigned long		count,
        void			*data)
{
	int			result = 0;
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	char			limit_string[12] = {'\0'};
	int			limit = 0;
	int			state = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_write_limit");

	if (!pr || (count > sizeof(limit_string) - 1))
		return_VALUE(-EINVAL);
	
	if (copy_from_user(limit_string, buffer, count))
		return_VALUE(-EFAULT);
	
	limit_string[count] = '\0';

	limit = simple_strtoul(limit_string, NULL, 0);
	
	result = acpi_processor_set_limit(pr->handle, limit, &state);
	if (0 != result)
		return_VALUE(result);

	return_VALUE(count);
}


static int
acpi_processor_add_fs (
	struct acpi_device	*device)
{
	struct proc_dir_entry	*entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_add_fs");

	if (!acpi_processor_dir) {
		acpi_processor_dir = proc_mkdir(ACPI_PROCESSOR_CLASS, 
			acpi_root_dir);
		if (!acpi_processor_dir)
			return_VALUE(-ENODEV);
	}

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
			acpi_processor_dir);
		if (!acpi_device_dir(device))
			return_VALUE(-ENODEV);
	}

	/* 'info' [R] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_INFO,
		S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_INFO));
	else {
		entry->read_proc = acpi_processor_read_info;
		entry->data = acpi_driver_data(device);
	}

	/* 'power' [R] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_POWER,
		S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_POWER));
	else {
		entry->read_proc = acpi_processor_read_power;
		entry->data = acpi_driver_data(device);
	}

	/* 'performance' [R/W] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_PERFORMANCE,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_PERFORMANCE));
	else {
		entry->read_proc = acpi_processor_read_performance;
		entry->write_proc = acpi_processor_write_performance;
		entry->data = acpi_driver_data(device);
	}

	/* 'throttling' [R/W] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_THROTTLING,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_THROTTLING));
	else {
		entry->read_proc = acpi_processor_read_throttling;
		entry->write_proc = acpi_processor_write_throttling;
		entry->data = acpi_driver_data(device);
	}

	/* 'thermal_limit' [R/W] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_LIMIT,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_LIMIT));
	else {
		entry->read_proc = acpi_processor_read_limit;
		entry->write_proc = acpi_processor_write_limit;
		entry->data = acpi_driver_data(device);
	}

	return_VALUE(0);
}


static int
acpi_processor_remove_fs (
	struct acpi_device	*device)
{
	ACPI_FUNCTION_TRACE("acpi_processor_remove_fs");

	if (!acpi_processor_dir)
		return_VALUE(-ENODEV);

	if (acpi_device_dir(device))
		remove_proc_entry(acpi_device_bid(device), acpi_processor_dir);

	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static int
acpi_processor_get_info (
	struct acpi_processor	*pr)
{
	acpi_status		status = 0;
	acpi_object		object = {0};
	acpi_buffer		buffer = {sizeof(acpi_object), &object};
	static int		cpu_count = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_get_info");

	if (!pr)
		return_VALUE(-EINVAL);

#ifdef CONFIG_SMP
	if (smp_num_cpus > 1)
		acpi_processor_smp = smp_num_cpus;
#endif

	acpi_processor_get_errata(pr);

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
	pr->id = cpu_count++;
	pr->acpi_id = object.processor.proc_id;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Processor [%d:%d]\n", pr->id, 
		pr->acpi_id));

	if (!object.processor.pblk_address)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No PBLK (NULL address)\n"));
	else if (object.processor.pblk_length < 6)
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
	}

	acpi_processor_get_power_info(pr);
	acpi_processor_get_performance_info(pr);
	acpi_processor_get_throttling_info(pr);
	acpi_processor_get_limit_info(pr);

	return_VALUE(0);
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

	if (0 != acpi_bus_get_device(pr->handle, &device))
		return_VOID;

	switch (event) {
	case ACPI_PROCESSOR_NOTIFY_PERFORMANCE:
	case ACPI_PROCESSOR_NOTIFY_POWER:
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
	int			result = 0;
	acpi_status		status = AE_OK;
	struct acpi_processor	*pr = NULL;
	u32			i = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_add");

	if (!device)
		return_VALUE(-EINVAL);

	pr = kmalloc(sizeof(struct acpi_processor), GFP_KERNEL);
	if (!pr)
		return_VALUE(-ENOMEM);
	memset(pr, 0, sizeof(struct acpi_processor));

	pr->handle = device->handle;
	sprintf(acpi_device_name(device), "%s", ACPI_PROCESSOR_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_PROCESSOR_CLASS);
	acpi_driver_data(device) = pr;

	result = acpi_processor_get_info(pr);
	if (0 != result)
		goto end;

	result = acpi_processor_add_fs(device);
	if (0 != result)
		goto end;

	/*
	 * TBD: Fix notify handler installation for processors.
	 *
	status = acpi_install_notify_handler(pr->handle, ACPI_DEVICE_NOTIFY, 
		acpi_processor_notify, pr);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Error installing notify handler\n"));
		result = -ENODEV;
		goto end;
	}
	*/

	acpi_processor_list[pr->id] = pr;

	/*
	 * Set Idle Handler
	 * ----------------
	 * Install the idle handler if power management (states other than C1)
	 * is supported.  Note that the default idle handler (default_idle)
	 * will be used on platforms that only support C1.
	 */
	if ((pr->id == 0) && (pr->flags.power)) {
		pm_idle_save = pm_idle;
		pm_idle = acpi_processor_idle;
	}
	
	printk(KERN_INFO PREFIX "%s [%s] (supports",
		acpi_device_name(device), acpi_device_bid(device));
	for (i=1; i<ACPI_C_STATE_COUNT; i++)
		if (pr->power.states[i].valid)
			printk(" C%d", i);
	if (pr->flags.performance)
		printk(", %d performance states", pr->performance.state_count);
	if (pr->flags.throttling)
		printk(", %d throttling states", pr->throttling.state_count);
	if (pr->errata.piix4.bmisx)
		printk(", PIIX4 errata");
	printk(")\n");

end:
	if (0 != result) {
		acpi_processor_remove_fs(device);
		kfree(pr);
	}

	return_VALUE(result);
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

	/*
	status = acpi_remove_notify_handler(pr->handle, ACPI_DEVICE_NOTIFY, 
		acpi_processor_notify);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Error removing notify handler\n"));
		return_VALUE(-ENODEV);
	}
	*/

	/* Unregister the idle handler when processor #0 is removed. */
	if (pr->id == 0)
		pm_idle = pm_idle_save;

	acpi_processor_remove_fs(device);

	acpi_processor_list[pr->id] = NULL;

	kfree(pr);

	return_VALUE(0);
}


static int __init
acpi_processor_init (void)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_init");

	memset(&acpi_processor_list, 0, sizeof(acpi_processor_list));

	result = acpi_bus_register_driver(&acpi_processor_driver);
	if (0 > result)
		return_VALUE(-ENODEV);

	return_VALUE(0);
}


static void __exit
acpi_processor_exit (void)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_exit");

	result = acpi_bus_unregister_driver(&acpi_processor_driver);
	if (0 == result)
		remove_proc_entry(ACPI_PROCESSOR_CLASS, acpi_root_dir);

	return_VOID;
}


module_init(acpi_processor_init);
module_exit(acpi_processor_exit);
