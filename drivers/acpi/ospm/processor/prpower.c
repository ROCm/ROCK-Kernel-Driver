/*****************************************************************************
 *
 * Module Name: prpower.c
 *   $Revision: 32 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


/* TBD: Linux specific */
#include <linux/sched.h>
#include <linux/pm.h>
#include <asm/io.h>

#include <acpi.h>
#include <bm.h>
#include "pr.h"

#define _COMPONENT		ACPI_PROCESSOR
	MODULE_NAME		("prpower")


/****************************************************************************
 *                                  Globals
 ****************************************************************************/

extern FADT_DESCRIPTOR		acpi_fadt;
static u32			last_idle_jiffies = 0;
static PR_CONTEXT		*processor_list[NR_CPUS];
static void			(*pr_pm_idle_save)(void) = NULL;
static u8			bm_control = 0;


/* Used for PIIX4 errata handling. */
unsigned short			acpi_piix4_bmisx = 0;


/****************************************************************************
 *                             External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    pr_power_activate_state
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
pr_power_activate_state (
	PR_CONTEXT		*processor,
	u32			next_state)
{

	PROC_NAME("pr_power_activate_state");

	if (!processor) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid (NULL) context.\n"));
		return;
	}

	processor->power.state[processor->power.active_state].promotion.count = 0;
	processor->power.state[processor->power.active_state].demotion.count = 0;

	/*
	 * Cleanup from old state.
	 */
	switch (processor->power.active_state) {

	case PR_C3:
		/* Disable bus master reload */
		acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_DO_NOT_LOCK,
			BM_RLD, 0);
		break;
	}

	/*
	 * Prepare to use new state.
	 */
	switch (next_state) {

	case PR_C3:
		/* Enable bus master reload */
		acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_DO_NOT_LOCK,
			BM_RLD, 1);
		break;
	}

	processor->power.active_state = next_state;
	
	return;
}


/****************************************************************************
 *
 * FUNCTION:    pr_power_idle
 *
 * PARAMETERS:  <none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
pr_power_idle (void)
{
	PR_CX			*c_state = NULL;
	u32			next_state = 0;
	u32			start_ticks, end_ticks, time_elapsed;
	PR_CONTEXT		*processor = NULL;

	PROC_NAME("pr_power_idle");

	processor = processor_list[smp_processor_id()];

	if (!processor || processor->power.active_state == PR_C0) {
		return;
	}

	next_state = processor->power.active_state;

	/*
	 * Check OS Idleness:
	 * ------------------
	 * If the OS has been busy (hasn't called the idle handler in a while)
	 * then automatically demote to the default power state (e.g. C1).
	 *
	 * TBD: Optimize by having scheduler determine business instead
	 *      of having us try to calculate it.
	 */
	if (processor->power.active_state != processor->power.default_state) {
		if ((jiffies - last_idle_jiffies) >= processor->power.busy_metric) {
			next_state = processor->power.default_state;
			if (next_state != processor->power.active_state) {
				pr_power_activate_state(processor, next_state);
			}
		}
	}

	disable();

	/*
	 * Log BM Activity:
	 * ----------------
	 * Read BM_STS and record its value for later use by C3 policy.
	 * (Note that we save the BM_STS values for the last 32 cycles).
	 */
	if (bm_control) {
		processor->power.bm_activity <<= 1;
		if (acpi_hw_register_bit_access(ACPI_READ, ACPI_MTX_DO_NOT_LOCK, BM_STS)) {
			processor->power.bm_activity |= 1;
			acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_DO_NOT_LOCK,
				BM_STS, 1);
		}
		else if (acpi_piix4_bmisx) {
			/*
			 * PIIX4 Errata:
			 * -------------
			 * This code is a workaround for errata #18 "C3 Power State/
			 * BMIDE and Type-F DMA Livelock" from the July '01 PIIX4
			 * specification update.  Note that BM_STS doesn't always
			 * reflect the true state of bus mastering activity; forcing
			 * us to manually check the BMIDEA bit of each IDE channel.
			 */
			if ((inb_p(acpi_piix4_bmisx + 0x02) & 0x01) ||
				(inb_p(acpi_piix4_bmisx + 0x0A) & 0x01))
				processor->power.bm_activity |= 1;
		}
	}

	c_state = &(processor->power.state[processor->power.active_state]);

	c_state->utilization++;

	/*
	 * Sleep:
	 * ------
	 * Invoke the current Cx state to put the processor to sleep.
	 */
	switch (processor->power.active_state) {

	case PR_C1:
		/* Invoke C1 */
		enable(); halt();
		/*
	 * TBD: Can't get time duration while in C1, as resumes
		 *      go to an ISR rather than here.
		 */
		time_elapsed = 0xFFFFFFFF;
		break;

	case PR_C2:
		/* See how long we're asleep for */
		acpi_get_timer(&start_ticks);
		/* Invoke C2 */
		acpi_os_read_port(processor->power.p_lvl2, NULL, 8);
		/* Dummy op - must do something useless after P_LVL2 read */
		acpi_hw_register_bit_access(ACPI_READ, ACPI_MTX_DO_NOT_LOCK, BM_STS);
		/* Compute time elapsed */
		acpi_get_timer(&end_ticks);
		/* Re-enable interrupts */
		enable();
		acpi_get_timer_duration(start_ticks, end_ticks, &time_elapsed);
		break;

	case PR_C3:
		/* Disable bus master arbitration */
		acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_DO_NOT_LOCK, ARB_DIS, 1);
		/* See how long we're asleep for */
		acpi_get_timer(&start_ticks);
		/* Invoke C3 */
		acpi_os_read_port(processor->power.p_lvl3, NULL, 8);
		/* Dummy op - must do something useless after P_LVL3 read */
		acpi_hw_register_bit_access(ACPI_READ, ACPI_MTX_DO_NOT_LOCK, BM_STS);
		/* Compute time elapsed */
		acpi_get_timer(&end_ticks);
		/* Enable bus master arbitration */
		acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_DO_NOT_LOCK,
			ARB_DIS, 0);
		/* Re-enable interrupts */
		enable();
		acpi_get_timer_duration(start_ticks, end_ticks, &time_elapsed);
		break;

	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Attempt to use unsupported power state C%d.\n", processor->power.active_state));
		enable();
		break;
	}

	/*
	 * Promotion?
	 * ----------
	 * Track the number of successful sleeps (time asleep is greater
	 * than time_threshold) and promote when count_threshold is
	 * reached.
	 */
	if ((c_state->promotion.target_state) && 	
		(time_elapsed >= c_state->promotion.time_threshold)) {

		c_state->promotion.count++;
 		c_state->demotion.count = 0;

		if (c_state->promotion.count >= c_state->promotion.count_threshold) {
			/*
			 * Bus Mastering Activity, if active and used
			 * by this state's promotion policy, prevents
			 * promotions from occuring.
			 */
			if (!bm_control || !(processor->power.bm_activity & c_state->promotion.bm_threshold))
				next_state = c_state->promotion.target_state;
		}
	}

	/*
	 * Demotion?
	 * ---------
	 * Track the number of shorts (time asleep is less than
	 * time_threshold) and demote when count_threshold is reached.
	 */
	if (c_state->demotion.target_state) {
			
		if (time_elapsed < c_state->demotion.time_threshold) {

			c_state->demotion.count++;
			c_state->promotion.count = 0;

			if (c_state->demotion.count >=
				c_state->demotion.count_threshold) {
				next_state = c_state->demotion.target_state;
			}
		}

		/*
		 * Bus Mastering Activity, if active and used by this
		 * state's promotion policy, causes an immediate demotion
		 * to occur.
		 */
		if (bm_control && (processor->power.bm_activity & c_state->demotion.bm_threshold))
			next_state = c_state->demotion.target_state;
	}

	/*
	 * New Cx State?
	 * -------------
	 * If we're going to start using a new Cx state we must clean up
	 * from the previous and prepare to use the new.
	 */
	if (next_state != processor->power.active_state) {
		pr_power_activate_state(processor, next_state);
		processor->power.active_state = processor->power.active_state;
	}

	/*
	 * Track OS Idleness:
	 * ------------------
	 * Record a jiffies timestamp to compute time elapsed between calls
	 * to the idle handler.
	 */
	last_idle_jiffies = jiffies;

	return;
}


/*****************************************************************************
 *
 * FUNCTION:    pr_power_set_default_policy
 *
 * PARAMETERS:
 *
 * RETURN:	
 *
 * DESCRIPTION: Sets the default Cx state policy (OS idle handler).  Our
 *              scheme is to promote quickly to C2 but more conservatively
 *              to C3.  We're favoring C2 for its characteristics of low
 *              latency (quick response), good power savings, and ability
 *              to allow bus mastering activity.
 *
 *              Note that Cx state policy is completely customizable, with
 *              the goal of having heuristics to alter policy dynamically.
 *
 ****************************************************************************/

acpi_status
pr_power_set_default_policy (
	PR_CONTEXT                 *processor)
{
	FUNCTION_TRACE("pr_power_set_default_policy");

	if (!processor) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Busy Metric:
	 * ------------
	 * Used to determine when the OS has been busy and thus when
	 * policy should return to using the default Cx state (e.g. C1).
	 * On Linux we use the number of jiffies (scheduler quantums)
	 * that transpire between calls to the idle handler.
	 *
	 * TBD: Linux-specific.
	 */
	processor->power.busy_metric = 2;

	/*
	 * C1:
	 * ---
	 * C1 serves as our default state.  It must be valid.
	 */
	if (processor->power.state[PR_C1].is_valid) {
		processor->power.active_state =
			processor->power.default_state = PR_C1;
	}
	else {
		processor->power.active_state =
			processor->power.default_state = PR_C0;
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * C2:
	 * ---
	 * Set default C1 promotion and C2 demotion policies.
	 */
	if (processor->power.state[PR_C2].is_valid) {
		/*
		 * Promote from C1 to C2 anytime we're asleep in C1 for
		 * longer than two times the C2 latency (to amortize cost
		 * of transition).  Demote from C2 to C1 anytime we're
		 * asleep in C2 for less than this time.
		 */
		processor->power.state[PR_C1].promotion.count_threshold = 10;
		processor->power.state[PR_C1].promotion.time_threshold =
			2 * processor->power.state[PR_C2].latency;
		processor->power.state[PR_C1].promotion.target_state = PR_C2;

		processor->power.state[PR_C2].demotion.count_threshold = 1;
		processor->power.state[PR_C2].demotion.time_threshold =
			2 * processor->power.state[PR_C2].latency;
		processor->power.state[PR_C2].demotion.target_state = PR_C1;
	}

	/*
	 * C3:
	 * ---
	 * Set default C2 promotion and C3 demotion policies.
	 */
	if ((processor->power.state[PR_C2].is_valid) &&
		(processor->power.state[PR_C3].is_valid)) {
		/*
		 * Promote from C2 to C3 after 4 cycles of no bus
		 * mastering activity (while maintaining sleep time
		 * criteria).  Demote immediately on a short or
		 * whenever bus mastering activity occurs.
		 */
		processor->power.state[PR_C2].promotion.count_threshold = 1;
		processor->power.state[PR_C2].promotion.time_threshold =
			2 * processor->power.state[PR_C3].latency;
		processor->power.state[PR_C2].promotion.bm_threshold =
			0x0000000F;
		processor->power.state[PR_C2].promotion.target_state =
			PR_C3;

		processor->power.state[PR_C3].demotion.count_threshold = 1;
		processor->power.state[PR_C3].demotion.time_threshold =
			2 * processor->power.state[PR_C3].latency;
		processor->power.state[PR_C3].demotion.bm_threshold =
			0x0000000F;
		processor->power.state[PR_C3].demotion.target_state =
			PR_C2;
	}

	return_ACPI_STATUS(AE_OK);
}

/*****************************************************************************
 *
 * FUNCTION:    pr_power_add_device
 *
 * PARAMETERS:  <none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

/*
 * TBD: 1. PROC_C1 support.
 *      2. Symmetric Cx state support (different Cx states supported
 *         by different CPUs results in lowest common denominator).
 */

acpi_status
pr_power_add_device (
	PR_CONTEXT                 *processor)
{
	FUNCTION_TRACE("pr_power_add_device");

	if (!processor) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * State Count:
	 * ------------
	 * Fixed at four (C0-C3).  We use is_valid to determine whether or
	 * not a state actually gets used.
	 */
	processor->power.state_count = PR_MAX_POWER_STATES;

	/*
	 * C0:
	 * ---
	 * C0 exists only as filler in our array. (Let's assume its valid!)
	 */
	processor->power.state[PR_C0].is_valid = TRUE;

	/*
	 * C1:
	 * ---
	 * ACPI states that C1 must be supported by all processors
	 * with a latency so small that it can be ignored.
	 *
	 * TBD: What about PROC_C1 support?
	 */
	processor->power.state[PR_C1].is_valid = TRUE;

	/*
	 * C2:
	 * ---
	 * We're only supporting C2 on UP systems with latencies <= 100us.
	 *
	 * TBD: Support for C2 on MP (P_LVL2_UP) -- I'm taking the
	 *      conservative approach for now.
	 */
	processor->power.state[PR_C2].latency = acpi_fadt.plvl2_lat;

#ifdef CONFIG_SMP
	if (smp_num_cpus == 1) {
#endif /*CONFIG_SMP*/
		if (acpi_fadt.plvl2_lat <= PR_MAX_C2_LATENCY) {
			processor->power.state[PR_C2].is_valid = TRUE;
			processor->power.p_lvl2 = processor->pblk.address + 4;
		}
#ifdef CONFIG_SMP
	}
#endif /*CONFIG_SMP*/


	/*
	 * C3:
	 * ---
	 * We're only supporting C3 on UP systems with latencies <= 1000us,
	 * and that include the ability to disable bus mastering while in
	 * C3 (ARB_DIS) but allows bus mastering requests to wake the system
	 * from C3 (BM_RLD).  Note his method of maintaining cache coherency
	 * (disabling of bus mastering) cannot be used on SMP systems, and
	 * flushing caches (e.g. WBINVD) is simply too costly at this time.
	 *
	 * TBD: Support for C3 on MP -- I'm taking the conservative
	 *      approach for now.
	 */
	processor->power.state[PR_C3].latency = acpi_fadt.plvl3_lat;

#ifdef CONFIG_SMP
	if (smp_num_cpus == 1) {
#endif /*CONFIG_SMP*/
		if ((acpi_fadt.plvl3_lat <= PR_MAX_C3_LATENCY) && bm_control) {
			processor->power.state[PR_C3].is_valid = TRUE;
			processor->power.p_lvl3 = processor->pblk.address + 5;
		}
#ifdef CONFIG_SMP
	}
#endif /*CONFIG_SMP*/

	/*
	 * Set Default Policy:
	 * -------------------
	 * Now that we know which state are supported, set the default
	 * policy.  Note that this policy can be changed dynamically
	 * (e.g. encourage deeper sleeps to conserve battery life when
	 * not on AC).
	 */
	pr_power_set_default_policy(processor);

	/*
	 * Save Processor Context:
	 * -----------------------
	 * TBD: Enhance Linux idle handler to take processor context
	 *      parameter.
	 */
	processor_list[processor->uid] = processor;

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    pr_power_remove_device
 *
 * PARAMETERS:
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_power_remove_device (
	PR_CONTEXT              *processor)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("pr_power_remove_device");

	if (!processor) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	MEMSET(&(processor->power), 0, sizeof(PR_POWER));

	processor_list[processor->uid] = NULL;

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    pr_power_initialize
 *
 * PARAMETERS:  <none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_power_initialize (void)
{
	u32			i = 0;

	FUNCTION_TRACE("pr_power_initialize");

	/* TBD: Linux-specific. */
	for (i=0; i<NR_CPUS; i++) {
		processor_list[i] = NULL;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Max CPUs[%d], this CPU[%d].\n", NR_CPUS, smp_processor_id()));

	/* Only use C3 if we can control bus mastering. */
	if (acpi_fadt.V1_pm2_cnt_blk && acpi_fadt.pm2_cnt_len)
		bm_control = 1;

	/*
	 * Install idle handler.
	 *
	 * TBD: Linux-specific (need OSL function).
	 */
	pr_pm_idle_save = pm_idle;
	pm_idle = pr_power_idle;

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    pr_power_terminate
 *
 * PARAMETERS:  <none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_power_terminate (void)
{
	FUNCTION_TRACE("pr_power_terminate");

	/*
	 * Remove idle handler.
	 *
	 * TBD: Linux-specific (need OSL function).
	 */
	pm_idle = pr_pm_idle_save;

	return_ACPI_STATUS(AE_OK);
}
