/*
 *  cpu.c - Processor handling
 *
 *  Copyright (C) 2000 Andrew Henroid
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("cpu")

unsigned long acpi_c2_exit_latency = ACPI_INFINITE;
unsigned long acpi_c3_exit_latency = ACPI_INFINITE;
unsigned long acpi_c2_enter_latency = ACPI_INFINITE;
unsigned long acpi_c3_enter_latency = ACPI_INFINITE;

static unsigned long acpi_pblk = ACPI_INVALID;
static int acpi_c2_tested = 0;
static int acpi_c3_tested = 0;
static int acpi_max_c_state = 1;
static int acpi_pm_tmr_len;

#define MAX_C2_LATENCY		100
#define MAX_C3_LATENCY		1000

/*
 * Clear busmaster activity flag
 */
static inline void
acpi_clear_bm_activity(void)
{
	acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_LOCK, BM_STS, 0);
}

/*
 * Returns 1 if there has been busmaster activity
 */
static inline int
acpi_bm_activity(void)
{
	return acpi_hw_register_bit_access(ACPI_READ, ACPI_MTX_LOCK, BM_STS);
}

/*
 * Set system to sleep through busmaster requests
 */
static void
acpi_sleep_on_busmaster(void)
{
	acpi_hw_register_bit_access (ACPI_WRITE, ACPI_MTX_LOCK, BM_RLD, 1);
}

/*
 * Set system to wake on busmaster requests
 */
static void
acpi_wake_on_busmaster(void)
{
	acpi_hw_register_bit_access (ACPI_WRITE, ACPI_MTX_LOCK, BM_RLD, 0);
}

u32
acpi_read_pm_timer(void)
{
	return acpi_hw_register_read(ACPI_MTX_LOCK, PM_TIMER);
}

/*
 * Do a compare, accounting for 24/32bit rollover
 */
static u32
acpi_compare_pm_timers(u32 first, u32 second)
{
	if (first < second) {
		return (second - first);
	} else {
		if (acpi_pm_tmr_len == 24)
			return (second + (0xFFFFFF - first));
		else
			return (second + (0xFFFFFFFF - first));
	}
}

/*
 * Idle loop (uniprocessor only)
 */
static void
acpi_idle(void)
{
	static int sleep_level = 1;
	FADT_DESCRIPTOR *fadt = &acpi_fadt;

	if (!fadt
	    || (STRNCMP(fadt->header.signature, ACPI_FADT_SIGNATURE, ACPI_SIG_LEN) != 0)
	    || !fadt->Xpm_tmr_blk.address
	    || !acpi_pblk)
		goto not_initialized;

	/*
	 * start from the previous sleep level..
	 */
	if (sleep_level == 1
	    || acpi_max_c_state < 2)
		goto sleep1;

	if (sleep_level == 2
	    || acpi_max_c_state < 3)
		goto sleep2;

      sleep3:
	sleep_level = 3;
	if (!acpi_c3_tested) {
		DEBUG_PRINT(ACPI_INFO, ("C3 works\n"));
		acpi_c3_tested = 1;
	}
	acpi_wake_on_busmaster();
	if (fadt->Xpm2_cnt_blk.address)
		goto sleep3_with_arbiter;

	for (;;) {
		unsigned long time;
		unsigned long diff;
		
		__cli();
		if (current->need_resched)
			goto out;
		if (acpi_bm_activity())
			goto sleep2;

		time = acpi_read_pm_timer();
		inb(acpi_pblk + ACPI_P_LVL3);
		/* Dummy read, force synchronization with the PMU */
		acpi_read_pm_timer();
		diff = acpi_compare_pm_timers(time, acpi_read_pm_timer());

		__sti();
		if (diff < acpi_c3_exit_latency)
			goto sleep2;
	}

      sleep3_with_arbiter:
	for (;;) {
		unsigned long time;
		unsigned long diff;

		__cli();
		if (current->need_resched)
			goto out;
		if (acpi_bm_activity())
			goto sleep2;

		time = acpi_read_pm_timer();
		
		/* Disable arbiter, park on CPU */
		acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_LOCK, ARB_DIS, 1);
		inb(acpi_pblk + ACPI_P_LVL3);
		/* Dummy read, force synchronization with the PMU */
		acpi_read_pm_timer();
		diff = acpi_compare_pm_timers(time, acpi_read_pm_timer());
		/* Enable arbiter again.. */
		acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_LOCK, ARB_DIS, 0);

		__sti();
		if (diff < acpi_c3_exit_latency)
			goto sleep2;
	}

      sleep2:
	sleep_level = 2;
	if (!acpi_c2_tested) {
		DEBUG_PRINT(ACPI_INFO, ("C2 works\n"));
		acpi_c2_tested = 1;
	}
	acpi_wake_on_busmaster();	/* Required to track BM activity.. */
	for (;;) {
		unsigned long time;
		unsigned long diff;

		__cli();
		if (current->need_resched)
			goto out;

		time = acpi_read_pm_timer();
		inb(acpi_pblk + ACPI_P_LVL2);
		/* Dummy read, force synchronization with the PMU */
		acpi_read_pm_timer();
		diff = acpi_compare_pm_timers(time, acpi_read_pm_timer());

		__sti();
		if (diff < acpi_c2_exit_latency)
			goto sleep1;
		if (acpi_bm_activity()) {
			acpi_clear_bm_activity();
			continue;
		}
		if (diff > acpi_c3_enter_latency
		    && acpi_max_c_state >= 3)
			goto sleep3;
	}

      sleep1:
	sleep_level = 1;
	acpi_sleep_on_busmaster();
	for (;;) {
		unsigned long time;
		unsigned long diff;

		__cli();
		if (current->need_resched)
			goto out;
		time = acpi_read_pm_timer();
		safe_halt();
		diff = acpi_compare_pm_timers(time, acpi_read_pm_timer());
		if (diff > acpi_c2_enter_latency
		    && acpi_max_c_state >= 2)
			goto sleep2;
	}

      not_initialized:
	for (;;) {
		__cli();
		if (current->need_resched)
			goto out;
		safe_halt();
	}

      out:
	__sti();
}

/*
 * Get processor information
 */
static ACPI_STATUS
acpi_found_cpu(ACPI_HANDLE handle, u32 level, void *ctx, void **value)
{
	ACPI_OBJECT obj;
	ACPI_BUFFER buf;

	buf.length = sizeof(obj);
	buf.pointer = &obj;
	if (!ACPI_SUCCESS(acpi_evaluate_object(handle, NULL, NULL, &buf)))
		return AE_OK;

	DEBUG_PRINT(ACPI_INFO, ("PBLK %d @ 0x%04x:%d\n",
			obj.processor.proc_id,
			obj.processor.pblk_address,
			obj.processor.pblk_length));

	if (acpi_pblk != ACPI_INVALID
	    || !obj.processor.pblk_address
	    || obj.processor.pblk_length != 6)
		return AE_OK;

	acpi_pblk = obj.processor.pblk_address;

	if (acpi_fadt.plvl2_lat
	    && acpi_fadt.plvl2_lat <= MAX_C2_LATENCY) {
		acpi_c2_exit_latency
			= ACPI_MICROSEC_TO_TMR_TICKS(acpi_fadt.plvl2_lat);
		acpi_c2_enter_latency
			= ACPI_MICROSEC_TO_TMR_TICKS(ACPI_TMR_HZ / 1000);
		acpi_max_c_state = 2;

		printk(KERN_INFO "ACPI: System firmware supports: C2");
	
		if (acpi_fadt.plvl3_lat
		    && acpi_fadt.plvl3_lat <= MAX_C3_LATENCY) {
			acpi_c3_exit_latency
				= ACPI_MICROSEC_TO_TMR_TICKS(acpi_fadt.plvl3_lat);
			acpi_c3_enter_latency
				= ACPI_MICROSEC_TO_TMR_TICKS(acpi_fadt.plvl3_lat * 5);
			acpi_max_c_state = 3;

			printk(" C3");
		}

		printk("\n");
	}

	return AE_OK;
}

static int
acpi_pm_timer_init(void)
{
	FADT_DESCRIPTOR *fadt = &acpi_fadt;

	if (fadt->tmr_val_ext) {
		acpi_pm_tmr_len = 32;
	} else {
		acpi_pm_tmr_len = 24;
	}

	DEBUG_PRINT(ACPI_INFO, ("PM Timer width: %d bits\n", acpi_pm_tmr_len));

	return AE_OK;
}

int
acpi_cpu_init(void)
{
	acpi_walk_namespace(ACPI_TYPE_PROCESSOR,
			    ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    acpi_found_cpu,
			    NULL,
			    NULL);

	acpi_pm_timer_init();


#ifdef CONFIG_SMP
	if (smp_num_cpus == 1)
		pm_idle = acpi_idle;
#else
	pm_idle = acpi_idle;
#endif

	return 0;
}
