/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 * Time routes for iSeries LPAR.
 *
 * Copyright (C) 2001 IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/time.h>
#include <linux/init.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/nvram.h>
#include <asm/cache.h>
#include <asm/8xx_immap.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/iSeries/Paca.h>

u64 next_jiffy_update_tb[NR_CPUS];
extern u64 get_tb64(void);

extern rwlock_t xtime_lock;
extern unsigned long wall_jiffies;

extern unsigned long prof_cpu_mask;
extern unsigned int * prof_buffer;
extern unsigned long prof_len;
extern unsigned long prof_shift;
extern char _stext;

extern int is_soft_enabled(void);

static inline void ppc_do_profile (unsigned long nip)
{
	if (!prof_buffer)
		return;

	/*
	 * Only measure the CPUs specified by /proc/irq/prof_cpu_mask.
	 * (default is all CPUs.)
	 */
	if (!((1<<smp_processor_id()) & prof_cpu_mask))
		return;

	nip -= (unsigned long) &_stext;
	nip >>= prof_shift;
	/*
	 * Don't ignore out-of-bounds EIP values silently,
	 * put them into the last histogram slot, so if
	 * present, they will show up as a sharp peak.
	 */
	if (nip > prof_len-1)
		nip = prof_len-1;
	atomic_inc((atomic_t *)&prof_buffer[nip]);
}

 /*
  * For iSeries shared processors, we have to let the hypervisor
  * set the hardware decrementer.  We set a virtual decrementer
  * in the ItLpPaca and call the hypervisor if the virtual
  * decrementer is less than the current value in the hardware
  * decrementer. (almost always the new decrementer value will
  * be greater than the current hardware decementer so the hypervisor
  * call will not be needed)
  *
  * When we yield the processor in idle.c (needed for shared processors)
  * we cannot yield for too long or the 32-bit tbl may wrap and then
  * we would lose jiffies.  In addition, if we yield for too long,
  * we might be pretty far off on timing for device drivers and such.
  * When the hypervisor returns to us after a yield we need to
  * determine whether decrementers might have been missed.  If so
  * we need to drive the timer_interrupt code to catch up (using
  * the tb)
  *
  * For processors other than processor 0, there is no correction
  * (in the code below) to next_dec so they're last_jiffy_stamp
  * values are going to be way off.
  *
  */
int timerRetEnabled = 0;
int timerRetDisabled = 0;

extern unsigned long iSeries_dec_value;
void timer_interrupt(struct pt_regs * regs)
{
	long next_dec;
	struct Paca * paca;
	unsigned long cpu = smp_processor_id();
	paca = (struct Paca *)mfspr(SPRG1);

	if ( is_soft_enabled() )
		BUG();

	if (regs->softEnable)
	  timerRetEnabled++;
	else
	  timerRetDisabled++;
	
	irq_enter();

	if (!user_mode(regs))
		ppc_do_profile(instruction_pointer(regs));
	while ( next_jiffy_update_tb[cpu] < get_tb64() ) {
#ifdef CONFIG_SMP
		smp_local_timer_interrupt(regs);
#endif
		if ( cpu == 0 ) {
			write_lock(&xtime_lock);
			do_timer(regs);
			if ( (time_status & STA_UNSYNC) == 0 &&
				xtime.tv_sec - last_rtc_update >= 659 &&
				abs(xtime.tv_usec - (1000000-1000000/HZ)) < 500000/HZ &&
				jiffies - wall_jiffies == 1) {
				if (ppc_md.set_rtc_time(xtime.tv_sec+1 + time_offset) == 0)
					last_rtc_update = xtime.tv_sec+1;
				else
				/* Try again one minute later */
					last_rtc_update += 60;
			}
			write_unlock(&xtime_lock);

		}
		next_jiffy_update_tb[cpu] += tb_ticks_per_jiffy;
	}
	next_dec = next_jiffy_update_tb[cpu] - get_tb64();
	if ( next_dec > paca->default_decr )
		next_dec = paca->default_decr;
	paca->xLpPacaPtr->xDecrInt = 0;
	set_dec( (unsigned)next_dec );

	irq_exit();
}
