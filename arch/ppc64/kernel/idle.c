/*
 * idle.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/time.h>

#ifdef CONFIG_PPC_ISERIES

#include <asm/iSeries/LparData.h>
#include <asm/iSeries/HvCall.h>
#include <asm/iSeries/ItLpQueue.h>

unsigned long maxYieldTime = 0;
unsigned long minYieldTime = 0xffffffffffffffffUL;

static void yield_shared_processor(void)
{
	unsigned long tb;
	unsigned long yieldTime;

	HvCall_setEnabledInterrupts(HvCall_MaskIPI |
				    HvCall_MaskLpEvent |
				    HvCall_MaskLpProd |
				    HvCall_MaskTimeout);

	tb = get_tb();
	/* Compute future tb value when yield should expire */
	HvCall_yieldProcessor(HvCall_YieldTimed, tb+tb_ticks_per_jiffy);

	yieldTime = get_tb() - tb;
	if (yieldTime > maxYieldTime)
		maxYieldTime = yieldTime;

	if (yieldTime < minYieldTime)
		minYieldTime = yieldTime;
	
	/*
	 * The decrementer stops during the yield.  Force a fake decrementer
	 * here and let the timer_interrupt code sort out the actual time.
	 */
	get_paca()->xLpPaca.xIntDword.xFields.xDecrInt = 1;
	process_iSeries_events();
}

int cpu_idle(void)
{
	struct paca_struct *lpaca;
	long oldval;
	unsigned long CTRL;

#warning fix iseries run light
#if 0
	/* ensure iSeries run light will be out when idle */
	current->thread.flags &= ~PPC_FLAG_RUN_LIGHT;
	CTRL = mfspr(CTRLF);
	CTRL &= ~RUNLATCH;
	mtspr(CTRLT, CTRL);
#endif

	lpaca = get_paca();

	while (1) {
		if (lpaca->xLpPaca.xSharedProc) {
			if (ItLpQueue_isLpIntPending(lpaca->lpQueuePtr))
				process_iSeries_events();
			if (!need_resched())
				yield_shared_processor();
		} else {
			oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);

			if (!oldval) {
				set_thread_flag(TIF_POLLING_NRFLAG);

				while (!need_resched()) {
					HMT_medium();
					if (ItLpQueue_isLpIntPending(lpaca->lpQueuePtr))
						process_iSeries_events();
					HMT_low();
				}

				HMT_medium();
				clear_thread_flag(TIF_POLLING_NRFLAG);
			} else {
				set_need_resched();
			}
		}

		if (need_resched())
			schedule();
	}

	return 0;
}

#else /* CONFIG_PPC_ISERIES */

int cpu_idle(void)
{
	long oldval;

	while (1) {
		oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);

		if (!oldval) {
			set_thread_flag(TIF_POLLING_NRFLAG);

			while (!need_resched()) {
				barrier();
				HMT_low();
			}

			HMT_medium();
			clear_thread_flag(TIF_POLLING_NRFLAG);
		} else {
			set_need_resched();
		}

		schedule();
	}

	return 0;
}

#endif /* CONFIG_PPC_ISERIES */

void default_idle(void)
{
	barrier();
}
