/* linux/include/asm-arm/arch-s3c2410/time.h
 *
 *  Copyright (C) 2003 Simtec Electronics <linux@simtec.co.uk>
 *    Ben Dooks, <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <asm/system.h>
#include <asm/leds.h>
#include <asm/mach-types.h>

#include <asm/io.h>
#include <asm/arch/map.h>
#include <asm/arch/regs-timer.h>

extern unsigned long (*gettimeoffset)(void);

static unsigned long timer_startval;
static unsigned long timer_ticks_usec;

#ifdef CONFIG_S3C2410_RTC
extern void s3c2410_rtc_check();
#endif

/* with an 12MHz clock, we get 12 ticks per-usec
 */


/***
 * Returns microsecond  since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 * IRQs are disabled before entering here from do_gettimeofday()
 */
static unsigned long s3c2410_gettimeoffset (void)
{
	unsigned long tdone;
	unsigned long usec;

	/* work out how many ticks have gone since last timer interrupt */

	tdone = timer_startval - __raw_readl(S3C2410_TCNTO(4));

	/* currently, tcnt is in 12MHz units, but this may change
	 * for non-bast machines...
	 */

	usec = tdone / timer_ticks_usec;

	return usec;
}


/*
 * IRQ handler for the timer
 */
static irqreturn_t
s3c2410_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_leds();
	do_timer(regs);

	do_set_rtc();
	//s3c2410_rtc_check();
	do_profile(regs);

	return IRQ_HANDLED;
}

/*
 * Set up timer interrupt, and return the current time in seconds.
 */

/* currently we only use timer4, as it is the only timer which has no
 * other function that can be exploited externally
*/

void __init time_init (void)
{
	unsigned long tcon;
	unsigned long tcnt;
	unsigned long tcfg1;
	unsigned long tcfg0;

	gettimeoffset = s3c2410_gettimeoffset;
	timer_irq.handler = s3c2410_timer_interrupt;

	tcnt = 0xffff;  /* default value for tcnt */

	/* read the current timer configuration bits */

	tcon = __raw_readl(S3C2410_TCON);
	tcfg1 = __raw_readl(S3C2410_TCFG1);
	tcfg0 = __raw_readl(S3C2410_TCFG0);

	/* configure the system for whichever machine is in use */

	if (machine_is_bast() || machine_is_vr1000()) {
		timer_ticks_usec = 12;	      /* timer is at 12MHz */
		tcnt = (timer_ticks_usec * (1000*1000)) / HZ;
	}

	/* for the h1940, we use the pclk from the core to generate
	 * the timer values. since 67.5MHz is not a value we can directly
	 * generate the timer value from, we need to pre-scale and
	 * divied before using it.
	 *
	 * overall divsior to get 200Hz is 337500
	 *   we can fit tcnt if we pre-scale by 6, producing a tick rate
	 *   of 11.25MHz, and a tcnt of 56250.
	 */

	if (machine_is_h1940()) {
		timer_ticks_usec = s3c2410_pclk / (1000*1000);
		timer_ticks_usec /= 6;

		tcfg1 &= ~S3C2410_TCFG1_MUX4_MASK;
		tcfg1 |= S3C2410_TCFG1_MUX4_DIV2;

		tcfg0 &= ~S3C2410_TCFG_PRESCALER1_MASK;
		tcfg0 |= ((6 - 1) / 2) << S3C2410_TCFG_PRESCALER1_SHIFT;

		tcnt = (s3c2410_pclk / 6) / HZ;
	}


	printk("setup_timer tcon=%08lx, tcnt %04lx, tcfg %08lx,%08lx\n",
	       tcon, tcnt, tcfg0, tcfg1);

	/* check to see if timer is within 16bit range... */
	if (tcnt > 0xffff) {
		panic("setup_timer: HZ is too small, cannot configure timer!");
		return;
	}

	__raw_writel(tcfg1, S3C2410_TCFG1);
	__raw_writel(tcfg0, S3C2410_TCFG0);

	timer_startval = tcnt;
	__raw_writel(tcnt, S3C2410_TCNTB(4));

	/* ensure timer is stopped... */

	tcon &= ~(7<<20);
	tcon |= S3C2410_TCON_T4RELOAD;
	tcon |= S3C2410_TCON_T4MANUALUPD;

	__raw_writel(tcon, S3C2410_TCON);
	__raw_writel(tcnt, S3C2410_TCNTB(4));
	__raw_writel(tcnt, S3C2410_TCMPB(4));

	setup_irq(IRQ_TIMER4, &timer_irq);

	/* start the timer running */
	tcon |= S3C2410_TCON_T4START;
	tcon &= ~S3C2410_TCON_T4MANUALUPD;
	__raw_writel(tcon, S3C2410_TCON);
}



