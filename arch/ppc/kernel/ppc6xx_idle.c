/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 * power_save() rountine for classic PPC CPUs.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/stringify.h>

#include <asm/cputable.h>
#include <asm/current.h>
#include <asm/processor.h>

unsigned long powersave_nap = 0;

#define DSSALL		.long	(0x1f<<26)+(0x10<<21)+(0x336<<1)

void
ppc6xx_idle(void)
{
	unsigned long hid0;
	int nap = powersave_nap;

	/* 7450 has no DOZE mode mode, we return if powersave_nap
	 * isn't enabled
	 */
	if (!(nap || (cur_cpu_spec[smp_processor_id()]->cpu_features
		      & CPU_FTR_CAN_DOZE)))
		return;
	/*
	 * Disable interrupts to prevent a lost wakeup
	 * when going to sleep.  This is necessary even with
	 * RTLinux since we are not guaranteed an interrupt
	 * didn't come in and is waiting for a __sti() before
	 * emulating one.  This way, we really do hard disable.
	 *
	 * We assume that we're sti-ed when we come in here.  We
	 * are in the idle loop so if we're cli-ed then it's a bug
	 * anyway.
	 *  -- Cort
	 */
	_nmask_and_or_msr(MSR_EE, 0);
	if (!need_resched()) {
		__asm__ __volatile__("mfspr %0,1008":"=r"(hid0):);
		hid0 &= ~(HID0_NAP | HID0_SLEEP | HID0_DOZE);
		hid0 |= (powersave_nap ? HID0_NAP : HID0_DOZE) | HID0_DPM;
		__asm__ __volatile__("mtspr 1008,%0"::"r"(hid0));
		/* Flush pending data streams, consider this instruction
		 * exist on all altivec capable CPUs
		 */
		__asm__ __volatile__("98:	" __stringify(DSSALL) "\n"
				     "	sync\n"
				     "99:\n"
				     ".section __ftr_fixup,\"a\"\n"
				     "	.long %0\n"
				     "	.long %1\n"
				     "	.long 98b\n"
				     "	.long 99b\n"
				     ".previous"::"i"
				     (CPU_FTR_ALTIVEC), "i"(CPU_FTR_ALTIVEC));

		/* set the POW bit in the MSR, and enable interrupts
		 * so we wake up sometime! */
		_nmask_and_or_msr(0, MSR_POW | MSR_EE);
	}
	_nmask_and_or_msr(0, MSR_EE);
}
