/*
 * Idle daemon for PowerPC.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
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
#ifdef CONFIG_PPC_ISERIES
#include <asm/time.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/HvCall.h>
#include <asm/hardirq.h>

static void yield_shared_processor(void);
static void run_light_on(int on);

extern unsigned long yield_count;

#else /* CONFIG_PPC_ISERIES */
#define run_light_on(x)         do { } while (0)
#endif /* CONFIG_PPC_ISERIES */

void power_save(void);

unsigned long zero_paged_on;
unsigned long powersave_nap;

void default_idle(void)
{
	int do_power_save = 0;

	/* Check if CPU can powersave */
	if (cur_cpu_spec[smp_processor_id()]->cpu_features &
		(CPU_FTR_CAN_DOZE | CPU_FTR_CAN_NAP))
		do_power_save = 1;

#ifdef CONFIG_PPC_ISERIES
	if (!current->need_resched) {
		/* Turn off the run light */
		run_light_on(0);
		yield_shared_processor(); 
	}
	HMT_low();
#endif
#ifdef CONFIG_SMP
	if (!do_power_save) {
		if (!need_resched()) {
			set_thread_flag(TIF_POLLING_NRFLAG);
			while (!test_thread_flag(TIF_NEED_RESCHED))
				barrier();
			clear_thread_flag(TIF_POLLING_NRFLAG);
		}
	}
#endif
	if (do_power_save && !need_resched())
		power_save();

	if (need_resched()) {
		run_light_on(1);
		schedule();
	}
#ifdef CONFIG_PPC_ISERIES
	else {
		run_light_on(0);
		yield_shared_processor();
		HMT_low();
	}
#endif /* CONFIG_PPC_ISERIES */
}

/*
 * SMP entry into the idle task - calls the same thing as the
 * non-smp versions. -- Cort
 */
int cpu_idle(void)
{
	for (;;)
		default_idle();
	return 0; 
}

void power_save(void)
{
	unsigned long hid0;
	int nap = powersave_nap;
	
	/* 7450 has no DOZE mode mode, we return if powersave_nap
	 * isn't enabled
	 */
	if (!(nap || (cur_cpu_spec[smp_processor_id()]->cpu_features & CPU_FTR_CAN_DOZE)))
		return;
	/*
	 * Disable interrupts to prevent a lost wakeup
	 * when going to sleep.  This is necessary even with
	 * RTLinux since we are not guaranteed an interrupt
	 * didn't come in and is waiting for a local_irq_enable() before
	 * emulating one.  This way, we really do hard disable.
	 * 
	 * We assume that we're sti-ed when we come in here.  We
	 * are in the idle loop so if we're cli-ed then it's a bug
	 * anyway.
	 *  -- Cort
	 */
	_nmask_and_or_msr(MSR_EE, 0);
	if (!need_resched())
	{
		asm("mfspr %0,1008" : "=r" (hid0) :);
		hid0 &= ~(HID0_NAP | HID0_SLEEP | HID0_DOZE);
		hid0 |= (powersave_nap? HID0_NAP: HID0_DOZE) | HID0_DPM;
		asm("mtspr 1008,%0" : : "r" (hid0));
		
		/* set the POW bit in the MSR, and enable interrupts
		 * so we wake up sometime! */
		_nmask_and_or_msr(0, MSR_POW | MSR_EE);
	}
	_nmask_and_or_msr(0, MSR_EE);
}

#ifdef CONFIG_PPC_ISERIES

extern void fake_interrupt(void);
extern u64  get_tb64(void);

void run_light_on(int on)
{
	unsigned long CTRL;

	CTRL = mfspr(CTRLF);
	CTRL = on? (CTRL | RUNLATCH): (CTRL & ~RUNLATCH);
	mtspr(CTRLT, CTRL);
}

void yield_shared_processor(void)
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
#endif /* CONFIG_PPC_ISERIES */
