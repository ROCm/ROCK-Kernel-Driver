/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 * Idle task for iSeries.
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
#include <linux/ptrace.h>
#include <linux/slab.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/cputable.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/HvCall.h>
#include <asm/hardirq.h>

static void yield_shared_processor(void);
static void run_light_on(int on);

extern unsigned long yield_count;

void iSeries_idle(void)
{
	if (!need_resched()) {
		/* Turn off the run light */
		run_light_on(0);
		yield_shared_processor(); 
		HMT_low();

#ifdef CONFIG_SMP
		set_thread_flag(TIF_POLLING_NRFLAG);
		while (!need_resched())
			barrier();
		clear_thread_flag(TIF_POLLING_NRFLAG);
#endif
	}

	if (need_resched()) {
		run_light_on(1);
		schedule();
		return;
	}

}

extern void fake_interrupt(void);
extern u64  get_tb64(void);

static void run_light_on(int on)
{
	unsigned long CTRL;

	CTRL = mfspr(CTRLF);
	CTRL = on? (CTRL | RUNLATCH): (CTRL & ~RUNLATCH);
	mtspr(CTRLT, CTRL);
}

static void yield_shared_processor(void)
{
	struct Paca *paca;
	u64 tb;

	/* Poll for I/O events */
	local_irq_disable();
	local_irq_enable();
	
	paca = (struct Paca *)mfspr(SPRG1);
	if ( paca->xLpPaca.xSharedProc ) {
		HvCall_setEnabledInterrupts( HvCall_MaskIPI |
                        HvCall_MaskLpEvent |
                        HvCall_MaskLpProd |
                        HvCall_MaskTimeout );

		/*
		 * Check here for any of the above pending...
		 * IPI and Decrementers are indicated in ItLpPaca
		 * LpEvents are indicated on the LpQueue
		 *
		 * Disabling/enabling will check for LpEvents, IPIs
		 * and decrementers
		 */
		local_irq_disable();
		local_irq_enable();

		++yield_count;

		/* Get current tb value */
		tb = get_tb64();
		/* Compute future tb value when yield will expire */
		tb += tb_ticks_per_jiffy;
		HvCall_yieldProcessor( HvCall_YieldTimed, tb );

		/* Check here for any of the above pending or timeout expired*/
		local_irq_disable();
		/*
		 * The decrementer stops during the yield.  Just force
		 * a fake decrementer now and the timer_interrupt
		 * code will straighten it all out
		 */
		paca->xLpPaca.xDecrInt = 1;
		local_irq_enable();
	}
}
