/*
 * processor_idle - idle state submodule to the ACPI processor driver
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/moduleparam.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/processor.h>

#define ACPI_PROCESSOR_COMPONENT        0x01000000
#define ACPI_PROCESSOR_CLASS            "processor"
#define ACPI_PROCESSOR_DRIVER_NAME      "ACPI Processor Driver"
#define _COMPONENT              ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME                ("acpi_processor")

#define US_TO_PM_TIMER_TICKS(t)		((t * (PM_TIMER_FREQUENCY/1000)) / 1000)
#define C2_OVERHEAD			4	/* 1us (3.579 ticks per us) */
#define C3_OVERHEAD			4	/* 1us (3.579 ticks per us) */

module_param_named(max_cstate, max_cstate, uint, 0);

/* --------------------------------------------------------------------------
                                Power Management
   -------------------------------------------------------------------------- */

/*
 * IBM ThinkPad R40e crashes mysteriously when going into C2 or C3.
 * For now disable this. Probably a bug somewhere else.
 *
 * To skip this limit, boot/load with a large max_cstate limit.
 */
int no_c2c3(struct dmi_system_id *id)
{
	if (max_cstate > ACPI_C_STATES_MAX)
		return 0;

	printk(KERN_NOTICE PREFIX "%s detected - C2,C3 disabled."
		" Override with \"processor.max_cstate=9\"\n", id->ident);

	max_cstate = 1;

	return 0;
}




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
	struct acpi_processor_cx *old, *new;

	if (!pr)
		return;

	old = &pr->power.states[pr->power.state];
	new = &pr->power.states[state];

 	old->promotion.count = 0;
 	new->demotion.count = 0;

	/* Cleanup from old state. */
	switch (old->type) {
	case ACPI_STATE_C3:
		/* Disable bus master reload */
		if (new->type != ACPI_STATE_C3)
			acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD, 0, ACPI_MTX_DO_NOT_LOCK);
		break;
	}

	/* Prepare to use new state. */
	switch (new->type) {
	case ACPI_STATE_C3:
		/* Enable bus master reload */
		if (old->type != ACPI_STATE_C3)
			acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD, 1, ACPI_MTX_DO_NOT_LOCK);
		break;
	}

	pr->power.state = state;

	return;
}


void acpi_processor_idle (void)
{
	struct acpi_processor	*pr = NULL;
	struct acpi_processor_cx *cx = NULL;
	int			next_state = 0;
	int			sleep_ticks = 0;
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
	switch (cx->type) {

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
		inb(cx->address);
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
		inb(cx->address);
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


int acpi_processor_get_power_info (
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
	pr->power.states[ACPI_STATE_C1].type = ACPI_STATE_C1;

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
			pr->power.states[ACPI_STATE_C2].type = ACPI_STATE_C2;
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
			pr->power.states[ACPI_STATE_C3].type = ACPI_STATE_C3;
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


/* proc interface */

static int acpi_processor_power_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor	*pr = (struct acpi_processor *)seq->private;
	unsigned int		i;

	ACPI_FUNCTION_TRACE("acpi_processor_power_seq_show");

	if (!pr)
		goto end;

	seq_printf(seq, "active state:            %d\n"
			"default state:           %d\n"
			"max_cstate:              %d\n"
			"bus master activity:     %08x\n",
			pr->power.state,
			pr->power.default_state,
			max_cstate,
			pr->power.bm_activity);

	seq_puts(seq, "states:\n");

	for (i = 1; i < ACPI_C_STATE_COUNT; i++) {
		seq_printf(seq, "   %c%d:                  ",
			(i == pr->power.state?'*':' '), i);

		if (!pr->power.states[i].valid) {
			seq_puts(seq, "<not supported>\n");
			continue;
		}

		switch (pr->power.states[i].type) {
		case ACPI_STATE_C1:
			seq_printf(seq, "type[C1] ");
			break;
		case ACPI_STATE_C2:
			seq_printf(seq, "type[C2] ");
			break;
		case ACPI_STATE_C3:
			seq_printf(seq, "type[C3] ");
			break;
		default:
			seq_printf(seq, "type[--] ");
			break;
		}

		if (pr->power.states[i].promotion.state)
			seq_printf(seq, "promotion[%d] ",
				pr->power.states[i].promotion.state);
		else
			seq_puts(seq, "promotion[-] ");

		if (pr->power.states[i].demotion.state)
			seq_printf(seq, "demotion[%d] ",
				pr->power.states[i].demotion.state);
		else
			seq_puts(seq, "demotion[-] ");

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

struct file_operations acpi_processor_power_fops = {
	.open 		= acpi_processor_power_open_fs,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
