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
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>

#include <asm/hardware.h>
#include <asm/memory.h>
#include <asm/system.h>


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


static int pxa_pm_enter(u32 state)
{
	unsigned long sleep_save[SLEEP_SAVE_SIZE];
	unsigned long checksum = 0;
	unsigned long delta;
	int i;

	if (state != PM_SUSPEND_MEM)
		return -EINVAL;

	/* preserve current time */
	delta = xtime.tv_sec - RCNR;

	/*
	 * Temporary solution.  This won't be necessary once
	 * we move pxa support into the serial driver
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
	 * we move pxa support into the serial driver.
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
	xtime.tv_sec = RCNR + delta;

#ifdef DEBUG
	printk(KERN_DEBUG "*** made it back from resume\n");
#endif

	return 0;
}

unsigned long sleep_phys_sp(void *sp)
{
	return virt_to_phys(sp);
}

/*
 * Called after processes are frozen, but before we shut down devices.
 */
static int pxa_pm_prepare(u32 state)
{
	return 0;
}

/*
 * Called after devices are re-setup, but before processes are thawed.
 */
static int pxa_pm_finish(u32 state)
{
	return 0;
}

/*
 * Set to PM_DISK_FIRMWARE so we can quickly veto suspend-to-disk.
 */
static struct pm_ops pxa_pm_ops = {
	.pm_disk_mode	= PM_DISK_FIRMWARE,
	.prepare	= pxa_pm_prepare,
	.enter		= pxa_pm_enter,
	.finish		= pxa_pm_finish,
};

static int __init pxa_pm_init(void)
{
	pm_set_ops(&pxa_pm_ops);
	return 0;
}

late_initcall(pxa_pm_init);
