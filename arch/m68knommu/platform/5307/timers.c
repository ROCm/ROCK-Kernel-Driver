/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5307/timers.c
 *
 *	Copyright (C) 1999-2003, Greg Ungerer (gerg@snapgear.com)
 */

/***************************************************************************/

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcftimer.h>
#include <asm/mcfsim.h>

/***************************************************************************/

/*
 *	Default the timer and vector to use for ColdFire. Some ColdFire
 *	CPU's and some boards may want different. Their sub-architecture
 *	startup code (in config.c) can change these if they want.
 */
unsigned int	mcf_timervector = 29;
unsigned int	mcf_profilevector = 31;
unsigned int	mcf_timerlevel = 5;

static volatile struct mcftimer *mcf_timerp;

/***************************************************************************/

void coldfire_tick(void)
{
	/* Reset the ColdFire timer */
	mcf_timerp->ter = MCFTIMER_TER_CAP | MCFTIMER_TER_REF;
}

void coldfire_timer_init(void (*handler)(int, void *, struct pt_regs *))
{
	/* Set up an internal TIMER as poll clock */
	mcf_timerp = (volatile struct mcftimer *) (MCF_MBAR + MCFTIMER_BASE1);
	mcf_timerp->tmr = MCFTIMER_TMR_DISABLE;

	mcf_timerp->trr = (unsigned short) ((MCF_BUSCLK / 16) / HZ);
	mcf_timerp->tmr = MCFTIMER_TMR_ENORI | MCFTIMER_TMR_CLK16 |
		MCFTIMER_TMR_RESTART | MCFTIMER_TMR_ENABLE;

	request_irq(mcf_timervector, handler, SA_INTERRUPT, "timer", NULL);
	mcf_settimericr(1, mcf_timerlevel);

#ifdef CONFIG_HIGHPROFILE
	coldfire_profile_init();
#endif
}

/***************************************************************************/

unsigned long coldfire_timer_offset(void)
{
	unsigned long trr, tcn;

	/* Get the values as longs for accurate calculation. */
	tcn = mcf_timerp->tcn;
	trr = mcf_timerp->trr;
	return((tcn * (1000000 / HZ)) / trr);
}

/***************************************************************************/
#ifdef CONFIG_HIGHPROFILE
/***************************************************************************/

/*
 *	Choose a reasonably fast profile timer. Make it an odd value to
 *	try and get good coverage of kernal operations.
 */
#define	PROFILEHZ	1013

static volatile struct mcftimer *mcf_proftp;

/*
 *	Use the other timer to provide high accuracy profiling info.
 */

void coldfire_profile_tick(int irq, void *dummy, struct pt_regs *regs)
{
	/* Reset ColdFire timer2 */
	mcf_proftp->ter = MCFTIMER_TER_CAP | MCFTIMER_TER_REF;

        if (!user_mode(regs)) {
                if (prof_buffer && current->pid) {
                        extern int _stext;
                        unsigned long ip = instruction_pointer(regs);
                        ip -= (unsigned long) &_stext;
                        ip >>= prof_shift;
                        if (ip < prof_len)
                                prof_buffer[ip]++;
                }
        }
}

void coldfire_profile_init(void)
{
	printk("PROFILE: lodging TIMER2 @ %dHz as profile timer\n", PROFILEHZ);

	/* Set up TIMER 2 as high speed profile clock */
	mcf_proftp = (volatile struct mcftimer *) (MCF_MBAR + MCFTIMER_BASE2);
	mcf_proftp->tmr = MCFTIMER_TMR_DISABLE;

	mcf_proftp->trr = (unsigned short) ((MCF_CLK / 16) / PROFILEHZ);
	mcf_proftp->tmr = MCFTIMER_TMR_ENORI | MCFTIMER_TMR_CLK16 |
		MCFTIMER_TMR_RESTART | MCFTIMER_TMR_ENABLE;

	request_irq(mcf_profilevector, coldfire_profile_tick,
		(SA_INTERRUPT | IRQ_FLG_FAST), "profile timer", NULL);
	mcf_settimericr(2, 7);
}

/***************************************************************************/
#endif	/* CONFIG_HIGHPROFILE */
/***************************************************************************/
