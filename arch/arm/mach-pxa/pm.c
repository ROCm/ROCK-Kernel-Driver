/*
 * PXA250/210 Power Management Routines
 *
 * Original code for the SA11x0:
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *
 * Modified for the PXA250 by Nicolas Pitre:
 * Copyright (c) 2002 Monta Vista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/sysctl.h>
#include <linux/errno.h>

#include <asm/hardware.h>
#include <asm/memory.h>
#include <asm/system.h>
#include <asm/leds.h>


/*
 * Debug macros
 */
#undef DEBUG

extern void pxa_cpu_suspend(void);
extern void pxa_cpu_resume(void);

#define SAVE(x)		sleep_save[SLEEP_SAVE_##x] = x
#define RESTORE(x)	x = sleep_save[SLEEP_SAVE_##x]

/*
 * List of global PXA peripheral registers to preserve.
 * More ones like CP and general purpose register values are preserved
 * with the stack pointer in sleep.S.
 */
enum {	SLEEP_SAVE_START = 0,

	SLEEP_SAVE_OSCR, SLEEP_SAVE_OIER,
	SLEEP_SAVE_OSMR0, SLEEP_SAVE_OSMR1, SLEEP_SAVE_OSMR2, SLEEP_SAVE_OSMR3,

	SLEEP_SAVE_GPDR0, SLEEP_SAVE_GPDR1, SLEEP_SAVE_GPDR2,
	SLEEP_SAVE_GRER0, SLEEP_SAVE_GRER1, SLEEP_SAVE_GRER2,
	SLEEP_SAVE_GFER0, SLEEP_SAVE_GFER1, SLEEP_SAVE_GFER2,
	SLEEP_SAVE_GAFR0_L, SLEEP_SAVE_GAFR1_L, SLEEP_SAVE_GAFR2_L,
	SLEEP_SAVE_GAFR0_U, SLEEP_SAVE_GAFR1_U, SLEEP_SAVE_GAFR2_U,

	SLEEP_SAVE_FFIER, SLEEP_SAVE_FFLCR, SLEEP_SAVE_FFMCR,
	SLEEP_SAVE_FFSPR, SLEEP_SAVE_FFISR,
	SLEEP_SAVE_FFDLL, SLEEP_SAVE_FFDLH,

	SLEEP_SAVE_ICMR,
	SLEEP_SAVE_CKEN,

	SLEEP_SAVE_CKSUM,

	SLEEP_SAVE_SIZE
};


int pm_do_suspend(void)
{
	unsigned long sleep_save[SLEEP_SAVE_SIZE];
	unsigned long checksum = 0;
	int i;

	cli();
	clf();

	leds_event(led_stop);

	/* preserve current time */
	RCNR = xtime.tv_sec;

	/*
	 * Temporary solution.  This won't be necessary once
	 * we move pxa support into the serial/* driver
	 * Save the FF UART
	 */
	SAVE(FFIER);
	SAVE(FFLCR);
	SAVE(FFMCR);
	SAVE(FFSPR);
	SAVE(FFISR);
	FFLCR |= 0x80;
	SAVE(FFDLL);
	SAVE(FFDLH);
	FFLCR &= 0xef;

	/* save vital registers */
	SAVE(OSCR);
	SAVE(OSMR0);
	SAVE(OSMR1);
	SAVE(OSMR2);
	SAVE(OSMR3);
	SAVE(OIER);

	SAVE(GPDR0); SAVE(GPDR1); SAVE(GPDR2);
	SAVE(GRER0); SAVE(GRER1); SAVE(GRER2);
	SAVE(GFER0); SAVE(GFER1); SAVE(GFER2);
	SAVE(GAFR0_L); SAVE(GAFR0_U);
	SAVE(GAFR1_L); SAVE(GAFR1_U);
	SAVE(GAFR2_L); SAVE(GAFR2_U);

	SAVE(ICMR);
	ICMR = 0;

	SAVE(CKEN);
	CKEN = 0;

	/* Note: wake up source are set up in each machine specific files */

	/* clear GPIO transition detect  bits */
	GEDR0 = GEDR0; GEDR1 = GEDR1; GEDR2 = GEDR2;

	/* Clear sleep reset status */
	RCSR = RCSR_SMR;

	/* set resume return address */
	PSPR = virt_to_phys(pxa_cpu_resume);

	/* before sleeping, calculate and save a checksum */
	for (i = 0; i < SLEEP_SAVE_SIZE - 1; i++)
		checksum += sleep_save[i];
	sleep_save[SLEEP_SAVE_CKSUM] = checksum;

	/* *** go zzz *** */
	pxa_cpu_suspend();

	/* after sleeping, validate the checksum */
	checksum = 0;
	for (i = 0; i < SLEEP_SAVE_SIZE - 1; i++)
		checksum += sleep_save[i];

	/* if invalid, display message and wait for a hardware reset */
	if (checksum != sleep_save[SLEEP_SAVE_CKSUM]) {
#ifdef CONFIG_ARCH_LUBBOCK
		LUB_HEXLED = 0xbadbadc5;
#endif
		while (1);
	}

	/* ensure not to come back here if it wasn't intended */
	PSPR = 0;

	/* restore registers */
	RESTORE(GPDR0); RESTORE(GPDR1); RESTORE(GPDR2);
	RESTORE(GRER0); RESTORE(GRER1); RESTORE(GRER2);
	RESTORE(GFER0); RESTORE(GFER1); RESTORE(GFER2);
	RESTORE(GAFR0_L); RESTORE(GAFR0_U);
	RESTORE(GAFR1_L); RESTORE(GAFR1_U);
	RESTORE(GAFR2_L); RESTORE(GAFR2_U);

	PSSR = PSSR_PH;

	RESTORE(OSMR0);
	RESTORE(OSMR1);
	RESTORE(OSMR2);
	RESTORE(OSMR3);
	RESTORE(OSCR);
	RESTORE(OIER);

	RESTORE(CKEN);

	ICLR = 0;
	ICCR = 1;
	RESTORE(ICMR);

	/*
	 * Temporary solution.  This won't be necessary once
	 * we move pxa support into the serial/* driver.
	 * Restore the FF UART.
	 */
	RESTORE(FFMCR);
	RESTORE(FFSPR);
	RESTORE(FFLCR);
	FFLCR |= 0x80;
	RESTORE(FFDLH);
	RESTORE(FFDLL);
	RESTORE(FFLCR);
	RESTORE(FFISR);
	FFFCR = 0x07;
	RESTORE(FFIER);

	/* restore current time */
	xtime.tv_sec = RCNR;

#ifdef DEBUG
	printk(KERN_DEBUG "*** made it back from resume\n");
#endif

	leds_event(led_start);

	sti();

	return 0;
}

unsigned long sleep_phys_sp(void *sp)
{
	return virt_to_phys(sp);
}

#ifdef CONFIG_SYSCTL
/*
 * ARGH!  ACPI people defined CTL_ACPI in linux/acpi.h rather than
 * linux/sysctl.h.
 *
 * This means our interface here won't survive long - it needs a new
 * interface.  Quick hack to get this working - use sysctl id 9999.
 */
#warning ACPI broke the kernel, this interface needs to be fixed up.
#define CTL_ACPI 9999
#define ACPI_S1_SLP_TYP 19

/*
 * Send us to sleep.
 */
static int sysctl_pm_do_suspend(void)
{
	int retval;

	retval = pm_send_all(PM_SUSPEND, (void *)3);

	if (retval == 0) {
		retval = pm_do_suspend();

		pm_send_all(PM_RESUME, (void *)0);
	}

	return retval;
}

static struct ctl_table pm_table[] =
{
	{ACPI_S1_SLP_TYP, "suspend", NULL, 0, 0600, NULL, (proc_handler *)&sysctl_pm_do_suspend},
	{0}
};

static struct ctl_table pm_dir_table[] =
{
	{CTL_ACPI, "pm", NULL, 0, 0555, pm_table},
	{0}
};

/*
 * Initialize power interface
 */
static int __init pm_init(void)
{
	register_sysctl_table(pm_dir_table, 1);
	return 0;
}

__initcall(pm_init);

#endif
