/*
 *  linux/arch/arm/mach-integrator/time.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/leds.h>
#include <asm/mach-types.h>

#include <asm/mach/time.h>

#define RTC_DR		(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 0)
#define RTC_MR		(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 4)
#define RTC_STAT	(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 8)
#define RTC_EOI		(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 8)
#define RTC_LR		(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 12)
#define RTC_CR		(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 16)

#define RTC_CR_MIE	0x00000001

extern int (*set_rtc)(void);

static int integrator_set_rtc(void)
{
	__raw_writel(xtime.tv_sec, RTC_LR);
	return 1;
}

static int integrator_rtc_init(void)
{
	__raw_writel(0, RTC_CR);
	__raw_writel(0, RTC_EOI);

	xtime.tv_sec = __raw_readl(RTC_DR);

	set_rtc = integrator_set_rtc;

	return 0;
}

__initcall(integrator_rtc_init);


/*
 * Where is the timer (VA)?
 */
#define TIMER0_VA_BASE (IO_ADDRESS(INTEGRATOR_CT_BASE)+0x00000000)
#define TIMER1_VA_BASE (IO_ADDRESS(INTEGRATOR_CT_BASE)+0x00000100)
#define TIMER2_VA_BASE (IO_ADDRESS(INTEGRATOR_CT_BASE)+0x00000200)
#define VA_IC_BASE     IO_ADDRESS(INTEGRATOR_IC_BASE) 

/*
 * How long is the timer interval?
 */
#define TIMER_INTERVAL	(TICKS_PER_uSEC * mSEC_10)
#if TIMER_INTERVAL >= 0x100000
#define TICKS2USECS(x)	(256 * (x) / TICKS_PER_uSEC)
#elif TIMER_INTERVAL >= 0x10000
#define TICKS2USECS(x)	(16 * (x) / TICKS_PER_uSEC)
#else
#define TICKS2USECS(x)	((x) / TICKS_PER_uSEC)
#endif

#define TIMER_CTRL_IE	(1 << 5)			/* Interrupt Enable */

/*
 * What does it look like?
 */
typedef struct TimerStruct {
	unsigned long TimerLoad;
	unsigned long TimerValue;
	unsigned long TimerControl;
	unsigned long TimerClear;
} TimerStruct_t;

extern unsigned long (*gettimeoffset)(void);

static unsigned long timer_reload;

/*
 * Returns number of ms since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 */
static unsigned long integrator_gettimeoffset(void)
{
	volatile TimerStruct_t *timer1 = (TimerStruct_t *)TIMER1_VA_BASE;
	unsigned long ticks1, ticks2, status;

	/*
	 * Get the current number of ticks.  Note that there is a race
	 * condition between us reading the timer and checking for
	 * an interrupt.  We get around this by ensuring that the
	 * counter has not reloaded between our two reads.
	 */
	ticks2 = timer1->TimerValue & 0xffff;
	do {
		ticks1 = ticks2;
		status = __raw_readl(VA_IC_BASE + IRQ_RAW_STATUS);
		ticks2 = timer1->TimerValue & 0xffff;
	} while (ticks2 > ticks1);

	/*
	 * Number of ticks since last interrupt.
	 */
	ticks1 = timer_reload - ticks2;

	/*
	 * Interrupt pending?  If so, we've reloaded once already.
	 */
	if (status & (1 << IRQ_TIMERINT1))
		ticks1 += timer_reload;

	/*
	 * Convert the ticks to usecs
	 */
	return TICKS2USECS(ticks1);
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t
integrator_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	volatile TimerStruct_t *timer1 = (volatile TimerStruct_t *)TIMER1_VA_BASE;

	// ...clear the interrupt
	timer1->TimerClear = 1;

	timer_tick(regs);

	return IRQ_HANDLED;
}

static struct irqaction integrator_timer_irq = {
	.name		= "Integrator Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= integrator_timer_interrupt
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init integrator_time_init(unsigned long reload, unsigned int ctrl)
{
	volatile TimerStruct_t *timer0 = (volatile TimerStruct_t *)TIMER0_VA_BASE;
	volatile TimerStruct_t *timer1 = (volatile TimerStruct_t *)TIMER1_VA_BASE;
	volatile TimerStruct_t *timer2 = (volatile TimerStruct_t *)TIMER2_VA_BASE;
	unsigned int timer_ctrl = 0x80 | 0x40;	/* periodic */

	timer_reload = reload;
	timer_ctrl |= ctrl;

	if (timer_reload > 0x100000) {
		timer_reload >>= 8;
		timer_ctrl |= 0x08; /* /256 */
	} else if (timer_reload > 0x010000) {
		timer_reload >>= 4;
		timer_ctrl |= 0x04; /* /16 */
	}

	/*
	 * Initialise to a known state (all timers off)
	 */
	timer0->TimerControl = 0;
	timer1->TimerControl = 0;
	timer2->TimerControl = 0;

	timer1->TimerLoad    = timer_reload;
	timer1->TimerValue   = timer_reload;
	timer1->TimerControl = timer_ctrl;

	/* 
	 * Make irqs happen for the system timer
	 */
	setup_irq(IRQ_TIMERINT1, &integrator_timer_irq);
	gettimeoffset = integrator_gettimeoffset;
}
