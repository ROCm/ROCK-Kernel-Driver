/*
 * Idle daemon for PowerPC.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 *
 * Originally Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * iSeries supported added by Mike Corrigan <mikejc@us.ibm.com>
 *
 * Additional shared processor, SMT, and firmware support
 *    Copyright (c) 2003 Dave Engebretsen <engebret@us.ibm.com>
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
#include <linux/cpu.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/HvCall.h>
#include <asm/iSeries/ItLpQueue.h>

extern long cede_processor(void);
extern long poll_pending(void);
extern void power4_idle(void);

int (*idle_loop)(void);

#ifdef CONFIG_PPC_ISERIES
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
	get_paca()->lppaca.xIntDword.xFields.xDecrInt = 1;
	process_iSeries_events();
}

int iSeries_idle(void)
{
	struct paca_struct *lpaca;
	long oldval;
	unsigned long CTRL;

	/* ensure iSeries run light will be out when idle */
	clear_thread_flag(TIF_RUN_LIGHT);
	CTRL = mfspr(CTRLF);
	CTRL &= ~RUNLATCH;
	mtspr(CTRLT, CTRL);
#if 0
	init_idle();	
#endif

	lpaca = get_paca();

	for (;;) {
		if (lpaca->lppaca.xSharedProc) {
			if (ItLpQueue_isLpIntPending(lpaca->lpqueue_ptr))
				process_iSeries_events();
			if (!need_resched())
				yield_shared_processor();
		} else {
			oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);

			if (!oldval) {
				set_thread_flag(TIF_POLLING_NRFLAG);

				while (!need_resched()) {
					HMT_medium();
					if (ItLpQueue_isLpIntPending(lpaca->lpqueue_ptr))
						process_iSeries_events();
					HMT_low();
				}

				HMT_medium();
				clear_thread_flag(TIF_POLLING_NRFLAG);
			} else {
				set_need_resched();
			}
		}

		schedule();
	}
	return 0;
}
#endif

int default_idle(void)
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
		if (cpu_is_offline(smp_processor_id()) &&
				system_state == SYSTEM_RUNNING)
			cpu_die();
	}

	return 0;
}

#ifdef CONFIG_PPC_PSERIES

DECLARE_PER_CPU(unsigned long, smt_snooze_delay);

int dedicated_idle(void)
{
	long oldval;
	struct paca_struct *lpaca = get_paca(), *ppaca;
	unsigned long start_snooze;
	unsigned long *smt_snooze_delay = &__get_cpu_var(smt_snooze_delay);

	ppaca = &paca[smp_processor_id() ^ 1];

	while (1) {
		/* Indicate to the HV that we are idle.  Now would be
		 * a good time to find other work to dispatch. */
		lpaca->lppaca.xIdle = 1;

		oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);
		if (!oldval) {
			set_thread_flag(TIF_POLLING_NRFLAG);
			start_snooze = __get_tb() +
				*smt_snooze_delay * tb_ticks_per_usec;
			while (!need_resched()) {
				/* need_resched could be 1 or 0 at this 
				 * point.  If it is 0, set it to 0, so
				 * an IPI/Prod is sent.  If it is 1, keep
				 * it that way & schedule work.
				 */
				if (*smt_snooze_delay == 0 ||
				    __get_tb() < start_snooze) {
					HMT_low(); /* Low thread priority */
					continue;
				}

				HMT_very_low(); /* Low power mode */

				/* If the SMT mode is system controlled & the 
				 * partner thread is doing work, switch into
				 * ST mode.
				 */
				if((naca->smt_state == SMT_DYNAMIC) &&
				   (!(ppaca->lppaca.xIdle))) {
					/* Indicate we are no longer polling for
					 * work, and then clear need_resched.  If
					 * need_resched was 1, set it back to 1
					 * and schedule work
					 */
					clear_thread_flag(TIF_POLLING_NRFLAG);
					oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);
					if(oldval == 1) {
						set_need_resched();
						break;
					}

					/* DRENG: Go HMT_medium here ? */
					local_irq_disable(); 

					/* SMT dynamic mode.  Cede will result 
					 * in this thread going dormant, if the
					 * partner thread is still doing work.
					 * Thread wakes up if partner goes idle,
					 * an interrupt is presented, or a prod
					 * occurs.  Returning from the cede
					 * enables external interrupts.
					 */
					cede_processor();
				} else {
					/* Give the HV an opportunity at the
					 * processor, since we are not doing
					 * any work.
					 */
					poll_pending();
				}
			}
		} else {
			set_need_resched();
		}

		HMT_medium();
		lpaca->lppaca.xIdle = 0;
		schedule();
		if (cpu_is_offline(smp_processor_id()) &&
				system_state == SYSTEM_RUNNING)
			cpu_die();
	}
	return 0;
}

int shared_idle(void)
{
	struct paca_struct *lpaca = get_paca();

	while (1) {
		if (cpu_is_offline(smp_processor_id()) &&
				system_state == SYSTEM_RUNNING)
			cpu_die();

		/* Indicate to the HV that we are idle.  Now would be
		 * a good time to find other work to dispatch. */
		lpaca->lppaca.xIdle = 1;

		if (!need_resched()) {
			local_irq_disable(); 
			
			/* 
			 * Yield the processor to the hypervisor.  We return if
			 * an external interrupt occurs (which are driven prior
			 * to returning here) or if a prod occurs from another 
			 * processor.  When returning here, external interrupts 
			 * are enabled.
			 */
			cede_processor();
		}

		HMT_medium();
		lpaca->lppaca.xIdle = 0;
		schedule();
	}

	return 0;
}
#endif

int cpu_idle(void)
{
	idle_loop();
	return 0; 
}

int native_idle(void)
{
	while(1) {
		if (!need_resched())
			power4_idle();
		if (need_resched())
			schedule();
	}
	return 0;
}

int idle_setup(void)
{
#ifdef CONFIG_PPC_ISERIES
	idle_loop = iSeries_idle;
#else
	if (systemcfg->platform & PLATFORM_PSERIES) {
		if (cur_cpu_spec->firmware_features & FW_FEATURE_SPLPAR) {
			if (get_paca()->lppaca.xSharedProc) {
				printk("idle = shared_idle\n");
				idle_loop = shared_idle;
			} else {
				printk("idle = dedicated_idle\n");
				idle_loop = dedicated_idle;
			}
		} else {
			printk("idle = default_idle\n");
			idle_loop = default_idle;
		}
	} else if (systemcfg->platform == PLATFORM_POWERMAC) {
		printk("idle = native_idle\n");
		idle_loop = native_idle;
	} else {
		printk("idle_setup: unknown platform, use default_idle\n");
		idle_loop = default_idle;
	}
#endif

	return 1;
}

